; CALLER linkage for send()
XLIB send
LIB send_callee
XREF ASMDISP_SEND_CALLEE

; int send(int sockfd, const void *buf, int len, int flags);
.send
	pop hl		; return address
	pop ix		; flags (not used)
	pop bc		; length
	pop de		; buffer
	pop af		; sockfd
	push af
	push de
	push bc
	push ix
	push hl
	jp send_callee + ASMDISP_SEND_CALLEE

