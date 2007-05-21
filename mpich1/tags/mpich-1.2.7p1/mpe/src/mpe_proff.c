/* myprof.c */
/* Custom Fortran interface file */
/* These have been edited because they require special string processing */
/* See mpe_prof.c for what these are interfacing to */

/* 
 * If not building for MPICH, then MPE_ErrPrint and the mpi_iargc_/mpir_getarg_
 * calls need to be replaced.
 */

#ifdef MPI_BUILD_PROFILING
#undef MPI_BUILD_PROFILING
#endif
#include "mpeconf.h"
#include "mpi.h"
#include "mpe.h"

#include <stdio.h>
#define MPE_ErrPrint(comm,errcode,str) (fprintf( stderr, "%s\n", str ),errcode)

#if defined( STDC_HEADERS ) || defined( HAVE_STDARG_H )
#include <stdarg.h>
#endif

#if ! defined( MPICH_NAME ) || defined ( MPICH2 )
/* If we aren't running MPICH, just use fprintf for errors */
/* Also avoid Fortran arguments */
#define mpir_iargc_() 0
#define mpir_getarg_( idx, str, ln ) strncpy(str,"Unknown",ln)
#else
/* Make sure that we get the correct Fortran form */

#ifdef F77_NAME_UPPER
#define mpir_iargc_ MPIR_IARGC
#define mpir_getarg_ MPIR_GETARG
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpir_iargc_ mpir_iargc__
#define mpir_getarg_ mpir_getarg__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpir_iargc_ mpir_iargc
#define mpir_getarg_ mpir_getarg
#endif

#endif

/* 
   Include a definition of MALLOC and FREE to allow the use of MPICH
   memory debug code 
*/
#if defined(MPIR_MEMDEBUG)
/* Enable memory tracing.  This requires MPICH's mpid/util/tr2.c codes */
#define MALLOC(a)    MPID_trmalloc((unsigned)(a),__LINE__,__FILE__)
#define FREE(a)      MPID_trfree(a,__LINE__,__FILE__)

#else
#define MALLOC(a) malloc(a)
#define FREE(a)   free(a)
#define MPID_trvalid(a)
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#endif

#ifndef DEBUG_ALL
#define DEBUG_ALL
#endif

/* Fortran logical values */
MPI_Fint MPER_F_TRUE = MPE_F77_TRUE_VALUE;
MPI_Fint MPER_F_FALSE = MPE_F77_FALSE_VALUE;

/* Fortran logical values */
#ifndef _CRAY
/*  extern MPI_Fint MPER_F_TRUE, MPER_F_FALSE;  */
#define MPIR_TO_FLOG(a) ((a) ? MPER_F_TRUE : MPER_F_FALSE)
/* 
   Note on true and false.  This code is only an approximation.
   Some systems define either true or false, and allow some or ALL other
   patterns for the other.  This is just like C, where 0 is false and 
   anything not zero is true.  Modify this test as necessary for your
   system.
 */
#define MPIR_FROM_FLOG(a) ( (a) == MPER_F_TRUE ? 1 : 0 )

#else
/* CRAY Vector processors only; these are defined in /usr/include/fortran.h 
   Thanks to lmc@cray.com */
#define MPIR_TO_FLOG(a) (_btol(a))
#define MPIR_FROM_FLOG(a) ( _ltob(&(a)) )    /*(a) must be a pointer */
#endif

/* MPIR_F_MPI_BOTTOM is the address of the Fortran MPI_BOTTOM value */
extern void *MPIR_F_MPI_BOTTOM;

/* A temporary until we can make this do
   MPIR_F_PTR(a) (((a)==(MPIR_F_MPI_BOTTOM))?MPI_BOTTOM:a) 
*/
#define MPIR_F_PTR(a) (a)

/* Error handling */
#if defined(USE_STDARG) && !defined(USE_OLDSTYLE_STDARG)
int MPER_Err_setmsg( int, int, const char *, const char *, const char *, ... );
#else
int MPER_Err_setmsg();
#endif

#ifndef MPIR_ERR_DEFAULT
#define MPIR_ERR_DEFAULT 1
#endif

#ifndef MPIR_ERROR
#define MPIR_ERROR(a,b,c) fprintf(stderr, "%s\n", c )
#endif

#ifndef MPIR_FALLOC
#define MPIR_FALLOC(ptr,expr,a,b,c) \
    if (! (ptr = (expr))) { MPIR_ERROR(a,b,c); }
#endif

#ifndef MPIR_USE_LOCAL_ARRAY
#define MPIR_USE_LOCAL_ARRAY 32
#endif

#ifndef HAVE_MPI_COMM_F2C
#define MPI_Comm_c2f(comm) (MPI_Fint)(comm)
#define MPI_Comm_f2c(comm) (MPI_Comm)(comm)
#endif
#ifndef HAVE_MPI_TYPE_F2C
#define MPI_Type_c2f(datatype) (MPI_Fint)(datatype)
#define MPI_Type_f2c(datatype) (MPI_Datatype)(datatype)
#endif
#ifndef HAVE_MPI_GROUP_F2C
#define MPI_Group_c2f(group) (MPI_Fint)(group)
#define MPI_Group_f2c(group) (MPI_Group)(group)
#endif
#ifndef HAVE_MPI_REQUEST_F2C
#define MPI_Request_c2f(request) (MPI_Fint)(request)
#define MPI_Request_f2c(request) (MPI_Request)(request)
#endif
#ifndef HAVE_MPI_OP_F2C
#define MPI_Op_c2f(op) (MPI_Fint)(op)
#define MPI_Op_f2c(op) (MPI_Op)(op)
#endif
#ifndef HAVE_MPI_ERRHANDLER_F2C
#define MPI_Errhandler_c2f(errhandler) (MPI_Fint)(errhandler)
#define MPI_Errhandler_f2c(errhandler) (MPI_Errhandler)(errhandler)
#endif
#ifndef HAVE_MPI_STATUS_F2C
#define MPI_Status_f2c(f_status,c_status) memcpy(c_status,f_status,sizeof(MPI_Status))
#define MPI_Status_c2f(c_status,f_status) memcpy(f_status,c_status,sizeof(MPI_Status))
#endif

#ifdef F77_NAME_UPPER
#define mpi_init_ MPI_INIT
#define mpi_bsend_init_ MPI_BSEND_INIT
#define mpi_bsend_ MPI_BSEND
#define mpi_buffer_attach_ MPI_BUFFER_ATTACH
#define mpi_buffer_detach_ MPI_BUFFER_DETACH
#define mpi_cancel_ MPI_CANCEL
#define mpi_request_free_ MPI_REQUEST_FREE
#define mpi_recv_init_ MPI_RECV_INIT
#define mpi_send_init_ MPI_SEND_INIT
#define mpi_get_count_ MPI_GET_COUNT
#define mpi_get_elements_ MPI_GET_ELEMENTS
#define mpi_ibsend_ MPI_IBSEND
#define mpi_iprobe_ MPI_IPROBE
#define mpi_irecv_ MPI_IRECV
#define mpi_irsend_ MPI_IRSEND
#define mpi_isend_ MPI_ISEND
#define mpi_issend_ MPI_ISSEND
#define mpi_pack_size_ MPI_PACK_SIZE
#define mpi_pack_ MPI_PACK
#define mpi_probe_ MPI_PROBE
#define mpi_recv_ MPI_RECV
#define mpi_rsend_init_ MPI_RSEND_INIT
#define mpi_rsend_ MPI_RSEND
#define mpi_send_ MPI_SEND
#define mpi_sendrecv_ MPI_SENDRECV
#define mpi_sendrecv_replace_ MPI_SENDRECV_REPLACE
#define mpi_ssend_init_ MPI_SSEND_INIT
#define mpi_ssend_ MPI_SSEND
#define mpi_startall_ MPI_STARTALL
#define mpi_start_ MPI_START
#define mpi_testall_ MPI_TESTALL
#define mpi_testany_ MPI_TESTANY
#define mpi_test_canceled_ MPI_TESTCANCEL
#define mpi_test_ MPI_TEST
#define mpi_testsome_ MPI_TESTSOME
#define mpi_type_commit_ MPI_TYPE_COMMIT
#define mpi_type_contiguous_ MPI_TYPE_CONTIGUOUS
#define mpi_type_extent_ MPI_TYPE_EXTENT
#define mpi_type_free_ MPI_TYPE_FREE
#define mpi_type_hindexed_ MPI_TYPE_HINDEXED
#define mpi_type_hvector_ MPI_TYPE_HVECTOR
#define mpi_type_indexed_ MPI_TYPE_INDEXED
#define mpi_type_lb_ MPI_TYPE_LB
#define mpi_type_size_ MPI_TYPE_SIZE
#define mpi_type_struct_ MPI_TYPE_STRUCT
#define mpi_type_ub_ MPI_TYPE_UB
#define mpi_type_vector_ MPI_TYPE_VECTOR
#define mpi_unpack_ MPI_UNPACK
#define mpi_waitall_ MPI_WAITALL
#define mpi_waitany_ MPI_WAITANY
#define mpi_wait_ MPI_WAIT
#define mpi_waitsome_ MPI_WAITSOME
#define mpi_allgather_ MPI_ALLGATHER
#define mpi_allgatherv_ MPI_ALLGATHERV
#define mpi_allreduce_ MPI_ALLREDUCE
#define mpi_alltoall_ MPI_ALLTOALL
#define mpi_alltoallv_ MPI_ALLTOALLV
#define mpi_barrier_ MPI_BARRIER
#define mpi_bcast_ MPI_BCAST
#define mpi_gather_ MPI_GATHER
#define mpi_gatherv_ MPI_GATHERV
#define mpi_op_create_ MPI_OP_CREATE
#define mpi_op_free_ MPI_OP_FREE
#define mpi_reduce_scatter_ MPI_REDUCE_SCATTER
#define mpi_reduce_ MPI_REDUCE
#define mpi_scan_ MPI_SCAN
#define mpi_scatter_ MPI_SCATTER
#define mpi_scatterv_ MPI_SCATTERV
#define mpi_finalize_ MPI_FINALIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_init_ mpi_init__
#define mpi_bsend_init_ mpi_bsend_init__
#define mpi_bsend_ mpi_bsend__
#define mpi_buffer_attach_ mpi_buffer_attach__
#define mpi_buffer_detach_ mpi_buffer_detach__
#define mpi_cancel_ mpi_cancel__
#define mpi_request_free_ mpi_request_free__
#define mpi_recv_init_ mpi_recv_init__
#define mpi_send_init_ mpi_send_init__
#define mpi_get_count_ mpi_get_count__
#define mpi_get_elements_ mpi_get_elements__
#define mpi_ibsend_ mpi_ibsend__
#define mpi_iprobe_ mpi_iprobe__
#define mpi_irecv_ mpi_irecv__
#define mpi_irsend_ mpi_irsend__
#define mpi_isend_ mpi_isend__
#define mpi_issend_ mpi_issend__
#define mpi_pack_size_ mpi_pack_size__
#define mpi_pack_ mpi_pack__
#define mpi_probe_ mpi_probe__
#define mpi_recv_ mpi_recv__
#define mpi_rsend_init_ mpi_rsend_init__
#define mpi_rsend_ mpi_rsend__
#define mpi_send_ mpi_send__
#define mpi_sendrecv_ mpi_sendrecv__
#define mpi_sendrecv_replace_ mpi_sendrecv_replace__
#define mpi_ssend_init_ mpi_ssend_init__
#define mpi_ssend_ mpi_ssend__
#define mpi_startall_ mpi_startall__
#define mpi_start_ mpi_start__
#define mpi_testall_ mpi_testall__
#define mpi_testany_ mpi_testany__
#define mpi_test_cancelled_ mpi_test_cancelled__
#define mpi_test_ mpi_test__
#define mpi_testsome_ mpi_testsome__
#define mpi_type_commit_ mpi_type_commit__
#define mpi_type_contiguous_ mpi_type_contiguous__
#define mpi_type_extent_ mpi_type_extent__
#define mpi_type_free_ mpi_type_free__
#define mpi_type_hindexed_ mpi_type_hindexed__
#define mpi_type_hvector_ mpi_type_hvector__
#define mpi_type_indexed_ mpi_type_indexed__
#define mpi_type_lb_ mpi_type_lb__
#define mpi_type_size_ mpi_type_size__
#define mpi_type_struct_ mpi_type_struct__
#define mpi_type_ub_ mpi_type_ub__
#define mpi_type_vector_ mpi_type_vector__
#define mpi_unpack_ mpi_unpack__
#define mpi_waitall_ mpi_waitall__
#define mpi_waitany_ mpi_waitany__
#define mpi_wait_ mpi_wait__
#define mpi_waitsome_ mpi_waitsome__
#define mpi_allgather_ mpi_allgather__
#define mpi_allgatherv_ mpi_allgatherv__
#define mpi_allreduce_ mpi_allreduce__
#define mpi_alltoall_ mpi_alltoall__
#define mpi_alltoallv_ mpi_alltoallv__
#define mpi_barrier_ mpi_barrier__
#define mpi_bcast_ mpi_bcast__
#define mpi_gather_ mpi_gather__
#define mpi_gatherv_ mpi_gatherv__
#define mpi_op_create_ mpi_op_create__
#define mpi_op_free_ mpi_op_free__
#define mpi_reduce_scatter_ mpi_reduce_scatter__
#define mpi_reduce_ mpi_reduce__
#define mpi_scan_ mpi_scan__
#define mpi_scatter_ mpi_scatter__
#define mpi_scatterv_ mpi_scatterv__
#define mpi_finalize_ mpi_finalize__
#elif defined(F77_NAME_LOWER)
#define mpi_init_ mpi_init
#define mpi_bsend_ mpi_bsend
#define mpi_bsend_init_ mpi_bsend_init
#define mpi_buffer_attach_ mpi_buffer_attach
#define mpi_buffer_detach_ mpi_buffer_detach
#define mpi_cancel_ mpi_cancel
#define mpi_request_free_ mpi_request_free
#define mpi_recv_init_ mpi_recv_init
#define mpi_send_init_ mpi_send_init
#define mpi_get_count_ mpi_get_count
#define mpi_get_elements_ mpi_get_elements
#define mpi_ibsend_ mpi_ibsend
#define mpi_iprobe_ mpi_iprobe
#define mpi_irecv_ mpi_irecv
#define mpi_irsend_ mpi_irsend
#define mpi_isend_ mpi_isend
#define mpi_issend_ mpi_issend
#define mpi_pack_size_ mpi_pack_size
#define mpi_pack_ mpi_pack
#define mpi_probe_ mpi_probe
#define mpi_recv_ mpi_recv
#define mpi_rsend_init_ mpi_rsend_init
#define mpi_rsend_ mpi_rsend
#define mpi_send_ mpi_send
#define mpi_sendrecv_ mpi_sendrecv
#define mpi_sendrecv_replace_ mpi_sendrecv_replace
#define mpi_ssend_init_ mpi_ssend_init
#define mpi_ssend_ mpi_ssend
#define mpi_startall_ mpi_startall
#define mpi_start_ mpi_start
#define mpi_testall_ mpi_testall
#define mpi_testany_ mpi_testany
#define mpi_test_cancelled_ mpi_test_cancelled
#define mpi_test_ mpi_test
#define mpi_testsome_ mpi_testsome
#define mpi_type_commit_ mpi_type_commit
#define mpi_type_contiguous_ mpi_type_contiguous
#define mpi_type_extent_ mpi_type_extent
#define mpi_type_free_ mpi_type_free
#define mpi_type_hindexed_ mpi_type_hindexed
#define mpi_type_hvector_ mpi_type_hvector
#define mpi_type_indexed_ mpi_type_indexed
#define mpi_type_lb_ mpi_type_lb
#define mpi_type_size_ mpi_type_size
#define mpi_type_struct_ mpi_type_struct
#define mpi_type_ub_ mpi_type_ub
#define mpi_type_vector_ mpi_type_vector
#define mpi_unpack_ mpi_unpack
#define mpi_waitall_ mpi_waitall
#define mpi_waitany_ mpi_waitany
#define mpi_wait_ mpi_wait
#define mpi_waitsome_ mpi_waitsome
#define mpi_allgather_ mpi_allgather
#define mpi_allgatherv_ mpi_allgatherv
#define mpi_allreduce_ mpi_allreduce
#define mpi_alltoall_ mpi_alltoall
#define mpi_alltoallv_ mpi_alltoallv
#define mpi_barrier_ mpi_barrier
#define mpi_bcast_ mpi_bcast
#define mpi_gather_ mpi_gather
#define mpi_gatherv_ mpi_gatherv
#define mpi_op_create_ mpi_op_create
#define mpi_op_free_ mpi_op_free
#define mpi_reduce_scatter_ mpi_reduce_scatter
#define mpi_reduce_ mpi_reduce
#define mpi_scan_ mpi_scan
#define mpi_scatter_ mpi_scatter
#define mpi_scatterv_ mpi_scatterv
#define mpi_finalize_ mpi_finalize
#endif

/*
 * Define prototypes next to the fortran2c wrapper to keep the compiler happy
 */



#if defined(USE_STDARG) && !defined(USE_OLDSTYLE_STDARG)
int MPER_Err_setmsg( int errclass, int errkind,
		     const char *routine_name, 
		     const char *generic_string, 
		     const char *default_string, ... )
{
    va_list Argp;
    va_start( Argp, default_string );
#else
/* This assumes old-style varargs support */
int MPER_Err_setmsg( errclass, errkind, routine_name, 
		     generic_string, default_string, va_alist )
int errclass, errkind;
const char *routine_name, *generic_string, *default_string;
va_dcl
{
    va_list Argp;
    va_start( Argp );
#endif

    va_end( Argp );
    fprintf( stderr, __FILE__":MPER_Err_setmg(%s) in MPE\n", routine_name );
    return errclass;
}


/****************************************************************************/

void mpi_init_ ( int * );
void mpi_init_( int *ierr )
{
    int Argc;
    int i, argsize = 1024;
    char **Argv, *p;
    int  ArgcSave;           /* Save the argument count */
    char **ArgvSave;         /* Save the pointer to the argument vector */

/* Recover the args with the Fortran routines iargc_ and getarg_ */
    ArgcSave	= Argc = mpir_iargc_() + 1; 
    ArgvSave	= Argv = (char **)MALLOC( Argc * sizeof(char *) );    
    if (!Argv) {
	*ierr = MPE_ErrPrint( (MPI_Comm)0, MPI_ERR_OTHER, 
			    "Out of space in MPI_INIT" );
	return;
    }
    for (i=0; i<Argc; i++) {
	ArgvSave[i] = Argv[i] = (char *)MALLOC( argsize + 1 );
	if (!Argv[i]) {
	    *ierr = MPE_ErrPrint( (MPI_Comm)0, MPI_ERR_OTHER, 
				"Out of space in MPI_INIT" );
	    return;
        }
	mpir_getarg_( &i, Argv[i], argsize );

	/* Trim trailing blanks */
	p = Argv[i] + argsize - 1;
	while (p > Argv[i]) {
	    if (*p != ' ') {
		p[1] = '\0';
		break;
	    }
	    p--;
	}
    }

    *ierr = MPI_Init( &Argc, &Argv );
    
    /* Recover space */
    for (i=0; i<ArgcSave; i++) {
	FREE( ArgvSave[i] );
    }
    FREE( ArgvSave );
}

void mpi_bsend_init_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *,
		               MPI_Fint *, MPI_Fint *, MPI_Fint *,
			           MPI_Fint * );
void mpi_bsend_init_( void *buf, MPI_Fint *count, MPI_Fint *datatype,
		              MPI_Fint *dest, MPI_Fint *tag, MPI_Fint *comm,
		              MPI_Fint *request, MPI_Fint *__ierr )
{
    MPI_Request lrequest;
    *__ierr = MPI_Bsend_init( MPIR_F_PTR(buf), (int)*count,
                              MPI_Type_f2c(*datatype),
                              (int)*dest,
                              (int)*tag, MPI_Comm_f2c(*comm),
                              &lrequest);
    *request = MPI_Request_c2f(lrequest);
}

void mpi_bsend_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *,
		          MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_bsend_( void *buf, MPI_Fint *count, MPI_Fint *datatype, 
                 MPI_Fint *dest, MPI_Fint *tag, MPI_Fint *comm, 
		 MPI_Fint *__ierr )
{
    *__ierr = MPI_Bsend( MPIR_F_PTR(buf), (int)*count, MPI_Type_f2c(*datatype),
                         (int)*dest, (int)*tag, MPI_Comm_f2c(*comm) );
}

void mpi_buffer_attach_ ( void *, MPI_Fint *, MPI_Fint * );
void mpi_buffer_attach_( void *buffer, MPI_Fint *size, MPI_Fint *__ierr )
{
    *__ierr = MPI_Buffer_attach(buffer,(int)*size);
}

void mpi_buffer_detach_ ( void **, MPI_Fint *, MPI_Fint * );
void mpi_buffer_detach_( void **buffer, MPI_Fint *size, MPI_Fint *__ierr )
{
    void *tmp = (void *)buffer;
    int lsize;

    *__ierr = MPI_Buffer_detach(&tmp,&lsize);
    *size = (MPI_Fint)lsize;
}

void mpi_cancel_ (MPI_Fint *, MPI_Fint *);
void mpi_cancel_( MPI_Fint *request, MPI_Fint *__ierr )
{
    MPI_Request lrequest;

    lrequest = MPI_Request_f2c(*request);  
    *__ierr = MPI_Cancel(&lrequest); 
}

void mpi_request_free_ ( MPI_Fint *, MPI_Fint * );
void mpi_request_free_( MPI_Fint *request, MPI_Fint *__ierr )
{
    MPI_Request lrequest = MPI_Request_f2c(*request);
    *__ierr = MPI_Request_free( &lrequest );
    *request = MPI_Request_c2f(lrequest);
}

void mpi_recv_init_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, 
		              MPI_Fint *, MPI_Fint *, MPI_Fint *,
			          MPI_Fint * );
void mpi_recv_init_( void *buf, MPI_Fint *count, MPI_Fint *datatype,
		             MPI_Fint *source, MPI_Fint *tag, MPI_Fint *comm,
		             MPI_Fint *request, MPI_Fint *__ierr )
{
    MPI_Request lrequest;
    *__ierr = MPI_Recv_init(MPIR_F_PTR(buf),(int)*count,
                            MPI_Type_f2c(*datatype),(int)*source,(int)*tag,
                            MPI_Comm_f2c(*comm),&lrequest);
    *request = MPI_Request_c2f(lrequest);
}

void mpi_send_init_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, 
		              MPI_Fint *, MPI_Fint *, MPI_Fint *,
			          MPI_Fint * );
void mpi_send_init_( void *buf, MPI_Fint *count, MPI_Fint *datatype,
		             MPI_Fint *dest, MPI_Fint *tag, MPI_Fint *comm,
		             MPI_Fint *request, MPI_Fint *__ierr )
{
    MPI_Request lrequest;
    *__ierr = MPI_Send_init(MPIR_F_PTR(buf),(int)*count,
                            MPI_Type_f2c(*datatype),(int)*dest,(int)*tag,
                            MPI_Comm_f2c(*comm),&lrequest);
    *request = MPI_Request_c2f( lrequest );
}

void mpi_get_count_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_get_count_( MPI_Fint *status, MPI_Fint *datatype, MPI_Fint *count,
		             MPI_Fint *__ierr )
{
    int lcount;
    MPI_Status c_status;

    MPI_Status_f2c(status, &c_status);
    *__ierr = MPI_Get_count(&c_status, MPI_Type_f2c(*datatype),
                            &lcount);
    *count = (MPI_Fint)lcount;
}

void mpi_get_elements_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_get_elements_ ( MPI_Fint *status, MPI_Fint *datatype,
		                 MPI_Fint *elements, MPI_Fint *__ierr )
{
    int lelements;
    MPI_Status c_status;

    MPI_Status_f2c(status, &c_status);
    *__ierr = MPI_Get_elements(&c_status,MPI_Type_f2c(*datatype),
		               &lelements);
    *elements = (MPI_Fint)lelements;
}

void mpi_ibsend_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *,
                           MPI_Fint *, MPI_Fint *, MPI_Fint *,
                           MPI_Fint * );
void mpi_ibsend_( void *buf, MPI_Fint *count, MPI_Fint *datatype,
		          MPI_Fint *dest, MPI_Fint *tag, MPI_Fint *comm,
			      MPI_Fint *request, MPI_Fint *__ierr )
{
    MPI_Request lrequest;
    *__ierr = MPI_Ibsend(MPIR_F_PTR(buf),(int)*count,MPI_Type_f2c(*datatype),
                         (int)*dest,(int)*tag,MPI_Comm_f2c(*comm),
                         &lrequest);
    *request = MPI_Request_c2f(lrequest);
}

void mpi_iprobe_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *,
                   MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_iprobe_( MPI_Fint *source, MPI_Fint *tag, MPI_Fint *comm,
		          MPI_Fint *flag, MPI_Fint *status, MPI_Fint *__ierr )
{
    int lflag;
    MPI_Status c_status;

    *__ierr = MPI_Iprobe((int)*source,(int)*tag,MPI_Comm_f2c(*comm),
                         &lflag,&c_status);
    *flag = MPIR_TO_FLOG(lflag);
    MPI_Status_c2f(&c_status, status);
}

void mpi_irecv_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *,
		          MPI_Fint *, MPI_Fint *, MPI_Fint *,
			      MPI_Fint * );
void mpi_irecv_( void *buf, MPI_Fint *count, MPI_Fint *datatype,
		         MPI_Fint *source, MPI_Fint *tag, MPI_Fint *comm,
		         MPI_Fint *request, MPI_Fint *__ierr )
{
    MPI_Request lrequest;
    *__ierr = MPI_Irecv(MPIR_F_PTR(buf),(int)*count,MPI_Type_f2c(*datatype),
                        (int)*source,(int)*tag,
                        MPI_Comm_f2c(*comm),&lrequest);
    *request = MPI_Request_c2f(lrequest);
}

void mpi_irsend_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *,
		           MPI_Fint *, MPI_Fint *, MPI_Fint *,
			       MPI_Fint * );
void mpi_irsend_( void *buf, MPI_Fint *count, MPI_Fint *datatype,
		          MPI_Fint *dest, MPI_Fint *tag, MPI_Fint *comm,
		          MPI_Fint *request, MPI_Fint *__ierr )
{
    MPI_Request lrequest;
    *__ierr = MPI_Irsend(MPIR_F_PTR(buf),(int)*count,MPI_Type_f2c(*datatype),
                         (int)*dest,(int)*tag,
			 MPI_Comm_f2c(*comm),&lrequest);
    *request = MPI_Request_c2f(lrequest);
}

void mpi_isend_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *,
		          MPI_Fint *, MPI_Fint *, MPI_Fint *,
			      MPI_Fint * );
void mpi_isend_( void *buf, MPI_Fint *count, MPI_Fint *datatype,
		         MPI_Fint *dest, MPI_Fint *tag, MPI_Fint *comm,
		         MPI_Fint *request, MPI_Fint *__ierr )
{
    MPI_Request lrequest;
    *__ierr = MPI_Isend(MPIR_F_PTR(buf),(int)*count,MPI_Type_f2c(*datatype),
                        (int)*dest,
                        (int)*tag,MPI_Comm_f2c(*comm),
                        &lrequest);
    *request = MPI_Request_c2f(lrequest);
}

void mpi_issend_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *,
		           MPI_Fint *, MPI_Fint *, MPI_Fint *,
			       MPI_Fint * );
void mpi_issend_( void *buf, MPI_Fint *count, MPI_Fint *datatype,
		          MPI_Fint *dest, MPI_Fint *tag, MPI_Fint *comm,
		          MPI_Fint *request, MPI_Fint *__ierr )
{
    MPI_Request lrequest;
    *__ierr = MPI_Issend(MPIR_F_PTR(buf),(int)*count,MPI_Type_f2c(*datatype),
                         (int)*dest, (int)*tag,
                         MPI_Comm_f2c(*comm),
                         &lrequest);
    *request = MPI_Request_c2f(lrequest);
}

void mpi_pack_size_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *,
		              MPI_Fint *, MPI_Fint * );
void mpi_pack_size_ ( MPI_Fint *incount, MPI_Fint *datatype, MPI_Fint *comm,
		              MPI_Fint *size, MPI_Fint *__ierr )
{
    int lsize;

    *__ierr = MPI_Pack_size((int)*incount, MPI_Type_f2c(*datatype),
                            MPI_Comm_f2c(*comm), &lsize);
    *size = (MPI_Fint)lsize;
}

void mpi_pack_ ( void *, MPI_Fint *, MPI_Fint *, void *,
		         MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_pack_ ( void *inbuf, MPI_Fint *incount, MPI_Fint *type,
		         void *outbuf, MPI_Fint *outcount, MPI_Fint *position,
		         MPI_Fint *comm, MPI_Fint *__ierr )
{
    int lposition;

    lposition = (int)*position;
    *__ierr = MPI_Pack(MPIR_F_PTR(inbuf), (int)*incount, MPI_Type_f2c(*type),
                       outbuf, (int)*outcount, &lposition,
                       MPI_Comm_f2c(*comm));
    *position = (MPI_Fint)lposition;
}

void mpi_probe_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *,
                  MPI_Fint *, MPI_Fint * );
void mpi_probe_( MPI_Fint *source, MPI_Fint *tag, MPI_Fint *comm,
		         MPI_Fint *status, MPI_Fint *__ierr )
{
    MPI_Status c_status;

    *__ierr = MPI_Probe((int)*source, (int)*tag, MPI_Comm_f2c(*comm),
                        &c_status);
    MPI_Status_c2f(&c_status, status);
}

void mpi_recv_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *,
                         MPI_Fint *, MPI_Fint *, MPI_Fint *,
                         MPI_Fint * );
void mpi_recv_( void *buf, MPI_Fint *count, MPI_Fint *datatype,
		        MPI_Fint *source, MPI_Fint *tag, MPI_Fint *comm,
		        MPI_Fint *status, MPI_Fint *__ierr )
{
    MPI_Status c_status;

    *__ierr = MPI_Recv(MPIR_F_PTR(buf), (int)*count,MPI_Type_f2c(*datatype),
                       (int)*source, (int)*tag,
                       MPI_Comm_f2c(*comm), &c_status);
    MPI_Status_c2f(&c_status, status);
}

void mpi_rsend_init_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *,
		               MPI_Fint *, MPI_Fint *, MPI_Fint *,
			           MPI_Fint * );
void mpi_rsend_init_( void *buf, MPI_Fint *count, MPI_Fint *datatype,
		              MPI_Fint *dest, MPI_Fint *tag, MPI_Fint *comm,
		              MPI_Fint *request, MPI_Fint *__ierr )
{
    MPI_Request lrequest;
    *__ierr = MPI_Rsend_init(MPIR_F_PTR(buf), (int)*count,
                             MPI_Type_f2c(*datatype), (int)*dest,
                             (int)*tag,
                             MPI_Comm_f2c(*comm), &lrequest);
    *request = MPI_Request_c2f(lrequest);
}

void mpi_rsend_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *,
                          MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_rsend_( void *buf, MPI_Fint *count, MPI_Fint *datatype,
		         MPI_Fint *dest, MPI_Fint *tag, MPI_Fint *comm,
		         MPI_Fint *__ierr )
{
    *__ierr = MPI_Rsend(MPIR_F_PTR(buf), (int)*count,MPI_Type_f2c(*datatype),
                        (int)*dest, (int)*tag, MPI_Comm_f2c(*comm));
}

void mpi_send_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *,
                         MPI_Fint *, MPI_Fint*, MPI_Fint * );

void mpi_send_( void *buf, MPI_Fint *count, MPI_Fint *datatype,
		        MPI_Fint *dest, MPI_Fint *tag, MPI_Fint *comm,
		        MPI_Fint *__ierr )
{
    *__ierr = MPI_Send(MPIR_F_PTR(buf), (int)*count, MPI_Type_f2c(*datatype),
                       (int)*dest, (int)*tag, MPI_Comm_f2c(*comm));
}

void mpi_sendrecv_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *,
                     void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *,
                     MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_sendrecv_( void *sendbuf, MPI_Fint *sendcount, MPI_Fint *sendtype,
		            MPI_Fint *dest, MPI_Fint *sendtag,
		            void *recvbuf, MPI_Fint *recvcount, MPI_Fint *recvtype,
		            MPI_Fint *source, MPI_Fint *recvtag,
		            MPI_Fint *comm, MPI_Fint *status, MPI_Fint *__ierr )
{
    MPI_Status c_status;

    *__ierr = MPI_Sendrecv(MPIR_F_PTR(sendbuf), (int)*sendcount,
                           MPI_Type_f2c(*sendtype), (int)*dest,
                           (int)*sendtag, MPIR_F_PTR(recvbuf),
                           (int)*recvcount, MPI_Type_f2c(*recvtype),
                           (int)*source, (int)*recvtag,
                           MPI_Comm_f2c(*comm), &c_status);
    MPI_Status_c2f(&c_status, status);
}

void mpi_sendrecv_replace_ ( void *, MPI_Fint *, MPI_Fint *,
                                     MPI_Fint *, MPI_Fint *, MPI_Fint *,
                                     MPI_Fint *, MPI_Fint *, MPI_Fint *,
                                     MPI_Fint * );
void mpi_sendrecv_replace_( void *buf, MPI_Fint *count, MPI_Fint *datatype,
		                    MPI_Fint *dest, MPI_Fint *sendtag,
		                    MPI_Fint *source, MPI_Fint *recvtag,
			                MPI_Fint *comm, MPI_Fint *status, MPI_Fint *__ierr )
{
    MPI_Status c_status;

    *__ierr = MPI_Sendrecv_replace(MPIR_F_PTR(buf), (int)*count,
                                   MPI_Type_f2c(*datatype), (int)*dest,
                                   (int)*sendtag, (int)*source, (int)*recvtag,
                                   MPI_Comm_f2c(*comm), &c_status );
    MPI_Status_c2f(&c_status, status);
}

void mpi_ssend_init_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *,
		               MPI_Fint *, MPI_Fint *, MPI_Fint *,
			           MPI_Fint * );
void mpi_ssend_init_( void *buf, MPI_Fint *count, MPI_Fint *datatype,
		              MPI_Fint *dest, MPI_Fint *tag, MPI_Fint *comm,
		              MPI_Fint *request, MPI_Fint *__ierr )
{
    MPI_Request lrequest;
    *__ierr = MPI_Ssend_init(MPIR_F_PTR(buf), (int)*count,
                             MPI_Type_f2c(*datatype), (int)*dest, (int)*tag,
                             MPI_Comm_f2c(*comm), &lrequest);
    *request = MPI_Request_c2f(lrequest);
}

void mpi_ssend_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *,
                          MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_ssend_( void *buf, MPI_Fint *count, MPI_Fint *datatype,
		         MPI_Fint *dest, MPI_Fint *tag, MPI_Fint *comm,
		         MPI_Fint *__ierr )
{
    *__ierr = MPI_Ssend(MPIR_F_PTR(buf), (int)*count,
                        MPI_Type_f2c(*datatype), (int)*dest, (int)*tag,
                        MPI_Comm_f2c(*comm));
}

void mpi_startall_ ( MPI_Fint *, MPI_Fint [], MPI_Fint * );
void mpi_startall_( MPI_Fint *count, MPI_Fint array_of_requests[],
		            MPI_Fint *__ierr )
{
    MPI_Request *lrequest = 0;
    MPI_Request local_lrequest[MPIR_USE_LOCAL_ARRAY];
    int i;

    if ((int)*count > 0) {
        if ((int)*count > MPIR_USE_LOCAL_ARRAY) {
            MPIR_FALLOC(lrequest,
			(MPI_Request*)MALLOC(sizeof(MPI_Request) * (int)*count),
			MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
			"MPI_STARTALL" );
        }
	else {
	    lrequest = local_lrequest;
	}
	for (i=0; i<(int)*count; i++) {
	    lrequest[i] = MPI_Request_f2c( array_of_requests[i] );
	}
	*__ierr = MPI_Startall((int)*count,lrequest);
    }
    else
        *__ierr = MPI_Startall((int)*count,(MPI_Request *)0);

    for (i=0; i<(int)*count; i++) {
        array_of_requests[i] = MPI_Request_c2f( lrequest[i]);
    }
    if ((int)*count > MPIR_USE_LOCAL_ARRAY) {
        FREE( lrequest );
    }
}

void mpi_start_ ( MPI_Fint *, MPI_Fint * );
void mpi_start_( MPI_Fint *request, MPI_Fint *__ierr )
{
    MPI_Request lrequest = MPI_Request_f2c(*request );
    *__ierr = MPI_Start( &lrequest );
    *request = MPI_Request_c2f(lrequest);
}

void mpi_testall_ ( MPI_Fint *, MPI_Fint [], MPI_Fint *,
		            MPI_Fint [][MPI_STATUS_SIZE], MPI_Fint * );
void mpi_testall_( MPI_Fint *count, MPI_Fint array_of_requests[],
                   MPI_Fint *flag,
		           MPI_Fint array_of_statuses[][MPI_STATUS_SIZE],
		           MPI_Fint *__ierr )
{
    int lflag;
    int i;
    MPI_Request *lrequest = 0;
    MPI_Request local_lrequest[MPIR_USE_LOCAL_ARRAY];
    MPI_Status *c_status = 0;
    MPI_Status local_c_status[MPIR_USE_LOCAL_ARRAY];

    if ((int)*count > 0) {
        if ((int)*count > MPIR_USE_LOCAL_ARRAY) {
            MPIR_FALLOC(lrequest,
			(MPI_Request*)MALLOC(sizeof(MPI_Request)* (int)*count),
                        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
                        "MPI_TESTALL");
            MPIR_FALLOC(c_status,
			(MPI_Status*)MALLOC(sizeof(MPI_Status)* (int)*count),
                        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
                        "MPI_TESTTALL");
        }
        else {
	    lrequest = local_lrequest;
            c_status = local_c_status;
	}
	for (i=0; i<(int)*count; i++) {
	    lrequest[i] = MPI_Request_f2c( array_of_requests[i] );
	}

	*__ierr = MPI_Testall((int)*count,lrequest,&lflag,c_status);
        /* By checking for lrequest[i] = 0, we handle persistant requests */
	for (i=0; i<(int)*count; i++) {
	        array_of_requests[i] = MPI_Request_c2f( lrequest[i] );
	}
    }
    else
	*__ierr = MPI_Testall((int)*count,(MPI_Request *)0,&lflag,c_status);
    
    *flag = MPIR_TO_FLOG(lflag);
    /* We must only copy for those elements that corresponded to non-null
       requests, and only if there is a change */
    if (lflag) {
	for (i=0; i<(int)*count; i++) {
	    MPI_Status_c2f( &c_status[i], &(array_of_statuses[i][0]) );
	}
    }

    if ((int)*count > MPIR_USE_LOCAL_ARRAY) {
	FREE( lrequest );
	FREE( c_status );
    }
}

void mpi_testany_ ( MPI_Fint *, MPI_Fint [], MPI_Fint *,
		            MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_testany_( MPI_Fint *count, MPI_Fint array_of_requests[],
                   MPI_Fint *index, MPI_Fint *flag, MPI_Fint *status,
		           MPI_Fint *__ierr )
{
    int lindex;
    int lflag;
    MPI_Request *lrequest;
    MPI_Request local_lrequest[MPIR_USE_LOCAL_ARRAY];
    MPI_Status c_status;
    int i;

    if ((int)*count > 0) {
	if ((int)*count > MPIR_USE_LOCAL_ARRAY) {
	    MPIR_FALLOC(lrequest,
	                (MPI_Request*)MALLOC(sizeof(MPI_Request)* (int)*count),
		        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
		        "MPI_TESTANY");
	}
	else 
	    lrequest = local_lrequest;
	
	for (i=0; i<(int)*count; i++) 
	    lrequest[i] = MPI_Request_f2c( array_of_requests[i] );
	
    }
    else
	lrequest = 0;

    *__ierr = MPI_Testany((int)*count,lrequest,&lindex,&lflag,&c_status);
    if (lindex != -1) {
        if (lflag && !*__ierr) {
	    array_of_requests[lindex] = MPI_Request_c2f(lrequest[lindex]);
        }
     }
    if ((int)*count > MPIR_USE_LOCAL_ARRAY) 
	FREE( lrequest );
    
    *flag = MPIR_TO_FLOG(lflag);
    /* See the description of waitany in the standard; the Fortran index ranges
       are from 1, not zero */
    *index = (MPI_Fint)lindex;
    if ((int)*index >= 0) *index = *index + 1;
    MPI_Status_c2f(&c_status, status);
}

void mpi_test_cancelled_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_test_cancelled_(MPI_Fint *status, MPI_Fint *flag, MPI_Fint *__ierr)
{
    int lflag;
    MPI_Status c_status;

    MPI_Status_f2c(status, &c_status);
    *__ierr = MPI_Test_cancelled(&c_status, &lflag);
    *flag = MPIR_TO_FLOG(lflag);
}

void mpi_test_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_test_ ( MPI_Fint *request, MPI_Fint *flag, MPI_Fint *status,
		         MPI_Fint *__ierr )
{
    int        l_flag;
    MPI_Status c_status;
    MPI_Request lrequest = MPI_Request_f2c(*request);

    *__ierr = MPI_Test( &lrequest, &l_flag, &c_status);
    *request = MPI_Request_c2f(lrequest);

    *flag = MPIR_TO_FLOG(l_flag);
    if (l_flag)
        MPI_Status_c2f(&c_status, status);
}

void mpi_testsome_ ( MPI_Fint *, MPI_Fint [], MPI_Fint *,
		             MPI_Fint [], MPI_Fint [][MPI_STATUS_SIZE],
		             MPI_Fint * );
void mpi_testsome_( MPI_Fint *incount, MPI_Fint array_of_requests[],
                    MPI_Fint *outcount, MPI_Fint array_of_indices[], 
                    MPI_Fint array_of_statuses[][MPI_STATUS_SIZE],
		            MPI_Fint *__ierr )
{
    int i,j,found;
    int loutcount;
    int *l_indices = 0;
    int local_l_indices[MPIR_USE_LOCAL_ARRAY];
    MPI_Request *lrequest = 0;
    MPI_Request local_lrequest[MPIR_USE_LOCAL_ARRAY];
    MPI_Status *c_status = 0;
    MPI_Status local_c_status[MPIR_USE_LOCAL_ARRAY];

    if ((int)*incount > 0) {
        if ((int)*incount > MPIR_USE_LOCAL_ARRAY) {
            MPIR_FALLOC(lrequest,
	               (MPI_Request*)MALLOC(sizeof(MPI_Request)* (int)*incount),
                        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
                        "MPI_TESTSOME");

	    MPIR_FALLOC(l_indices,(int*)MALLOC(sizeof(int)* (int)*incount),
		        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
		        "MPI_TESTSOME" );
	    MPIR_FALLOC(c_status,
	                (MPI_Status*)MALLOC(sizeof(MPI_Status)* (int)*incount),
		        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
		        "MPI_TESTSOME" );
	}
        else {
	    lrequest = local_lrequest;
	    l_indices = local_l_indices;
	    c_status = local_c_status;
	}

	for (i=0; i<(int)*incount; i++) {
	    lrequest[i] = MPI_Request_f2c( array_of_requests[i] );
	}
	*__ierr = MPI_Testsome((int)*incount,lrequest,&loutcount,l_indices,
			       c_status);

	/* By checking for lrequest[l_indices[i] =  0, 
           we handle persistant requests */
	for (i=0; i<(int)*incount; i++) {
	    if ( i < loutcount ) {
		    array_of_requests[l_indices[i]] =
			MPI_Request_c2f(lrequest[l_indices[i]] );
	    }
	    else {
		found = 0;
		j = 0;
		while ( (!found) && (j<loutcount) ) {
		    if (l_indices[j++] == i)
			found = 1;
		}
		if (!found)
		    array_of_requests[i] = MPI_Request_c2f( lrequest[i] );
	    }
	}
    }
    else
	*__ierr = MPI_Testsome( (int)*incount, (MPI_Request *)0, &loutcount, 
				l_indices, c_status );

    for (i=0; i<loutcount; i++) {
        MPI_Status_c2f(&c_status[i], &(array_of_statuses[i][0]) );
	if (l_indices[i] >= 0)
	    array_of_indices[i] = l_indices[i] + 1;
    }
    *outcount = (MPI_Fint)loutcount;
    if ((int)*incount > MPIR_USE_LOCAL_ARRAY) {
        FREE( l_indices );
        FREE( lrequest );
        FREE( c_status );
    }

}

void mpi_type_commit_ ( MPI_Fint *, MPI_Fint * );
void mpi_type_commit_ ( MPI_Fint *datatype, MPI_Fint *__ierr )
{
    MPI_Datatype ldatatype = MPI_Type_f2c(*datatype);
    *__ierr = MPI_Type_commit( &ldatatype );
    *datatype = MPI_Type_c2f(ldatatype);
}

void mpi_type_contiguous_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_type_contiguous_( MPI_Fint *count, MPI_Fint *old_type,
		                   MPI_Fint *newtype, MPI_Fint *__ierr )
{
    MPI_Datatype  ldatatype;

    *__ierr = MPI_Type_contiguous((int)*count, MPI_Type_f2c(*old_type),
                                  &ldatatype);
    *newtype = MPI_Type_c2f(ldatatype);
}

void mpi_type_extent_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_type_extent_( MPI_Fint *datatype, MPI_Fint *extent, MPI_Fint *__ierr )
{
    MPI_Aint c_extent;
    *__ierr = MPI_Type_extent(MPI_Type_f2c(*datatype), &c_extent);
    /* Really should check for truncation, ala mpi_address_ */
    *extent = (MPI_Fint)c_extent;
}

void mpi_type_free_ ( MPI_Fint *, MPI_Fint * );
void mpi_type_free_ ( MPI_Fint *datatype, MPI_Fint *__ierr )
{
    MPI_Datatype ldatatype = MPI_Type_f2c(*datatype);
    *__ierr = MPI_Type_free(&ldatatype);
    *datatype = MPI_Type_c2f(ldatatype);
}

void mpi_type_hindexed_ ( MPI_Fint *, MPI_Fint [], MPI_Fint [],
		                  MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_type_hindexed_( MPI_Fint *count, MPI_Fint blocklens[],
                         MPI_Fint indices[], MPI_Fint *old_type,
			             MPI_Fint *newtype, MPI_Fint *__ierr )
{
    MPI_Aint     *c_indices;
    MPI_Aint     local_c_indices[MPIR_USE_LOCAL_ARRAY];
    int          i, mpi_errno;
    int          *l_blocklens; 
    int          local_l_blocklens[MPIR_USE_LOCAL_ARRAY];
    MPI_Datatype ldatatype;
    static char  myname[] = "MPI_TYPE_HINDEXED";

    if ((int)*count > 0) {
	if ((int)*count > MPIR_USE_LOCAL_ARRAY) {
	/* We really only need to do this when 
	   sizeof(MPI_Aint) != sizeof(INTEGER) */
	    MPIR_FALLOC(c_indices,
	                (MPI_Aint *) MALLOC( *count * sizeof(MPI_Aint) ),
		        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, myname );

	    MPIR_FALLOC(l_blocklens,(int *) MALLOC( *count * sizeof(int) ),
		        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, myname );
	}
	else {
	    c_indices = local_c_indices;
	    l_blocklens = local_l_blocklens;
	}

	for (i=0; i<(int)*count; i++) {
	    c_indices[i] = (MPI_Aint) indices[i];
            l_blocklens[i] = (int) blocklens[i];
	}
	*__ierr = MPI_Type_hindexed((int)*count,l_blocklens,c_indices,
                                    MPI_Type_f2c(*old_type),
				    &ldatatype);
	if ((int)*count > MPIR_USE_LOCAL_ARRAY) {
	    FREE( c_indices );
            FREE( l_blocklens );
	}
        *newtype = MPI_Type_c2f(ldatatype);
    }
    else if ((int)*count == 0) {
	*__ierr = MPI_SUCCESS;
        *newtype = 0;
    }
    else {
	mpi_errno = MPER_Err_setmsg( MPI_ERR_COUNT, MPIR_ERR_DEFAULT, myname,
				     (char *)0, (char *)0, (int)(*count) );
	*__ierr = MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
	return;
    }
}

void mpi_type_hvector_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *,
		                 MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_type_hvector_( MPI_Fint *count, MPI_Fint *blocklen, MPI_Fint *stride,
		                MPI_Fint *old_type, MPI_Fint *newtype,
			            MPI_Fint *__ierr )
{
    MPI_Aint     c_stride = (MPI_Aint)*stride;
    MPI_Datatype ldatatype;

    *__ierr = MPI_Type_hvector((int)*count, (int)*blocklen, c_stride,
                               MPI_Type_f2c(*old_type),
                               &ldatatype);
    *newtype = MPI_Type_c2f(ldatatype);
}

void mpi_type_indexed_ ( MPI_Fint *, MPI_Fint [], MPI_Fint [],
		                 MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_type_indexed_( MPI_Fint *count, MPI_Fint blocklens[],
                        MPI_Fint indices[], MPI_Fint *old_type,
			            MPI_Fint *newtype, MPI_Fint *__ierr )
{
    int          i;
    int          *l_blocklens = 0;
    int          local_l_blocklens[MPIR_USE_LOCAL_ARRAY];
    int          *l_indices = 0;
    int          local_l_indices[MPIR_USE_LOCAL_ARRAY];
    MPI_Datatype ldatatype;
    static char myname[] = "MPI_TYPE_INDEXED";

    if ((int)*count > 0) {
	if ((int)*count > MPIR_USE_LOCAL_ARRAY) {
	    MPIR_FALLOC(l_blocklens,(int *) MALLOC( *count * sizeof(int) ),
		        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, myname );

	    MPIR_FALLOC(l_indices,(int *) MALLOC( *count * sizeof(int) ),
		        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, myname );
	}
	else {
	    l_blocklens = local_l_blocklens;
	    l_indices = local_l_indices;
	}

        for (i=0; i<(int)*count; i++) {
	    l_indices[i] = (int)indices[i];
	    l_blocklens[i] = (int)blocklens[i];
         }
    }
 
    *__ierr = MPI_Type_indexed((int)*count, l_blocklens, l_indices,
                               MPI_Type_f2c(*old_type), 
                               &ldatatype);
    if ((int)*count > MPIR_USE_LOCAL_ARRAY) {
        FREE( l_indices );
        FREE( l_blocklens );
    }
    *newtype = MPI_Type_c2f(ldatatype);
}

void mpi_type_lb_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_type_lb_ ( MPI_Fint *datatype, MPI_Fint *displacement,
		            MPI_Fint *__ierr )
{
    MPI_Aint   c_displacement;

    *__ierr = MPI_Type_lb(MPI_Type_f2c(*datatype), &c_displacement);
    /* Should check for truncation */
    *displacement = (MPI_Fint)c_displacement;
}

void mpi_type_size_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_type_size_ ( MPI_Fint *datatype, MPI_Fint *size, MPI_Fint *__ierr )
{
    /* MPI_Aint c_size;*/
    int c_size;
    *__ierr = MPI_Type_size(MPI_Type_f2c(*datatype), &c_size);
    /* Should check for truncation */
    *size = (MPI_Fint)c_size;
}

void mpi_type_struct_ ( MPI_Fint *, MPI_Fint [], MPI_Fint [],
		                MPI_Fint [], MPI_Fint *, MPI_Fint * );
void mpi_type_struct_( MPI_Fint *count, MPI_Fint blocklens[],
                       MPI_Fint indices[], MPI_Fint old_types[],
		               MPI_Fint *newtype, MPI_Fint *__ierr )
{
    MPI_Aint     *c_indices;
    MPI_Aint     local_c_indices[MPIR_USE_LOCAL_ARRAY];
    MPI_Datatype *l_datatype;
    MPI_Datatype local_l_datatype[MPIR_USE_LOCAL_ARRAY];
    MPI_Datatype l_newtype;
    int          *l_blocklens;
    int          local_l_blocklens[MPIR_USE_LOCAL_ARRAY];
    int          i;
    int          mpi_errno;
    static char  myname[] = "MPI_TYPE_STRUCT";
    
    if ((int)*count > 0) {
	if ((int)*count > MPIR_USE_LOCAL_ARRAY) {
	/* Since indices come from MPI_ADDRESS (the FORTRAN VERSION),
	   they are currently relative to MPIF_F_MPI_BOTTOM.  
	   Convert them back */
	    MPIR_FALLOC(c_indices,
	                (MPI_Aint *) MALLOC( *count * sizeof(MPI_Aint) ),
		        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, myname );

	    MPIR_FALLOC(l_blocklens,(int *) MALLOC( *count * sizeof(int) ),
		        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, myname );

	    MPIR_FALLOC(l_datatype,
	               (MPI_Datatype *) MALLOC( *count * sizeof(MPI_Datatype) ),
		        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, myname );
	}
	else {
	    c_indices = local_c_indices;
	    l_blocklens = local_l_blocklens;
	    l_datatype = local_l_datatype;
	}

	for (i=0; i<(int)*count; i++) {
	    c_indices[i] = (MPI_Aint) indices[i]/* + (MPI_Aint)MPIR_F_MPI_BOTTOM*/;
            l_blocklens[i] = (int) blocklens[i];
            l_datatype[i] = MPI_Type_f2c(old_types[i]);
	}
	*__ierr = MPI_Type_struct((int)*count, l_blocklens, c_indices,
                                  l_datatype,
				  &l_newtype);

        if ((int)*count > MPIR_USE_LOCAL_ARRAY) {    
	    FREE( c_indices );
            FREE( l_blocklens );
            FREE( l_datatype );
	}
    }
    else if ((int)*count == 0) {
	*__ierr = MPI_SUCCESS;
	*newtype = 0;
    }
    else {
	mpi_errno = MPER_Err_setmsg( MPI_ERR_COUNT, MPIR_ERR_DEFAULT, myname,
				     (char *)0, (char *)0, (int)(*count) );
	*__ierr = MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
	return;
    }
    *newtype = MPI_Type_c2f(l_newtype);

}

void mpi_type_ub_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_type_ub_ ( MPI_Fint *datatype, MPI_Fint *displacement,
                    MPI_Fint *__ierr )
{
    MPI_Aint c_displacement;

    *__ierr = MPI_Type_ub(MPI_Type_f2c(*datatype), &c_displacement);
    /* Should check for truncation */
    *displacement = (MPI_Fint)c_displacement;
}

void mpi_type_vector_ ( MPI_Fint *, MPI_Fint *, MPI_Fint *,
		                MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_type_vector_( MPI_Fint *count, MPI_Fint *blocklen, MPI_Fint *stride,
		               MPI_Fint *old_type, MPI_Fint *newtype, MPI_Fint *__ierr )
{
    MPI_Datatype l_datatype;

    *__ierr = MPI_Type_vector((int)*count, (int)*blocklen, (int)*stride,
                              MPI_Type_f2c(*old_type),
                              &l_datatype);
    *newtype = MPI_Type_c2f(l_datatype);
}

void mpi_unpack_ ( void *, MPI_Fint *, MPI_Fint *, void *,
                           MPI_Fint *, MPI_Fint *, MPI_Fint *,
                           MPI_Fint * );
void mpi_unpack_ ( void *inbuf, MPI_Fint *insize, MPI_Fint *position,
		           void *outbuf, MPI_Fint *outcount, MPI_Fint *type,
		           MPI_Fint *comm, MPI_Fint *__ierr )
{
    int l_position;
    l_position = (int)*position;

    *__ierr = MPI_Unpack(inbuf, (int)*insize, &l_position,
                         MPIR_F_PTR(outbuf), (int)*outcount,
                         MPI_Type_f2c(*type), MPI_Comm_f2c(*comm) );
    *position = (MPI_Fint)l_position;
}

void mpi_waitall_ ( MPI_Fint *, MPI_Fint [],
		            MPI_Fint [][MPI_STATUS_SIZE], MPI_Fint *);
void mpi_waitall_( MPI_Fint *count, MPI_Fint array_of_requests[],
                   MPI_Fint array_of_statuses[][MPI_STATUS_SIZE],
		           MPI_Fint *__ierr )
{
    int i;
    MPI_Request *lrequest = 0;
    MPI_Request local_lrequest[MPIR_USE_LOCAL_ARRAY];
    MPI_Status *c_status = 0;
    MPI_Status local_c_status[MPIR_USE_LOCAL_ARRAY];

    if ((int)*count > 0) {
	if ((int)*count > MPIR_USE_LOCAL_ARRAY) {
	    MPIR_FALLOC(lrequest,(MPI_Request*)MALLOC(sizeof(MPI_Request) * 
                        (int)*count), MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, 
		        "MPI_WAITALL" );

	    MPIR_FALLOC(c_status,(MPI_Status*)MALLOC(sizeof(MPI_Status) * 
                        (int)*count), MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, 
		        "MPI_WAITALL" );
	}
	else {
	    lrequest = local_lrequest;
	    c_status = local_c_status;
	}

	for (i=0; i<(int)*count; i++) {
	    lrequest[i] = MPI_Request_f2c( array_of_requests[i] );
	}

	*__ierr = MPI_Waitall((int)*count,lrequest,c_status);
	/* By checking for lrequest[i] = 0, we handle persistant requests */
	for (i=0; i<(int)*count; i++) {
	        array_of_requests[i] = MPI_Request_c2f( lrequest[i] );
	}
    }
    else 
	*__ierr = MPI_Waitall((int)*count,(MPI_Request *)0, c_status );

    for (i=0; i<(int)*count; i++) 
	MPI_Status_c2f(&(c_status[i]), &(array_of_statuses[i][0]) );
    
    if ((int)*count > MPIR_USE_LOCAL_ARRAY) {
        FREE( lrequest );
        FREE( c_status );
    }
}

void mpi_waitany_ ( MPI_Fint *, MPI_Fint [], MPI_Fint *,
		            MPI_Fint *, MPI_Fint * );
void mpi_waitany_( MPI_Fint *count, MPI_Fint array_of_requests[],
                   MPI_Fint *index, MPI_Fint *status, MPI_Fint *__ierr )
{

    int lindex;
    MPI_Request *lrequest;
    MPI_Request local_lrequest[MPIR_USE_LOCAL_ARRAY];
    MPI_Status c_status;
    int i;

    if ((int)*count > 0) {
	if ((int)*count > MPIR_USE_LOCAL_ARRAY) {
	    MPIR_FALLOC(lrequest,
	                (MPI_Request*)MALLOC(sizeof(MPI_Request) * (int)*count),
		        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, 
		        "MPI_WAITANY" );
	}
	else 
	    lrequest = local_lrequest;
	
	for (i=0; i<(int)*count; i++) 
	    lrequest[i] = MPI_Request_f2c( array_of_requests[i] );
    }
    else
	lrequest = 0;

    *__ierr = MPI_Waitany((int)*count,lrequest,&lindex,&c_status);

    if (lindex != -1) {
	if (!*__ierr) {
                array_of_requests[lindex] = MPI_Request_c2f(lrequest[lindex]);
	}
    }

   if ((int)*count > MPIR_USE_LOCAL_ARRAY) {
	FREE( lrequest );
    }

    /* See the description of waitany in the standard; the Fortran index ranges
       are from 1, not zero */
    *index = (MPI_Fint)lindex;
    if ((int)*index >= 0) *index = (MPI_Fint)*index + 1;
    MPI_Status_c2f(&c_status, status);
}

void mpi_wait_ ( MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_wait_ ( MPI_Fint *request, MPI_Fint *status, MPI_Fint *__ierr )
{
    MPI_Request lrequest;
    MPI_Status c_status;

    lrequest = MPI_Request_f2c(*request);
    *__ierr = MPI_Wait(&lrequest, &c_status);
    *request = MPI_Request_c2f(lrequest);

    MPI_Status_c2f(&c_status, status);
}

void mpi_waitsome_ ( MPI_Fint *, MPI_Fint [], MPI_Fint *,
		             MPI_Fint [], MPI_Fint [][MPI_STATUS_SIZE],
		             MPI_Fint * );
void mpi_waitsome_( MPI_Fint *incount, MPI_Fint array_of_requests[],
                    MPI_Fint *outcount, MPI_Fint array_of_indices[], 
                    MPI_Fint array_of_statuses[][MPI_STATUS_SIZE],
		            MPI_Fint *__ierr )
{
    int i,j,found;
    int loutcount;
    int *l_indices = 0;
    int local_l_indices[MPIR_USE_LOCAL_ARRAY];
    MPI_Request *lrequest = 0;
    MPI_Request local_lrequest[MPIR_USE_LOCAL_ARRAY];
    MPI_Status * c_status = 0;
    MPI_Status local_c_status[MPIR_USE_LOCAL_ARRAY];

    if ((int)*incount > 0) {
	if ((int)*incount > MPIR_USE_LOCAL_ARRAY) {
	    MPIR_FALLOC(lrequest,
	               (MPI_Request*)MALLOC(sizeof(MPI_Request)* (int)*incount),
		        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, 
		        "MPI_WAITSOME" );

	    MPIR_FALLOC(l_indices,(int*)MALLOC(sizeof(int)* (int)*incount),
		        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, 
		        "MPI_WAITSOME" );

	    MPIR_FALLOC(c_status,
	                (MPI_Status*)MALLOC(sizeof(MPI_Status)* (int)*incount),
		        MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, 
		        "MPI_WAITSOME" );
	}
	else {
	    lrequest = local_lrequest;
	    l_indices = local_l_indices;
	    c_status = local_c_status;
	}

	for (i=0; i<(int)*incount; i++) 
	    lrequest[i] = MPI_Request_f2c( array_of_requests[i] );
	
	*__ierr = MPI_Waitsome((int)*incount,lrequest,&loutcount,l_indices,
			       c_status);

/* By checking for lrequest[l_indices[i]] = 0, 
   we handle persistant requests */
        for (i=0; i<(int)*incount; i++) {
	    if ( i < loutcount) {
                if (l_indices[i] >= 0) {
		        array_of_requests[l_indices[i]] = 
			      MPI_Request_c2f( lrequest[l_indices[i]] );
		}
	    }
	    else {
		found = 0;
		j = 0;
		while ( (!found) && (j<loutcount) ) {
		    if (l_indices[j++] == i)
			found = 1;
		}
		if (!found)
	            array_of_requests[i] = MPI_Request_c2f( lrequest[i] );
	    }
	}
    }
    else 
	*__ierr = MPI_Waitsome( (int)*incount, (MPI_Request *)0, &loutcount,
			       l_indices, c_status );

    for (i=0; i<loutcount; i++) {
	MPI_Status_c2f( &c_status[i], &(array_of_statuses[i][0]) );
	if (l_indices[i] >= 0)
	    array_of_indices[i] = l_indices[i] + 1;

    }
    *outcount = (MPI_Fint)loutcount;
    if ((int)*incount > MPIR_USE_LOCAL_ARRAY) {
        FREE( l_indices );
        FREE( lrequest );
        FREE( c_status );
    }
}

void mpi_allgather_ ( void *, MPI_Fint *, MPI_Fint *, void *,
                              MPI_Fint *, MPI_Fint *, MPI_Fint *,
                              MPI_Fint * );
void mpi_allgather_ ( void *sendbuf, MPI_Fint *sendcount, MPI_Fint *sendtype,
                      void *recvbuf, MPI_Fint *recvcount, MPI_Fint *recvtype,
		              MPI_Fint *comm, MPI_Fint *__ierr )
{
    *__ierr = MPI_Allgather(MPIR_F_PTR(sendbuf), (int)*sendcount,
                            MPI_Type_f2c(*sendtype),
                            MPIR_F_PTR(recvbuf),
                            (int)*recvcount,
                            MPI_Type_f2c(*recvtype),
                            MPI_Comm_f2c(*comm));
}

void mpi_allgatherv_ ( void *, MPI_Fint *, MPI_Fint *,
		               void *, MPI_Fint *, MPI_Fint *,
		               MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_allgatherv_ ( void *sendbuf, MPI_Fint *sendcount,  MPI_Fint *sendtype,
		               void *recvbuf, MPI_Fint *recvcounts, MPI_Fint *displs,
		               MPI_Fint *recvtype, MPI_Fint *comm, MPI_Fint *__ierr )
{
    if (sizeof(MPI_Fint) == sizeof(int))
        *__ierr = MPI_Allgatherv(MPIR_F_PTR(sendbuf), *sendcount,
                                 MPI_Type_f2c(*sendtype),
                                 MPIR_F_PTR(recvbuf), recvcounts,
                                 displs, MPI_Type_f2c(*recvtype),
                                 MPI_Comm_f2c(*comm));
    else {
        int size;
        int *l_recvcounts;
        int *l_displs;
        int i;

        MPI_Comm_size(MPI_Comm_f2c(*comm), &size);

        MPIR_FALLOC(l_recvcounts,(int*)MALLOC(sizeof(int)* size),
                    MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
                    "MPI_Allgatherv");
        MPIR_FALLOC(l_displs,(int*)MALLOC(sizeof(int)* size),
                    MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
                    "MPI_Allgatherv");
        for (i=0; i<size; i++) {
            l_recvcounts[i] = (int)recvcounts[i];
            l_displs[i] = (int)displs[i];
        }

        *__ierr = MPI_Allgatherv(MPIR_F_PTR(sendbuf), (int)*sendcount,
                                 MPI_Type_f2c(*sendtype),
                                 MPIR_F_PTR(recvbuf), l_recvcounts,
                                 l_displs, MPI_Type_f2c(*recvtype),
                                 MPI_Comm_f2c(*comm));
        FREE( l_recvcounts );
        FREE( l_displs );
    }
}

void mpi_allreduce_ ( void *, void *, MPI_Fint *, MPI_Fint *,
                      MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_allreduce_ ( void *sendbuf, void *recvbuf, MPI_Fint *count,
                      MPI_Fint *datatype, MPI_Fint *op, MPI_Fint *comm,
                      MPI_Fint *__ierr )
{
    *__ierr = MPI_Allreduce(MPIR_F_PTR(sendbuf),MPIR_F_PTR(recvbuf),
                            (int)*count, MPI_Type_f2c(*datatype),
                            MPI_Op_f2c(*op), MPI_Comm_f2c(*comm) );
}


void mpi_alltoall_ ( void *, MPI_Fint *, MPI_Fint *, void *,
                             MPI_Fint *, MPI_Fint *, MPI_Fint *,
                             MPI_Fint * );
void mpi_alltoall_( void *sendbuf, MPI_Fint *sendcount, MPI_Fint *sendtype,
                    void *recvbuf, MPI_Fint *recvcnt, MPI_Fint *recvtype,
                    MPI_Fint *comm, MPI_Fint *__ierr )
{
    *__ierr = MPI_Alltoall(MPIR_F_PTR(sendbuf), (int)*sendcount,
                           MPI_Type_f2c(*sendtype), MPIR_F_PTR(recvbuf),
                           (int)*recvcnt, MPI_Type_f2c(*recvtype),
                           MPI_Comm_f2c(*comm) );
}

void mpi_alltoallv_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *, 
                      void *, MPI_Fint *, MPI_Fint *, MPI_Fint *,
                      MPI_Fint *, MPI_Fint * );
void mpi_alltoallv_ ( void *sendbuf, MPI_Fint *sendcnts,
                      MPI_Fint *sdispls, MPI_Fint *sendtype,
                      void *recvbuf, MPI_Fint *recvcnts,
                      MPI_Fint *rdispls, MPI_Fint *recvtype,
                      MPI_Fint *comm, MPI_Fint *__ierr )
{
    if (sizeof(MPI_Fint) == sizeof(int))
    *__ierr = MPI_Alltoallv(MPIR_F_PTR(sendbuf), sendcnts,
                                sdispls, MPI_Type_f2c(*sendtype),
                    MPIR_F_PTR(recvbuf), recvcnts,
                                rdispls, MPI_Type_f2c(*recvtype),
                    MPI_Comm_f2c(*comm) );
    else {

        int *l_sendcnts;
        int *l_sdispls;
        int *l_recvcnts;
        int *l_rdispls;
    int size;
    int i;

    MPI_Comm_size(MPI_Comm_f2c(*comm), &size);

    MPIR_FALLOC(l_sendcnts,(int*)MALLOC(sizeof(int)* size),
            MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
            "MPI_Alltoallv");
    MPIR_FALLOC(l_sdispls,(int*)MALLOC(sizeof(int)* size),
            MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
            "MPI_Alltoallv");
    MPIR_FALLOC(l_recvcnts,(int*)MALLOC(sizeof(int)* size),
            MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
            "MPI_Alltoallv");
    MPIR_FALLOC(l_rdispls,(int*)MALLOC(sizeof(int)* size),
            MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
            "MPI_Alltoallv");

    for (i=0; i<size; i++) {
        l_sendcnts[i] = (int)sendcnts[i];
        l_sdispls[i] = (int)sdispls[i];
        l_recvcnts[i] = (int)recvcnts[i];
        l_rdispls[i] = (int)rdispls[i];
    }
    *__ierr = MPI_Alltoallv(MPIR_F_PTR(sendbuf), l_sendcnts,
                                l_sdispls, MPI_Type_f2c(*sendtype),
                    MPIR_F_PTR(recvbuf), l_recvcnts,
                                l_rdispls, MPI_Type_f2c(*recvtype),
                    MPI_Comm_f2c(*comm) );
    FREE( l_sendcnts);
    FREE( l_sdispls );
    FREE( l_recvcnts);
    FREE( l_rdispls );
    }
}

void mpi_barrier_ ( MPI_Fint *, MPI_Fint * );
void mpi_barrier_ ( MPI_Fint *comm, MPI_Fint *__ierr )
{
    *__ierr = MPI_Barrier( MPI_Comm_f2c(*comm) );
}

void mpi_bcast_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *,
                          MPI_Fint *, MPI_Fint * );
void mpi_bcast_ ( void *buffer, MPI_Fint *count, MPI_Fint *datatype,
		          MPI_Fint *root, MPI_Fint *comm, MPI_Fint *__ierr )
{
    *__ierr = MPI_Bcast(MPIR_F_PTR(buffer), (int)*count,
                        MPI_Type_f2c(*datatype), (int)*root,
                        MPI_Comm_f2c(*comm));
}

void mpi_gather_ ( void *, MPI_Fint *, MPI_Fint *,
                   void *, MPI_Fint *, MPI_Fint *,
                   MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_gather_ ( void *sendbuf, MPI_Fint *sendcnt, MPI_Fint *sendtype,
                   void *recvbuf, MPI_Fint *recvcount, MPI_Fint *recvtype,
                   MPI_Fint *root, MPI_Fint *comm, MPI_Fint *__ierr )
{
    *__ierr = MPI_Gather(MPIR_F_PTR(sendbuf), (int)*sendcnt,
                         MPI_Type_f2c(*sendtype), MPIR_F_PTR(recvbuf),
                         (int)*recvcount, MPI_Type_f2c(*recvtype),
                         (int)*root, MPI_Comm_f2c(*comm));
}

void mpi_gatherv_ ( void *, MPI_Fint *, MPI_Fint *,
                    void *, MPI_Fint *, MPI_Fint *,
                    MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_gatherv_ ( void *sendbuf, MPI_Fint *sendcnt, MPI_Fint *sendtype,
                    void *recvbuf, MPI_Fint *recvcnts, MPI_Fint *displs,
                    MPI_Fint *recvtype, MPI_Fint *root, MPI_Fint *comm,
                    MPI_Fint *__ierr )
{

    if (sizeof(MPI_Fint) == sizeof(int))
        *__ierr = MPI_Gatherv(MPIR_F_PTR(sendbuf), *sendcnt,
                              MPI_Type_f2c(*sendtype), MPIR_F_PTR(recvbuf),
                              recvcnts, displs,
                              MPI_Type_f2c(*recvtype), *root,
                              MPI_Comm_f2c(*comm));
    else {
    int size;
        int *l_recvcnts;
        int *l_displs;
    int i;

    MPI_Comm_size(MPI_Comm_f2c(*comm), &size);

    MPIR_FALLOC(l_recvcnts,(int*)MALLOC(sizeof(int)* size),
            MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
            "MPI_Gatherv");
    MPIR_FALLOC(l_displs,(int*)MALLOC(sizeof(int)* size),
            MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
            "MPI_Gatherv");
    for (i=0; i<size; i++) {
        l_recvcnts[i] = (int)recvcnts[i];
        l_displs[i] = (int)displs[i];
    }
        *__ierr = MPI_Gatherv(MPIR_F_PTR(sendbuf), (int)*sendcnt,
                              MPI_Type_f2c(*sendtype), MPIR_F_PTR(recvbuf),
                              l_recvcnts, l_displs,
                              MPI_Type_f2c(*recvtype), (int)*root,
                              MPI_Comm_f2c(*comm));
    FREE( l_recvcnts );
    FREE( l_displs );
    }

}

#ifdef  FORTRAN_SPECIAL_FUNCTION_PTR
void mpi_op_create_( MPI_User_function **, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#else
void mpi_op_create_( MPI_User_function *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
#endif

void mpi_op_create_(
#ifdef FORTRAN_SPECIAL_FUNCTION_PTR
	MPI_User_function **function,
#else
	MPI_User_function *function,
#endif
	MPI_Fint *commute, MPI_Fint *op, MPI_Fint *__ierr)
{

    MPI_Op l_op;

#ifdef FORTRAN_SPECIAL_FUNCTION_PTR
    *__ierr = MPI_Op_create(*function,MPIR_FROM_FLOG((int)*commute),
                            &l_op);
#elif defined(_TWO_WORD_FCD)
    int tmp = *commute;
    *__ierr = MPI_Op_create(*function,MPIR_FROM_FLOG(tmp),&l_op);

#else
    *__ierr = MPI_Op_create(function,MPIR_FROM_FLOG((int)*commute),
                            &l_op);
#endif
    *op = MPI_Op_c2f(l_op);
}

void mpi_op_free_ ( MPI_Fint *, MPI_Fint * );
void mpi_op_free_( MPI_Fint *op, MPI_Fint *__ierr )
{
    MPI_Op l_op = MPI_Op_f2c(*op);
    *__ierr = MPI_Op_free(&l_op);
}

void mpi_reduce_scatter_ ( void *, void *, MPI_Fint *, MPI_Fint *,
                           MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_reduce_scatter_ ( void *sendbuf, void *recvbuf,
                           MPI_Fint *recvcnts, MPI_Fint *datatype,
                           MPI_Fint *op, MPI_Fint *comm, MPI_Fint *__ierr )
{

    if (sizeof(MPI_Fint) == sizeof(int))
        *__ierr = MPI_Reduce_scatter(MPIR_F_PTR(sendbuf),
                                     MPIR_F_PTR(recvbuf), recvcnts,
                                     MPI_Type_f2c(*datatype), MPI_Op_f2c(*op),
                                     MPI_Comm_f2c(*comm));
    else {
        int size;
        int *l_recvcnts;
    int i;

    MPI_Comm_size(MPI_Comm_f2c(*comm), &size);

    MPIR_FALLOC(l_recvcnts,(int*)MALLOC(sizeof(int)* size),
            MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
            "MPI_Reduce_scatter");
    for (i=0; i<size; i++)
        l_recvcnts[i] = (int)recvcnts[i];

        *__ierr = MPI_Reduce_scatter(MPIR_F_PTR(sendbuf),
                                     MPIR_F_PTR(recvbuf), l_recvcnts,
                                     MPI_Type_f2c(*datatype), MPI_Op_f2c(*op),
                                     MPI_Comm_f2c(*comm));
    FREE( l_recvcnts);
    }

}

void mpi_reduce_ ( void *, void *, MPI_Fint *, MPI_Fint *,
                   MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_reduce_ ( void *sendbuf, void *recvbuf, MPI_Fint *count,
                   MPI_Fint *datatype, MPI_Fint *op, MPI_Fint *root,
		           MPI_Fint *comm, MPI_Fint *__ierr )
{
    *__ierr = MPI_Reduce(MPIR_F_PTR(sendbuf), MPIR_F_PTR(recvbuf),
                         (int)*count, MPI_Type_f2c(*datatype),
                         MPI_Op_f2c(*op), (int)*root,
                         MPI_Comm_f2c(*comm));
}

void mpi_scan_ ( void *, void *, MPI_Fint *, MPI_Fint *,
                 MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_scan_ ( void *sendbuf, void *recvbuf, MPI_Fint *count,
                 MPI_Fint *datatype, MPI_Fint *op, MPI_Fint *comm,
                 MPI_Fint *__ierr )
{
    *__ierr = MPI_Scan(MPIR_F_PTR(sendbuf), MPIR_F_PTR(recvbuf),
                       (int)*count, MPI_Type_f2c(*datatype),
                       MPI_Op_f2c(*op), MPI_Comm_f2c(*comm));
}

void mpi_scatter_ ( void *, MPI_Fint *, MPI_Fint *,
                    void *, MPI_Fint *, MPI_Fint *,
                    MPI_Fint *, MPI_Fint *, MPI_Fint * );
void mpi_scatter_ ( void *sendbuf, MPI_Fint *sendcnt, MPI_Fint *sendtype,
                    void *recvbuf, MPI_Fint *recvcnt, MPI_Fint *recvtype,
                    MPI_Fint *root, MPI_Fint *comm, MPI_Fint *__ierr )
{
    *__ierr = MPI_Scatter(MPIR_F_PTR(sendbuf), (int)*sendcnt,
                          MPI_Type_f2c(*sendtype), MPIR_F_PTR(recvbuf),
                          (int)*recvcnt, MPI_Type_f2c(*recvtype),
                          (int)*root, MPI_Comm_f2c(*comm));
}

void mpi_scatterv_ ( void *, MPI_Fint *, MPI_Fint *, MPI_Fint *,
                     void *, MPI_Fint *, MPI_Fint *, MPI_Fint *,
                     MPI_Fint *, MPI_Fint * );
void mpi_scatterv_ ( void *sendbuf, MPI_Fint *sendcnts,
                     MPI_Fint *displs, MPI_Fint *sendtype,
                     void *recvbuf, MPI_Fint *recvcnt, 
                     MPI_Fint *recvtype, MPI_Fint *root,
                     MPI_Fint *comm, MPI_Fint *__ierr )
{
    if (sizeof(MPI_Fint) == sizeof(int))
        *__ierr = MPI_Scatterv(MPIR_F_PTR(sendbuf), sendcnts, displs,
                           MPI_Type_f2c(*sendtype), MPIR_F_PTR(recvbuf),
                           *recvcnt, MPI_Type_f2c(*recvtype),
                           *root, MPI_Comm_f2c(*comm) );
    else {
    int size;
        int *l_sendcnts;
        int *l_displs;
    int i;

    MPI_Comm_size(MPI_Comm_f2c(*comm), &size);

    MPIR_FALLOC(l_sendcnts,(int*)MALLOC(sizeof(int)* size),
            MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
            "MPI_Scatterv");
    MPIR_FALLOC(l_displs,(int*)MALLOC(sizeof(int)* size),
            MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,
            "MPI_Scatterv");
    for (i=0; i<size; i++) {
        l_sendcnts[i] = (int)sendcnts[i];
        l_displs[i] = (int)displs[i];
    }

        *__ierr = MPI_Scatterv(MPIR_F_PTR(sendbuf), l_sendcnts, l_displs,
                               MPI_Type_f2c(*sendtype), MPIR_F_PTR(recvbuf),
                               (int)*recvcnt, MPI_Type_f2c(*recvtype),
                               (int)*root, MPI_Comm_f2c(*comm) );
        FREE( l_sendcnts);
        FREE( l_displs);
    }
}

void mpi_finalize_ ( int * );
void mpi_finalize_( ierr )
int *ierr; 
{
    *ierr = MPI_Finalize();
}
