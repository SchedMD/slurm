#define BUILDING_VMPI_PROTO_MODULE
#include "chconfig.h"

#if defined(VMPI)


/* 
 * THIS MUST BE THE VENDOR'S mpi.h ... NOT MPICH's, 
 * so we must make sure that this file is compiled WITHOUT MPICH's -I path.
 */
#include <mpi.h>
#include "vmpi.h"
#include "debug.h"
#include "mem.h"
#include "protos.h"

#if defined(DEBUG_MPID)
#    define debug_printf(A) printf A
#   define debug_check_mpi_result(F,R)			\
    {							\
    	if ((R) != MPI_SUCCESS)				\
    	{						\
    	    debug_printf(("ERROR: failed " F "\n"));	\
    	}						\
    }
#else
#    define debug_printf(A) printf A
#    define debug_check_mpi_result(F,R)
#endif /* defined(DEBUG_MPID) */

static int vmpi_initialized = 0;

/*******************/
/* Local Functions */
/*******************/
static int mpi_error_to_vmpi_error(
    int					mpi_error);


#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_init
int mp_init(
    int *				argc,
    char ***				argv)
{
    int					initialized;
    int					rc;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_MP);
    
    /*
     * Call the vendor implementation of MPI_Init(), but only if another
     * library/module hasn't already called MPI_Init().  See the comments in
     * mp_finalize() for a more detailed description of the problem.
     */
    MPI_Initialized(&initialized);
    if (!vmpi_initialized && !initialized)
    {
	rc = MPI_Init(argc, argv);

	debug_check_mpi_result("MPI_Init()", rc);
	
	if (rc == MPI_SUCCESS)
	{
	    vmpi_initialized = 1;
	}
    }
    else
    {
	rc = MPI_SUCCESS;
    }

    MPI_Errhandler_set(MPI_COMM_WORLD, MPI_ERRORS_RETURN);

  /* fn_exit: */
    rc = mpi_error_to_vmpi_error(rc);
    DEBUG_FN_EXIT(DEBUG_MODULE_MP);
    return rc;
} /* end mp_init() */


#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_finalize
void mp_finalize()
{
    DEBUG_FN_ENTRY(DEBUG_MODULE_MP);
    
    /*
     * Call the vendor implementation of MPI_Finalize(), but only if we also
     * called MPI_Init().  If some other library/module called MPI_Init(), then
     * we should let them decide when to call MPI_Finalize().
     *
     * This is particularily important for Globus/Nexus which delays calling
     * MPI_Finalize() until exit() is called.  It does this so that Nexus can
     * be activated and deactivated multiple times, something MPI can't handle.
     * Also, Nexus keeps on outstanding receive posted until exit() is called,
     * and calling MPI_Finalize() before that receive is cancelled causes some
     * implementations (SGI) to hang.
     */
    if (vmpi_initialized)
    {
	MPI_Finalize();
    }

  /* fn_exit: */
    DEBUG_FN_EXIT(DEBUG_MODULE_MP);
} /* end mp_finalize() */


/* 
 * '*nbytes' includes null char at end
 */
#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_create_miproto
void mp_create_miproto(
    char **				mp_miproto,
    int *				nbytes)
{

    int					my_rank;
    char				s_my_rank[10];
    char				s_mpitype[10];
    char *				buff;
    int					size;

    DEBUG_FN_ENTRY(DEBUG_MODULE_MP);

    globus_libc_sprintf(s_mpitype, "%d", mpi);
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
    globus_libc_sprintf(s_my_rank, "%d", my_rank);

    if (my_rank)
    {
	/* slave */
	MPI_Bcast(&size, 1, MPI_INT, 0, MPI_COMM_WORLD);
	buff = globus_libc_malloc(size);
	MPI_Bcast(buff, size, MPI_BYTE, 0, MPI_COMM_WORLD);
    }
    else
    {
	/* master */
	buff = globus_get_unique_session_string();
	size = strlen(buff) + 1;
	MPI_Bcast(&size, 1, MPI_INT, 0, MPI_COMM_WORLD);
	MPI_Bcast(buff, size, MPI_BYTE, 0, MPI_COMM_WORLD);
    } /* endif */

    *nbytes = strlen(s_mpitype) + 1 + strlen(buff) + 1 + strlen(s_my_rank) + 1;
    if (!(*mp_miproto = globus_libc_malloc(*nbytes)))
    {
	globus_libc_fprintf(stderr, 
	    "ERROR: mp_create_miproto() failed malloc of %d bytes\n", 
	    *nbytes);
	abort();
    } /* endif */

    globus_libc_sprintf(*mp_miproto, "%s %s %s", s_mpitype, buff, s_my_rank);

    globus_libc_free(buff);

  /* fn_exit: */
    DEBUG_FN_EXIT(DEBUG_MODULE_MP);
} /* end mp_create_miproto() */


#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_send
int mp_send(
    void *				buff, 
    int					count, 
    void *				type, 
    int					dest, 
    int					tag,
    void *				comm)
{
    int					rc;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_MP);
    
    rc = MPI_Send(buff,
		  count,
		  *((MPI_Datatype *) type),
		  dest,
		  tag,
		  *((MPI_Comm *) comm));

    debug_check_mpi_result("MPI_Send()", rc);

  /* fn_exit: */
    rc = mpi_error_to_vmpi_error(rc);
    DEBUG_FN_EXIT(DEBUG_MODULE_MP);
    return rc;
} /* end mp_send() */


#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_isend
int mp_isend(
    void *				buff, 
    int					count, 
    void *				type, 
    int					dest, 
    int					tag,
    void *				comm,
    void *				request)
{
    int					rc;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_MP);

    rc = MPI_Isend(buff,
		   count,
		   *((MPI_Datatype *) type),
		   dest,
		   tag,
		   *((MPI_Comm *) comm),
		   (MPI_Request *) request);

    debug_check_mpi_result("MPI_Isend()", rc);

  /* fn_exit: */
    rc = mpi_error_to_vmpi_error(rc);
    DEBUG_FN_EXIT(DEBUG_MODULE_MP);
    return rc;
} /* end mpi_isend() */


#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_ssend
int mp_ssend(
    void *				buff, 
    int					count, 
    void *				type, 
    int					dest, 
    int					tag,
    void *				comm)
{
    int					rc;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_MP);

    rc = MPI_Ssend(buff,
		   count,
		   *((MPI_Datatype *) type),
		   dest,
		   tag,
		   *((MPI_Comm *) comm));

    debug_check_mpi_result("MPI_Ssend()", rc);

  /* fn_exit: */
    rc = mpi_error_to_vmpi_error(rc);
    DEBUG_FN_EXIT(DEBUG_MODULE_MP);
    return rc;
} /* end mpi_Ssend() */


#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_issend
int mp_issend(
    void *				buff, 
    int					count, 
    void *				type, 
    int					dest, 
    int					tag,
    void *				comm,
    void *				request)
{
    int					rc;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_MP);

    rc = MPI_Issend(buff,
		    count,
		    *((MPI_Datatype *) type),
		    dest,
		    tag,
		    *((MPI_Comm *) comm),
		    (MPI_Request *) request);

    debug_check_mpi_result("MPI_Issend()", rc);

  /* fn_exit: */
    rc = mpi_error_to_vmpi_error(rc);
    DEBUG_FN_EXIT(DEBUG_MODULE_MP);
    return rc;
} /* end mpi_issend() */


#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_cancel
int mp_cancel(void *request)
{
    int rc;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_MP);

    rc = MPI_Cancel((MPI_Request *) request);

    debug_check_mpi_result("MPI_Cancel()", rc);

  /* fn_exit: */
    rc = mpi_error_to_vmpi_error(rc);
    DEBUG_FN_EXIT(DEBUG_MODULE_MP);
    return rc;

} /* end mpi_cancel() */

#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_recv
int mp_recv(
    void *				buff, 
    int					count, 
    void *				type, 
    int					src, 
    int					tag,
    void *				comm,
    void *				status)
{
    int					rc;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_MP);

    if (src == VMPI_ANY_SOURCE) src = MPI_ANY_SOURCE;
    if (tag == VMPI_ANY_TAG)    tag = MPI_ANY_TAG;

    rc = MPI_Recv(buff,
		  count,
		  *((MPI_Datatype *) type),
		  src,
		  tag,
		  *((MPI_Comm *) comm),
		  (MPI_Status *) status);

    debug_check_mpi_result("MPI_Recv()", rc);

  /* fn_exit: */
    rc = mpi_error_to_vmpi_error(rc);
    DEBUG_FN_EXIT(DEBUG_MODULE_MP);
    return rc;
} /* end mp_recv() */

#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_wait
int mp_wait(
    void *				request,
    void *				status)
{
    int					rc;

    DEBUG_FN_ENTRY(DEBUG_MODULE_MP);

    rc = MPI_Wait((MPI_Request *) request, (MPI_Status *) status);

    debug_check_mpi_result("MPI_Wait()", rc);

  /* fn_exit: */
    rc = mpi_error_to_vmpi_error(rc);
    DEBUG_FN_EXIT(DEBUG_MODULE_MP);
    return rc;
} /* end mp_wait() */

#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_test_cancelled
int mp_test_cancelled(
    void *				status,
    int *				flag)
{
    int					rc;

    DEBUG_FN_ENTRY(DEBUG_MODULE_MP);

    rc = MPI_Test_cancelled((MPI_Status *) status, flag);

    debug_check_mpi_result("MPI_Test_cancelled()", rc);

  /* fn_exit: */
    rc = mpi_error_to_vmpi_error(rc);
    DEBUG_FN_EXIT(DEBUG_MODULE_MP);
    return rc;
} /* end mp_test_cancelled() */

#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_test
int mp_test(
    void *				request,
    int *				flag,
    void *				status)
{
    int					rc;

    DEBUG_FN_ENTRY(DEBUG_MODULE_MP);

    rc = MPI_Test((MPI_Request *) request, flag, (MPI_Status *) status);

    debug_check_mpi_result("MPI_Test()", rc);

  /* fn_exit: */
    rc = mpi_error_to_vmpi_error(rc);
    DEBUG_FN_EXIT(DEBUG_MODULE_MP);
    return rc;
} /* end mp_test() */

#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_probe
int mp_probe(
    int					src,
    int					tag,
    void *				comm,
    void *				status)
{
    int					rc;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_MP);

    if (src == VMPI_ANY_SOURCE) src = MPI_ANY_SOURCE;
    if (tag == VMPI_ANY_TAG)    tag = MPI_ANY_TAG;

    rc = MPI_Probe(src, tag, *((MPI_Comm *) comm), (MPI_Status *) status);

    debug_check_mpi_result("MPI_Probe()", rc);

  /* fn_exit: */
    rc = mpi_error_to_vmpi_error(rc);
    DEBUG_FN_EXIT(DEBUG_MODULE_MP);
    return rc;
} /* end mp_probe() */


#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_iprobe
int mp_iprobe(
    int					src,
    int					tag,
    void *				comm,
    int *				flag,
    void *				status)
{
    int					rc;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_MP);

    if (src == VMPI_ANY_SOURCE) src = MPI_ANY_SOURCE;
    if (tag == VMPI_ANY_TAG)    tag = MPI_ANY_TAG;

    rc = MPI_Iprobe(src,
		    tag,
		    *((MPI_Comm *) comm),
		    flag,
		    (MPI_Status *) status);

    debug_check_mpi_result("MPI_IProbe()", rc);

  /* fn_exit: */
    rc = mpi_error_to_vmpi_error(rc);
    DEBUG_FN_EXIT(DEBUG_MODULE_MP);
    return rc;
} /* end mp_iprobe() */


#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_get_count
int mp_get_count(
    void *				status,
    void *				type, 
    int *				count)
{
    int					rc;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_MP);

    rc = MPI_Get_count((MPI_Status *) status,
		       *((MPI_Datatype *) type),
		       count);

    debug_check_mpi_result("MPI_Get_count()", rc);

  /* fn_exit: */
    rc = mpi_error_to_vmpi_error(rc);
    DEBUG_FN_EXIT(DEBUG_MODULE_MP);
    return rc;
} /* mpi_get_count() */


#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_get_elements
int mp_get_elements(
    void *				status,
    void *				type, 
    int *				elements)
{
    int					rc;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_MP);

    rc = MPI_Get_elements((MPI_Status *) status,
			  *((MPI_Datatype *) type),
			  elements);

    debug_check_mpi_result("MPI_Get_elements()", rc);

  /* fn_exit: */
    rc = mpi_error_to_vmpi_error(rc);
    DEBUG_FN_EXIT(DEBUG_MODULE_MP);
    return rc;
} /* mpi_get_elements() */


#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_status_get_source
int mp_status_get_source(
    void *				status)
{
    int					rc;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_MP);

    rc = ((MPI_Status *) status)->MPI_SOURCE;

  /* fn_exit: */
    DEBUG_PRINTF(DEBUG_MODULE_MP, DEBUG_INFO_RC,
		 ("source=%d\n", rc));
    DEBUG_FN_EXIT(DEBUG_MODULE_MP);
    return rc;
}


#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_status_get_tag
int mp_status_get_tag(
    void *				status)
{
    int					rc;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_MP);

    rc = ((MPI_Status *) status)->MPI_TAG;

  /* fn_exit: */
    DEBUG_PRINTF(DEBUG_MODULE_MP, DEBUG_INFO_RC,
		 ("tag=%d\n", rc));
    DEBUG_FN_EXIT(DEBUG_MODULE_MP);
    return rc;
}


#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_status_get_error
int mp_status_get_error(
    void *				status)
{
    int					rc;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_MP);

    rc = mpi_error_to_vmpi_error(((MPI_Status *) status)->MPI_ERROR);

  /* fn_exit: */
    DEBUG_PRINTF(DEBUG_MODULE_MP, DEBUG_INFO_RC,
		 ("error=%d\n", rc));
    DEBUG_FN_EXIT(DEBUG_MODULE_MP);
    return rc;
}


int mp_comm_get_size()
{
    return sizeof(MPI_Comm);
}

#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_comm_split
int mp_comm_split(
    void *				oldcomm,
    int					color,
    int					key,
    void *				newcomm)
{
    MPI_Comm				comm_world = MPI_COMM_WORLD;
    MPI_Comm				newcomm_tmp = 0;
    MPI_Comm *				oldcomm_ptr;
    MPI_Comm *				newcomm_ptr;
    int					rc;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_MP | DEBUG_MODULE_COMM);

    DEBUG_PRINTF(DEBUG_MODULE_MP | DEBUG_MODULE_COMM, DEBUG_INFO_ARGS,
		 ("oldcomm=0x%08lx, color=%d, key=%d, newcomm=0x%08lx\n",
		  (unsigned long) oldcomm, color, key,
		  (unsigned long) newcomm));

    if (color == VMPI_UNDEFINED)
    {
	color = MPI_UNDEFINED;
    }

    oldcomm_ptr = oldcomm ? (MPI_Comm *) oldcomm : &comm_world;
    newcomm_ptr = newcomm ? (MPI_Comm *) newcomm : &newcomm_tmp;
    
    rc = MPI_Comm_split(*oldcomm_ptr, color, key, newcomm_ptr);

    if (rc != MPI_SUCCESS)
    {
	DEBUG_PRINTF(DEBUG_MODULE_MP | DEBUG_MODULE_COMM, DEBUG_INFO_MISC,
		     ("vendor MPI_Comm_split() failed"));
    }
    
  /* fn_exit: */
    rc = mpi_error_to_vmpi_error(rc);
    DEBUG_PRINTF(DEBUG_MODULE_MP | DEBUG_MODULE_COMM, DEBUG_INFO_RC,
		 ("rc=%d\n", rc));
    DEBUG_FN_EXIT(DEBUG_MODULE_MP | DEBUG_MODULE_COMM);
    return rc;
}


#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_comm_dup
int mp_comm_dup(
    void *				oldcomm,
    void *				newcomm)
{
    MPI_Comm				comm_world = MPI_COMM_WORLD;
    MPI_Comm *				oldcomm_ptr;
    int					rc;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_MP | DEBUG_MODULE_COMM);

    DEBUG_PRINTF(DEBUG_MODULE_MP | DEBUG_MODULE_COMM, DEBUG_INFO_ARGS,
		 ("oldcomm=0x%08lx, newcomm=0x%08lx\n",
		  (unsigned long) oldcomm,
		  (unsigned long) newcomm));

    oldcomm_ptr = oldcomm ? (MPI_Comm *) oldcomm : &comm_world;
    
    rc = MPI_Comm_dup(*(MPI_Comm *) oldcomm_ptr, (MPI_Comm *) newcomm);

    if (rc != MPI_SUCCESS)
    {
	DEBUG_PRINTF(DEBUG_MODULE_MP | DEBUG_MODULE_COMM, DEBUG_INFO_MISC,
		     ("vendor MPI_Comm_dup() failed"));
    }
    
  /* fn_exit: */
    rc = mpi_error_to_vmpi_error(rc);
    DEBUG_PRINTF(DEBUG_MODULE_MP | DEBUG_MODULE_COMM, DEBUG_INFO_RC,
		 ("rc=%d\n", rc));
    DEBUG_FN_EXIT(DEBUG_MODULE_MP | DEBUG_MODULE_COMM);
    return rc;
}


#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_intercomm_create
int mp_intercomm_create(
    void *				local_comm,
    int					local_leader,
    void *				peer_comm,
    int					remote_leader,
    int					tag,
    void *				newintercomm)
{
    int					rc;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_MP | DEBUG_MODULE_COMM);

    DEBUG_PRINTF(DEBUG_MODULE_MP | DEBUG_MODULE_COMM, DEBUG_INFO_ARGS,
		 ("local_comm=0x%08lx, local_leader=%d, "
		  "peer_comm=0x%08lx, remote_leader=%d, "
		  "tag=%d, newintercomm=0x%08lx\n",
		  (unsigned long) local_comm,
		  local_leader,
		  (unsigned long) peer_comm,
		  remote_leader,
		  tag,
		  (unsigned long) newintercomm));

    rc = MPI_Intercomm_create(*(MPI_Comm *) local_comm,
			      local_leader,
			      *(MPI_Comm *) peer_comm,
			      remote_leader,
			      tag,
			      (MPI_Comm *) newintercomm);

    if (rc != MPI_SUCCESS)
    {
	DEBUG_PRINTF(DEBUG_MODULE_MP | DEBUG_MODULE_COMM, DEBUG_INFO_MISC,
		     ("vendor MPI_Intercomm_create() failed"));
    }
    
  /* fn_exit: */
    rc = mpi_error_to_vmpi_error(rc);
    DEBUG_PRINTF(DEBUG_MODULE_MP | DEBUG_MODULE_COMM, DEBUG_INFO_RC,
		 ("rc=%d\n", rc));
    DEBUG_FN_EXIT(DEBUG_MODULE_MP | DEBUG_MODULE_COMM);
    return rc;
}


#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_intercomm_merge
int mp_intercomm_merge(
    void *				intercomm,
    int					high,
    void *				intracomm)
{
    int					rc;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_MP | DEBUG_MODULE_COMM);

    DEBUG_PRINTF(DEBUG_MODULE_MP | DEBUG_MODULE_COMM, DEBUG_INFO_ARGS,
		 ("intercomm=0x%08lx, high=%d, intracomm=0x%08lx\n",
		  (unsigned long) intercomm,
		  high,
		  (unsigned long) intracomm));

    rc = MPI_Intercomm_merge(*(MPI_Comm *) intercomm,
			     high,
			     (MPI_Comm *) intracomm);

    if (rc != MPI_SUCCESS)
    {
	DEBUG_PRINTF(DEBUG_MODULE_MP | DEBUG_MODULE_COMM, DEBUG_INFO_MISC,
		     ("vendor MPI_Intercomm_merge() failed"));
    }
    
  /* fn_exit: */
    rc = mpi_error_to_vmpi_error(rc);
    DEBUG_PRINTF(DEBUG_MODULE_MP | DEBUG_MODULE_COMM, DEBUG_INFO_RC,
		 ("rc=%d\n", rc));
    DEBUG_FN_EXIT(DEBUG_MODULE_MP | DEBUG_MODULE_COMM);
    return rc;
}


#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_comm_free
int mp_comm_free(
    void *				comm)
{
    int					rc;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_MP | DEBUG_MODULE_COMM);

    DEBUG_PRINTF(DEBUG_MODULE_MP | DEBUG_MODULE_COMM, DEBUG_INFO_ARGS,
		 ("comm=0x%08lx\n",
		  (unsigned long) comm));

    rc = MPI_Comm_free((MPI_Comm *) comm);
    
    if (rc != MPI_SUCCESS)
    {
	DEBUG_PRINTF(DEBUG_MODULE_MP | DEBUG_MODULE_COMM, DEBUG_INFO_MISC,
		     ("vendor MPI_Comm_free() failed"));
    }
    
  /* fn_exit: */
    rc = mpi_error_to_vmpi_error(rc);
    DEBUG_PRINTF(DEBUG_MODULE_MP | DEBUG_MODULE_COMM, DEBUG_INFO_RC,
		 ("rc=%d\n", rc));
    DEBUG_FN_EXIT(DEBUG_MODULE_MP | DEBUG_MODULE_COMM);
    return rc;
}    


#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_request_free
int mp_request_free(
    void *				request)
{
    int					rc;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_MP);

    rc = MPI_Request_free((MPI_Request *) request);

    debug_check_mpi_result("MPI_Request_free()", rc);

  /* fn_exit: */
    rc = mpi_error_to_vmpi_error(rc);
    DEBUG_FN_EXIT(DEBUG_MODULE_MP);
    return rc;
} /* end mp_request_free() */


int mp_type_commit(void *type)
{
    return mpi_error_to_vmpi_error(MPI_Type_commit((MPI_Datatype *) type));
}


int mp_type_free(void *type)
{
    return mpi_error_to_vmpi_error(MPI_Type_free((MPI_Datatype *) type));
}


/*
 * Define Fortran types for Cray T3E
 */
#if defined(MPICH_ARCH_cray_t3e)
#   define MPI_CHARACTER _MPIF_CHARACTER
#   define MPI_INTEGER _MPIF_INTEGER
#   define MPI_REAL _MPIF_REAL
#   define MPI_DOUBLE_PRECISION _MPIF_DOUBLE_PRECISION
#   define MPI_COMPLEX _MPIF_COMPLEX
#   define MPI_DOUBLE_COMPLEX _MPIF_DOUBLE_COMPLEX
#   define MPI_LOGICAL _MPIF_LOGICAL
#   define MPI_2REAL _MPIF_2REAL
#   define MPI_2DOUBLE_PRECISION _MPIF_2DOUBLE_PRECISION
#   define MPI_2INTEGER _MPIF_2INTEGER
#endif


/********************************/
/* Derived Datatype Contructors */
/********************************/

#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_type_permanent_setup
int mp_type_permanent_setup(
    void *				mpi_type,
    int					vmpi_type)
{
    MPI_Datatype			rc = 0;

    DEBUG_FN_ENTRY(DEBUG_MODULE_MP | DEBUG_MODULE_TYPES);
    DEBUG_PRINTF(DEBUG_MODULE_MP | DEBUG_MODULE_TYPES, DEBUG_INFO_ARGS,
		 ("vmpi_type=%d\n", vmpi_type));
    /*
     * In some cases, the vendor does not have pre-defined types that match
     * the pre-defined types in MPICH.  In those cases, we have to create
     * new types that match what MPICH already does in src/env/initdte.c.
     */
    switch(vmpi_type)
    {
      case VMPI_CHAR:			rc = MPI_CHAR;			break;
      case VMPI_CHARACTER:		rc = MPI_CHARACTER;		break;
      case VMPI_UNSIGNED_CHAR:		rc = MPI_UNSIGNED_CHAR;		break;
      case VMPI_BYTE:			rc = MPI_BYTE;			break;
      case VMPI_SHORT:			rc = MPI_SHORT;			break;
      case VMPI_UNSIGNED_SHORT:		rc = MPI_UNSIGNED_SHORT;	break;
      case VMPI_INT:			rc = MPI_INT;			break;
      case VMPI_UNSIGNED:		rc = MPI_UNSIGNED;		break;
      case VMPI_LONG:			rc = MPI_LONG;			break;
      case VMPI_UNSIGNED_LONG:		rc = MPI_UNSIGNED_LONG;		break;
      case VMPI_FLOAT:			rc = MPI_FLOAT;			break;
      case VMPI_DOUBLE:			rc = MPI_DOUBLE;		break;
      case VMPI_LONG_DOUBLE:		rc = MPI_LONG_DOUBLE;		break;
      case VMPI_LONG_LONG:
      case VMPI_LONG_LONG_INT:
#	if (VENDOR_HAS_MPI_LONG_LONG_INT)
	{
	    rc = MPI_LONG_LONG_INT;
	}
#	elif (VENDOR_HAS_MPI_LONG_LONG)
	{
	    rc = MPI_LONG_LONG;
	}
#       else
        {
	    DEBUG_PRINTF(DEBUG_MODULE_MP | DEBUG_MODULE_TYPES,
			 DEBUG_INFO_WARNING,
			 ("Vendor MPI does not support MPI_LONG_LONG_INT "
			  "or MPI_LONG_LONG\n"));
	    rc = MPI_DATATYPE_NULL;
	}
#	endif
	break;
      case VMPI_PACKED:			rc = MPI_BYTE;			break;
      case VMPI_LB:			rc = MPI_LB;			break;
      case VMPI_UB:			rc = MPI_UB;			break;
      case VMPI_FLOAT_INT:		rc = MPI_FLOAT_INT;		break;
      case VMPI_DOUBLE_INT:		rc = MPI_DOUBLE_INT;		break;
      case VMPI_LONG_INT:		rc = MPI_LONG_INT;		break;
      case VMPI_SHORT_INT:		rc = MPI_SHORT_INT;		break;
      case VMPI_2INT:			rc = MPI_2INT;			break;
      case VMPI_LONG_DOUBLE_INT:	rc = MPI_LONG_DOUBLE_INT;	break;
      case VMPI_COMPLEX:		rc = MPI_COMPLEX;		break;
      case VMPI_DOUBLE_COMPLEX:		rc = MPI_DOUBLE_COMPLEX;	break;
      case VMPI_LOGICAL:		rc = MPI_LOGICAL;		break;
      case VMPI_REAL:			rc = MPI_REAL;			break;
      case VMPI_DOUBLE_PRECISION:	rc = MPI_DOUBLE_PRECISION;	break;
      case VMPI_INTEGER:		rc = MPI_INTEGER;		break;
      case VMPI_2INTEGER:		rc = MPI_2INTEGER;		break;
      case VMPI_2COMPLEX:
#	if (VENDOR_HAS_MPI_2COMPLEX)
	{
	    rc = MPI_2COMPLEX;
	}
#       else
        {
	    MPI_Type_contiguous(2, MPI_COMPLEX, &rc);
	    MPI_Type_commit(&rc);
	}
#	endif
	break;
      case VMPI_2DOUBLE_COMPLEX:
#	if (VENDOR_HAS_MPI_2DOUBLE_COMPLEX)
	{
	    rc = MPI_2DOUBLE_COMPLEX;
	}
#       else
        {
	    MPI_Type_contiguous(2, MPI_DOUBLE_COMPLEX, &rc);
	    MPI_Type_commit(&rc);
	}
#	endif
	break;
      case VMPI_2REAL:			rc = MPI_2REAL;			break;
      case VMPI_2DOUBLE_PRECISION:	rc = MPI_2DOUBLE_PRECISION;	break;

      default:
	 globus_libc_fprintf(stderr,
		      "ERROR: mp_type_permanent_setup(): "
		      "encountered unrecognizable type %d\n",
		       vmpi_type);
         abort();
         break;
     } /* end switch */

  /* fn_exit: */
    *((MPI_Datatype *) mpi_type) = rc;
    DEBUG_PRINTF(DEBUG_MODULE_TYPES, DEBUG_INFO_RC,
		 ("mpi_type=%d\n", rc));
    DEBUG_FN_EXIT(DEBUG_MODULE_TYPES);

    return mpi_error_to_vmpi_error(MPI_SUCCESS);
}
/* mp_type_permanent_setup() */


int mp_type_permanent_free(
    void *				mpi_type,
    int					vmpi_type)
{
    /*
     * This is where we have to free any types that were not pre-defined in
     * the vendor's MPI
     */
    
    return mpi_error_to_vmpi_error(MPI_SUCCESS);
}
/* mp_type_permanent_setup() */


int mp_type_contiguous(
    int					count,
    void *				old_type,
    void *				new_type)
{
    return mpi_error_to_vmpi_error(
	    MPI_Type_contiguous(count,
			   *((MPI_Datatype *) old_type), 
			   (MPI_Datatype *) new_type));
} /* end mp_type_contiguous() */


int mp_type_hvector(
    int					count, 
    int					blocklength, 
    MPI_Aint				stride, 
    void *				old_type, 
    void *				new_type)
{
    return mpi_error_to_vmpi_error(
	MPI_Type_hvector(count,
			 blocklength,
			 stride,
			 *((MPI_Datatype *) old_type), 
			 (MPI_Datatype *) new_type));
} /* end mp_type_vector() */

int mp_type_hindexed(
    int					count, 
    int *				blocklengths, 
    MPI_Aint *				displacements, 
    void *				old_type, 
    void *				new_type)
{
    return mpi_error_to_vmpi_error(
	    MPI_Type_hindexed(count,
			   blocklengths,
			   displacements,
			   *((MPI_Datatype *) old_type), 
			   (MPI_Datatype *) new_type));
} /* end mp_type_hindexed() */

int mp_type_struct(
    int					count, 
    int *				blocklengths, 
    MPI_Aint *				displacements, 
    void *				old_types, 
    void *				new_type)
{
    return mpi_error_to_vmpi_error(
	    MPI_Type_struct(count,
			   blocklengths,
			   displacements,
			   (MPI_Datatype *) old_types, 
			   (MPI_Datatype *) new_type));
} /* end mp_type_struct() */

/******************************************************/
/* START Special boostrap wrappers for vMPI functions */
/******************************************************/

/*
 * these are NOT general-purpose wrappers for vMPI functions.
 * these are special-purpose wrappers for vMPI functions that
 * are needed during MPID_Init.  they participate in the 
 * all-to-all distribution function.  
 *
 * we needed to write these special-purpose wrappers because
 * the call to MPID_Init (our function that calls all these functions)
 * appears _before_ the call to MPIR_Init_dtes (which creates/registers
 * all the datatypes) in MPIR_Init in mpich/src/env/initutil.c ...
 * this means that the native MPI datatypes we'd like to use (MPI_{INT,CHAR})
 * do not exist at our device level yet, so we accomodate them here.
 *
 * these functions were written assuming vMPI MPI_COMM_WORLD as
 * the communicator with root always 0.
 *
 */

/* mp_bootstrap_bcast can be called to bcast an MPI_INT or an MPI_CHAR */

#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_bootstrap_bcast
int mp_bootstrap_bcast(void *buff, int	count, int type)
{
    int	rc;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_MP);

    rc = MPI_Bcast(buff,
		    count,
		    (type == 0 ? MPI_INT : MPI_CHAR),
		    0, /* root */
		    MPI_COMM_WORLD);

    debug_check_mpi_result("MPI_bootstrap_Bcast()", rc);
    
  /* fn_exit: */
    rc = mpi_error_to_vmpi_error(rc);
    DEBUG_FN_EXIT(DEBUG_MODULE_MP);
    return rc;
}

/* mp_bootstrap_gather is called only to gather an MPI_INT */

#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_gatherint
int mp_bootstrap_gather(void *sbuff, int scnt, void *rbuff, int rcnt)
{
    int	rc;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_MP);

    rc = MPI_Gather(sbuff,
		    scnt,
		    MPI_INT,
		    rbuff,
		    rcnt,
		    MPI_INT,
		    0, /* root */
		    MPI_COMM_WORLD);

    debug_check_mpi_result("MPI_bootstrap_Gather()", rc);
    
  /* fn_exit: */
    rc = mpi_error_to_vmpi_error(rc);
    DEBUG_FN_EXIT(DEBUG_MODULE_MP);
    return rc;
}

/* mp_bootstrap_gatherv is called only to gatherv an MPI_CHAR */

#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mp_bootstrap_gatherv
int mp_bootstrap_gatherv(void *sbuff, 
			int scnt, 
			void *rbuff, 
			int *rcnts, 
			int *displs)
{
    int	rc;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_MP);

    rc = MPI_Gatherv(sbuff,
		    scnt,
		    MPI_CHAR,
		    rbuff,
		    rcnts,
		    displs,
		    MPI_CHAR,
		    0, /* root */
		    MPI_COMM_WORLD);

    debug_check_mpi_result("MPI_bootstrap_Gatherv()", rc);
    
  /* fn_exit: */
    rc = mpi_error_to_vmpi_error(rc);
    DEBUG_FN_EXIT(DEBUG_MODULE_MP);
    return rc;
}

/****************************************************/
/* END Special boostrap wrappers for vMPI functions */
/****************************************************/

/*******************/
/* Local Functions */
/*******************/

static int mpi_error_to_vmpi_error(
    int					error_code)
{
    int error_class;
    int rc;

    MPI_Error_class(error_code, &error_class);
    switch(error_class)
    {
      case MPI_SUCCESS:		rc=VMPI_SUCCESS;	break;
      case MPI_ERR_BUFFER:	rc=VMPI_ERR_BUFFER;	break;
      case MPI_ERR_COUNT:	rc=VMPI_ERR_COUNT;	break;
      case MPI_ERR_TYPE:	rc=VMPI_ERR_TYPE;	break;
      case MPI_ERR_TAG:	rc=VMPI_ERR_TAG;		break;
      case MPI_ERR_COMM:	rc=VMPI_ERR_COMM;	break;
      case MPI_ERR_RANK:	rc=VMPI_ERR_RANK;	break;
      case MPI_ERR_ROOT:	rc=VMPI_ERR_ROOT;	break;
      case MPI_ERR_GROUP:	rc=VMPI_ERR_GROUP;	break;
      case MPI_ERR_OP:		rc=VMPI_ERR_OP;		break;
      case MPI_ERR_TOPOLOGY:	rc=VMPI_ERR_TOPOLOGY;	break;
      case MPI_ERR_DIMS:	rc=VMPI_ERR_DIMS;	break;
      case MPI_ERR_ARG:	rc=VMPI_ERR_ARG;		break;
      case MPI_ERR_UNKNOWN:	rc=VMPI_ERR_UNKNOWN;	break;
      case MPI_ERR_TRUNCATE:	rc=VMPI_ERR_TRUNCATE;	break;
      case MPI_ERR_OTHER:	rc=VMPI_ERR_OTHER;	break;
      case MPI_ERR_INTERN:	rc=VMPI_ERR_INTERN;	break;
      case MPI_ERR_IN_STATUS:	rc=VMPI_ERR_IN_STATUS;	break;
      case MPI_ERR_PENDING:	rc=VMPI_ERR_PENDING;	break;
      case MPI_ERR_REQUEST:	rc=VMPI_ERR_REQUEST;	break;
#if 0
      case MPI_ERR_ACCESS:	rc=VMPI_ERR_ACCESS;	break;
      case MPI_ERR_AMODE:	rc=VMPI_ERR_AMODE;	break;
      case MPI_ERR_BAD_FILE:	rc=VMPI_ERR_BAD_FILE;	break;
      case MPI_ERR_CONVERSION:	rc=VMPI_ERR_CONVERSION;	break;
      case MPI_ERR_DUP_DATAREP:
	rc=VMPI_ERR_DUP_DATAREP;			break;
      case MPI_ERR_FILE_EXISTS:
	rc=VMPI_ERR_FILE_EXISTS;			break;
      case MPI_ERR_FILE_IN_USE:
	rc=VMPI_ERR_FILE_IN_USE;			break;
      case MPI_ERR_FILE:	rc=VMPI_ERR_FILE;	break;
      case MPI_ERR_INFO:	rc=VMPI_ERR_INFO;	break;
      case MPI_ERR_INFO_KEY:	rc=VMPI_ERR_INFO_KEY;	break;
      case MPI_ERR_INFO_VALUE:	rc=VMPI_ERR_INFO_VALUE;	break;
      case MPI_ERR_INFO_NOKEY:	rc=VMPI_ERR_INFO_NOKEY;	break;
      case MPI_ERR_IO:		rc=VMPI_ERR_IO;		break;
      case MPI_ERR_NAME:	rc=VMPI_ERR_NAME;	break;
	/* case MPI_ERR_EXHAUSTED:	rc=VMPI_ERR_EXHAUSTED;	break; */
      case MPI_ERR_NOT_SAME:	rc=VMPI_ERR_NOT_SAME;	break;
      case MPI_ERR_NO_SPACE:	rc=VMPI_ERR_NO_SPACE;	break;
      case MPI_ERR_NO_SUCH_FILE:
	rc=VMPI_ERR_NO_SUCH_FILE;			break;
      case MPI_ERR_PORT:	rc=VMPI_ERR_PORT;	break;
      case MPI_ERR_QUOTA:	rc=VMPI_ERR_QUOTA;	break;
      case MPI_ERR_READ_ONLY:	rc=VMPI_ERR_READ_ONLY;	break;
      case MPI_ERR_SERVICE:	rc=VMPI_ERR_SERVICE;	break;
      case MPI_ERR_SPAWN:	rc=VMPI_ERR_SPAWN;	break;
      case MPI_ERR_UNSUPPORTED_DATAREP:
	rc=VMPI_ERR_UNSUPPORTED_DATAREP;		break;
      case MPI_ERR_UNSUPPORTED_OPERATION:
	rc=VMPI_ERR_UNSUPPORTED_OPERATION;		break;
      case MPI_ERR_WIN:	rc=VMPI_ERR_WIN;		break;
      case MPI_ERR_LASTCODE:	rc=VMPI_ERR_LASTCODE;	break;
#endif
      default:
        globus_libc_fprintf(stderr, "ERROR: mpi_error_to_vmpi_error(): "
       "error code %d generated encountered unrecognizable error class %d\n", 
			   error_code, error_class);
        abort();
        break;
    } /* end switch */

    return rc;

} /* end mpi_error_to_vmpi_error() */

#endif /* VMPI */
