/*
;The MIT License
;
;Copyright (c) 2011 Dylan Smith
;
;Permission is hereby granted, free of charge, to any person obtaining a copy
;of this software and associated documentation files (the "Software"), to deal
;in the Software without restriction, including without limitation the rights
;to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
;copies of the Software, and to permit persons to whom the Software is
;furnished to do so, subject to the following conditions:
;
;The above copyright notice and this permission notice shall be included in
;all copies or substantial portions of the Software.
;
;THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
;IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
;FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
;AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
;LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
;OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
;THE SOFTWARE.
*/
#include <spectrum.h>
//#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include "matchmake.h"
// Initialize the match making stuff.

//long heap;
#define HLCALL  0x3FFA
#define CLEAR42 0x3E30

void main() {
	int rc;
  // Reset the PRINT42 routine
#asm
  ld hl, CLEAR42
  call HLCALL
#endasm
  

//	mallinit();
//	sbrk(27000,1024);
	zx_border(0);
	ia_cls();
	inputinit();
	getPlayerData();

	rc=initConnection(getServer(), getPlayer());
	if(rc == 0) {
		ui_status(rc, "Connected");
		fadeOut();
		drawMatchmakingScreen();
		readyToMatchmake();
		rc=messageloop();
	}
	else
		ui_status(rc, "Connection failed");

	fadeOut();
	inputexit();
}

