#include "chconfig.h"
#include "globdev.h"
#include "reqalloc.h"
#include "queue.h" /* for MPID_Dequeue() */

/********************/
/* Global Variables */
/********************/
#if defined(VMPI)
struct mpi_posted_queue   MpiPostedQueue;
#endif

volatile int		  TcpOutstandingRecvReqs = 0;
extern volatile int	  TcpOutstandingSendReqs;
extern globus_size_t      Headerlen;

/*******************/
/* Local Functions */
/*******************/

static int extract_partial_from_buff(globus_byte_t **src, 
					globus_byte_t *dest,
					int count,
					struct MPIR_DATATYPE *datatype,
					int format,
					int *remaining_nbytes,
					globus_bool_t *done,
					int *nbytes_rcvd);
/*
 * MPID_RecvDatatype
 */
#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME MPID_RecvDatatype
void MPID_RecvDatatype(struct MPIR_COMMUNICATOR *comm,
                       void *buf,
                       int count,
                       struct MPIR_DATATYPE *datatype,
                       int src_lrank,
                       int tag,
                       int context_id,
                       MPI_Status *status,
                       int *error_code)
{
    MPI_Request request;
    MPIR_RHANDLE *rhandle;

    DEBUG_FN_ENTRY(DEBUG_MODULE_RECV);
    
    /* Make sure the receive is valid */
    if (buf == NULL && count > 0 && datatype->is_contig)
    {
	status->MPI_ERROR = *error_code = MPI_ERR_BUFFER;
	goto fn_exit;
    } /* endif */

#   if defined(VMPI)
    {
	/*
	 * If we know that we will be receiving the message via VMPI AND there
	 * are no other unsatisfied recvs, then we can simply do a mp_recv.
	 */
	if (get_proto(comm, src_lrank) == mpi)
	{
	    globus_bool_t		tcp_outstanding_reqs;
	
	    tcp_outstanding_reqs = 
		((TcpOutstandingSendReqs > 0)
		 ? GLOBUS_TRUE : GLOBUS_FALSE) ||
		((TcpOutstandingRecvReqs > 0)
		 ? GLOBUS_TRUE : GLOBUS_FALSE);

	    if (MpiPostedQueue.head == NULL && !tcp_outstanding_reqs)
	    {
		/* 
		 * NOTE: under the assumption that vendor's implement 'packing'
		 *       by simply copying the data into the buffer, we 
		 *       simply strip our single 'format' byte we inserted
		 *       during _our_ packing process from the front
		 *       of the buffer when sending over vMPI and then
                 *       insert here again if receive type is packed.  
		 *       this allows the receiver to receive the data as
		 *       either packed or the basic datatype.
		 *
		 *       also, the user should have called MPID_Pack_size
		 *       to get the value for 'count'.  we subtract 
                 *       sizeof(unsigned char) from the buffer size to 
                 *       account for the format byte we stripped on the
		 *       sending side.
		 */

		int				req_rank;
		int				req_tag;
		int				err;
		int				adj;
	    
		req_rank = (src_lrank == MPI_ANY_SOURCE 
			    ? VMPI_ANY_SOURCE 
			    : comm->lrank_to_vlrank[src_lrank]);
		req_tag = (tag == MPI_ANY_TAG ? VMPI_ANY_TAG : tag);
	    
		if (datatype->dte_type == MPIR_PACKED)
		{
		    *((unsigned char *) buf) = GLOBUS_DC_FORMAT_LOCAL;
		    adj = sizeof(unsigned char);
		}
		else
		{
		    adj = 0;
		} /* endif */

		err = vmpi_error_to_mpich_error(
		    mp_recv((void *) (((char *) buf) + adj), 
			    count-adj, 
			    datatype->vmpi_type,
			    req_rank, 
			    req_tag,
			    comm->vmpi_comm,
			    STATUS_INFO_GET_VMPI_PTR(*status)));

		status->MPI_SOURCE = comm->vlrank_to_lrank[
		    mp_status_get_source(
			STATUS_INFO_GET_VMPI_PTR(*status))];
		status->MPI_TAG = mp_status_get_tag(
		    STATUS_INFO_GET_VMPI_PTR(*status));
		STATUS_INFO_SET_COUNT_VMPI(*status);
		status->MPI_ERROR = err;
		
		goto fn_exit;
	    }
	}
    }
#   endif
    
    /* allocate req to pass to MPID_IrecvDatatype ... */
    /* code copied from mpich/src/pt2pt/isend.c       */

    /* NICK: seems expensive to init/destroy each time ... perhaps */
    /*       we manage do our own mutex alloc/free pool            */

    MPID_RecvAlloc(rhandle);
    if (!rhandle)
    {
	DEBUG_PRINTF(DEBUG_MODULE_RECV, DEBUG_INFO_FAILURE,
		     ("ERROR - could not malloc shandle\n"));
	/* MPI_ERR_NO_MEM is for MPI_Alloc_mem only */
	status->MPI_ERROR = *error_code = MPI_ERR_EXHAUSTED;
	goto fn_exit;
    } /* endif */

    MPID_Request_init(rhandle, MPIR_RECV);
    request = (MPI_Request) rhandle;

    MPID_IrecvDatatype(comm,
		    buf,
		    count,
		    datatype,
		    src_lrank,
		    tag,
		    context_id,
		    request,
		    error_code);
    if (*error_code == 0) /* everything still ok */
    {
	MPID_RecvComplete(request, status, error_code);
    }
    else
    {
	status->MPI_ERROR = *error_code;
    } /* endif */
    
    MPID_RecvFree(rhandle);

  fn_exit:
    DEBUG_FN_EXIT(DEBUG_MODULE_RECV);
} /* end MPID_RecvDatatype() */

/*
 * MPID_IrecvDatatype
 */
#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME MPID_IrecvDatatype
void MPID_IrecvDatatype(struct MPIR_COMMUNICATOR *comm,
                        void *buf,
                        int count,
                        struct MPIR_DATATYPE *datatype,
                        int src_lrank,
                        int tag,
                        int context_id,
                        MPI_Request request,
                        int *error_code)
{

    MPIR_RHANDLE *posted = (MPIR_RHANDLE *) request;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_RECV);
/* globus_libc_fprintf(stderr, "NICK: %d enter MPID_IrecvDatatype: tag %d context %d src_lrank %d\n", MPID_MyWorldRank, tag, context_id, src_lrank); */
    
    /* Make sure the receive is valid */
    if (buf == NULL && count > 0 && datatype->is_contig)
    {
	posted->s.MPI_ERROR = *error_code = MPI_ERR_BUFFER;
	goto fn_exit;
    } /* endif */

    posted->buf         = buf;
    posted->req_count   = count;
    posted->comm        = (struct MPIR_COMMUNICATOR *) NULL;
    posted->datatype    = MPIR_Type_dup(datatype);
    posted->is_complete = GLOBUS_FALSE;
    if ((posted->req_src_proto = get_proto(comm, src_lrank)) == -1)
    {
	globus_libc_fprintf(stderr,
		"ERROR: MPID_IrecvDatatype: could not determine protocol\n");
	MPIR_Type_free(&(posted->datatype));
	posted->s.MPI_ERROR = *error_code = MPI_ERR_INTERN;
	posted->is_complete = GLOBUS_TRUE;
	goto fn_exit;
    } /* endif */

#   if defined(VMPI)
    {
	if (posted->req_src_proto == mpi 
	    || (posted->req_src_proto == unknown && !(posted->is_complete)))
	{
	    /* try MPI */
	    
	    MPIR_REF_INCR(comm);
	    posted->comm = comm;

	    posted->req_rank = (src_lrank == MPI_ANY_SOURCE 
				? VMPI_ANY_SOURCE 
				: comm->lrank_to_vlrank[src_lrank]);
	    posted->req_tag = (tag == MPI_ANY_TAG ? VMPI_ANY_TAG : tag);
	    posted->req_context_id = context_id;
	    posted->my_mp = (struct mpircvreq *) NULL;

	    mpi_recv_or_post(posted, error_code);
	} /* endif (proto == mpi) */
    } /* endif */
#   endif

    if (posted->req_src_proto == tcp 
	|| (posted->req_src_proto == unknown && !(posted->is_complete)))
    {
	/* try TCP */
	MPIR_RHANDLE *unexpected;

	/* search 'unexpected' queue if not there, place into 'posted' queue */
	MPID_Search_unexpected_queue_and_post(src_lrank,
						tag,
						context_id,
						posted,
						&unexpected);
/* globus_libc_fprintf(stderr, "NICK: %d MPID_IrecvDatatype: tag %d context %d src_lrank %d: found unexpected = %c\n", MPID_MyWorldRank, tag, context_id, src_lrank, (unexpected ? 'T' : 'F')); */

	if (unexpected)
	{
	    int tmp;
	    int rc;

#           if defined(VMPI)
	    {
		if (posted->req_src_proto == unknown)
		{
		    /* need to remove from MpiPostedQueue */
		    if (posted->my_mp)
		    {
			remove_and_free_mpircvreq(posted->my_mp);
			posted->my_mp = (struct mpircvreq *) NULL;
		    }
		    else
		    {
			/* 
			 * NICK: in single-threaded case i'm pretty sure 
			 *       that this is a fatal error, but for now 
			 *       simply printing warning and continuing.
			 */
			globus_libc_fprintf(stderr, 
			    "WARNING: MPID_IrecvDatatype: detected incoming "
			    "message from unknown recv source over TCP but "
			    "did NOT find request in MPI queue\n");
		    } /* endif */
		} /* endif */
	    } /* endif */
#           endif

	    tmp = MPI_SUCCESS;
	    if (unexpected->needs_ack)
	    {
		if (!send_ack_over_tcp(unexpected->partner, 
				    (void *) (unexpected->liba), 
				    unexpected->libasize))
		{
		    /* posted->s.MPI_ERROR = *error_code = MPI_ERR_INTERN; */
		    tmp = MPI_ERR_INTERN;
		} /* endif */
	    } /* endif */

	    {
		unsigned char *		buf;
		int			len;
		int			format;
		
		buf = unexpected->buf;
		len = unexpected->len;
		format = unexpected->src_format;
		
		if (unexpected->packed_flag &&
		    posted->datatype->dte_type != MPIR_PACKED)
		{
		    format = buf[0];
		    buf++;
		    len--;
		}
		else if (posted->datatype->dte_type == MPIR_PACKED &&
			 !unexpected->packed_flag)
		{
		    g_malloc(buf, globus_byte_t *, len + 1);
		    buf[0] = format;
		    memcpy(buf + 1, unexpected->buf, len);
		}
		
		rc = extract_data_into_req(posted,
					   buf, 
					   len,
					   format,
					   unexpected->s.MPI_SOURCE,
					   unexpected->s.MPI_TAG);

		if (posted->datatype->dte_type == MPIR_PACKED &&
		    !unexpected->packed_flag)
		{
		    g_free(buf);
		} /* endif */
	    }
	    
	    if (rc)
		posted->s.MPI_ERROR = *error_code = MPI_ERR_INTERN;
	    else
	    {
		if (tmp == MPI_SUCCESS)
		    *error_code = posted->s.MPI_ERROR;
		else 
		    posted->s.MPI_ERROR = *error_code = tmp;
	    } /* endif */
#           if defined(VMPI)
	    {
		if (posted->req_src_proto == unknown)
		{
		    MPI_Comm c = posted->comm->self;
		    MPI_Comm_free(&c);
		} /* endif */
	    } /* endif */
#           endif
	    MPIR_Type_free(&(posted->datatype));
	    posted->is_complete = GLOBUS_TRUE;

	    /* cleanup unexpected */
	    g_free(unexpected->buf);
	    MPID_RecvFree(unexpected);
	} 
	else
	{
	    TcpOutstandingRecvReqs ++;
	} /* endif */
    } /* endif */

  fn_exit:
    DEBUG_FN_EXIT(DEBUG_MODULE_RECV);

} /* end MPID_IrecvDatatype() */

/*
 * MPID_RecvComplete
 */
#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME MPID_RecvComplete
void MPID_RecvComplete(MPI_Request request,
                       MPI_Status *status,
                       int *error_code)
{
    MPIR_RHANDLE *rhandle = (MPIR_RHANDLE *) request;
    globus_bool_t done = GLOBUS_FALSE;

    DEBUG_FN_ENTRY(DEBUG_MODULE_RECV);
    

/* globus_libc_fprintf(stderr, "NICK: %d enter MPID_RecvComplete: rhandle->is_complete %c TcpOutstandingRecvReqs %d TcpOutstandingSendReqs %d\n", MPID_MyWorldRank, (rhandle->is_complete ? 'T' : 'F'), TcpOutstandingRecvReqs, TcpOutstandingSendReqs); */
    *error_code = 0;
    while (*error_code == 0 && !done)
    {
	MPID_RecvIcomplete(request, status, error_code);
	done = rhandle->is_complete;
    } /* endwhile */

  /* fn_exit: */
    DEBUG_FN_EXIT(DEBUG_MODULE_RECV);

} /* end MPID_RecvComplete() */

/*
 * MPID_RecvIcomplete
 *
 * returns request->is_complete
 */
#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME MPID_RecvIcomplete
int MPID_RecvIcomplete(MPI_Request request,
                       MPI_Status *status, /* possibly NULL */
                       int *error_code)
{
    MPIR_RHANDLE *rhandle = (MPIR_RHANDLE *) request;
    int rc;

    DEBUG_FN_ENTRY(DEBUG_MODULE_RECV);
    
    rc = rhandle->is_complete;

    /* give all protos that are waiting for something a nudge */
    if (!rc)
	MPID_DeviceCheck(MPID_NOTBLOCKING);

    /* all protos tried ... tabulate results */
    if ((rc = rhandle->is_complete) == GLOBUS_TRUE)
    {
	if (status)
	{
	    *status = rhandle->s;
	}
	*error_code = rhandle->s.MPI_ERROR;
    } /* endif */

  /* fn_exit: */
    DEBUG_FN_EXIT(DEBUG_MODULE_RECV);

    return rc;

} /* end MPID_RecvIcomplete() */

/*
 * MPID_RecvCancel
 *
 * most of this code copied from ch2/adi2cancel.c
 */
#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME MPID_RecvCancel
void MPID_RecvCancel(MPI_Request request, int *error_code )
{
    MPIR_RHANDLE *rhandle = (MPIR_RHANDLE *) request;

    DEBUG_FN_ENTRY(DEBUG_MODULE_RECV);

    if (!(rhandle->is_complete))
    {
	rhandle->is_complete = GLOBUS_TRUE;
	rhandle->s.MPI_TAG   = MPIR_MSG_CANCELLED;

#           if defined(VMPI)
	{
	    /* attempt to remove from MpiPostedQueue */
	    remove_and_free_mpircvreq(rhandle->my_mp);
	    rhandle->my_mp = (struct mpircvreq *) NULL;
	}
#           endif

	/* attempt to remove from 'posted' queue */
	MPID_Dequeue(&MPID_recvs.posted, rhandle);

    } /* endif */

    if (rhandle->handle_type == MPIR_PERSISTENT_RECV)
	((MPIR_PRHANDLE *) request)->active = 0;

    *error_code = 0;

  /* fn_exit: */
    DEBUG_FN_EXIT(DEBUG_MODULE_RECV);

} /* end MPID_RecvCancel() */

/*
 * remote_size
 *
 * NOTE: there is one more datatype found in datatype.h ... MPIR_FORT_INT
 *       it has been explained to me by bill that we do not have to
 *       support an explicit case for that type because it is a
 *       synonym for one of the other types we already have a case
 *       statement for (which type it is a synonym for is architecture 
 *       dependent and determined during mpich configuration).
 *
 */
int remote_size(int count, struct MPIR_DATATYPE *datatype, int format)
{
    int rc;

    switch (datatype->dte_type)
    {
      case MPIR_CHAR:   rc=globus_dc_sizeof_remote_char(count,format);    break;
      case MPIR_UCHAR:  rc=globus_dc_sizeof_remote_u_char(count,format);  break;
      case MPIR_PACKED: rc = count; /* this will be a memcpy */           break;
      case MPIR_BYTE:   rc = count; /* this will be a memcpy */           break;
      case MPIR_SHORT:  rc=globus_dc_sizeof_remote_short(count,format);   break;
      case MPIR_USHORT: rc=globus_dc_sizeof_remote_u_short(count,format); break;
      case MPIR_LOGICAL: /* 'logical' in FORTRAN is always same as 'int' */
      case MPIR_INT:    rc=globus_dc_sizeof_remote_int(count,format);     break;
      case MPIR_UINT:   rc=globus_dc_sizeof_remote_u_int(count,format);   break;
      case MPIR_LONG:   rc=globus_dc_sizeof_remote_long(count,format);    break;
      case MPIR_LONGLONGINT:   
		rc=globus_dc_sizeof_remote_long_long(count,format);       break;
      case MPIR_ULONG:  rc=globus_dc_sizeof_remote_u_long(count,format);  break;
      case MPIR_FLOAT:  rc=globus_dc_sizeof_remote_float(count,format);   break;
      case MPIR_DOUBLE: rc=globus_dc_sizeof_remote_double(count,format);  break;
      case MPIR_LONGDOUBLE: /* not supported by Globus */ rc = 0;         break;
      case MPIR_UB:
      case MPIR_LB:     rc = 0;                                           break;
      case MPIR_COMPLEX: 
	rc = globus_dc_sizeof_remote_float(2*count, format); 
	break;
      case MPIR_DOUBLE_COMPLEX:
	rc = globus_dc_sizeof_remote_double(2*count, format); 
	break;
      case MPIR_CONTIG:
	rc = remote_size(count * datatype->count, datatype->old_type, format);
	break;
      case MPIR_VECTOR:
      case MPIR_HVECTOR:
	{
	  int tmp = remote_size(datatype->blocklen, datatype->old_type, format);
	  rc = (tmp == -1 ? -1 : tmp*count*datatype->count);
	}
        break;
      case MPIR_INDEXED:
      case MPIR_HINDEXED:
	{
	  int i, tmp, tmp2;
	  for (rc = tmp = tmp2 = i = 0; tmp2 != -1 && i < datatype->count; i++)
	  {
	    tmp2=remote_size(datatype->blocklens[i],datatype->old_type,format);
	    if (tmp2 == -1)
	      tmp = -1;
	    else
	      tmp += tmp2;
	  } /* endfor */
	  if (tmp != -1)
	    rc = tmp*count;
	  else
	    rc = -1;
	}
        break;
      case MPIR_STRUCT:
	{
	  int i, tmp, tmp2;
	  for (rc = tmp = tmp2 = i = 0; tmp2 != -1 && i < datatype->count; i++)
	  {
	    tmp2 = remote_size(datatype->blocklens[i],
				datatype->old_types[i],
				format);
	    if (tmp2 == -1) 
	      tmp = -1; 
	    else 
	      tmp += tmp2;
	  } /* endfor */
	  if (tmp != -1)
	    rc = tmp*count;
	  else
	    rc = -1;
	}
        break;
      default:
        globus_libc_fprintf(stderr,
	    "ERROR: remote_size: encountered unrecognizable MPIR type %d\n", 
	    datatype->dte_type);
	rc = -1;
        break;
    } /* end switch() */

    return rc;

} /* end remote_size() */

/*
 * extract_data_into_req
 */
int extract_data_into_req(MPIR_RHANDLE *req,
			       void *src_buff, 
			       int src_len,
			       int src_format,
			       int src_lrank,
			       int src_tag)
{
    struct MPIR_DATATYPE *dest_datatype = req->datatype;
    int req_count = req->req_count;
    int max_src_bufflen;
    int rc;

    /* 
     * need to set status's source&tag unconditionally.  even if there is an 
     * error, (e.g., recv buffer not big enough causing MPI_ERR_TRUNCATE),
     * the MPICH test suite STILL expects this information to be in the status.
     */
    req->s.MPI_SOURCE = src_lrank;
    req->s.MPI_TAG    = src_tag;

    /* determine if dest buff is big enough for message cached in src_buff  */
    /* by calculating req_count*sizeof(dest_datatype) in remote format     */
    /* and comparing that to src_len (len of cached msg in remote format)  */
    /* in other words, i'm prepared to rcv req_count*sizeof(dest_datatype) */
    /* which means i can recv at most max_src_bufflen bytes                 */

    max_src_bufflen = remote_size(req_count, dest_datatype, src_format);

    if (max_src_bufflen < 0)
    {
	/* something bad happened */
	rc = -1;
    }
    else if (src_len > max_src_bufflen)
    {
	/* there was NOT enough room */
	/* NICK: should i try to recv as much as i can? OR am i */
	/* allowed to call this an erroneous condition and not  */
	/* have to do anything?                                 */
	req->s.MPI_ERROR = MPI_ERR_TRUNCATE;
	rc = 0; /* returning non-error w.r.t. this function */
    } 
    else
    {
	rc = 0;

	req->len = 0;
	if (max_src_bufflen > 0 && req_count > 0)
	{
	    int src_unit_nbytes    = max_src_bufflen/req_count;
	    int n_complete         = src_len / src_unit_nbytes;
	    int src_partial_nbytes = src_len % src_unit_nbytes;
	    globus_byte_t *src  = (globus_byte_t *) src_buff;
	    globus_byte_t *dest = (globus_byte_t *) req->buf;

	    /* extracting all complete data elements first */
	    if (n_complete)
	    {
		rc = extract_complete_from_buff(&src, 
						dest, 
						n_complete, 
						dest_datatype, 
						src_format,
						&(req->len));
	    } /* endif */

	    /* extracting remaining partial last element */
	    if (!rc && src_partial_nbytes)
	    {
		globus_bool_t done = GLOBUS_FALSE;

		rc = extract_partial_from_buff(&src, 
						dest+req->len, 
						1,
						dest_datatype, 
						src_format,
						&src_partial_nbytes,
						&done,
						&(req->len));
	    }  /* endif */
	} /* endif */

	req->s.count = req->len;
	STATUS_INFO_SET_COUNT_LOCAL(req->s);
    } /* endif */

    return rc;

} /* end extract_data_into_req() */

/*
 * send_ack_over_tcp
 *
 * assumed that messaging to grank is known to be TCP
 * returns GLOBUS_TRUE upon successful completion, else returns GLOBUS_FALSE
 */

globus_bool_t send_ack_over_tcp(int grank, void *liba, int libasize)
{
    struct channel_t *cp;

    if (!(cp = get_channel(grank)))
    {
        globus_libc_fprintf(stderr,
	    "ERROR: send_ack_over_tcp: proc %d failed get_channel for"
	    " grank %d\n",
            MPID_MyWorldRank, grank); 
        print_channels();
	return GLOBUS_FALSE;
    } 
    else if (!(cp->selected_proto))
    {
        globus_libc_fprintf(stderr,
	    "ERROR: send_ack_over_tcp: proc %d does not have selected proto for"
	    " grank %d\n",
            MPID_MyWorldRank, grank); 
        print_channels();
	return GLOBUS_FALSE;
    } 
    else if ((cp->selected_proto)->type == tcp)
    {
            struct tcp_miproto_t *tp = (struct tcp_miproto_t *) 
                (cp->selected_proto)->info;
	    struct tcpsendreq * sr;
	    
            if (!(tp->handlep))
	    {
		globus_libc_fprintf(stderr,
		    "ERROR: send_ack_over_tcp: proc %d found NULL handlep"
		    " for grank %d\n",
		    MPID_MyWorldRank, grank);
		print_channels();
		return GLOBUS_FALSE;
	    } /* endif */

	    /* packing header: type=ack,liba */
	    if (Headerlen-globus_dc_sizeof_int(1) < libasize)
	    {
		globus_libc_fprintf(
		    stderr,
		    "ERROR: send_ack_over_tcp: deteremined that Headerlen "
		    "(%d) - sizeof(int) (%ld) < waiter for ack's libasize %d"
		    " and will therefore not fit into header\n", 
		    Headerlen, 
		    globus_dc_sizeof_int(1), 
		    libasize);
		return GLOBUS_FALSE;
	    } /* endif */

	    g_malloc(sr, struct tcpsendreq *, sizeof(struct tcpsendreq));
	    g_malloc(sr->liba, void *, libasize);
	    sr->type          = ack;
	    sr->dest_grank    = grank;
	    sr->libasize      = libasize;
	    memcpy(sr->liba, liba, libasize);

	    enqueue_tcp_send(sr);
    }
    else
    {
        globus_libc_fprintf(stderr,
	    "ERROR: send_ack_over_tcp: proc %d called with selected protocol to"
	    " grank %d something other than TCP\n",
            MPID_MyWorldRank, grank); 
        print_channels();
	return GLOBUS_FALSE;
    } /* endif */

    return GLOBUS_TRUE;

} /* end send_ack_over_tcp() */

/*
 * extract_complete_from_buff
 *
 * It is assumed that there are 'count' 'datatype' in the 'src' buff
 * and that ALL of them are complete.  In other words, that there is
 * no partial data in the 'src' buff like missing data from the last part
 * of the last user-defined data structure in a vector of user-define data 
 * structures.
 *
 * To retreive data from a 'src' buff where there is incomplete data, you
 * should call extract_partial_from_buff() (below).  
 * extract_complete_from_buff() may be used to extract the first N-1 elements 
 * of a vector of 'datatype' because those must be complete according to the 
 * MPI standard, and then use extract_partial_from_buff() from the last 
 * element if it is known to be incomplete.
 *
 * it is assumed that 'nbytes_rcvd' already has useful information
 * and this function simply adds to that count.
 *
 * NOTE: there is one more datatype found in datatype.h ... MPIR_FORT_INT
 *       it has been explained to me by bill that we do not have to
 *       support an explicit case for that type because it is a
 *       synonym for one of the other types we already have a case
 *       statement for (which type it is a synonym for is architecture 
 *       dependent and determined during mpich configuration).
 *
 * returns 0 upon successful completion, otherwise returns 1.
 */
int extract_complete_from_buff(globus_byte_t **src, 
				globus_byte_t *dest,
				int count,
				struct MPIR_DATATYPE *datatype,
				int format,
				int *nbytes_rcvd)
{
    int rc = 0;

    switch (datatype->dte_type)
    {
      case MPIR_CHAR: 
	globus_dc_get_char(src, (char *) dest, count, format); 
	(*nbytes_rcvd) += globus_dc_sizeof_char(count);
	break;
      case MPIR_UCHAR: 
	globus_dc_get_u_char(src, (u_char *) dest, count, format); 
	(*nbytes_rcvd) += globus_dc_sizeof_u_char(count);
	break;
      case MPIR_PACKED: /* THIS MUST BE A MEMCPY, i.e., NOT CONVERTED */ 
	memcpy((void *) dest, (void *) *src, count);
	(*src) += count;
	(*nbytes_rcvd) += count;
	break;
      case MPIR_BYTE: /* THIS MUST BE A MEMCPY, i.e., NOT CONVERTED */
	memcpy((void *) dest, (void *) *src, count);
	(*src) += count;
	(*nbytes_rcvd) += count;
        break;
      case MPIR_SHORT:
        globus_dc_get_short(src, (short *) dest, count, format);
	(*nbytes_rcvd) += globus_dc_sizeof_short(count);
        break;
      case MPIR_USHORT:
        globus_dc_get_u_short(src, (u_short *) dest, count, format);
	(*nbytes_rcvd) += globus_dc_sizeof_u_short(count);
        break;
      case MPIR_LOGICAL: /* 'logical' in FORTRAN is always same as 'int' */
      case MPIR_INT: 
	globus_dc_get_int(src, (int *) dest, count, format); 
	(*nbytes_rcvd) += globus_dc_sizeof_int(count);
	break;
      case MPIR_UINT:
	globus_dc_get_u_int(src, (u_int *) dest, count, format); 
	(*nbytes_rcvd) += globus_dc_sizeof_u_int(count);
        break;
      case MPIR_LONG:
	globus_dc_get_long(src, (long *) dest, count, format); 
	(*nbytes_rcvd) += globus_dc_sizeof_long(count);
        break;
      case MPIR_LONGLONGINT:
	globus_dc_get_long_long(src, (long long *) dest, count, format); 
	(*nbytes_rcvd) += globus_dc_sizeof_long_long(count);
        break;
      case MPIR_ULONG:
	globus_dc_get_u_long(src, (u_long *) dest, count, format); 
	(*nbytes_rcvd) += globus_dc_sizeof_u_long(count);
        break;
      case MPIR_FLOAT:
	globus_dc_get_float(src, (float *) dest, count, format); 
	(*nbytes_rcvd) += globus_dc_sizeof_float(count);
        break;
      case MPIR_DOUBLE:
	globus_dc_get_double(src, (double *) dest, count, format); 
	(*nbytes_rcvd) += globus_dc_sizeof_double(count);
        break;
      case MPIR_LONGDOUBLE: /* not supported by Globus */ break;
      case MPIR_UB: /* MPIR_UB and MPIR_LB are 0-byte datatypes */
      case MPIR_LB:
        break;
      case MPIR_COMPLEX:
	globus_dc_get_float(src, (float *) dest, 2*count, format);
	(*nbytes_rcvd) += globus_dc_sizeof_float(2*count);
        break;
      case MPIR_DOUBLE_COMPLEX:
	globus_dc_get_double(src, (double *) dest, 2*count, format);
	(*nbytes_rcvd) += globus_dc_sizeof_double(2*count);
        break;
      case MPIR_CONTIG:
	rc = extract_complete_from_buff(src, 
					dest, 
					count * datatype->count, 
					datatype->old_type, 
					format,
					nbytes_rcvd);
        break;
      case MPIR_VECTOR:
      case MPIR_HVECTOR:
	{
	  globus_byte_t *tmp = dest;
	  int i, j;
	  for (i = 0; !rc && i < count; i++)
	  {
	    dest = tmp;
	    for (j = 0; !rc && j < datatype->count; j++)
	    {
	      rc = extract_complete_from_buff(src, 
	      				      dest, 
					      datatype->blocklen, 
					      datatype->old_type, 
					      format,
					      nbytes_rcvd);
	      dest += datatype->stride;
	    } /* endfor */
	    tmp += datatype->extent;
	  } /* endfor */
	}
        break;
      case MPIR_INDEXED:
      case MPIR_HINDEXED:
	{
	  globus_byte_t *tmp;
	  int i, j;
	  for (i = 0; !rc && i < count; i++)
	  {
	    for (j = 0; !rc && j < datatype->count; j++)
	    {
	      tmp = dest + datatype->indices[j];
	      rc = extract_complete_from_buff(src, 
	      				      tmp, 
					      datatype->blocklens[j], 
					      datatype->old_type, 
					      format,
					      nbytes_rcvd);
	    } /* endfor */
	    dest += datatype->extent;
	  } /* endfor */
	}
        break;
      case MPIR_STRUCT:
	{
	  globus_byte_t *tmp;
	  int i, j;
	  for (i = 0; !rc && i < count; i++)
	  {
	    for (j = 0; !rc && j < datatype->count; j++)
	    {
	      tmp = dest + datatype->indices[j];
	      rc = extract_complete_from_buff(src, 
	      				      tmp, 
					      datatype->blocklens[j], 
					      datatype->old_types[j], 
					      format,
					      nbytes_rcvd);
	    } /* endfor */
	    dest += datatype->extent;
	  } /* endfor */
	}
        break;
      default:
        globus_libc_fprintf(stderr,
	    "ERROR: extract_complete_from_buff: encountered unrecognizable MPIR"
	    " type %d\n", 
	    datatype->dte_type);
	rc = 1;
        break;
    } /* end switch() */

    return rc;

} /* end extract_complete_from_buff() */

#if defined(VMPI)
/*
 * remove_and_free_mpircvreq
 *
 */
void remove_and_free_mpircvreq(struct mpircvreq *mp)
{
    if (mp)
    {
	if (mp->prev)
	    (mp->prev)->next = mp->next;
	else
	    MpiPostedQueue.head = mp->next;

	if (mp->next)
	    (mp->next)->prev = mp->prev;
	else
	    MpiPostedQueue.tail = mp->prev;

	g_free(mp);
    } /* endif */

} /* end remove_and_free_mpircvreq() */
#endif /* defined(VMPI) */

#if defined(VMPI)
/*
 * mpi_recv_or_post(MPIR_RHANDLE *in_req, int *error_code)
 *     - error_code possibly NULL
 *
 * it is assumed that upon entrance to this function that:
 *     - in_req->is_complete == GLOBUS_FALSE
 *
 * at end of function:
 *   if in_req was completed && was in the MpiPostedQueue (in_req->my_mp!=NULL)
 *       in_req (in_req->my_mp) is removed from MpiPostedQueue
 *   endif
 *   
 *   if in_req was not completed and not in MpiPostedQueue (in_req->my_mp==NULL)
 *       a node is allocated for in_req (in_req->my_mp) and it is
 *           placed into the MpiPostedQueue
 *   endif
 *
 * returns in_req->is_complete
 */
int mpi_recv_or_post(MPIR_RHANDLE *in_req, int *error_code)
{
    int err;
    int flag;

    do 
    {
	err = vmpi_error_to_mpich_error(
	    mp_iprobe(in_req->req_rank,
		      in_req->req_tag,
		      in_req->comm->vmpi_comm,
		      &flag,
		      STATUS_INFO_GET_VMPI_PTR(in_req->s))
					);
	if (err != MPI_SUCCESS)
	{
	    /*
	     * NICK: don't know if this is the correct thing to do here.
	     *       the user is calling an MPI recv function and we are
	     *       returning the error status of an MPI probe 
	     *       function.  i don't know if all the err statuses 
	     *       from probe make sense as a return code for recv.  
	     *       the fact that we are calling probe as an 
	     *       implementation for our recv suggests that we might 
	     *       be better off returning MPI_ERR_INTERN here.  
	     *       on the other hand, there might be more 
	     *       information in the return code of probe and
	     *       the user might be better off with that.  
	     *       don't know.
	     *       the standard does not specify anything about
	     *       what functions can return which error codes,
	     *       so we're probably ok doing what we're doing here.
	     */
	    in_req->s.MPI_SOURCE = in_req->comm->vlrank_to_lrank[
				    mp_status_get_source(
					STATUS_INFO_GET_VMPI_PTR(in_req->s))];
	    in_req->s.MPI_TAG = mp_status_get_tag(
				    STATUS_INFO_GET_VMPI_PTR(in_req->s));
	    STATUS_INFO_SET_COUNT_VMPI(in_req->s);
	    MPIR_Type_free(&(in_req->datatype));
	    {
		MPI_Comm c = in_req->comm->self;
		MPI_Comm_free(&c);
	    }
	    in_req->s.MPI_ERROR = err;
	    if (error_code)
		*error_code = err;
		
	    in_req->is_complete = GLOBUS_TRUE;
	    
	    if (((MPI_Request) in_req)->chandle.ref_count <= 0)
	    {
		MPID_RecvFree(in_req);
	    }

	    return GLOBUS_TRUE;
	} /* endif */

	if (flag)
	{
	    /*
	     * there is an unreceived message that matches
	     * our request.  we need to make find out if this
	     * same message also matches a previously posted request.
	     */
	    struct mpircvreq *mp;
	    struct mpircvreq *matched_mp;
	    struct mpircvreq *remove_mp;
	    MPIR_RHANDLE *rhandle;

	    /* 
	     * if in_req is in MpiPostedQueue then scope of search in
	     * MpiPostedQueue is up to (but not including me), otherwise 
	     * scope is whole MpiPostedQueue.  we are searching for
	     * another req that can match the successful probe.
	     */
	    for (mp = MpiPostedQueue.head, 
		    matched_mp = (struct mpircvreq *) NULL; 
			!matched_mp && mp != in_req->my_mp; 
			    mp = mp->next)
	    {

		rhandle = mp->req;
		if (rhandle->req_context_id == in_req->req_context_id
		    &&
		    (rhandle->req_rank == VMPI_ANY_SOURCE
			|| rhandle->req_rank == mp_status_get_source(
					    STATUS_INFO_GET_VMPI_PTR(
						in_req->s)))
		    && 
		    (rhandle->req_tag == VMPI_ANY_TAG 
			|| rhandle->req_tag == mp_status_get_tag(
					    STATUS_INFO_GET_VMPI_PTR(
						in_req->s))))
		{
		    matched_mp = mp;
		} /* endif */
	    } /* endfor */

	    if (matched_mp)
	    {
		/*
		 * the message we found by probing does match a previously 
		 * posted request.  we have to satisfy the previously posted 
		 * req first.  by issuing a recv on this req it is possible 
		 * that it will match a message other than the one one we 
		 * found by probe ... it might match one that arrived before 
		 * the one we found with probe.  we do know that there is at 
		 * least one message that the previously posted req will match.
		 */

		rhandle   = matched_mp->req;
		remove_mp = matched_mp;
	    }
	    else
	    {
		/*
		 * the message we found by probing does not match any of the 
		 * previously posted rcvs ... it's OK to rcv into req passed 
		 * to this function.
		 */
		rhandle   = in_req;
		remove_mp = in_req->my_mp;
	    } /* endif */

	    if (remove_mp)
	    {
		/* 
		 * the req we are about to rcv into has been placed in
		 * our MpiPostedQueue, and if rhandle->req_src_proto == unknown,
		 * in TCP 'posted' queue as well.  we must update the queue(s)
		 * by removing this req.
		 */

		/* first removing from our MpiPostedQueue */
		remove_and_free_mpircvreq(remove_mp);
		rhandle->my_mp = (struct mpircvreq *) NULL;

		/* now removing, if necessary, from TCP 'posted' queue */
		if (rhandle->req_src_proto == unknown)
		{
		    /* 
		     * because the source was unknown, we have added 
		     * (in MPID_IrecvDatatype) a request to the 'posted' 
		     * queue that must now be extracted.
		     */
		    
		    MPIR_RHANDLE *posted;
		    int found;

		    MPID_Msg_arrived(rhandle->comm->vlrank_to_lrank[
			    mp_status_get_source(
				STATUS_INFO_GET_VMPI_PTR(rhandle->s))],
				    rhandle->req_tag,
				    rhandle->req_context_id,
				    &posted,
				    &found);

		    if (found)
		    {
			TcpOutstandingRecvReqs --;
		    }
		    else
		    {
			/* 
			 * NICK: in single-threaded case i'm pretty sure 
			 *       that this is a fatal error, but for now 
			 *       simply printing warning and continuing.
			 */
			globus_libc_fprintf(stderr, 
			    "WARNING: MPID_RecvIcomplete: detected incoming "
			    "message from unknown recv source over MPI but "
			    "did NOT find request in TCP queue\n");
		    } /* endif */
		} /* endif */
	    } /* endif */

	    MPID_Type_validate_vmpi(rhandle->datatype);
	    {
                /* 
                 * NOTE: under the assumption that vendor's implement 'packing'
                 *       by simply copying the data into the buffer, we 
                 *       simply strip our single 'format' byte we inserted
                 *       during _our_ packing process from the front
                 *       of the buffer when sending over vMPI and then
                 *       insert here again if receive type is packed.  
                 *       this allows the receiver to receive the data as
                 *       either packed or the basic datatype.
                 *
                 *       also, the user should have called MPID_Pack_size
                 *       to get the value for 'count'.  we subtract 
                 *       sizeof(unsigned char) from the buffer size to 
                 *       account for the format byte we stripped on the
                 *       sending side.
                 */

                int adj;

                if (rhandle->datatype->dte_type == MPIR_PACKED)
                {
                    *((unsigned char *)(rhandle->buf)) = GLOBUS_DC_FORMAT_LOCAL;
                    adj = sizeof(unsigned char);
                }
                else
                {
                    adj = 0;
                } /* endif */

		err = vmpi_error_to_mpich_error(
		    mp_recv((void *) (((char *) (rhandle->buf)) + adj), 
			    rhandle->req_count-adj, 
			    rhandle->datatype->vmpi_type,
			    rhandle->req_rank, 
			    rhandle->req_tag,
			    rhandle->comm->vmpi_comm,
			    STATUS_INFO_GET_VMPI_PTR(rhandle->s)));
	    }

	    /*
	     * need to set source & tag info, even in case of error 
	     * mpich test suite, pt2pt/truc.c, explicitly tests for 
	     * sending a message that's too big on the recv side.
	     * the recv returns and sets status.MPI_ERROR to 
	     * MPI_ERR_TRUNCATE, but expects to have status.MPI_SOURCE
	     * and status.MPI_TAG set to the proper values.
	     */
	    rhandle->s.MPI_SOURCE = rhandle->comm->vlrank_to_lrank[
		mp_status_get_source(
		    STATUS_INFO_GET_VMPI_PTR(rhandle->s))];
	    rhandle->s.MPI_TAG = mp_status_get_tag(
		STATUS_INFO_GET_VMPI_PTR(rhandle->s));
	    STATUS_INFO_SET_COUNT_VMPI(rhandle->s);
	    MPIR_Type_free(&(rhandle->datatype));
	    {
		MPI_Comm c = rhandle->comm->self;
		MPI_Comm_free(&c);
	    }

	    rhandle->s.MPI_ERROR = err;
	    rhandle->is_complete = GLOBUS_TRUE;

	    if (((MPI_Request) rhandle)->chandle.ref_count <= 0)
	    {
		MPID_RecvFree(rhandle);
	    }
	} /* endif (flag) */
    } while (flag && !(in_req->is_complete));

    if (!(in_req->is_complete) && !(in_req->my_mp))
    {
	/* 
	 * this request could not be satisfied and it does not already
	 * reside in our MpiPostedQueue so we need to enqueue 
	 */
	struct mpircvreq *new_mp;

	g_malloc(new_mp, struct mpircvreq *, sizeof(struct mpircvreq));

	new_mp->req   = in_req;
	in_req->my_mp = new_mp;

	if (MpiPostedQueue.tail)
	{
	    new_mp->prev = MpiPostedQueue.tail;
	    (MpiPostedQueue.tail)->next = new_mp;
	    MpiPostedQueue.tail = new_mp;
	}
	else
	{
	    MpiPostedQueue.head = MpiPostedQueue.tail = new_mp;
	    (MpiPostedQueue.tail)->prev = (struct mpircvreq *) NULL;
	} /* endif */
	new_mp->next = (struct mpircvreq *) NULL;
    } /* endif */

    return in_req->is_complete;

} /* end mpi_recv_or_post() */
#endif /* defined(VMPI) */

/*******************/
/* Local Functions */
/*******************/

/*
 * extract_partial_from_buff
 *
 * This function is called when we know that data is missing from the 
 * end of 'datatype'.  This most commonly occurs when the 'datatype' is
 * a user-defined structure.  According to the MPI standard, it is acceptable
 * to receive into an array of N of any datatype (including user-defined data
 * structures) and only send some M <= N elements, and furthermore, the 
 * Mth element sent may have data omitted from the end.  This function is
 * called to handle the special case of extracting the partial data from that 
 * last element.
 *
 * The reason a new function was written is that care must be taken to only
 * extract as much data as is there.  So before each datatype->dte_type, we
 * must calculate the amount of data to retrieve.  This takes time, and
 * in practice is rarely called for.  So, for the majority of the cases,
 * extract_complete_from_buff() (above) can be used when full data is sent.  
 * There the calculation is *not* made before each datatype->dte_type.  
 * For cases where data is missing from the Mth element, 
 * extract_complete_from_buff() is called to rapidly extract the first 
 * M-1 elements from the 'src' buff and then this function is called to 
 * carefully extract the remaining data from the Mth element.
 *
 * it is assumed that 'nbytes_rcvd' already has useful information
 * and this function simply adds to that count.
 *
 * This is a recursive function and it is assumed that * *done == GLOBUS_FALSE 
 * on the its initial call.
 *
 * NOTE: there is one more datatype found in datatype.h ... MPIR_FORT_INT
 *       it has been explained to me by bill that we do not have to
 *       support an explicit case for that type because it is a
 *       synonym for one of the other types we already have a case
 *       statement for (which type it is a synonym for is architecture 
 *       dependent and determined during mpich configuration).
 *
 */
static int extract_partial_from_buff(globus_byte_t **src, 
					globus_byte_t *dest,
					int count,
					struct MPIR_DATATYPE *datatype,
					int format,
					int *remaining_nbytes,
					globus_bool_t *done,
					int *nbytes_rcvd)
{
    int rc = 0;

    switch (datatype->dte_type)
    {
      case MPIR_CHAR:           case MPIR_UCHAR:       case MPIR_PACKED: 
      case MPIR_BYTE:           case MPIR_SHORT:       case MPIR_USHORT:
      case MPIR_LOGICAL:        case MPIR_INT:         case MPIR_UINT:
      case MPIR_LONG:           case MPIR_LONGLONGINT: case MPIR_ULONG:   
      case MPIR_FLOAT:          case MPIR_DOUBLE:      case MPIR_COMPLEX: 
      case MPIR_DOUBLE_COMPLEX:
	{
	  /* basic data types */
          int unit_size, inbuf_nelem, extract_nelem; 

	  if ((unit_size = remote_size(1, datatype, format)) <= 0)
	  {
	      /* something bad happened, return immediately */
	      return 1;
	  } /* endif */

	  inbuf_nelem = (*remaining_nbytes) / unit_size;
	  if ((extract_nelem = (count<inbuf_nelem ? count : inbuf_nelem)) != 0)
	  {
	    if (!(rc = extract_complete_from_buff(src, 
					  dest, 
					  extract_nelem, 
					  datatype, 
					  format,
					  nbytes_rcvd)))
	        (*remaining_nbytes) -= (extract_nelem*unit_size);
	  } /* endif */

	  if (!rc && extract_nelem < count)
	  {
	    *done = GLOBUS_TRUE;
            /*
             * at this point if there are residual bytes and we are dealing 
             * with a basic datatype, then those residual bytes will never 
             * be extracted.  checking for that here and printing warning 
	     * message.
             */
	    if (*remaining_nbytes > 0)
	    {
	       globus_libc_fprintf(stderr,
		"WARNING: after extracting %d of type %d (%d bytes), incoming "
		"buffer has %d bytes of residual data at its end that was NOT "
		"and WILL NEVER BE extracted ... that residual data was lost "
		"(all byte counts in dataorigin format)\n", 
		  extract_nelem, 
		  datatype->dte_type, 
		  extract_nelem * unit_size, 
		  *remaining_nbytes);
	     } /* endif */
	  } /* endif */
	}
        break;
      case MPIR_LONGDOUBLE: /* not supported by Globus */ break;
      case MPIR_UB: /* MPIR_UB and MPIR_LB are 0-byte datatypes */
      case MPIR_LB:
        break;
      /*
       * rest are complex data types requiring special care
       * by decomposing down to their basic types
       */
      case MPIR_CONTIG:
	rc = extract_partial_from_buff(src, 
					dest,
					count*datatype->count,
					datatype->old_type,
					format,
					remaining_nbytes,
					done,
					nbytes_rcvd);
        break;
      case MPIR_VECTOR:
      case MPIR_HVECTOR:
	{
	  globus_byte_t *tmp = dest;
	  int i, j;
	  for (i = 0; !rc && !(*done) && i < count; i++)
	  {
	    dest = tmp;
	    for (j = 0; !rc && !(*done) && j < datatype->count; j++)
	    {
	      rc = extract_partial_from_buff(src, 
					    dest,
					    datatype->blocklen,
					    datatype->old_type,
					    format,
					    remaining_nbytes,
					    done,
					    nbytes_rcvd);
	      dest += datatype->stride;
	    } /* endfor */
	    tmp += datatype->extent;
	  } /* endfor */
	}
        break;
      case MPIR_INDEXED:
      case MPIR_HINDEXED:
      case MPIR_STRUCT:
	{
	  globus_byte_t *tmp;
	  int i, j;
	  for (i = 0; !rc && !(*done) && i < count; i++)
	  {
	    for (j = 0; !rc && !(*done) && j < datatype->count; j++)
	    {
	      tmp = dest + datatype->indices[j];
	      rc = extract_partial_from_buff(src, 
					    tmp,
					    datatype->blocklens[j],
					    datatype->old_types[j],
					    format,
					    remaining_nbytes,
					    done,
					    nbytes_rcvd);
	    } /* endfor */
	    dest += datatype->extent;
	  } /* endfor */
	}
        break;
      default:
        globus_libc_fprintf(stderr,
	    "ERROR: extract_partial_from_buff: encountered unrecognizable MPIR "
	    "type %d\n", 
	    datatype->dte_type);
	rc = 1;
        break;
    } /* end switch() */

    return rc;

} /* end extract_partial_from_buff() */
