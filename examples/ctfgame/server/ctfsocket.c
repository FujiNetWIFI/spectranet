// The MIT License
// 
// Copyright (c) 2011 Dylan Smith
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifdef UNIX
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "ctfmessage.h"
#include "ctfserv.h"
#include "ctfsocket.h"

int sockfd;

// Input buffer
unsigned char msgbuf[MSGBUFSZ];

// Client list
struct sockaddr_in *cliaddr[MAXCLIENTS];

// Client ping data
struct _ping pingdata[MAXCLIENTS];

// Client output buffers
unsigned char *playerBuf[MAXCLIENTS];
unsigned char *playerBufPtr[MAXCLIENTS];

// Make the socket.
// Returns -1 if the socket could not be created
// Returns -2 if bind() fails
int makeSocket() {

    struct sockaddr_in locaddr;
    int i;
#ifdef WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 0), &wsaData) != 0) 
        return;
#endif

    sockfd=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sockfd < 0) return -1;

    locaddr.sin_family=AF_INET;
    locaddr.sin_port=htons(CTFPORT);
    locaddr.sin_addr.s_addr=htonl(INADDR_ANY);
    if(bind(sockfd, (struct sockaddr *)&locaddr, sizeof(locaddr)) == -1)
        return -2;

    // zero out client pointers
    memset(cliaddr, 0, sizeof(cliaddr));
    memset(playerBuf, 0, sizeof(playerBuf));

    return 0;
}

// Effectively the main game loop. Uses select() to wait for
// messages and time the game loop in a rudimentary fashion.
// (A more complex game would need a more complex way of timing
// the game play, but here we just use the timeout on select() to
// time the actual game progress).
//
// Note this code is unashamedly Unix oriented, but it should port
// to Windows with minimal modifications (possibly no modifications)
// since Windows at least implements the select syscall for sockets.
// It should run straight out of the box on anything Unixy (Mac, Linux,
// BSD etc.)
int messageLoop() {
    fd_set fds;
    struct timeval timeout;
    struct timeval last, now;
    int rc, tick;

    // select() should return immediately if there's nothing for
    // us to process.
    timeout.tv_sec=0;		
    timeout.tv_usec=1;

    while(1) {
        // initialize last time value
        gettimeofday(&last, NULL);

        // Set up the set of file descriptors
        FD_ZERO(&fds);
        FD_SET(sockfd, &fds);
        rc=select(sockfd+1, &fds, NULL, NULL, &timeout);

        if(rc < 0) {
            perror("select");
            return rc;
        }

        if(FD_ISSET(sockfd, &fds)) {	
            rc=getMessage();
            if(rc < 0) {
                return rc;
            }
        }
        else {

            // Make any pending updates
            makeUpdates();	

            // Do pings
            doPing();

            // wait for GAMETICK microseconds.
            gettimeofday(&now, NULL);
            tick=GAMETICK-usecdiff(&last, &now);
            if(tick > 0)
                usleep(tick);
        }
    }
    return 0;
}

// Receive a message from the socket and dispatch it.
int getMessage() {
    struct sockaddr_in rxaddr;
    ssize_t bytes;
    ssize_t bytesleft;
    unsigned char *msgptr;
    socklen_t addrlen=sizeof(rxaddr);
    int clientid;
    Player *player;

    memset(msgbuf, 0, sizeof(msgbuf));
    bytes=recvfrom(sockfd, msgbuf, sizeof(msgbuf), 0,
            (struct sockaddr *)&rxaddr, &addrlen);
    if(bytes < 0) {
        perror("recvfrom");
        return bytes;
    }

    msgptr=msgbuf;
    if(*msgptr == HELLO) {
        // New connection
        addNewClient(msgptr+1, &rxaddr, 0);
    }
    else if(*msgptr == SPECHELLO) {
        // New spectator
        addNewClient(msgptr+1, &rxaddr, SPECTATOR|PLYRREADY);

    } else {
        clientid=findClient(&rxaddr);
        if(clientid < 0) 
            // unknown client - not a fatal error, just don't
            // process the message
            return 0;
        pingdata[clientid].frames=0;
        pingdata[clientid].rspmiss=0;
        player=getPlayer(clientid);

        // Find the client connection
        while((bytesleft=bytes-(msgptr-msgbuf)) > 0) {
#ifdef COMMS_DEBUG
            printf("DEBUG: %ld: Client: %d message %x remain: %d\n", getframes(), clientid, *msgptr, bytesleft);
#endif
            switch(*msgptr) {
                case START:
                    msgptr++;
                    startPlayer(clientid);
                    break;
                case CONTROL:
                    msgptr++;
                    player->playerobj->ctrls=*msgptr;
                    msgptr++;
                    break;
                case BYE:
                    msgptr++;
                    sendByeAck(clientid);
                    removeClient(clientid);
                    break;
                case CLIENTRDY:
                    msgptr++;
                    player->flags |= RUNNING;
                    break;
                case VIEWPORT:
                    msgptr++;

                    // Portability TODO: this doesn't work for bigendian
                    memcpy(&player->view, msgptr, sizeof(Viewport));

                    player->flags |= NEWVIEWPORT;
                    msgptr+=sizeof(Viewport);

                    break;
                case PINGMSG:
                    // The ping structure will have already been reset, we only
                    // need advance the pointer.
                    msgptr++;
                    break;
                case TEAMREQUEST:
                    msgptr++;
                    setPlayerTeam(player, *msgptr);
                    msgptr++;
                    break;
                case MMSTART:
                    msgptr++;
                    player->flags |= MATCHMAKING;
                    updateMatchmaker(clientid);
                    break;
                case MMSTOP:
                    msgptr++;
                    tryToStopMatchmaking();
                    //          player->flags &= (0xFF ^ MATCHMAKING); 
                    break;
                case MMREADY:
                    msgptr++;

                    // Ignore the message if the player is not on a team yet
                    if(player->team < 2) {
                        player->flags |= PLYRREADY;
                        updateAllMatchmakers();
                    }
                    break;
                case SERVERKILL:
                    printError("Warning: Got serverkill from client %d", clientid);
                    // stop processing this block
                    return 0;
                default:
                    printError("Unknown message %x from client %d",
                            *msgptr, clientid);
                    // stop processing messages in this block as soon
                    // as we get a bad one.
                    return 0;
            }
        }	
    }

    return bytes;
}

// Add a new client.
int addNewClient(char *hello, struct sockaddr_in *client, uchar pflags) {
    int i;
    char ackbuf[2];
    ackbuf[0]=ACK;

    // Find a new slot
    for(i=0; i<MAXCLIENTS; i++) {
        if(cliaddr[i] == NULL) {
            cliaddr[i]=(struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
            memcpy(cliaddr[i], client, sizeof(struct sockaddr_in));

            // create the main message buffer for the player
            playerBuf[i] = (unsigned char *)malloc(MSGBUFSZ);
            memset(playerBuf[i], 0, MSGBUFSZ);
            playerBufPtr[i]=playerBuf[i]+1;

            printMessage("Hello from client %s:%d",
                inet_ntoa(client->sin_addr), ntohs(client->sin_port));

            // initialize the player object
            if(makeNewPlayer(i, hello, pflags)) {

                // send the acknowledgement
                ackbuf[1]=ACKOK;
                if(sendto(sockfd, ackbuf, sizeof(ackbuf), 0,
                            (struct sockaddr *)client, sizeof(struct sockaddr_in)) < 0) {
                    perror("sendto");
                    return -1;
                }
            } else {
                printError("Could not create player.");

                // Give the client the bad news
                ackbuf[1]=UNABLE;
                if(sendto(sockfd, ackbuf, sizeof(ackbuf), 0,
                            (struct sockaddr *)client, sizeof(struct sockaddr_in)) < 0) {
                    perror("sendto");
                    return -1;
                }
            }

            pingdata[i].frames=0;
            pingdata[i].rspmiss=0;
            return 0;
        }
    }

    // If we get here, we've run out of client slots.
    // Reply "no can do"
    ackbuf[1]=ACKTOOMANY;
    if(sendto(sockfd, ackbuf, sizeof(ackbuf), 0,
                (struct sockaddr *)client, sizeof(struct sockaddr_in)) < 0) {
        perror("sendto");
        return -1;
    }
    return 0;
}

// Find the client's address in the list and remove it.
void removeClient(int clientid) {
    if(cliaddr[clientid]) {
        free(cliaddr[clientid]);
        cliaddr[clientid]=NULL;
    }

    if(playerBuf[clientid]) {
        free(playerBuf[clientid]);
        playerBuf[clientid]=NULL;
        playerBufPtr[clientid]=NULL;
    }

    removePlayer(clientid);
}

// Find the client's address in the list and return the index.
int findClient(struct sockaddr_in *client) {
    int i;
    for(i=0; i<MAXCLIENTS; i++) {
        if(cliaddr[i] &&
                client->sin_addr.s_addr == cliaddr[i]->sin_addr.s_addr &&
                client->sin_port == cliaddr[i]->sin_port) {
            return i;
        }
    }


    printError("findClient: erk, unable to find client %s:%d",
            inet_ntoa(client->sin_addr), ntohs(client->sin_port));
    return -1;
}

// Send all client message buffers, and flush the buffers.
void sendClientMessages() {
    int i;
    int rc;

    for(i=0; i<MAXCLIENTS; i++) {
        if(cliaddr[i]) {
            rc=sendMessage(i);
            if(rc < 0) {
                // Error when calling sendto; eliminate the client.
                printError("Transmit error: removing client %d", i);
                removeClient(i);
            }
        }
    }
}

// Send the message buffer to a client.
int sendMessage(int clientno) {
    ssize_t bytes=playerBufPtr[clientno] - playerBuf[clientno];

    // if the buffer pointer is only 1 beyond the start, there
    // are no messages to send
    if(bytes < 2) 
        return 0;
    //debugMsg(playerBuf[clientno], bytes);
#ifdef COMMS_DEBUG
    printf("DEBUG: sending %d messages to client %d\n",
            *playerBuf[clientno], clientno);
#endif
    if(sendto(sockfd, playerBuf[clientno], bytes, 0,
                (struct sockaddr *)cliaddr[clientno], sizeof(struct sockaddr_in)) < 0) {
        perror("sendto");
        return -1;
    }

    // set buffer pointer to the first byte of the first msg
    playerBufPtr[clientno]=playerBuf[clientno]+1;
    *playerBuf[clientno]=0;
    return 0;
}

// Broadcast message line messages
void broadcastStatusMsg(char *str) {
    int i;
    MessageMsg msg;
    strlcpy(msg.message, str, sizeof(msg.message));
    msg.msgsz=strlen(msg.message);

    for(i=0; i<MAXCLIENTS; i++) {
        if(cliaddr[i]) {
            addMessage(i, MESSAGEMSG, &msg, sizeof(MessageMsg));
        }
    }

    // Also print the message on the server scoreboard.
    printMessage(str);
}

// Broadcast player ids
void broadcastPlayerIdMsg() {
    int i, j, nump;
    PlayerIdMsg playerId[MAXCLIENTS];
    Player *p;

    nump=0;
    for(i=0; i<MAXCLIENTS; i++) {
        p=getPlayer(i);
        if(p && !(p->flags & SPECTATOR)) {
            playerId[nump].ownerid=i;
            strlcpy(&playerId[nump].ownername, p->name, MAXNAME);
            nump++;
        }
    }

    for(i=0; i<MAXCLIENTS; i++) {
        p=getPlayer(i);
        if(p && p->flags & SPECTATOR) {
            for(j=0; j<nump; j++) {
                addMessage(i, PLAYERIDMSG, &playerId[j], sizeof(PlayerIdMsg));
            }
        }
    }
}

// Broadcast matchmaking messages

int sendMessageBuf(int clientno, char *buf, ssize_t bufsz) {
    if(sendto(sockfd, buf, bufsz, 0,
                (struct sockaddr *)cliaddr[clientno], sizeof(struct sockaddr_in)) < 0) {
        perror("sendto");
        return -1;
    }
}

// Add a message to the message buffer
int addMessage(int clientno, unsigned char msgid, void *msg, ssize_t msgsz) {
    ssize_t bytesused = playerBufPtr[clientno]-playerBuf[clientno]+msgsz+2;

    //printf("addMessage: clientno %d msgid %d msgsz %d\n",
    //		clientno, msgid, msgsz);
    if(bytesused > MSGBUFSZ) {
        printError("too many messages for client %d", clientno);
        return -1;
    }

    // Increment the message count
    (*playerBuf[clientno])++;
    *playerBufPtr[clientno]++ = msgid;
    memcpy(playerBufPtr[clientno], msg, msgsz);
    playerBufPtr[clientno] += msgsz;
    return 0;
}

// The following functions basically exist so that architecture-specific
// stuff can be done. The client is always little endian, but the
// server may not be.
//
// Initialize the client.
int addInitGameMsg(int clientno, MapXY *xy) {
    return addMessage(clientno, STARTACK, xy, sizeof(MapXY));
}

// Tell the client that the viewport has to change, and where
// the player's tank currently is so it may select a viewport.
int addChangeViewportMsg(int clientno, int x, int y) {
    MapXY xy;
    xy.mapx=x;
    xy.mapy=y;
    return addMessage(clientno, VIEWPORT, &xy, sizeof(MapXY));
}

// Tell the client to create a new sprite.
int addSpriteMsg(int clientno, SpriteMsg *msm) {
    return addMessage(clientno, SPRITEMSG, msm, sizeof(SpriteMsg));
}

// Send a sprite message with 16 bit coords.
int addSpriteMsg16(int clientno, SpriteMsg16 *msm) {
    return addMessage(clientno, SPRITEMSG16, msm, sizeof(SpriteMsg16));
}

// Tell the client to remove a sprite
int addDestructionMsg(int clientno, RemoveSpriteMsg *rm) {
    return addMessage(clientno, RMSPRITEMSG, rm, sizeof(RemoveSpriteMsg));
}

// Acknowledge a disconnect by sending just a BYEACK. Don't send
// any queued messages.
int sendByeAck(int clientno) {
    char bye[2];
    bye[0]=1;
    bye[1]=BYEACK;
    if(sendto(sockfd, bye, 2, 0,
                (struct sockaddr *)cliaddr[clientno], sizeof(struct sockaddr_in)) < 0) {
        perror("sendByeAck: sendto");
        return -1;
    }
    return 0;
}

// Ping any players that we've not heard from in a while and update
// all the ping structures.
void doPing() {
    int i;
    uchar pingmsg=0;
    char *gonestr;
    Player *p;

    for(i=0; i<MAXCLIENTS; i++) {
        p=getPlayer(i);
        if(p && p->flags & (MATCHMAKING|RUNNING))
        {
            if(pingdata[i].rspmiss > MAXRSPMISS) {
                // make a copy of the player name since it will get freed with
                // the player object
                gonestr=(char *)malloc(MAXSTATUSMSG);
                snprintf(gonestr, MAXSTATUSMSG, "%s pang out", p->name);

                removeClient(i);

                // broadcast the vanishing of the player
                broadcastStatusMsg(gonestr);
                free(gonestr);
                continue;
            }

            if(pingdata[i].frames > PINGFRAMES) {
#ifdef COMMS_DEBUG
                printf("Ping client %d\n", i);
#endif
                addMessage(i, PINGMSG, &pingmsg, sizeof(pingmsg));
                pingdata[i].frames=0;
                pingdata[i].rspmiss++;
                continue;
            }

            pingdata[i].frames++;
        }
    }
}

void debugMsg(uchar *msg, int bytes) {
    int i;
    printf("Msg: ");
    for(i=0; i < bytes; i++) {
        printf("%x ", *msg);
        msg++;
    }
    printf("\n");
}

// Give the difference between the two timevals in microseconds.
int usecdiff(struct timeval *first, struct timeval *last) {
    int secdiff;

    secdiff=last->tv_sec - first->tv_sec;
    if(!secdiff)
        return last->tv_usec - first->tv_usec;

    if(secdiff == 1) {
        return(1000000-first->tv_usec) + last->tv_usec;
    }

    return(1000000-last->tv_usec) + first->tv_usec + ((secdiff-1) * 1000000);
}

