/*
 *  $Id: mpiimpl.h,v 1.26 2004/05/17 13:55:05 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */

#ifndef _MPIRIMPL_INCLUDE
#define _MPIRIMPL_INCLUDE

#if defined(HAVE_MPICHCONF_H) && !defined(MPICHCONF_INC)
/* This includes the definitions found by configure, and can be found in
   the include directory corresponding to this configuration
 */
#define MPICHCONF_INC
#include "mpichconf.h"
#endif
#if defined(HAVE_NO_C_CONST) && !defined(const)
#define const
#endif

#if defined(USE_MPI_INTERNALLY)
#include "pmpi2mpi.h"
#endif

/* mpi.h includes most of the definitions (all of the user-visible ones) */

/* If using HP weak symbols, we must suppress the MPI prototypes when 
   building the libraries */
#if defined(HAVE_PRAGMA_HP_SEC_DEF)
#define MPICH_SUPPRESS_PROTOTYPES
#endif
#include "mpi.h"

/* The rest of these contain the details of the structures that are not 
   user-visible */ 
#include "patchlevel.h"

/* For debugging, use PRINTF, FPRINTF, SPRINTF, FPUTS.  This allows us to 
   grep for printf to find stray error messages that should be handled with
   the error message facility (errorstring/errmsg)
   */
#ifndef PRINTF
#define PRINTF printf
#define FPRINTF fprintf
#define SPRINTF sprintf
#define FPUTS   fputs
#endif

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

/* The device knows a lot about communicators, requests, etc. */
#include "mpid.h"
#include "sendq.h"
/* Anything the device does NOT know about is included here */
/* FROM MPIR.H */
/* memory management for fixed-size blocks */
extern void *MPIR_errhandlers;  /* sbcnst Error handlers */

/* MPIR_F_MPI_BOTTOM is the address of the Fortran MPI_BOTTOM value */
extern void *MPIR_F_MPI_BOTTOM;

/* MPIR_F_STATUS_IGNORE and MPIR_F_STATUS_IGNORE are special markers
   used in Fortran instead of null pointers */
extern void *MPIR_F_STATUS_IGNORE;
extern void *MPIR_F_STATUSES_IGNORE;

/* MPIR_F_PTR checks for the Fortran MPI_BOTTOM and provides the value 
   MPI_BOTTOM if found 
   See src/pt2pt/addressf.c for why MPIR_F_PTR(a) is just (a)
*/
/*  #define MPIR_F_PTR(a) (((a)==(MPIR_F_MPI_BOTTOM))?MPI_BOTTOM:a) */
#define MPIR_F_PTR(a) (a)

/* use local array if count < MPIRUSE_LOCAL_ARRAY */
#define MPIR_USE_LOCAL_ARRAY 32

/* End of FROM MPIR.H */
/* FROM MPI_BC.H */
/* Value of tag in status for a cancelled message */
/* #define MPIR_MSG_CANCELLED (-3) */

/* This is the only global state in MPI */
extern int MPIR_Has_been_initialized;
/* End of FROM MPI_BC.H */

/* info is a linked list of these structures */
struct MPIR_Info {
    int cookie;
    char *key, *value;
    struct MPIR_Info *next;
};

#define MPIR_INFO_COOKIE 5835657

extern MPI_Info *MPIR_Infotable;
extern int MPIR_Infotable_ptr, MPIR_Infotable_max;

/* FROM DMPIATOM.H, used in group_diff, group_excl, group_inter,
   group_rexcl, group_union */
/* These are used in the Group manipulation routines */
#define MPIR_UNMARKED 0
#define MPIR_MARKED   1
/* End of FROM DMPIATOM.H */

/* Old-style thread macros */
#define MPID_THREAD_LOCK(a,b) MPID_THREAD_DS_LOCK(b)
#define MPID_THREAD_UNLOCK(a,b) MPID_THREAD_DS_UNLOCK(b)
#define MPID_THREAD_LOCK_INIT(a,b) MPID_THREAD_DS_LOCK_INIT(b)
#define MPID_THREAD_LOCK_FINISH(a,b) MPID_THREAD_DS_LOCK_FREE(b)

/* 
   Some code prototypes want FILE * .  We should limit these to
   the ones that need them. 
 */
#include <stdio.h>

/* NOT ADI2 */
/*#include "dmpiatom.h" */
/*#include "mpi_bc.h" */
/*#include "dmpi.h"*/
/*#include "mpir.h" */
/*#include "mpi_ad.h" */
/*#include "mpid.h"*/
/* If MPI_ADI2 */

/* 
   mpiprof contains the renamings for the profiling interface.  For it to
   be loaded, the symbol MPI_BUILD_PROFILING must be defined.  This 
   will be used in the makefiles for the construction of the MPI library
   routines to build the profiling interface
 */
#include "mpiprof.h"
#ifdef MPI_BUILD_PROFILING
/* Reload the bindings, this time for the PMPI bindings */
#undef __MPI_BINDINGS
#include "binding.h"
#endif

/* These are special macros for interfacing with Fortran */
#ifdef _CRAY
#include <fortran.h>
#endif

/* 
   The wrapper generator now makes "universal" wrappers.
 */
#if (defined(MPI_rs6000) || defined(MPI_NO_FORTRAN_USCORE))
/* Nothing to do */
#elif defined(MPI_cray) || defined(MPI_ncube)
/* Fortran is uppercase, no trailing underscore */
#ifndef FORTRANCAPS
#define FORTRANCAPS
#endif

#else
/* Most common Unix case is FORTRANUNDERSCORE, so choose that unless
   FORTRANNOUNDERSCORE explicitly set */
#if !defined(FORTRANUNDERSCORE) && !defined(FORTRANNOUNDERSCORE)
#define FORTRANUNDERSCORE
#endif

#endif
extern struct MPIR_COMMUNICATOR *MPIR_COMM_WORLD;
extern struct MPIR_GROUP *MPIR_GROUP_EMPTY;

/* Some of the internals work by generating MPI_PACKED data */
/* Should this be &MPIR_I_PACKED */
extern struct MPIR_DATATYPE *MPIR_PACKED_PTR;

/* Provide a variety of macroed versions of communicator enquiry
 * functions for use inside the implementation. This should remove
 * a fair amount of overhead, given that we had already checked the 
 * communicator on entering the outermost MPI function.
 */
#define MPIR_Group_dup(s,r) {(*(r) = (s)); \
    if ((s)) {MPIR_REF_INCR(s); }}

#define MPIR_Comm_size(comm, size) ((*(size) = (comm)->local_group->np),MPI_SUCCESS)
#define MPIR_Comm_rank(comm, size) ((*(size) = (comm)->local_rank),MPI_SUCCESS)

/* Here are bindings for some of the INTERNAL MPICH routines.  These are
   used to help us ensure that the code has no obvious bugs (i.e., mismatched
   args) 
 */
#define MPIR_GET_OP_PTR(op) \
    (struct MPIR_OP *)MPIR_ToPointer( op )
#define MPIR_TEST_MPI_OP(op,ptr,comm,routine_name) \
   if ((!(ptr) && (mpi_errno = (MPI_ERR_OP))) || \
    (((ptr)->cookie != MPIR_OP_COOKIE) && (mpi_errno = MPI_ERR_OP)) ){\
     return MPIR_ERROR(comm,mpi_errno,routine_name);}

/* coll */
extern void MPIR_MAXF  ( void *, void *, int *, MPI_Datatype * ) ;
extern void MPIR_MINF  ( void *, void *, int *, MPI_Datatype * ) ;
extern void MPIR_SUM  ( void *, void *, int *, MPI_Datatype * ) ;
extern void MPIR_PROD  ( void *, void *, int *, MPI_Datatype * ) ;
extern void MPIR_LAND  ( void *, void *, int *, MPI_Datatype * ) ;
extern void MPIR_BAND  ( void *, void *, int *, MPI_Datatype * ) ;
extern void MPIR_LOR  ( void *, void *, int *, MPI_Datatype * ) ;
extern void MPIR_BOR  ( void *, void *, int *, MPI_Datatype * ) ;
extern void MPIR_LXOR  ( void *, void *, int *, MPI_Datatype * ) ;
extern void MPIR_BXOR  ( void *, void *, int *, MPI_Datatype * ) ;
extern void MPIR_MAXLOC  ( void *, void *, int *, MPI_Datatype * ) ;
extern void MPIR_MINLOC  ( void *, void *, int *, MPI_Datatype * ) ;

extern int MPIR_intra_Scan ( void *sendbuf, void *recvbuf, int count, 
		      struct MPIR_DATATYPE *datatype, MPI_Op op, 
		      struct MPIR_COMMUNICATOR *comm );

/* context */
#ifdef FOO
int MPIR_Attr_copy_node( struct MPIR_COMMUNICATOR *, 
				     struct MPIR_COMMUNICATOR *, 
				     MPIR_HBT_node * );
int MPIR_Attr_copy_subtree( struct MPIR_COMMUNICATOR *, 
					 struct MPIR_COMMUNICATOR *, 
					 MPIR_HBT *, MPIR_HBT_node * );
int MPIR_Attr_free_node( struct MPIR_COMMUNICATOR *, 
				     MPIR_HBT_node * );
int MPIR_Attr_free_subtree( struct MPIR_COMMUNICATOR *, 
					MPIR_HBT_node * ) ;
#endif
void MPIR_Attr_make_perm ( int );
int MPIR_Attr_copy( struct MPIR_COMMUNICATOR *, 
				struct MPIR_COMMUNICATOR * );
int MPIR_Attr_free_tree  ( struct MPIR_COMMUNICATOR * ) ;
int MPIR_Attr_dup_tree( struct MPIR_COMMUNICATOR *, 
				    struct MPIR_COMMUNICATOR * );
int MPIR_Attr_create_tree  ( struct MPIR_COMMUNICATOR * ) ;
int MPIR_Keyval_create( MPI_Copy_function *, MPI_Delete_function *, 
				    int *, void *, int );
int MPIR_Comm_make_coll( struct MPIR_COMMUNICATOR *, 
				     MPIR_COMM_TYPE );
int MPIR_Comm_N2_prev  ( struct MPIR_COMMUNICATOR *, int * ) ;
int MPIR_Dump_comm  ( struct MPIR_COMMUNICATOR * ) ;
int MPIR_Intercomm_high  ( struct MPIR_COMMUNICATOR *, int * ) ;
struct MPIR_GROUP * MPIR_CreateGroup  ( int ) ;
void MPIR_FreeGroup  ( struct MPIR_GROUP * ) ;
void MPIR_SetToIdentity  ( struct MPIR_GROUP * ) ;
void MPIR_Comm_remember  ( struct MPIR_COMMUNICATOR * ) ;
void MPIR_Comm_forget    ( struct MPIR_COMMUNICATOR * ) ;
#ifndef MPIR_Group_dup
/* If it's not a macro, then it must be a function */
int MPIR_Group_dup  ( struct MPIR_GROUP *, struct MPIR_GROUP * * ) ;
#endif
int MPIR_Dump_group  ( struct MPIR_GROUP * ) ;
int MPIR_Dump_ranks  ( int, int * ) ;
int MPIR_Dump_ranges  ( int, int * ) ;
int MPIR_Powers_of_2  ( int, int *, int * ) ;
int MPIR_Group_N2_prev  ( struct MPIR_GROUP *, int * ) ;
int MPIR_Sort_split_table  ( int, int, int *, int *, int * ) ;
int MPIR_Context_alloc( struct MPIR_COMMUNICATOR *, int, 
				    MPIR_CONTEXT * );
int MPIR_Context_dealloc( struct MPIR_COMMUNICATOR *, int, 
				      MPIR_CONTEXT );
int MPIR_dup_fn   ( MPI_Comm, int, void *, void *, void *, int * ) ;
void MPIR_Comm_init( struct MPIR_COMMUNICATOR *, 
		     struct MPIR_COMMUNICATOR *, MPIR_COMM_TYPE );

/* pt2pt */
void MPIR_Set_Status_error_array ( MPI_Request [], int, int, int,
					     MPI_Status [] );
void MPIR_Sendq_init ( void );
void MPIR_Sendq_finalize ( void );
void MPIR_Remember_send ( MPIR_SHANDLE *, void *, int, MPI_Datatype,
			  int, int, struct MPIR_COMMUNICATOR * );
void MPIR_Forget_send ( MPIR_SHANDLE * );

/* env */
int MPIR_Init  ( int *, char *** ) ;
int MPIR_Op_setup  ( MPI_User_function *, int, int, MPI_Op ) ;
void * MPIR_Breakpoint ( void );
int MPIR_GetErrorMessage ( int, char *, const char ** );
void MPIR_Init_dtes ( void );
void MPIR_Free_dtes ( void );
void MPIR_Datatype_iscontig ( MPI_Datatype, int * );
void MPIR_Msg_queue_export( void );
int MPIR_Errhandler_create ( MPI_Handler_function *, MPI_Errhandler );
void MPIR_Errhandler_mark ( MPI_Errhandler, int );
char *MPIR_Err_map_code_to_string( int );
/* topol */
void MPIR_Topology_Init (void);
void MPIR_Topology_Free (void);
void MPIR_Topology_init (void);
void MPIR_Topology_finalize (void);

/* Error handling */
#if defined(USE_STDARG) && !defined(USE_OLDSTYLE_STDARG)
int MPIR_Err_setmsg( int, int, const char *, const char *, const char *, ... );
#else
int MPIR_Err_setmsg();
#endif
/* util */
/*
void *MPIR_SBalloc  ( void * ) ;
void MPIR_SBfree  ( void *, void * ) ;
 */
int MPIR_dump_dte ( MPI_Datatype, int );

int MPIR_BsendInitBuffer  ( void *, int ) ;
int MPIR_BsendRelease  ( void **, int * ) ;
int MPIR_BsendBufferPrint  ( void ) ;
void MPIR_IbsendDatatype ( struct MPIR_COMMUNICATOR *, void *, int, 
			   struct MPIR_DATATYPE *,
			   int, int, int, int, MPI_Request, int * );

void MPIR_HBT_Free (void);
void MPIR_HBT_Init (void);
#ifdef FOO
int MPIR_HBT_new_tree  ( MPIR_HBT ** ) ;
int MPIR_HBT_new_node  ( int, void *, MPIR_HBT_node ** ) ;
int MPIR_HBT_free_node  ( MPIR_HBT_node * ) ;
int MPIR_HBT_free_subtree  ( MPIR_HBT_node * ) ;
int MPIR_HBT_free_tree  ( MPIR_HBT * ) ;
int MPIR_HBT_lookup  ( MPIR_HBT *, int, MPIR_HBT_node ** ) ;
int MPIR_HBT_insert  ( MPIR_HBT *, MPIR_HBT_node * ) ;
int MPIR_HBT_delete  ( MPIR_HBT *, int, MPIR_HBT_node ** ) ;
#endif

void MPIR_DestroyPointer ( void );
void *MPIR_ToPointer ( int );
int  MPIR_FromPointer (void *);
void MPIR_RmPointer ( int );
int  MPIR_UsePointer (FILE *);
void MPIR_RegPointerIdx ( int, void * );
void MPIR_PointerPerm ( int );
void MPIR_DumpPointers ( FILE * );
void MPIR_PointerOpts ( int );

#ifdef HAVE_PRINT_BACKTRACE
void MPIR_Print_backtrace( char *, int, char *, ... );
void MPIR_Save_executable_name( const char * );
#endif

/* Parts of MPID/MPICH interface not declared elsewhere (should they be?) */
void MPIR_Ref_init ( int, char * );

/* Collective setup */
void MPIR_Comm_collops_init ( struct MPIR_COMMUNICATOR *, MPIR_COMM_TYPE );

#endif




