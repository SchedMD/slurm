/* Definitions for the device only 
   This is an example that can can be used by channel codes
 */
#ifndef MPID_DEV_H
#define MPID_DEV_H

/* mpich-mpid contains device-specific definitions */
#if defined(HAVE_MPICH_MPID_H) && !defined(MPICHMPID_INC)
#define MPICHMPID_INC
#include "mpich-mpid.h"
#endif

#include "dev.h"

/* Globals - For the device */
extern int          MPID_n_pending;
extern MPID_DevSet *MPID_devset;
extern MPID_INFO   *MPID_tinfo;

/* packets.h include chdef.h and channel.h */
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
extern MPID_Device *MPID_CH_InitMsgPass ( int *, char ***, int, int );
extern MPID_Protocol *MPID_CH_Short_setup (void);
extern MPID_Protocol *MPID_CH_Eagerb_setup (void);
extern MPID_Protocol *MPID_CH_Rndvb_setup (void);
extern MPID_Protocol *MPID_CH_Eagern_setup (void);
extern MPID_Protocol *MPID_CH_Rndvn_setup (void);
extern int MPID_CH_Check_incoming ( MPID_Device *, MPID_BLOCKING_TYPE);
extern int  MPID_CH_Init_hetero ( int *, char *** );
extern void MPID_CH_Pkt_pack ( void *, int, int );
extern void MPID_CH_Pkt_unpack ( void *, int, int );

extern int MPID_PackMessageFree (MPIR_SHANDLE *);
extern void MPID_PackMessage (void *, int, struct MPIR_DATATYPE *, 
					struct MPIR_COMMUNICATOR *, int, 
					MPID_Msgrep_t, MPID_Msg_pack_t, 
					void **, int *, int *);
extern void MPID_UnpackMessageSetup ( int, struct MPIR_DATATYPE *, 
						struct MPIR_COMMUNICATOR *,
						int, MPID_Msgrep_t, void **, 
						int *, int * );
extern int MPID_UnpackMessageComplete ( MPIR_RHANDLE * );

/* Routines used to cancel sends */
extern void MPID_SendCancelPacket ( MPI_Request *, int * );
extern void MPID_SendCancelOkPacket ( void *, int );
extern void MPID_RecvCancelOkPacket ( void *, int );
extern void MPID_FinishCancelPackets ( MPID_Device * );

/* 
   Devices that provide their own datatype handling may need to provide their
   own MPI_Get_count and MPI_Get_elements routines.  They should provide
   MPID_Get_count and MPID_Get_elements, and define the macros
   MPID_HAS_GET_COUNT and MPID_HAS_GET_ELEMENTS in mpid.h .  The 
   definitions of the MPID versions is exactly the same as the MPI versions.
 */

/* 
 * We can communicate some information to the device by way of attributes 
 * (communicator construction should have had info!).  The following 
 * include file simply defines the GET/SET operations as empty.
 */
#endif
