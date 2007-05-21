#ifndef MPID_BIND
#define MPID_BIND
/* These are the bindings of the ADI2 routines 
 * 
 * This is not necessarily a complete set.  Check the ADI2 documentation
 * in http://www.mcs.anl.gov/mpi/mpich/workingnote/nextgen/note.html .
 *
 * These include routines that are internal to the device implementation.
 * A device implementation that replaces a large piece such as the datatype
 * handling may not need to replace all of the routines (e.g., MPID_Msg_rep is 
 * used only by the pack/send/receive routines, not by the MPIR level)
 *
 */

void MPID_Init ( int *, char ***, void *, int *);
void MPID_End  (void);
void MPID_Abort ( struct MPIR_COMMUNICATOR *, int, char *, char * );
int  MPID_DeviceCheck ( MPID_BLOCKING_TYPE );
void MPID_Node_name ( char *, int );
int  MPID_WaitForCompleteSend (MPIR_SHANDLE *);
int  MPID_WaitForCompleteRecv (MPIR_RHANDLE *);
void MPID_Version_name (char *);

/* SetPktSize is used by util/cmnargs.c */
void MPID_SetPktSize ( int );

void MPID_RecvContig ( struct MPIR_COMMUNICATOR *, void *, int, int,
				 int, int, MPI_Status *, int * );
void MPID_IrecvContig ( struct MPIR_COMMUNICATOR *, void *, int, 
				  int, int, int, MPI_Request, int * );
void MPID_RecvComplete ( MPI_Request, MPI_Status *, int *);
int  MPID_RecvIcomplete ( MPI_Request, MPI_Status *, int *);

void MPID_SendContig ( struct MPIR_COMMUNICATOR *, void *, int, int,
				 int, int, int, MPID_Msgrep_t, int * );
void MPID_BsendContig ( struct MPIR_COMMUNICATOR *, void *, int, 
				  int, int, int, int, MPID_Msgrep_t, int * );
void MPID_SsendContig ( struct MPIR_COMMUNICATOR *, void *, int, 
				  int, int, int, int, MPID_Msgrep_t, int * );
void MPID_IsendContig ( struct MPIR_COMMUNICATOR *, void *, int, 
				  int, int, int, int, MPID_Msgrep_t, 
				  MPI_Request, int * );
void MPID_IssendContig ( struct MPIR_COMMUNICATOR *, void *, int, 
				   int, int, int, int, 
				   MPID_Msgrep_t, MPI_Request, int * );
void MPID_SendComplete ( MPI_Request, int *);
int  MPID_SendIcomplete ( MPI_Request, int *);

void MPID_Probe ( struct MPIR_COMMUNICATOR *, int, int, int, int *, 
			    MPI_Status * );
void MPID_Iprobe ( struct MPIR_COMMUNICATOR *, int, int, int, int *,
			     int *, MPI_Status * );

void MPID_SendCancel ( MPI_Request, int * );
void MPID_RecvCancel ( MPI_Request, int * );

/* General MPI Datatype routines  */
void MPID_SendDatatype ( struct MPIR_COMMUNICATOR *, void *, int, 
			 struct MPIR_DATATYPE *, int, int, int, int, int * );
void MPID_SsendDatatype ( struct MPIR_COMMUNICATOR *, void *, int, 
			  struct MPIR_DATATYPE *, int, int, int, int, int * );
void MPID_IsendDatatype ( struct MPIR_COMMUNICATOR *, void *, int, 
			  struct MPIR_DATATYPE *, 
			  int, int, int, int, MPI_Request, int * );
void MPID_IssendDatatype ( struct MPIR_COMMUNICATOR *, void *, int, 
			   struct MPIR_DATATYPE *, 
			   int, int, int, int, MPI_Request, int * );
void MPID_RecvDatatype ( struct MPIR_COMMUNICATOR *, void *, int, 
			 struct MPIR_DATATYPE *, 
			 int, int, int, MPI_Status *, int * );
void MPID_IrecvDatatype ( struct MPIR_COMMUNICATOR *, void *, int, 
			  struct MPIR_DATATYPE *, 
			  int, int, int, MPI_Request, int * );

void MPID_Status_set_bytes( MPI_Status *, int );

/* Pack and unpack support */
void MPID_Msg_rep ( struct MPIR_COMMUNICATOR *, int, 
		    struct MPIR_DATATYPE *, MPID_Msgrep_t *, 
		    MPID_Msg_pack_t * );
void MPID_Msg_act ( struct MPIR_COMMUNICATOR *, int, 
		    struct MPIR_DATATYPE *, MPID_Msgrep_t, 
		    MPID_Msg_pack_t * );
void MPID_Pack_size ( int, struct MPIR_DATATYPE *, MPID_Msg_pack_t, int * );
void MPID_Pack ( void *, int, struct MPIR_DATATYPE *, 
		 void *, int, int *, struct MPIR_COMMUNICATOR *, 
		 int, MPID_Msgrep_t, MPID_Msg_pack_t, int * );
void MPID_Unpack ( void *, int, MPID_Msgrep_t, int *, 
		   void *, int, struct MPIR_DATATYPE *, int *, 
		   struct MPIR_COMMUNICATOR *, int, int * );

/* Requests */
void MPID_Request_free (MPI_Request);

/* Communicators 
 * These are often defined as simple macros.  These prototypes show how
 * they should be defined if they are routines.
 */
#ifndef MPID_CommInit
int MPID_CommInit ( struct MPIR_COMMUNICATOR *, struct MPIR_COMMUNICATOR * );
#endif
#ifndef MPID_CommFree
int MPID_CommFree ( struct MPIR_COMMUNICATOR * );
#endif

/*
 * Miscellaneous routines
 * These are often defined as simple macros.  These prototypes show how
 * they should be defined if they are routines.
 */
#ifdef __cplusplus
extern "C" {
#endif
#ifndef MPID_Wtime
void MPID_Wtime ( double * );
#endif
#ifndef MPID_Wtick
void MPID_Wtick ( double * );
#endif
#ifdef __cplusplus
};
#endif

/* 
 * These are debugging commands; they are exported so that the command-line
 * parser and other routines can control the debugging output
 */
void MPID_SetDebugFile ( char * );
void MPID_Set_tracefile ( char * );
void MPID_SetSpaceDebugFlag ( int );
void MPID_SetDebugFlag ( int );
void MPID_SetMsgDebugFlag( int );
#endif
