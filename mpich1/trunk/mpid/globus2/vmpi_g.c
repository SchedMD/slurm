#include "chconfig.h"
#include <globdev.h>

#if defined(VMPI)

int vmpi_error_to_mpich_error(
    int					vmpi_error)
{
    int rc;

    switch(vmpi_error)
    {
      case VMPI_SUCCESS:	         rc=MPI_SUCCESS;		 break;
      case VMPI_ERR_BUFFER:	         rc=MPI_ERR_BUFFER;	         break;
      case VMPI_ERR_COUNT:	         rc=MPI_ERR_COUNT;	         break;
      case VMPI_ERR_TYPE:	         rc=MPI_ERR_TYPE;	         break;
      case VMPI_ERR_TAG:	         rc=MPI_ERR_TAG;		 break;
      case VMPI_ERR_COMM:	         rc=MPI_ERR_COMM;	         break;
      case VMPI_ERR_RANK:	         rc=MPI_ERR_RANK;	         break;
      case VMPI_ERR_ROOT:	         rc=MPI_ERR_ROOT;	         break;
      case VMPI_ERR_GROUP:	         rc=MPI_ERR_GROUP;	         break;
      case VMPI_ERR_OP:		         rc=MPI_ERR_OP;		         break;
      case VMPI_ERR_TOPOLOGY:	         rc=MPI_ERR_TOPOLOGY;	         break;
      case VMPI_ERR_DIMS:	         rc=MPI_ERR_DIMS;	         break;
      case VMPI_ERR_ARG:	         rc=MPI_ERR_ARG;		 break;
      case VMPI_ERR_UNKNOWN:	         rc=MPI_ERR_UNKNOWN;	         break;
      case VMPI_ERR_TRUNCATE:	         rc=MPI_ERR_TRUNCATE;	         break;
      case VMPI_ERR_OTHER:	         rc=MPI_ERR_OTHER;	         break;
      case VMPI_ERR_INTERN:	         rc=MPI_ERR_INTERN;	         break;
      case VMPI_ERR_IN_STATUS:	         rc=MPI_ERR_IN_STATUS;	         break;
      case VMPI_ERR_PENDING:	         rc=MPI_ERR_PENDING;	         break;
      case VMPI_ERR_REQUEST:	         rc=MPI_ERR_REQUEST;	         break;
      case VMPI_ERR_ACCESS:	         rc=MPI_ERR_ACCESS;	         break;
      case VMPI_ERR_AMODE:	         rc=MPI_ERR_AMODE;	         break;
      case VMPI_ERR_BAD_FILE:	         rc=MPI_ERR_BAD_FILE;	         break;
      case VMPI_ERR_CONVERSION:	         rc=MPI_ERR_CONVERSION;	         break;
      case VMPI_ERR_DUP_DATAREP:          rc=MPI_ERR_DUP_DATAREP;        break;
      case VMPI_ERR_FILE_EXISTS:          rc=MPI_ERR_FILE_EXISTS;        break;
      case VMPI_ERR_FILE_IN_USE:          rc=MPI_ERR_FILE_IN_USE;        break;
      case VMPI_ERR_FILE:	         rc=MPI_ERR_FILE;	         break;
      case VMPI_ERR_INFO:	         rc=MPI_ERR_INFO;	         break;
      case VMPI_ERR_INFO_KEY:	         rc=MPI_ERR_INFO_KEY;	         break;
      case VMPI_ERR_INFO_VALUE:	         rc=MPI_ERR_INFO_VALUE;	         break;
      case VMPI_ERR_INFO_NOKEY:	         rc=MPI_ERR_INFO_NOKEY;	         break;
      case VMPI_ERR_IO:		         rc=MPI_ERR_IO;		         break;
      case VMPI_ERR_NAME:	         rc=MPI_ERR_NAME;	         break;
      /* case VMPI_ERR_EXHAUSTED:	         rc=MPI_ERR_EXHAUSTED;	       break; */
      case VMPI_ERR_NOT_SAME:	         rc=MPI_ERR_NOT_SAME;	         break;
      case VMPI_ERR_NO_SPACE:	         rc=MPI_ERR_NO_SPACE;	         break;
      case VMPI_ERR_NO_SUCH_FILE:          rc=MPI_ERR_NO_SUCH_FILE;	 break;
      case VMPI_ERR_PORT:	         rc=MPI_ERR_PORT;	         break;
      case VMPI_ERR_QUOTA:	         rc=MPI_ERR_QUOTA;	         break;
      case VMPI_ERR_READ_ONLY:	         rc=MPI_ERR_READ_ONLY;	         break;
      case VMPI_ERR_SERVICE:	         rc=MPI_ERR_SERVICE;	         break;
      case VMPI_ERR_SPAWN:	         rc=MPI_ERR_SPAWN;	         break;
      case VMPI_ERR_UNSUPPORTED_DATAREP: rc=MPI_ERR_UNSUPPORTED_DATAREP; break;
      case VMPI_ERR_UNSUPPORTED_OPERATION: 
	    rc=MPI_ERR_UNSUPPORTED_OPERATION;		                 break;
      case VMPI_ERR_WIN:	         rc=MPI_ERR_WIN;		 break;
      case VMPI_ERR_LASTCODE:	         rc=MPI_ERR_LASTCODE;	         break;
      default:
	{
	    char err[1024];
	    globus_libc_sprintf(err, "ERROR: vmpi_error_to_mpich_error(): "
			   "encountered unrecognizable type %d", 
			   vmpi_error);
	    MPID_Abort(NULL, 0, "MPICH-G2", err);
	}
        break;
    } /* end switch */

    return rc;

} /* end vmpi_error_to_mpich_error() */


/*
 * vmpi_grank_to_mpich_grank
 *
 * based on the assumption that MPICH MPI_COMM_WORLD rank
 * is contiguous for all procs on a single MPI machine
 * AND that the MPICH MPI_COMM_WORLD ranks increase
 * as do the vMPI MPI_COMM_WORLD ranks, i.e.,
 * let r = lowest MPICH MPI_COMM_WORLD rank in this subjob, then
 *      vMPI MPI_COMM_WORLD rank 0 -maps to-> r
 *      vMPI MPI_COMM_WORLD rank 1 -maps to-> r+1
 *      vMPI MPI_COMM_WORLD rank 2 -maps to-> r+2
 *                 .
 *                etc 
 *                 .
 * we can make this assumption because vMPI versions must be configured
 * with MPI-enabled versions of globus, and in such versions, the MPICH 
 * MPI_COMM_WORLD ranks were assigned during MPID_Init (globus_init) phase 
 * in which duroc subjob indicies are simply the vMPI MPI_COMM_WORLD ranks.
 */
int vmpi_grank_to_mpich_grank(
    int					vmpi_grank)
{

    struct channel_t *			channel;
    struct mpi_miproto_t *		mpi_miproto;

    if (!(channel = get_channel(MPID_MyWorldRank)))
    {
	globus_libc_fprintf(stderr, 
	    "ERROR: vmpi_grank_to_mpich_grank: failed get_channel channel to "
	    "myself (%d)", 
	    MPID_MyWorldRank);
	print_channels();
	MPID_Abort(NULL, 0, "MPICH-G2", "");
	return -1;
    }
    else if (!(channel->selected_proto))
    {
	globus_libc_fprintf(stderr, 
	    "ERROR: vmpi_grank_to_mpich_grank: discovered channel to "
	    "myself (%d) does has NULL selected_proto", 
	    MPID_MyWorldRank);
	print_channels();
	MPID_Abort(NULL, 0, "MPICH-G2", "");
	return -1;
    }
    else if (channel->selected_proto->type == mpi)
    {
	mpi_miproto = (struct mpi_miproto_t *) (channel->selected_proto->info);
    }
    else
    {
	/* at this point we know that the selected proto to myself */
	/* NOT MPI.  in a world in which TCP and MPI are the only  */
	/* protos, this would be an error.  however, later we may  */
	/* add other protos which are better than MPI (e.g., shm)  */
	/* in which case this would NOT be an error condition.     */

	struct miproto_t *		miproto;

	miproto = channel->proto_list;
	while(miproto->type != mpi)
	{
	    miproto = miproto->next;
	    if (miproto == NULL)
	    {
		MPID_Abort(NULL, 0, "MPICH-G2", 
		    "vmpi_grank_to_mpich_grank(): miproto == NULL");
	    } /* endif */
	}

	mpi_miproto = (struct mpi_miproto_t *) (miproto->info);
    } /* endif */

    return MPID_MyWorldRank + vmpi_grank - mpi_miproto->rank;

} /* end vmpi_grank_to_mpich_grank() */

#endif /* defined(VMPI) */
