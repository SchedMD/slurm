#ifndef _MPI_PT2PT
#define _MPI_PT2PT

#if defined(USE_STDARG) && !defined(MPIR_USE_STDARG)
/* This SAME test must be used in pt2pt/mperror.c */
#define MPIR_USE_STDARG
#endif

int MPIR_Pack  ( struct MPIR_COMMUNICATOR *, int, void *, int, 
			   struct MPIR_DATATYPE *, void *, int, int *);
int MPIR_Pack_size  ( int, struct MPIR_DATATYPE *, 
				struct MPIR_COMMUNICATOR *, int, int *);
int MPIR_Unpack ( struct MPIR_COMMUNICATOR *, void *, int, int, 
			    struct MPIR_DATATYPE *, 
			    MPID_Msgrep_t, void *, int *, int * );
int MPIR_UnPackMessage ( char *, int, MPI_Datatype, int, MPI_Request, int * );

/* These are used in the debugging-enabled version */
void MPIR_Sendq_init ( void );
void MPIR_Sendq_finalize ( void );
void MPIR_Remember_send ( MPIR_SHANDLE *, void *, int, MPI_Datatype,
				    int, int, struct MPIR_COMMUNICATOR * );
void MPIR_Forget_send ( MPIR_SHANDLE * );

/* Datatype service routines */
int MPIR_Type_free ( struct MPIR_DATATYPE ** );
#ifdef FOO
void MPIR_Type_free_struct ( struct MPIR_DATATYPE * );
#endif
struct MPIR_DATATYPE *MPIR_Type_dup ( struct MPIR_DATATYPE * );
int MPIR_Type_permanent ( struct MPIR_DATATYPE * );
void MPIR_Free_perm_type ( MPI_Datatype );
void MPIR_Free_struct_internals ( struct MPIR_DATATYPE * );
void MPIR_Type_get_limits ( struct MPIR_DATATYPE *, MPI_Aint *, MPI_Aint *);
extern MPI_Handler_function MPIR_Errors_are_fatal;
extern MPI_Handler_function MPIR_Errors_return;
extern MPI_Handler_function MPIR_Errors_warn;
/* Since these are declared as handler functions, we do not
   redeclare them */
#ifdef MPIR_USE_STDARG
/* gcc requires an explicit declaration when checking for strict prototypes */
void MPIR_Errors_are_fatal ( MPI_Comm *, int *, ... );
void MPIR_Errors_return ( MPI_Comm *, int *, ... );
void MPIR_Errors_warn ( MPI_Comm *, int *, ... );
#else
#ifdef FOO
/* Otherwise, just accept the typedef declaration */
void MPIR_Errors_are_fatal ( MPI_Comm *, int *, char *, char *, int *);
void MPIR_Errors_return ( MPI_Comm *, int *, char *, char *, int *);
void MPIR_Errors_warn ( MPI_Comm *, int *, char *, char *, int *);
#endif
#endif /* MPIR_USE_STDARG */

#endif
