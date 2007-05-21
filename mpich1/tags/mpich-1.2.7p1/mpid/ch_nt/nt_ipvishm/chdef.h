/*
    These are the definitions particular to the nt_ipvishm implementation.
 */

#ifndef __commnt_ipvishm
#define __commnt_ipvishm

#define MSG_OTHER 0
#define HOSTNAMELEN 100
//*
#define PI_NO_NSEND
#define PI_NO_NRECV
/*/
#undef PI_NO_NRECV
#undef PI_NO_NSEND
//*/
#define PInsend			NT_PInsend
#define PInsendrr		NT_PInsend
#define PIwsend(type,buffer,length,to,datatype,sid)		NT_PIwait(sid)
#define PIwsendrr		PIwsend

#define PInrecv			NT_PInrecv
#define PInrecvrr		NT_PInrecv
#define PIwrecv(type,buffer,length,datatype,rid)		NT_PIwait(rid)
#define PIwrecvrr		PIwrecv

#define PInstatus		NT_PInstatus
//*/

#define PSAllProcs 0
#define MPIDTRANSPORT "ch_nt"

#define PIbsend(type,buffer,length,to,datatype) NT_PIbsend(type,buffer,length,to,datatype)
#define PIbrecv(type,buffer,length,datatype) NT_PIbrecv(type,buffer,length,datatype)
#define PInprobe(type) NT_PInprobe(type)

#define PIfrom() g_nLastRecvFrom
#define PImytid g_nIproc

/* Initialization routines */
#define PIiInit   MPID_NT_ipvishm_Init
#define PIiFinish MPID_NT_ipvishm_End
//#define SYexitall(msg,code) nt_error(msg,code)
#define SYexitall MPID_NT_ipvishm_exitall

void MPID_NT_ipvishm_Init( int *, char *** );
void MPID_NT_ipvishm_End(void);
void MPID_NT_ipvishm_fixupdevpointers(MPID_Device *pDevice);

#endif
