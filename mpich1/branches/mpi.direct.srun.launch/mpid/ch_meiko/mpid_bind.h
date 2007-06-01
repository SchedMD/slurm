#ifndef _MPID_BIND
#define _MPID_BIND
/* Bindings for the Device routines */

extern void MPID_MEIKO_Abort ( );
extern void MPID_MEIKO_Myrank ( int * ), 
            MPID_MEIKO_Mysize ( int * ), 
            MPID_MEIKO_End (void);
extern void MPID_MEIKO_Node_name (char *, int );
extern void MPID_MEIKO_Version_name (char *);

extern void *MPID_MEIKO_Init ( int *, char *** );

extern int MPID_MEIKO_post_send ( MPIR_SHANDLE * ), 
           MPID_MEIKO_post_send_sync ( MPIR_SHANDLE *),
           MPID_MEIKO_complete_send ( MPIR_SHANDLE *),
           MPID_MEIKO_Blocking_send ( MPIR_SHANDLE *),
           MPID_MEIKO_post_recv ( MPIR_RHANDLE * ),
           MPID_MEIKO_blocking_recv ( MPIR_RHANDLE *), 
           MPID_MEIKO_complete_recv ( MPIR_RHANDLE *);
extern void MPID_MEIKO_check_device ( int ), 
   MPID_MEIKO_Probe ( int, int, int, MPI_Status * );
extern int MPID_MEIKO_Iprobe ( int, int, int, int *, MPI_Status * );
extern int MPID_MEIKO_Cancel (MPIR_COMMON *); 
extern int MPID_MEIKO_check_incoming (MPID_BLOCKING_TYPE);

extern void MPID_SetSpaceDebugFlag ( int );
extern void MPID_SetDebugFile ( char * );

#ifndef MPID_MEIKO_Wtime
extern double MPID_MEIKO_Wtime (void);
#endif
extern double MPID_MEIKO_Wtick (void);

#ifdef MPID_DEVICE_CODE


extern MPID_Aint MPID_MEIKO_Get_Sync_Id 
    ( MPIR_SHANDLE *, MPID_SHANDLE * );
extern int MPID_MEIKO_Lookup_SyncAck 
    ( MPID_Aint, MPIR_SHANDLE **,MPID_SHANDLE **);
extern int MPID_SyncAck ( MPID_Aint, int );
extern void MPID_SyncReturnAck ( MPID_Aint, int );
extern void MPID_Sync_discard ( MPIR_SHANDLE * );

extern void MPID_MEIKO_Pkt_pack ( MPID_PKT_T *, int, int );
extern void MPID_MEIKO_Pkt_unpack ( MPID_PKT_T *, int, int );

extern void MPID_MEIKO_Print_Send_Handle ( MPIR_SHANDLE *);

/*
These are now static ...
extern int MPID_MEIKO_Copy_body_short 
    ( MPIR_RHANDLE *, MPID_PKT_T *, void * );
extern int MPID_MEIKO_Copy_body_sync_short 
    ( MPIR_RHANDLE *, MPID_PKT_T *, int );
extern int MPID_MEIKO_Copy_body_long 
    ( MPIR_RHANDLE *, MPID_PKT_T *, int );
extern int MPID_MEIKO_Copy_body_sync_long
    ( MPIR_RHANDLE *, MPID_PKT_T *pkt, int );
 */
extern int MPID_MEIKO_Process_unexpected 
    ( MPIR_RHANDLE *, MPIR_RHANDLE *);
extern int MPID_MEIKO_Copy_body 
    ( MPIR_RHANDLE *, MPID_PKT_T *, int);
extern int MPID_MEIKO_Copy_body_unex
    ( MPIR_RHANDLE *, MPID_PKT_T *, int);
extern int MPID_MEIKO_Ack_Request 
    ( MPIR_RHANDLE *, int, MPID_Aint, int );
extern int MPID_MEIKO_Complete_Rndv ( MPID_RHANDLE * );
extern int MPID_MEIKO_Do_Request ( int, int, MPID_Aint );
extern void MPID_MEIKO_Init_recv_code (void);
extern void MPID_MEIKO_Init_send_code  (void);
extern void MPID_PrintMsgDebug  (void);
extern void MPID_SetSyncDebugFlag  ( void *, int );

/* These are defined only for if MPID_USE_GET is */
extern void * MPID_SetupGetAddress ( void *, int *, int );
extern void   MPID_FreeGetAddress ( void * );

extern int MPID_MEIKO_Do_get ( MPIR_RHANDLE *, int, MPID_PKT_GET_T * );

extern void MPID_MEIKO_Get_print_pkt (FILE *, MPID_PKT_T *);
extern void MPID_MEIKO_Print_pkt_data (char *, char *, int );

#endif /* MPID_DEVICE_CODE */
#endif /* _MPID_BIND */






