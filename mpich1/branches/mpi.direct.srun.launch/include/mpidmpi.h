#ifndef _MPIR_DMPI_INC
#define _MPIR_DMPI_INC

void MPID_BSwap_N_inplace ( unsigned char *, int, int );
void MPID_BSwap_short_inplace ( unsigned char *, int );
void MPID_BSwap_int_inplace ( unsigned char *, int );
void MPID_BSwap_long_inplace ( unsigned char *, int );
void MPID_BSwap_float_inplace ( unsigned char *, int );
void MPID_BSwap_double_inplace ( unsigned char *, int );
void MPID_BSwap_long_double_inplace ( unsigned char *, int );
void MPID_BSwap_N_copy ( unsigned char *, unsigned char *, int, int );
void MPID_BSwap_short_copy ( unsigned char *, unsigned char *, int );
void MPID_BSwap_int_copy ( unsigned char *, unsigned char *, int );
void MPID_BSwap_long_copy ( unsigned char *, unsigned char *, int );
void MPID_BSwap_float_copy ( unsigned char *, unsigned char *, int );
void MPID_BSwap_double_copy ( unsigned char *, unsigned char *, int );
void MPID_BSwap_long_double_copy ( unsigned char *, unsigned char *, int );

int MPID_Type_swap_copy ( unsigned char *, unsigned char *, 
				    struct MPIR_DATATYPE *, int, void * );
void MPID_Type_swap_inplace ( unsigned char *, 
					struct MPIR_DATATYPE *, int );
int MPID_Type_XDR_encode ( unsigned char *, unsigned char *, 
				     struct MPIR_DATATYPE *, int, void * );
int MPID_Type_XDR_decode ( unsigned char *, int, 
				     struct MPIR_DATATYPE *, int, 
				     unsigned char *, int, int *, int *, 
				     void * );
int MPID_Type_convert_copy ( struct MPIR_COMMUNICATOR *, void *, 
				       int, void *, 
				       struct MPIR_DATATYPE *, 
				       int, int, int * );
int MPID_Mem_convert_len ( MPID_Msgrep_t, struct MPIR_DATATYPE *, int );
int MPID_Mem_XDR_Len ( struct MPIR_DATATYPE *, int );

int MPIR_Comm_needs_conversion ( struct MPIR_COMMUNICATOR * );
int MPIR_Dest_needs_converstion ( int );
void MPIR_Pack_Hvector ( struct MPIR_COMMUNICATOR *, char *, int, 
				   struct MPIR_DATATYPE*, int, char * );
void MPIR_UnPack_Hvector ( char *, int, struct MPIR_DATATYPE*, int, char * );
int MPIR_HvectorLen ( int, struct MPIR_DATATYPE * );
int MPIR_PackMessage ( char *, int, struct MPIR_DATATYPE *, int, 
				 int, MPI_Request );
int MPIR_EndPackMessage ( MPI_Request );
int MPIR_SetupUnPackMessage ( char *, int, struct MPIR_DATATYPE *, 
					int, MPI_Request );
int MPIR_Receive_setup ( MPI_Request * );
int MPIR_Send_setup ( MPI_Request * );
int MPIR_SendBufferFree ( MPI_Request );

int MPIR_Elementcnt ( unsigned char *, int, struct MPIR_DATATYPE*, 
			    int, unsigned char *, int, int *, int *, void * );
void DMPI_msg_arrived ( int, int, MPIR_CONTEXT, MPIR_RHANDLE **, int * );
void DMPI_free_unexpected ( MPIR_RHANDLE * );

/*
int MPIR_Pack  ( struct MPIR_COMMUNICATOR *, int, void *, int, 
                           struct MPIR_DATATYPE *,
			   void *, int, int * );
int MPIR_Pack_size ( int, struct MPIR_DATATYPE *, 
                               struct MPIR_COMMUNICATOR *, int, int * );
*/
int MPIR_Pack2 ( char *, int, int, struct MPIR_DATATYPE *, 
		int  (*) (unsigned char *, unsigned char *, 
				     struct MPIR_DATATYPE*, int, 
				     void *), void *, char *, int *, int * );
int MPIR_Unpack2 ( char *, int, struct MPIR_DATATYPE*, 
		   int  (*)(unsigned char *, int, struct MPIR_DATATYPE*, int,
			   unsigned char *, int, int *, int *, void *),
		   void *, char *, int, int *, int * );
int MPIR_Unpack ( struct MPIR_COMMUNICATOR *, void *, int, int, 
			    struct MPIR_DATATYPE *,
			    MPID_Msgrep_t, 
			    void *, int *, int * );

int MPIR_Printcontig ( unsigned char *, unsigned char *, 
				 struct MPIR_DATATYPE*, int, void *);
int MPIR_Printcontig2 ( char *, int, struct MPIR_DATATYPE*, 
				  int, char *, void * );
int MPIR_Printcontig2a ( unsigned char *, int, 
				   struct MPIR_DATATYPE*, int, 
			unsigned char *, int, int *, int *, void * );

#ifdef MPID_INCLUDE_STDIO
int MPIR_PrintDatatypePack ( FILE *, int, struct MPIR_DATATYPE*, long, long );
int MPIR_PrintDatatypeUnpack ( FILE *, int, MPI_Datatype, long, long );
#endif

#endif
