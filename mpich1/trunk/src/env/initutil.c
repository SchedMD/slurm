/*
 *  $Id: initutil.c,v 1.35 2004/04/30 20:39:20 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */


#include "mpiimpl.h"
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include "cmnargs.h"
#include "sbcnst2.h"
/* Error handlers in pt2pt */
#include "mpipt2pt.h"

#if defined(MPID_HAS_PROC_INFO)
/* This is needed to use select for a timeout */
#ifdef HAVE_WINSOCK2_H
#include <winsock2.h>
#include "nt_global.h"
#else
#include <sys/time.h>
#endif
#include <sys/types.h>
#endif

#ifdef HAVE_UNISTD_H
/* For nice, sleep */
#include <unistd.h>
#endif

#if defined(MPE_USE_EXTENSIONS) && !defined(MPI_NO_MPEDBG)
#include "../../mpe/include/mpeexten.h"
#endif

#ifndef PATCHLEVEL_SUBMINOR
#define PATCHLEVEL_SUBMINOR 0
#endif

/* 
   Global definitions of variables that hold information about the
   version and patchlevel.  This allows easy access to the version 
   and configure information without requiring the user to run an MPI
   program 
*/
const int MPIR_Version_patches[]    = { PATCHES_APPLIED_LIST -1 };
const int MPIR_Version_major        = PATCHLEVEL_MAJOR;
const int MPIR_Version_minor        = PATCHLEVEL_MINOR;
const int MPIR_Version_subminor     = PATCHLEVEL_SUBMINOR;
const char MPIR_Version_string[]    = PATCHLEVEL_STRING;
const char MPIR_Version_date[]      = PATCHLEVEL_RELEASE_DATE;
const char MPIR_Version_configure[] = CONFIGURE_ARGS_CLEAN;
#ifdef MPIRUN_DEVICE
const char MPIR_Version_device[]    = MPIRUN_DEVICE;
#else
const char MPIR_Version_device[]    = "Unknown MPICH device";
#endif

/* #define DEBUG(a) {a}  */
#define DEBUG(a)


/* SEBASTIEN L: EXPERIMENTS!!! */
#include "mpidefs.h"
/* END OF EXPERIMENTATIONS */


/* need to change these later */
MPI_Info *MPIR_Infotable = NULL;
int MPIR_Infotable_ptr = 0, MPIR_Infotable_max = 0;

/* Global memory management variables for fixed-size blocks */
void *MPIR_errhandlers;  /* sbcnst Error handlers */
void *MPIR_qels;         /* sbcnst queue elements */
void *MPIR_fdtels;       /* sbcnst flat datatype elements */
void *MPIR_topo_els;     /* sbcnst topology elements */

/* Global communicators.  Initialize as null in case we fail during startup */
/* We need the structure that MPI_COMM_WORLD refers to so often, 
   we export it */
struct MPIR_COMMUNICATOR *MPIR_COMM_WORLD = 0;
struct MPIR_COMMUNICATOR *MPIR_COMM_SELF = 0;

struct MPIR_GROUP *MPIR_GROUP_EMPTY = 0;

/* Home for this variable (used in MPI_Initialized) */
int MPIR_Has_been_initialized = 0;

/* MPI_Comm MPI_COMM_SELF = 0, MPI_COMM_WORLD = 0; */
/* MPI_Group MPI_GROUP_EMPTY = 0; */

/* Global MPIR process id (from device) */
int MPIR_tid;

/* Permanent attributes */
/* Places to hold the values of the attributes */
static int MPI_TAG_UB_VAL, MPI_HOST_VAL, MPI_IO_VAL, MPI_WTIME_IS_GLOBAL_VAL;

/* Command-line flags */
int MPIR_Print_queues = 0;
#ifdef MPIR_MEMDEBUG
int MPIR_Dump_Mem = 1;
#else
int MPIR_Dump_Mem = 0;
#endif
int MPIR_Dump_Ptrs = 0;

/* MPICH extension keyvals */
int MPICHX_QOS_BANDWIDTH  = MPI_KEYVAL_INVALID;
int MPICHX_QOS_PARAMETERS = MPI_KEYVAL_INVALID;

#ifndef MPID_NO_FORTRAN
#include "mpi_fortran.h"
#endif

/*
   MPIR_Init - Initialize the MPI execution environment

   Input Parameters:
+  argc - Pointer to the number of arguments 
-  argv - Pointer to the argument vector

   See MPI_Init for the description of the input to this routine.

   This routine is in a separate file from MPI_Init to allow profiling 
   libraries to not replace MPI_Init; without this, you can get errors
   from the linker about multiply defined libraries.

 */
int MPIR_Init(int *argc, char ***argv)
{
    int            size, mpi_errno, i;
    void           *ADIctx = 0;
    static char myname[] = "MPI_INIT";

    TR_PUSH("MPIR_Init");

    if (MPIR_Has_been_initialized) 
    return 
        MPIR_ERROR( (struct MPIR_COMMUNICATOR *)0, 
	    MPIR_ERRCLASS_TO_CODE(MPI_ERR_OTHER,MPIR_ERR_INIT), myname);

    /* Sanity check.  If this program is being run with MPIRUN, check that
       we have the expected information.  That is, make sure that we
       are not trying to use mpirun.ch_p4 to start mpirun.ch_shmem.
       This has a fall through in that if there is no information, the test
       is ignored
     */
#if defined(MPIRUN_DEVICE) && defined(MPIRUN_MACHINE)
    {char *p1, *p2;
#ifdef HAVE_NO_C_CONST
    extern char *getenv (char *);
#else
    extern char *getenv (const char *);
#endif

    mpi_errno = MPI_SUCCESS;
    p1 = getenv( "MPIRUN_DEVICE" );
    p2 = getenv( "MPIRUN_MACHINE" );
    if (p1 && strcmp( p1, MPIRUN_DEVICE ) != 0) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_OTHER, MPIR_ERR_MPIRUN, myname,
				     (char *)0,(char *)0, p1, MPIRUN_DEVICE );
    }
    else if (p2 && strcmp( p2, MPIRUN_MACHINE ) != 0) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_OTHER, MPIR_ERR_MPIRUN_MACHINE, 
				     myname,
				     (char *)0,(char *)0, p2, MPIRUN_MACHINE );
    }

    if (mpi_errno) {
	MPIR_Errors_are_fatal( (MPI_Comm*)0, &mpi_errno, myname,
			       __FILE__, (int *)0 );
    }
    }
#endif

    /* If we wanted to be able to check if we're being debugged,
     * (so that we could explicitly request that the other processes
     * come up stopped), this would be a good place to do it.
     * That information should be available by looking at a global.
     *
     * For now we don't bother, but assume that we're cheating and using
     * an extra argument to mpirun which 
     * 1) starts a debugger on the host process
     * 2) causes the other processes to stop in mpi_init (see below).
     */
    MPID_Init( argc, argv, (void *)0, &mpi_errno );
    if (mpi_errno) {
	MPIR_Errors_are_fatal( (MPI_Comm*)0, &mpi_errno, myname, 
			       __FILE__, (int *)0 );
    }
#ifdef HAVE_PRINT_BACKTRACE
    MPIR_Save_executable_name( (const char *)(*argv)[0] );
#endif

    DEBUG(MPIR_tid=MPID_MyWorldRank;)

#ifdef MPID_HAS_PROC_INFO
    if (MPID_MyWorldRank == 0) {
	/* We're the master process, so we need to grab the info
	 * about where and who all the other processes are 
	 * and flatten it in case the debugger wants it.
	 */
	MPIR_proctable = (MPIR_PROCDESC *)MALLOC(MPID_MyWorldSize*sizeof(MPIR_PROCDESC));
	
	/* Cause extra state to be remembered */
	MPIR_being_debugged = 1;
	
	/* Link in the routine that contains info on the location of
	   the message queue DLL */
	MPIR_Msg_queue_export();

	if (MPIR_proctable)
	{
	    for (i=0; i<MPID_MyWorldSize; i++)
	    {
		MPIR_PROCDESC *this = &MPIR_proctable[i];

		this->pid = MPID_getpid(i, &this->host_name, &this->executable_name);
		DEBUG(PRINTF("[%d] %s :: %s %d\n", i, 
			     this->host_name ? this->host_name : "local",
			     this->executable_name ? this->executable_name : "", 
			     this->pid);)
	    }
	    
	    MPIR_proctable_size = MPID_MyWorldSize;
	    /* Let the debugger know that the info is now valid */
	    MPIR_debug_state    = MPIR_DEBUG_SPAWNED;
	    MPIR_Breakpoint();  
	}
    } 
#endif

    /* Indicate that any pointer conversions are permanent */
    MPIR_PointerPerm( 1 );

    DEBUG(PRINTF("[%d] About to do allocations\n", MPIR_tid);)

    /* initialize topology code */
    MPIR_Topology_init();

    /* initialize memory allocation data structures */
    MPIR_errhandlers= MPID_SBinit( sizeof( struct MPIR_Errhandler ), 10, 10 );

    MPIR_SENDQ_INIT();
    MPIR_HBT_Init();
    MPIR_Topology_Init();

    /* This handles ALL datatype initialization */
    MPIR_Init_dtes();
#ifndef MPID_NO_FORTRAN
    MPIR_InitFortranDatatypes();
#endif
    /* Create Error handlers */
    /* Must create at preassigned values */
    MPIR_Errhandler_create( MPIR_Errors_are_fatal, MPI_ERRORS_ARE_FATAL );
    MPIR_Errhandler_create( MPIR_Errors_return,    MPI_ERRORS_RETURN );
    MPIR_Errhandler_create( MPIR_Errors_warn,      MPIR_ERRORS_WARN );
    
    /* GROUP_EMPTY is a valid empty group */
    DEBUG(PRINTF("[%d] About to create groups and communicators\n", MPIR_tid);)
    MPIR_GROUP_EMPTY     = MPIR_CreateGroup(0);
    MPIR_GROUP_EMPTY->self = MPI_GROUP_EMPTY;
    MPIR_RegPointerIdx( MPI_GROUP_EMPTY, MPIR_GROUP_EMPTY );
    MPIR_GROUP_EMPTY->permanent = 1;

    MPIR_ALLOC(MPIR_COMM_WORLD,NEW(struct MPIR_COMMUNICATOR),
	       (struct MPIR_COMMUNICATOR *)0,
	       MPI_ERR_EXHAUSTED,myname);
    MPIR_SET_COOKIE(MPIR_COMM_WORLD,MPIR_COMM_COOKIE)
    MPIR_RegPointerIdx( MPI_COMM_WORLD, MPIR_COMM_WORLD );
    MPIR_COMM_WORLD->self = MPI_COMM_WORLD;

    MPIR_COMM_WORLD->comm_type	   = MPIR_INTRA;
    MPIR_COMM_WORLD->ADIctx	   = ADIctx;
    size     = MPID_MyWorldSize;
    MPIR_tid = MPID_MyWorldRank;
    MPIR_COMM_WORLD->group	   = MPIR_CreateGroup( size );
    MPIR_COMM_WORLD->group->self   = 
	(MPI_Group) MPIR_FromPointer( MPIR_COMM_WORLD->group );
#if defined(MPID_DEVICE_SETS_LRANKS)
    MPID_Set_lranks ( MPIR_COMM_WORLD->group );
#else
    MPIR_SetToIdentity( MPIR_COMM_WORLD->group );
#endif
    MPIR_Group_dup ( MPIR_COMM_WORLD->group, 
			   &(MPIR_COMM_WORLD->local_group) );
    MPIR_COMM_WORLD->local_rank	   = MPIR_COMM_WORLD->local_group->local_rank;
    MPIR_COMM_WORLD->lrank_to_grank = MPIR_COMM_WORLD->group->lrank_to_grank;
    MPIR_COMM_WORLD->np		   = MPIR_COMM_WORLD->group->np;
    MPIR_COMM_WORLD->send_context   = MPIR_WORLD_PT2PT_CONTEXT;
    MPIR_COMM_WORLD->recv_context   = MPIR_WORLD_PT2PT_CONTEXT;
    MPIR_COMM_WORLD->error_handler  = MPI_ERRORS_ARE_FATAL;
    MPIR_COMM_WORLD->use_return_handler = 0;
    MPIR_Errhandler_mark( MPI_ERRORS_ARE_FATAL, 1 );
    MPIR_COMM_WORLD->ref_count	   = 1;
    MPIR_COMM_WORLD->permanent	   = 1;
    MPIR_Attr_create_tree ( MPIR_COMM_WORLD );
    (void)MPID_CommInit( (struct MPIR_COMMUNICATOR *)0, MPIR_COMM_WORLD );

    MPIR_COMM_WORLD->comm_cache	   = 0;
    MPIR_Comm_make_coll ( MPIR_COMM_WORLD, MPIR_INTRA );

    MPIR_COMM_WORLD->comm_name      = 0;
    MPI_Comm_set_name ( MPI_COMM_WORLD, "MPI_COMM_WORLD");

    /* Predefined attributes for MPI_COMM_WORLD */
    DEBUG(PRINTF("[%d] About to create keyvals\n", MPIR_tid);)
#define NULL_COPY (MPI_Copy_function *)0
#define NULL_DEL  (MPI_Delete_function*)0
	i = MPI_TAG_UB;
    MPIR_Keyval_create( NULL_COPY, NULL_DEL, &i, (void *)0, 0 );
        i = MPI_HOST;
    MPIR_Keyval_create( NULL_COPY, NULL_DEL, &i, (void *)0, 0 );
        i = MPI_IO;
    MPIR_Keyval_create( NULL_COPY, NULL_DEL, &i, (void *)0, 0 );
        i = MPI_WTIME_IS_GLOBAL;
    MPIR_Keyval_create( NULL_COPY, NULL_DEL, &i, (void *)0, 0 );

    /* Initialize any device-specific keyvals */
    MPID_KEYVAL_INIT();
    MPI_TAG_UB_VAL = MPID_TAG_UB;
#ifndef MPID_HOST
#define MPID_HOST MPI_PROC_NULL
#endif    
    MPI_HOST_VAL   = MPID_HOST;

    /* The following isn't strictly correct, but I'm going to leave it
       in for now.  I've tried to make this correct for a few systems
       for which I know the answer.  
     */
    /* MPI_PROC_NULL is the correct answer for IBM MPL version 1 and
       perhaps for some other systems */
    /*     MPI_IO_VAL = MPI_PROC_NULL; */
#ifndef MPID_IO
#define MPID_IO MPI_ANY_SOURCE
#endif
    MPI_IO_VAL = MPID_IO;
    /* The C versions - pass the address of the variable containing the 
       value */
    MPI_Attr_put( MPI_COMM_WORLD, MPI_TAG_UB, (void*)&MPI_TAG_UB_VAL );
    MPI_Attr_put( MPI_COMM_WORLD, MPI_HOST,   (void*)&MPI_HOST_VAL );
    MPI_Attr_put( MPI_COMM_WORLD, MPI_IO,     (void*)&MPI_IO_VAL );

    /* This is a dummy call to force MPI_Attr_get to be loaded */
    if (MPI_IO_VAL == -37) {
	void *ptr; int flag;
	MPI_Attr_get( MPI_COMM_SELF, MPI_IO, &ptr, &flag );
    }
/* Add the flag on whether the timer is global */
#ifdef MPID_Wtime_is_global
    MPI_WTIME_IS_GLOBAL_VAL = MPID_Wtime_is_global();
#else
    MPI_WTIME_IS_GLOBAL_VAL = 0;
#endif    
    MPI_Attr_put( MPI_COMM_WORLD, MPI_WTIME_IS_GLOBAL, 
		  (void *)&MPI_WTIME_IS_GLOBAL_VAL );
/* Make these permanent.  Must do this AFTER the values are set (because
   changing a value of a permanent attribute is an error) */
    MPIR_Attr_make_perm( MPI_TAG_UB );
    MPIR_Attr_make_perm( MPI_HOST );
    MPIR_Attr_make_perm( MPI_IO );
    MPIR_Attr_make_perm( MPI_WTIME_IS_GLOBAL );

    /* Remember COMM_WORLD for the debugger */
    MPIR_Comm_remember ( MPIR_COMM_WORLD );

    /* COMM_SELF is the communicator consisting only of myself */
    MPIR_ALLOC(MPIR_COMM_SELF,NEW(struct MPIR_COMMUNICATOR),
	       (struct MPIR_COMMUNICATOR *)0,
	       MPI_ERR_EXHAUSTED,myname);
    MPIR_SET_COOKIE(MPIR_COMM_SELF,MPIR_COMM_COOKIE)
    MPIR_RegPointerIdx( MPI_COMM_SELF, MPIR_COMM_SELF );
    MPIR_COMM_SELF->self = MPI_COMM_SELF;

    MPIR_COMM_SELF->comm_type		    = MPIR_INTRA;
    MPIR_COMM_SELF->group		    = MPIR_CreateGroup( 1 );
    MPIR_COMM_SELF->group->self   = 
	(MPI_Group) MPIR_FromPointer( MPIR_COMM_SELF->group );
    MPIR_COMM_SELF->group->local_rank	    = 0;
    MPIR_COMM_SELF->group->lrank_to_grank[0] = MPIR_tid;
    MPIR_Group_dup ( MPIR_COMM_SELF->group, 
			    &(MPIR_COMM_SELF->local_group) );
    MPIR_COMM_SELF->local_rank	      = 
	MPIR_COMM_SELF->local_group->local_rank;
    MPIR_COMM_SELF->lrank_to_grank     = 
	MPIR_COMM_SELF->group->lrank_to_grank;
    MPIR_COMM_SELF->np		      = MPIR_COMM_SELF->group->np;
    MPIR_COMM_SELF->send_context	      = MPIR_SELF_PT2PT_CONTEXT;
    MPIR_COMM_SELF->recv_context	      = MPIR_SELF_PT2PT_CONTEXT;
    MPIR_COMM_SELF->error_handler      = MPI_ERRORS_ARE_FATAL;
    MPIR_COMM_SELF->use_return_handler = 0;
    MPIR_Errhandler_mark( MPI_ERRORS_ARE_FATAL, 1 );
    MPIR_COMM_SELF->ref_count	      = 1;
    MPIR_COMM_SELF->permanent	      = 1;
    MPIR_Attr_create_tree ( MPIR_COMM_SELF );
    (void)MPID_CommInit( MPIR_COMM_WORLD, MPIR_COMM_SELF );
    MPIR_COMM_SELF->comm_cache	      = 0;
    MPIR_Comm_make_coll ( MPIR_COMM_SELF, MPIR_INTRA );
    /* Remember COMM_SELF for the debugger */
    MPIR_COMM_SELF->comm_name          = 0;
    MPI_Comm_set_name ( MPI_COMM_SELF, "MPI_COMM_SELF");
    MPIR_Comm_remember ( MPIR_COMM_SELF );


    /* Predefined combination functions */
    DEBUG(PRINTF("[%d] About to create combination functions\n", MPIR_tid);)

    MPIR_Op_setup( MPIR_MAXF,   1, 1, MPI_MAX );
    MPIR_Op_setup( MPIR_MINF,   1, 1, MPI_MIN );
    MPIR_Op_setup( MPIR_SUM,    1, 1, MPI_SUM );
    MPIR_Op_setup( MPIR_PROD,   1, 1, MPI_PROD );
    MPIR_Op_setup( MPIR_LAND,   1, 1, MPI_LAND );
    MPIR_Op_setup( MPIR_BAND,   1, 1, MPI_BAND );
    MPIR_Op_setup( MPIR_LOR,    1, 1, MPI_LOR );
    MPIR_Op_setup( MPIR_BOR,    1, 1, MPI_BOR );
    MPIR_Op_setup( MPIR_LXOR,   1, 1, MPI_LXOR );
    MPIR_Op_setup( MPIR_BXOR,   1, 1, MPI_BXOR );
    MPIR_Op_setup( MPIR_MAXLOC, 1, 1, MPI_MAXLOC );
    MPIR_Op_setup( MPIR_MINLOC, 1, 1, MPI_MINLOC );

#ifndef MPID_NO_FORTRAN
    MPIR_InitFortran( );
#endif
    MPIR_PointerPerm( 0 );

    DEBUG(PRINTF("[%d] About to search for argument list options\n",MPIR_tid);)

    /* Search for "-mpi debug" options etc.  We need a better interface.... */
    if (argv && *argv) {
	for (i=1; i<*argc; i++) {
	    if ((*argv)[i]) {
		if (strcmp( (*argv)[i], "-mpiqueue" ) == 0) {
		    MPIR_Print_queues = 1;
		    (*argv)[i] = 0;
		    }
		else if (strcmp((*argv)[i],"-mpiversion" ) == 0) {
		    char ADIname[128];
		    char *patches = PATCHES_APPLIED;
		    MPID_Version_name( ADIname );
		    PRINTF( "MPICH %3.1f.%d%s of %s., %s\n", 
			    PATCHLEVEL, PATCHLEVEL_SUBMINOR, 
			    PATCHLEVEL_RELEASE_KIND, PATCHLEVEL_RELEASE_DATE,
			    ADIname );
		    PRINTF( "Configured with %s\n", CONFIGURE_ARGS_CLEAN );
		    if (strlen(patches) > 0) {
			PRINTF( "Patches applied %s\n", patches );
		    }
		    (*argv)[i] = 0;
		    }
#ifdef HAVE_NICE
		else if (strcmp((*argv)[i],"-mpinice" ) == 0) {
		    int niceincr;
		    (*argv)[i] = 0;
		    i++;
		    if (i <*argc) {
			niceincr = atoi( (*argv)[i] );
			nice(niceincr);
			(*argv)[i] = 0;
			}
		    else {
			printf( "Missing argument for -mpinice\n" );
			}
		    }
#endif
#ifdef FOO
#if defined(MPE_USE_EXTENSIONS) && !defined(MPI_NO_MPEDBG)
		else if (strcmp((*argv)[i],"-mpedbg" ) == 0) {
		    MPE_Errors_call_dbx_in_xterm( (*argv)[0], (char *)0 ); 
		    MPE_Signals_call_debugger();
		    (*argv)[i] = 0;
		    }
#endif
#if defined(MPE_USE_EXTENSIONS) && !defined(MPI_NO_MPEDBG)
		else if (strcmp((*argv)[i],"-mpegdb" ) == 0) {
		    MPE_Errors_call_gdb_in_xterm( (*argv)[0], (char *)0 ); 
		    MPE_Signals_call_debugger();
		    (*argv)[i] = 0;
		    }
#endif
#endif
		else if (strcmp((*argv)[i],"-mpichtv" ) == 0) {
		    (*argv)[i] = 0; /* Eat it up so the user doesn't see it */

		    /* Cause extra state to be remembered */
		    MPIR_being_debugged = 1;
		}
		else if (strcmp((*argv)[i],"-mpichksq") == 0) {
                  /* This tells us to Keep Send Queues so that we 
		   * can look at them if we're attached to.
		   */
	          (*argv)[i] = 0; /* Eat it up so the user doesn't see it */
	          MPIR_being_debugged = 1;
	        }
	      
#ifdef MPIR_PTRDEBUG
		else if (strcmp((*argv)[i],"-mpiptrs") == 0) {
		    MPIR_Dump_Ptrs = 1;
		}
#endif
#ifdef MPIR_MEMDEBUG
		else if (strcmp((*argv)[i],"-mpimem" ) == 0) {
		    MPID_trDebugLevel( 1 );
		    }
#endif
		}
	    }
	/* Remove the null arguments */
	MPID_ArgSqueeze( argc, *argv );
	}

/* As per Jim Cownie's request #3683; allows debugging even if this startup
   code should not be used. */
/* The real answer is to use a different definition for this, since
   stop-when-starting-for-debugger is different from HAS_PROC_INFO */
#ifdef MPID_HAS_PROC_INFO
    /* Check to see if we're not the master,
     * and wait for the debugger to attach if we're 
     * a slave. The debugger will reset the debug_gate.
     * There is no code in the library which will do it !
     */
    if (MPIR_being_debugged && MPID_MyWorldRank != 0) {
	while (MPIR_debug_gate == 0) {
	    /* Wait to be attached to, select avoids 
	     * signaling and allows a smaller timeout than 
	     * sleep(1)
	     */
	    struct timeval timeout;
	    timeout.tv_sec  = 0;
	    timeout.tv_usec = 250000;
	    select( 0, (void *)0, (void *)0, (void *)0,
		    &timeout );
	}
    }
#endif

    /* barrier */
    MPIR_Has_been_initialized = 1;

    DEBUG(PRINTF("[%d] About to exit from MPI_Init\n", MPIR_tid);)
    TR_POP;
    return MPI_SUCCESS;
}


/****************************************************************************/
/* The various MPI objects (MPI_Errhandler, MPI_Op, ... ) require some      */
/* special routines to initialize and manipulate them.  For the "smaller"   */
/* objects, that code is here.  The larger objects (e.g., MPI_Comm)         */
/* have their own xxx_util.c or initxxx.c files that contain the needed     */
/* code.                                                                    */
/****************************************************************************/
/* Utility code for Errhandlers                                             */
/****************************************************************************/
#define MPIR_SBalloc MPID_SBalloc
int MPIR_Errhandler_create( function, errhandler )
MPI_Handler_function *function;
MPI_Errhandler       errhandler;
{
    struct MPIR_Errhandler *new;

    MPIR_ALLOC(new,(struct MPIR_Errhandler*) MPIR_SBalloc( MPIR_errhandlers ),
	       MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, 
			   "MPI_ERRHANDLER_CREATE" );

    MPIR_SET_COOKIE(new,MPIR_ERRHANDLER_COOKIE);
    new->routine   = function;
    new->ref_count = 1;

    MPIR_RegPointerIdx( errhandler, new );
    return MPI_SUCCESS;
}

/* Change the reference count of errhandler by incr */
#if 0
#   ifdef MPIR_ToPointer
#   undef MPIR_ToPointer
#   endif
#endif

void MPIR_Errhandler_mark( errhandler, incr )
MPI_Errhandler errhandler;
int            incr;
{
    struct MPIR_Errhandler *new = (struct MPIR_Errhandler *) 
	MPIR_ToPointer( errhandler );
    if (new) {
	if (incr == 1) {
	    MPIR_REF_INCR(new);
	}
	else {
	    MPIR_REF_DECR(new);
	}
    }
}
