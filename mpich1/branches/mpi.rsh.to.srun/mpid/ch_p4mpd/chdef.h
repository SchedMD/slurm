/*
    These are the definitions particular to the p4 implementation.
 */

#ifndef __commp4
#define __commp4

/* Include my own copy since I don't need all of those awful definitions */
#include "p4.h"

extern int __P4FROM, __P4LEN, __P4TYPE, __P4GLOBALTYPE;

/* Convert generic datatype to p4 version */
#define MSG_OTHER P4NOX

#define PI_NO_NSEND
#define PI_NO_NRECV
#define PSAllProcs 0

#define MPIDTRANSPORT  "ch_p4"

/* Use only the nonblocking send/recv routines */
#define PIbsend(type,buffer,length,to,datatype) \
	     p4_sendx(type,to,(char*)(buffer),length,datatype)

#define PIbrecv(type,buffer,length,datatype)  \
        {char *__p4lbuf=0;__P4LEN=length;__P4FROM= -1;__P4TYPE=type;\
        p4_recv(&__P4TYPE,&__P4FROM,&__p4lbuf,&__P4LEN);\
        memcpy(buffer,__p4lbuf,__P4LEN);p4_msg_free(__p4lbuf);}

#define PInprobe(type) (__P4TYPE=type,__P4FROM= -1,\
        p4_messages_available(&__P4TYPE,&__P4FROM))

#define PIsize() __P4LEN
#define PIfrom() __P4FROM

/* Global operation used ONLY in heterogeneous setup code, and only on
   all processes (procset == PSAllProcs) */
#define PIgimax(val,n,work,procset)  \
      p4_global_op(__P4GLOBALTYPE,val,n,sizeof(int),p4_int_max_op,P4INT)
#define PInumtids (p4_num_total_slaves()+1)
#define PImytid    p4_get_my_id()

/* Initialization routines */
#define PIiInit   MPID_P4_Init
/* #define PIiFinish() MPID_P4_End(); MPID_Close_sockets() old version*/
#define PIiFinish() MPID_P4_End()
/* following SYexitall updated by RMB 12/6/99 */
/* #define SYexitall(msg,code) p4_error(msg,code) */
#define SYexitall(msg,code) MPD_Abort(code)

void MPID_P4_Init ( int *, char *** );
void MPID_P4_End  (void);

#endif
