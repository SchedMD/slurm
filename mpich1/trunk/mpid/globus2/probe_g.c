#include "chconfig.h"
#include "globdev.h"

/********************/
/* Global Variables */
/********************/

#ifdef GLOBUS_CALLBACK_GLOBAL_SPACE
extern globus_callback_space_t MpichG2Space;
#endif

/*******************/
/* Local Functions */
/*******************/

static int get_elements_from_partial(int req_nelem,
				struct MPIR_DATATYPE *datatype,
				int dataformat,
				int *nbytes_remaining,
				int *elements,
				globus_bool_t *done);

/*
 * MPID_Probe 
 */
void MPID_Probe(struct MPIR_COMMUNICATOR *comm,
                int tag,
                int context_id,
                int src_lrank,
                int *error_code,
                MPI_Status *status)
{
    int found   = 0;
    *error_code = 0;

#   if defined(VMPI)
    {
	int proto = get_proto(comm, src_lrank);

	if (proto == mpi)
	{
	    void * vmpi_status;
	    int vmpi_src;
	    int vmpi_tag;

	    vmpi_status = STATUS_INFO_GET_VMPI_PTR(*status);
	    
	    if (src_lrank == MPI_ANY_SOURCE)
	    {
		vmpi_src = VMPI_ANY_SOURCE;
	    }
	    else
	    {
		vmpi_src = comm->lrank_to_vlrank[src_lrank];
	    }

	    vmpi_tag = (tag == MPI_ANY_TAG ? VMPI_ANY_TAG : tag);

	    *error_code = vmpi_error_to_mpich_error(
		mp_probe(vmpi_src, vmpi_tag, comm->vmpi_comm, vmpi_status));

	    status->MPI_SOURCE = comm->vlrank_to_lrank[
		mp_status_get_source(vmpi_status)];
	    status->MPI_TAG = mp_status_get_tag(vmpi_status);
	    status->MPI_ERROR = *error_code;
	    STATUS_INFO_SET_COUNT_VMPI(*status);

	    return;
	}
    }
#   endif /* defined(VMPI) */
    
    while (*error_code == 0 && !found)
    {
	MPID_Iprobe(comm,
		    tag,
		    context_id,
		    src_lrank,
		    &found,
		    error_code,
		    status);
    } /* endwhile */

} /* end MPID_Probe() */

/*
 * MPID_Iprobe
 */
void MPID_Iprobe(struct MPIR_COMMUNICATOR *comm,
                 int tag,
                 int context_id,
                 int src_lrank,
                 int *found,
                 int *error_code,
                 MPI_Status *status)
{
    MPIR_RHANDLE *unexpected = (MPIR_RHANDLE *) NULL;

    *found = GLOBUS_FALSE;

#   ifdef VMPI
    {
	int proto = get_proto(comm, src_lrank);

	if (proto == mpi || proto == unknown)
	{
	    void * vmpi_status;
	    int vmpi_src;
	    int vmpi_tag;

	    vmpi_status = STATUS_INFO_GET_VMPI_PTR(*status);
	    
	    if (src_lrank == MPI_ANY_SOURCE)
	    {
		vmpi_src = VMPI_ANY_SOURCE;
	    }
	    else
	    {
		vmpi_src = comm->lrank_to_vlrank[src_lrank];
	    }

	    vmpi_tag = (tag == MPI_ANY_TAG ? VMPI_ANY_TAG : tag);

	    *error_code = vmpi_error_to_mpich_error(
		mp_iprobe(vmpi_src,
			  vmpi_tag,
			  comm->vmpi_comm,
			  found,
			  vmpi_status));

	    if (*found)
	    {
		status->MPI_SOURCE = comm->vlrank_to_lrank[
		    mp_status_get_source(vmpi_status)];
		status->MPI_TAG = mp_status_get_tag(vmpi_status);
		status->MPI_ERROR = *error_code;
		STATUS_INFO_SET_COUNT_VMPI(*status);
	    }

	    if (*found || *error_code != MPI_SUCCESS)
	    {
		return;
	    }
	} /* endif */
    }
#   endif 

    /* try TCP */

    /*
     * need G2_POLL here so that if MPID_Iprobe called 
     * by MPID_probe() progress is guaranteed, that is, if
     * MPID_Probe is called before TCP data arrives the waiting 
     * proc (i.e, the one that called MPI_Probe) must be assured
     * that once the TCP data is sent that it (a) will be received
     * and (b) it will be detected (i.e., progress).
     */
    G2_POLL

    /* search 'unexpected' queue ... does NOT remove from queue */
    MPID_Search_unexpected_queue(src_lrank,
				 tag,
				 context_id,
				 GLOBUS_FALSE, /* do not remove from queue */
				 &unexpected);

    if (unexpected)
    {
	*found = GLOBUS_TRUE;

	status->MPI_SOURCE    = unexpected->s.MPI_SOURCE;
	status->MPI_TAG       = unexpected->s.MPI_TAG;
	status->MPI_ERROR     = unexpected->s.MPI_ERROR;
	/* 
	 * setting status->count and bits in status->private_count 
	 * to indicate that status->count should be interpreted 
	 * (e.g., by MPID_Get_{count,elements} as nbytes in data 
	 * origin format
	 */
	status->count         = unexpected->len;
	STATUS_INFO_SET_FORMAT(*status, unexpected->src_format);
	STATUS_INFO_SET_COUNT_REMOTE(*status);
    } /* endif */

} /* end MPID_Iprobe() */

/*
 * MPID_Get_count
 *
 * return into 'count' the number of COMPLETE 'datatype' in the buffer
 * described by 'status'
 *
 * there are a couple of potential erroneuous/weird scenarios:
 *      - 'datatype' is a non-empty (i.e., sizeof(datatype)>0) complex type 
 *        AND there is not enough data to completely fill all the datatypes 
 *        (last one only partially filled).
 *        in this case *count = MPI_UNDEFINED
 *      - sizeof(datatype) == 0, in this case the "correct" count cannot be
 *        determined ... *count could be set to anything from 0-infinity,
 *        the MPI standard does not discuss this case (at least i couldn't
 *        find anything on it) so we look at the number of bytes in the 
 *        data buffer,
 *        - if sizeof(databuff) == 0 then we take a guess and set *count = 0
 *          and hope that's what the user expected.
 *        - if sizeof(databuff) > 0 then things are REALLY messed up and we
 *          give up by setting *count = MPI_UNDEFINED.
 */
int MPID_Get_count(MPI_Status *status, 
                    MPI_Datatype datatype,
                    int *count)
{
    struct MPIR_DATATYPE *dtype_ptr = 
			(struct MPIR_DATATYPE *) MPIR_GET_DTYPE_PTR(datatype);

#   if defined(VMPI)
    if (STATUS_INFO_IS_COUNT_VMPI(*status))
    {
	MPID_Type_validate_vmpi(dtype_ptr);
	return vmpi_error_to_mpich_error(
		    mp_get_count(STATUS_INFO_GET_VMPI_PTR(*status),
				dtype_ptr->vmpi_type,
				count));
    }
    else
#   endif /* defined(VMPI) */
    if (status->count == 0)
    {
	/*
	 * this is more than just an optimization.  if the app calls
	 * MPI_{Recv,Irecv} from MPI_PROC_NULL, then the MPICH code
	 * simply sets status->count=0 and does NOT call our 
	 * MPID_{Recv,Irecv}, and therefore we don't get to set
	 * status->private_count to ISLOCAL or ISDATAORIGIN.
	 * without that setting, the rest of the code in this 
	 * function will fail.
	 */

	*count = 0;
    }
    else if (dtype_ptr->size <= 0)
    {
	/* 
	 * this is weird ... we're being asked to count how many 
	 * 0-byte data elements are in a non-empty buffer ... the 
	 * "correct" answer is anywhere from 0-inifinite ... (probably
	 * _countably_ infinite, if that helps ;-))
	 */

	 *count = MPI_UNDEFINED;
    }
    else 
    {
	int unit_size;

	if (STATUS_INFO_IS_COUNT_LOCAL(*status))
	{
	    /* status->count is the number of bytes in local format */
	    unit_size = dtype_ptr->size;
	}
	else if (STATUS_INFO_IS_COUNT_REMOTE(*status))
	{
	    /* status->count is the number of bytes in remote format */
	    unit_size = remote_size(1,
				    dtype_ptr,
				    STATUS_INFO_GET_FORMAT(*status));
	    if (unit_size <= 0)
	    {
		globus_libc_fprintf(stderr, 
		    "ERROR: MPID_Get_count: datatype %d local size %d, "
		    "remote size %d\n",
		    dtype_ptr->dte_type, dtype_ptr->size, unit_size);
		return MPI_ERR_INTERN;
	    } /* endif */
	} 
	else
	{
		globus_libc_fprintf(stderr, 
		    "ERROR: MPID_Get_count: could not interpret "
		    "status->private_count %d\n",
		    status->extra[0]);
		return MPI_ERR_INTERN;
	} /* endif */

	if (status->count % unit_size)
	    /* uh-oh! last element is only partially filled */
	    *count = MPI_UNDEFINED;
	else
	    *count = status->count / unit_size;
    } /* endif */

    return MPI_SUCCESS;

} /* end MPID_Get_count() */

/*
 * MPID_Get_elements
 *
 * return into 'elements' the number of basic datatypes that are in
 * the buffer described by status.  for complex 'datatype' this requires 
 * counting how many basic datatypes there are, which includes counting those
 * basic datatypes that appear in a potentially partially-filled last datatype.
 *
 * there is a potentially weird scenario:
 *      - sizeof(datatype) == 0, in this case the "correct" count cannot be
 *        determined ... *count could be set to anything from 0-infinity,
 *        the MPI standard does not discuss this case (at least i couldn't
 *        find anything on it) so we look at the number of bytes in the 
 *        data buffer,
 *        - if sizeof(databuff) == 0 then we take a guess and set 
 *          *elements = 0, rc = MPI_SUCCESS, and hope that's what 
 *          the user expected.
 *        - if sizeof(databuff) > 0 then things are REALLY messed up and we
 *          give up by simply returning rc = MPI_ERR_INTERN.
 */
int MPID_Get_elements(MPI_Status *status, 
                    MPI_Datatype  datatype,
                    int *elements)
{
    struct MPIR_DATATYPE *dtype_ptr = 
			(struct MPIR_DATATYPE *) MPIR_GET_DTYPE_PTR(datatype);

#   if defined(VMPI)
    if (STATUS_INFO_IS_COUNT_VMPI(*status))
    {
	MPID_Type_validate_vmpi(dtype_ptr);
	return vmpi_error_to_mpich_error(
		    mp_get_elements(STATUS_INFO_GET_VMPI_PTR(*status),
				    dtype_ptr->vmpi_type,
				    elements));
    }
    else
#   endif /* defined(VMPI) */
    if (status->count == 0)
    {
	/*
	 * this is more than just an optimization.  if the app calls
	 * MPI_{Recv,Irecv} from MPI_PROC_NULL, then the MPICH code
	 * simply sets status->count=0 and does NOT call our 
	 * MPID_{Recv,Irecv}, and therefore we don't get to set
	 * status->private_count to ISLOCAL or ISDATAORIGIN.
	 * without that setting, the rest of the code in this 
	 * function will fail.
	 */

	*elements = 0;
    }
    else if (dtype_ptr->size <= 0)
    {
	/* 
	 * this is weird ... we're being asked to count how many 
	 * 0-byte data elements are in a non-empty buffer ... the 
	 * "correct" answer is anywhere from 0-inifinite ... (probably
	 * _countably_ infinite, if that helps ;-))
	 */

	return MPI_ERR_INTERN;
    }
    else 
    {
	int unit_size;
	int format;
	int nbytes_remaining;

	if (STATUS_INFO_IS_COUNT_LOCAL(*status))
	{
	    /* status->count is the number of bytes in local format */
	    format = GLOBUS_DC_FORMAT_LOCAL;
	    unit_size = dtype_ptr->size;
	}
	else if (STATUS_INFO_IS_COUNT_REMOTE(*status))
	{
	    /* status->count is the number of bytes in remote format */
	    format = STATUS_INFO_GET_FORMAT(*status);
	    if ((unit_size = remote_size(1, dtype_ptr, format)) <= 0)
	    {
		globus_libc_fprintf(stderr,
		    "ERROR: MPID_Get_elements: datatype %d local size %d, "
		    "remote size %d\n",
		    dtype_ptr->dte_type, dtype_ptr->size, unit_size);
		return MPI_ERR_INTERN;
	    } /* endif */
	} 
	else
	{
		globus_libc_fprintf(stderr,
		    "ERROR: MPID_Get_elements: could not interpret "
		    "status->private_count %d\n",
		    status->extra[0]);
		return MPI_ERR_INTERN;
	} /* endif */

	/* count the basic datatypes in 'full' ones */
	*elements = (status->count / unit_size) * dtype_ptr->elements;

	if ((nbytes_remaining = status->count-(*elements * unit_size)) > 0)
	{
	    /* last element is only partially filled ... need */
	    /* to count the basic datatypes in that one too   */

	    globus_bool_t done = GLOBUS_FALSE;

	    if (get_elements_from_partial(1,
				    dtype_ptr,
				    format,
				    &nbytes_remaining,
				    elements,
				    &done))
		/* something bad happened */
		return MPI_ERR_INTERN;
	    else if (nbytes_remaining > 0)
	    {
		/*
		 * after counting all the basic element types we can, 
		 * decrementing nbytes_remaining along the way, there 
		 * are STILL residual bytes left over that could not be 
		 * accounted for based on the 'datatype' we were passed.  
		 * still going to return *elements and MPI_SUCCESS, but 
		 * printing warning message (stderr) here.   
		 */
		globus_libc_fprintf(stderr, 
		    "WARNING: MPID_Get_elements counted all the basic "
		    "datatypes it could based\n");
		globus_libc_fprintf(stderr, 
		    "         the specified datatype, but still had %d "
		    "residual bytes that\n",
		    nbytes_remaining);
		globus_libc_fprintf(stderr, 
		    "         could not be accounted for.\n");
	    } /* endif */
	} /* endif */
    } /* endif */

    return MPI_SUCCESS;

} /* end MPID_Get_elements() */

/*
 * get_proto
 *
 * returns -1 if error
 */
int get_proto(struct MPIR_COMMUNICATOR *comm, int src_lrank)
{
    int rc;

    if (src_lrank == MPI_ANY_SOURCE)
	rc = (comm->vmpi_only ? mpi : unknown);
    else
    {
	if (src_lrank >= 0 && src_lrank < comm->np)
	{
	    int src_grank = comm->lrank_to_grank[src_lrank];
	    struct channel_t *cp;

	    if (!(cp = get_channel(src_grank)))
	    {
		rc = -1;
		globus_libc_fprintf(stderr, 
		    "ERROR: get_proto: proc %d failed get_channel "
		    "for src_grank %d\n",
		    MPID_MyWorldRank, src_grank);
		print_channels();
	    }
	    else if (!(cp->selected_proto))
	    {
		rc = -1;
		globus_libc_fprintf(stderr, 
		    "ERROR: get_proto: proc %d has NULL selected protocol "
		    "for src_grank %d\n",
		    MPID_MyWorldRank, src_grank);
		print_channels();
	    } 
	    else
		rc = (cp->selected_proto)->type;
	}
	else
	{
	    rc = -1;
	    globus_libc_fprintf(stderr,
		"ERROR: get_proto: src_lrank %d out of bounds for communicator "
		"with %d procs\n",
		src_lrank, 
		comm->np);
	} /* endif */
    } /* endif */

    return rc;

} /* end get_proto() */

/*******************/
/* Local Functions */
/*******************/

/*
 * get_elements_from_partial
 *
 * this is a recursive function.  on the initial call it is assumed 
 * that count=1, *done=GLOBUS_FALSE, *nbytes_remaining>0, and *elements 
 * already has meaningful data. 
 *
 * this function gets initially called by MPID_Get_elements when
 * we are asked to count the the number basic datatypes in a
 * complex datatype in a buffer in which there is not enough
 * data to completely fill the last datatype.  we are counting
 * the basic datatypes in the last (partially filled) datatype
 * and adding that count to *elements and decrementing *nbytes_remaining
 * along the way.
 *
 * NOTE: there is one more datatype found in datatype.h ... MPIR_FORT_INT
 *       it has been explained to me by bill that we do not have to
 *       support an explicit case for that type because it is a
 *       synonym for one of the other types we already have a case
 *       statement for (which type it is a synonym for is architecture 
 *       dependent and determined during mpich configuration).
 *
 *
 * returns 0 upon successful completion
 */

static int get_elements_from_partial(int req_nelem,
				struct MPIR_DATATYPE *datatype,
				int format,
				int *nbytes_remaining,
				int *elements,
				globus_bool_t *done)
{
    int rc = 0;

    switch (datatype->dte_type)
    {
	case MPIR_CHAR:    case MPIR_UCHAR:      case MPIR_PACKED: 
	case MPIR_BYTE:    case MPIR_SHORT:       case MPIR_USHORT:
	case MPIR_LOGICAL: case MPIR_INT:         case MPIR_UINT:
	case MPIR_LONG:    case MPIR_LONGLONGINT: case MPIR_ULONG:   
	case MPIR_FLOAT:   case MPIR_DOUBLE:      case MPIR_COMPLEX: 
	case MPIR_DOUBLE_COMPLEX:
	    {
		/* basic datatypes */
		int unit_size;
		int inbuf_nelem;
		int nelem;

		if ((unit_size = remote_size(1, datatype, format)) <= 0)
		{
		    globus_libc_fprintf(stderr, 
			"ERROR: get_elements_from_partial: datatype %d "
			"format %d got invalid remote unit size %d\n",
			datatype->dte_type, format, unit_size);
		    return 1;
		} /* endif */

		inbuf_nelem = (*nbytes_remaining) / unit_size;
		if ((nelem = (req_nelem<inbuf_nelem ? req_nelem : inbuf_nelem))
		    != 0)
		{
		    (*nbytes_remaining) -= (nelem * unit_size);
		    (*elements)         += nelem;
		} /* endif */

		if (nelem < req_nelem)
		    *done = GLOBUS_TRUE;
	    }
	break;
	case MPIR_LONGDOUBLE: /* not supported */ break;
	case MPIR_UB:         /* MPIR_UB and MPIR_LB are 0-byte datatypes */
	case MPIR_LB:
	break;

	/*
	* rest are complex data types requiring special care
	* by decomposing down to their basic types
	*/
	case MPIR_CONTIG:
	    rc = get_elements_from_partial(req_nelem * datatype->count, 
					    datatype->old_type,
					    format,
					    nbytes_remaining,
					    elements,
					    done);
	break;
	case MPIR_VECTOR:
	case MPIR_HVECTOR:
	{
	    int i, j;

	    for (i = 0; !rc && !(*done) && i < req_nelem; i ++)
		for (j = 0; !rc && !(*done) && j < datatype->count; j ++)
		    rc = get_elements_from_partial(datatype->blocklen,
						    datatype->old_type,
						    format,
						    nbytes_remaining,
						    elements,
						    done);
						    
	}
	break;
	case MPIR_INDEXED:
	case MPIR_HINDEXED:
	{
	    int i, j;

	    for (i = 0; !rc && !(*done) && i < req_nelem; i ++)
		for (j = 0; !rc && !(*done) && j < datatype->count; j ++)
		    rc = get_elements_from_partial(datatype->blocklens[j],
						    datatype->old_types[j],
						    format,
						    nbytes_remaining,
						    elements,
						    done);
						    
	}
	break;
	case MPIR_STRUCT:
	{
	    int i, j;

	    for (i = 0; !rc && !(*done) && i < req_nelem; i ++)
		for (j = 0; !rc && !(*done) && j < datatype->count; j ++)
		    rc = get_elements_from_partial(datatype->blocklens[j],
						    datatype->old_types[j],
						    format,
						    nbytes_remaining,
						    elements,
						    done);
						    
	}
	break;
	default:
	    globus_libc_fprintf(stderr,
		"ERROR: get_elements_from_partial: encountered unrecognizable "
		"datatype %d\n", 
		datatype->dte_type);
	    return 1;
	break;
    } /* end switch() */

    return rc;

} /* end get_elements_from_partial() */
