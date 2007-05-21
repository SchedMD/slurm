/*
    These are the definitions particular to the Intel NX implementation.
 */

#ifndef __commnx
#define __commnx

extern int __NUMNODES, __MYPROCID;

#define PSAllProcs 0

#define MPIDTRANSPORT  "ch_nx"
/* Note that the send/recv id in a request is actually an integer ARRAY
   of 4 elements; we pick the first (0 origin) element to hold the
   Intel id.  */

/* Prefer the nonblocking, but provide the blocking. */
#define PIbsend(tag,buffer,length,to,datatype) \
             _csend(tag,buffer,length,to,0)
#define PInsend(tag,buffer,length,to,datatype,sid) \
             sid[0] =_isend(tag,buffer, length, to,0)
#define PInsendrr(tag,buffer,length,to,datatype,sid) \
             sid[0] =_isend((tag)|0x40000000,buffer,length,to,0)
#define PIwsend(tag,buffer,length,to,datatype,sid) \
        msgwait(sid[0])
#define PIwsendrr PIwsend

#define PIbrecv(tag,buffer,length,datatype)  \
        _crecv(tag,buffer,length)
#define PInrecv(tag,buffer,length,datatype,rid) \
        rid[0] =_irecv(tag,buffer,length)
#define PInrecvrr(tag,buffer,length,datatype,rid) \
        rid[0] =_irecv((tag)|0x40000000,buffer,length)
#define PIwrecv(tag,buffer,length,datatype,rid) \
        msgwait(rid[0])

#define PIwrecvrr PIwrecv

#define PInprobe(tag) \
        iprobe(tag)

#define PInstatus(rid) \
     msgdone(rid[0])

#define PIsize() infocount()
#define PIfrom() infonode()

/* Global operation used ONLY in heterogeneous setup code so not needed here */

#define PInumtids  __NUMNODES
#define PImytid    __MYPROCID

/* Initialization routines */
#define PIiInit   MPID_NX_Init
#define PIiFinish MPID_NX_End
#ifdef inteldelta
#define SYexitall(msg,code) killproc(-1,0);
#elif !defined(PARAGON_HAS_NO_KILLPROC)
#define SYexitall(msg,code) killproc(-1,-1);
#else
#define SYexitall(msg,code) exit(code);
#endif

void MPID_NX_Init ( int *, char *** );
void MPID_NX_End  (void);
#endif
