/* Definitions for the device only 
 */
#ifndef MPID_DEV_H
#define MPID_DEV_H

#include "dev.h"

/* Globals - For the device */
#include "nt_global.h"
extern int          MPID_n_pending;
extern MPID_DevSet *MPID_devset;
extern MPID_INFO   *MPID_tinfo;

/* packets.h include chdef.h and channel.h */
extern int g_nMPID_PKT_DATA_LONG_LEN;
#include "packets.h"
#include "mpid_debug.h"

/* 
   Common macro for checking the actual length (msglen) against the
   declared max length in a handle (dmpi_recv_handle).  
   Resets msglen if it is too long; also sets err to MPI_ERR_TRUNCATE.
   This will set the error field to be added to a handle "soon" 
   (Check for truncation)

   This does NOT call the MPID_ErrorHandler because that is for panic
   situations.
 */
#define MPID_CHK_MSGLEN(rhandle,msglen,err) \
if ((rhandle)->len < (msglen)) {\
    err = MPI_ERR_TRUNCATE;\
    rhandle->s.MPI_ERROR = MPI_ERR_TRUNCATE;\
    msglen = (rhandle)->len;\
    }
#define MPID_CHK_MSGLEN2(actlen,msglen,err) \
if ((actlen) < (msglen)) {\
    err = MPI_ERR_TRUNCATE;\
    msglen = (actlen);\
    }

/* Function prototypes for routines known only to the device */
extern MPID_Device *MPID_CH_InitMsgPass ANSI_ARGS(( int *, char ***, 
						    int, int ));
extern MPID_Protocol *MPID_CH_Short_setup ANSI_ARGS((void));
extern MPID_Protocol *MPID_CH_Eagerb_setup ANSI_ARGS((void));
extern MPID_Protocol *MPID_CH_Rndvb_setup ANSI_ARGS((void));
//extern MPID_Protocol *MPID_CH_Eagern_setup ANSI_ARGS((void));
extern MPID_Protocol *MPID_NT_Rndvn_setup ANSI_ARGS((void));
extern int MPID_CH_Check_incoming ANSI_ARGS(( MPID_Device *, 
					      MPID_BLOCKING_TYPE));
extern int  MPID_CH_Init_hetero ANSI_ARGS(( int *, char *** ));
extern void MPID_CH_Pkt_pack ANSI_ARGS(( void *, int, int ));
extern void MPID_CH_Pkt_unpack ANSI_ARGS(( void *, int, int ));

extern int MPID_PackMessageFree ANSI_ARGS((MPIR_SHANDLE *));
extern void MPID_PackMessage ANSI_ARGS((void *, int, struct MPIR_DATATYPE *, 
					struct MPIR_COMMUNICATOR *, int, 
					MPID_Msgrep_t, MPID_Msg_pack_t, 
					void **, int *, int *));
extern void MPID_UnpackMessageSetup ANSI_ARGS(( int, struct MPIR_DATATYPE *, 
						struct MPIR_COMMUNICATOR *,
						int, MPID_Msgrep_t, void **, 
						int *, int * ));
extern int MPID_UnpackMessageComplete ANSI_ARGS(( MPIR_RHANDLE * ));

/* 
   Devices that provide their own datatype handling may need to provide their
   own MPI_Get_count and MPI_Get_elements routines.  They should provide
   MPID_Get_count and MPID_Get_elements, and define the macros
   MPID_HAS_GET_COUNT and MPID_HAS_GET_ELEMENTS in mpid.h .  The 
   definitions of the MPID versions is exactly the same as the MPI versions.
 */

/* Error handling */
#if defined(USE_STDARG) && !defined(USE_OLDSTYLE_STDARG)
int MPIR_Err_setmsg( int, int, const char *, const char *, const char *, ... );
#else
int MPIR_Err_setmsg();
#endif

/* prototypes */
void MPID_SendCancelPacket( MPI_Request *request, int *err_code );
void MPID_SendCancelOkPacket( void *in_pkt, int from );
void MPID_RecvCancelOkPacket( void *in_pkt, int from );
void MPID_FinishCancelPackets( MPID_Device *dev );

#endif
