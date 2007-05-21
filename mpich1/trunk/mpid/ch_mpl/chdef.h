/*
    These are the definitions particular to the IBM/MPL implementation.
 */

#ifndef __commmpl
#define __commmpl

#include "mpproto.h"

extern int __MPLFROM, __MPLLEN, __MPLTYPE, __NUMNODES, __MYPROCID;

#define PSAllProcs 0

#define MPIDTRANSPORT  "ch_mpl"

/* We need a special recvid/sendid for asynchronous transfers.  We overlay
   this on the 4 ints in the ASYNC{Send,Recv}Id_t */

typedef struct { int id, from, type; } MPL_Aid;

/* Prefer the nonblocking, but provide the blocking. */
#define PIbsend(type,buffer,length,to,datatype) \
             mpc_bsend(buffer,length,to,type)
#define PInsend(type,buffer,length,to,datatype,sid) \
             mpc_send(buffer, length, to, type, &((MPL_Aid*)(sid))->id)
#define PInsendrr PInsend
#define PIwsend(type,buffer,length,to,datatype,sid) \
        {int __d; mp_wait( &((MPL_Aid*)(sid))->id, &__d );}
#define PIwsendrr PIwsend

#define PIbrecv(type,buffer,length,datatype)  \
        {__MPLFROM=-1;__MPLTYPE=type;\
	 mpc_brecv(buffer,length,&__MPLFROM,&__MPLTYPE,(size_t*)&__MPLLEN);}
/* Note that this stashes the type/from in the rid structure to protect
   against overwrites */
#define PInrecv(tag,buffer,length,datatype,rid) \
    {((MPL_Aid*)(rid))->type=tag;((MPL_Aid*)(rid))->from=-1;\
    mpc_recv(buffer,length,&((MPL_Aid*)(rid))->from,&((MPL_Aid*)(rid))->type,\
	     &((MPL_Aid*)(rid))->id);}
#define PInrecvrr PInrecv
#define PIwrecv(type,buffer,length,datatype,rid) \
        mp_wait( &((MPL_Aid*)(rid))->id, &__MPLLEN )

#define PIwrecvrr PIwrecv

#define PInprobe(type) \
    (__MPLFROM=-1,__MPLTYPE=type,\
     mp_probe(&__MPLFROM,&__MPLTYPE,&__MPLLEN),(__MPLLEN>=0))

#define PInstatus(rid) \
     (mp_status( &((MPL_Aid *)(rid))->id ) > -1)

#define PIsize() __MPLLEN
#define PIfrom() __MPLFROM

/* Global operation used ONLY in heterogeneous setup code so not needed here */

#define PInumtids  __NUMNODES
#define PImytid    __MYPROCID

/* Initialization routines */
#define PIiInit   MPID_MPL_Init
#define PIiFinish MPID_MPL_End
#define SYexitall(msg,code) mpc_stopall(code)

void MPID_MPL_Init ( int *, char *** );
void MPID_MPL_End  (void);
#endif
