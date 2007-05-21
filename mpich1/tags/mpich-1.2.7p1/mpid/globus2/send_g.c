#include "chconfig.h"
#include <sys/time.h> /* for gettimeofday() */

#include "globdev.h"
#include "reqalloc.h"

/***************************/
/* Local Utility Functions */
/***************************/

static int proto_from_valid_send(void *buf, 
				int count, 
				struct MPIR_DATATYPE *datatype, 
				int dest_grank);
static int enqueue_cancel_tcp_data(MPIR_SHANDLE *sreq);
static int write_all_tcp_cancels(struct tcp_miproto_t *tp);
static int start_tcp_send(struct tcpsendreq *sr);
static void write_callback(void *arg, 
			    globus_io_handle_t *handle, 
			    globus_result_t result, 
			    globus_byte_t *buff, 
			    globus_size_t nbytes);
/* START GRIDFTP */
static void gridftp_setup_sockets_callback(void *callback_arg,
                                struct globus_ftp_control_handle_s *handle,
                                unsigned int stripe_ndx,
                                globus_bool_t reuse,
                                globus_object_t *error);
static void gridftp_write_callback(void *callback_arg,
                                    globus_ftp_control_handle_t *handle,
                                    globus_object_t *error,
                                    globus_byte_t *buffer,
                                    globus_size_t length,
                                    globus_off_t offset,
                                    globus_bool_t eof);
void g_ftp_monitor_init(g_ftp_perf_monitor_t *monitor);
void g_ftp_monitor_reset(g_ftp_perf_monitor_t *monitor);
/* END GRIDFTP */
static void remove_and_continue(struct tcpsendreq *sr);
static void free_and_mark_sreq(struct tcpsendreq *sr, globus_bool_t data_sent);
static void send_datatype(struct MPIR_COMMUNICATOR *comm,
			    void *buf,
			    int	count,
			    struct MPIR_DATATYPE *datatype,
			    int	src_lrank,
			    int	tag,
			    int	context_id,
			    int	dest_grank,
			    int *error_code);
static void ssend_datatype(struct MPIR_COMMUNICATOR *comm,
			    void *buf,
			    int count,
			    struct MPIR_DATATYPE *datatype,
			    int	src_lrank,
			    int	tag,
			    int	context_id,
			    int	dest_grank,
			    int *error_code);
static void get_unique_msg_id(long *sec, long *usec, unsigned long *ctr);

/********************/
/* Global Variables */
/********************/

#ifdef GLOBUS_CALLBACK_GLOBAL_SPACE
extern globus_callback_space_t MpichG2Space;
#endif

extern volatile int	         TcpOutstandingRecvReqs;
extern globus_size_t             Headerlen;
#if defined(VMPI)
extern struct mpi_posted_queue   MpiPostedQueue;
#endif
volatile int                     TcpOutstandingSendReqs = 0;
extern struct commworldchannels *CommWorldChannelsTable;
/* for unique msg id's (used in conjunction with MPID_MyWorldRank) */
extern struct timeval LastTimeILookedAtMyWatch;
extern unsigned long NextMsgIdCtr;


/*
 * MPID_SendDatatype()
 */
#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME MPID_SendDatatype
void MPID_SendDatatype(struct MPIR_COMMUNICATOR *comm,
                       void *buf,
                       int count,
                       struct MPIR_DATATYPE *datatype,
                       int src_lrank,
                       int tag,
                       int context_id,
                       int dest_grank,
                       int *error_code)
{
    int proto;

    DEBUG_FN_ENTRY(DEBUG_MODULE_SEND);
    DEBUG_PRINTF(DEBUG_MODULE_SEND, DEBUG_INFO_ARGS,
		 ("dest_grank %d type %d count %d context %d tag %d\n",
		  dest_grank,
		  datatype->dte_type,
		  count,
		  context_id,
		  tag));

    if ((proto = proto_from_valid_send(buf, count, datatype, dest_grank)) < 0)
    {
        *error_code = MPI_ERR_BUFFER;
    }
#   if defined(VMPI)
    else if (proto == mpi)
    {
	int				dest;
	globus_bool_t		tcp_outstanding_recv_reqs;
    
	MPID_Type_validate_vmpi(datatype);
	dest = comm->vgrank_to_vlrank[VMPI_GRank_to_VGRank[dest_grank]];

	tcp_outstanding_recv_reqs = 
	    (TcpOutstandingRecvReqs > 0) ? GLOBUS_TRUE : GLOBUS_FALSE;

	if (MpiPostedQueue.head == NULL && !tcp_outstanding_recv_reqs)
	{
	    /* 
	     * NOTE: under the assumption that vendor's implement 'packing'
	     *       by simply copying the data into the buffer, we 
	     *       simply strip our single 'format' byte we inserted
	     *       during _our_ packing process from the front
	     *       of the buffer when sending over vMPI.  this
	     *       allows the receiver to receive the data as
	     *       either packed or the basic datatype.
	     *
	     *       also, the user should have called MPID_Pack_size
	     *       to get the value for 'count'.  we add sizeof(unsigned char)
	     *       to the buffer size to account for the format byte
	     *       we prepend ... we subtract that here if sending
	     *       packed data.
	     */

	    int adj = (datatype->dte_type == MPIR_PACKED 
		    ? sizeof(unsigned char) : 0);

	    if (datatype->dte_type == MPIR_PACKED 
		&& *((unsigned char *) buf) != GLOBUS_DC_FORMAT_LOCAL)
	    {
		globus_libc_fprintf(stderr, 
		    "ERROR: attempt to send MPI_PACKED with "
		    "illegal embedded format\n");
		*error_code = MPI_ERR_INTERN;
		goto fn_exit;
	    } /* endif */

	    *error_code = vmpi_error_to_mpich_error(
		mp_send((void *) (((char *) buf) + adj), 
			count-adj, 
			datatype->vmpi_type,
			dest,
			tag,
			comm->vmpi_comm));
	}
	else
	{
	    send_datatype(comm,
			  buf,
			  count,
			  datatype,
			  src_lrank,
			  tag,
			  context_id,
			  dest_grank,
			  error_code);
	} /* endif */
    }
#   endif
    else if (proto == tcp)
    {
	send_datatype(comm,
		      buf,
		      count,
		      datatype,
		      src_lrank,
		      tag,
		      context_id,
		      dest_grank,
		      error_code);
    }
    else
    {
	DEBUG_PRINTF(DEBUG_MODULE_SEND, DEBUG_INFO_FAILURE,
		     ("ERROR - proc %d selected proto for dest %d "
		      "has unrecognizable proto type %d\n",
		      MPID_MyWorldRank,
		      dest_grank,
		      proto));
#	if DEBUG_CHECK(DEBUG_MODULE_SEND, DEBUG_INFO_FAILURE)
	{
	    print_channels();
	}
#	endif

        *error_code = MPI_ERR_INTERN;
    } /* endif */

#if defined(VMPI)
  fn_exit:
#endif
    DEBUG_FN_EXIT(DEBUG_MODULE_SEND);
} /* end MPID_SendDatatype() */

/*
 * MPID_IsendDatatype()
 */
#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME MPID_IsendDatatype
void MPID_IsendDatatype(struct MPIR_COMMUNICATOR *comm,
                        void *buf,
                        int count,
                        struct MPIR_DATATYPE *datatype,
                        int src_lrank,
                        int tag,
                        int context_id,
                        int dest_grank,
                        MPI_Request request,
                        int *error_code)
{
    int proto;
    MPIR_SHANDLE *sreq = (MPIR_SHANDLE *) request;

    DEBUG_FN_ENTRY(DEBUG_MODULE_SEND);
    DEBUG_PRINTF(DEBUG_MODULE_SEND, DEBUG_INFO_ARGS,
		 ("dest_grank %d type %d count %d context %d tag %d\n",
		  dest_grank,
		  datatype->dte_type,
		  count,
		  context_id,
		  tag));
    
/* globus_libc_fprintf(stderr, "NICK: %d enter MPID_IsendDatatype: tag %d context %d dest_grank %d\n", MPID_MyWorldRank, tag, context_id, dest_grank); */
    proto = proto_from_valid_send(buf, count, datatype, dest_grank);
    sreq->req_src_proto = proto;
    sreq->is_complete = GLOBUS_FALSE;
    
    if (proto < 0)
    {
        *error_code = MPI_ERR_BUFFER;
    }
#   if defined(VMPI)
    else if (proto == mpi)
    {
	/* 
	 * NOTE: under the assumption that vendor's implement 'packing'
	 *       by simply copying the data into the buffer, we 
	 *       simply strip our single 'format' byte we inserted
	 *       during _our_ packing process from the front
	 *       of the buffer when sending over vMPI.  this
	 *       allows the receiver to receive the data as
	 *       either packed or the basic datatype.
	 *
	 *       also, the user should have called MPID_Pack_size
	 *       to get the value for 'count'.  we add sizeof(unsigned char)
	 *       to the buffer size to account for the format byte
	 *       we prepend ... we subtract that here if sending
	 *       packed data.
	 */

	int adj = (datatype->dte_type == MPIR_PACKED 
		? sizeof(unsigned char) : 0);
	int dest;

	if (datatype->dte_type == MPIR_PACKED 
	    && *((unsigned char *) buf) != GLOBUS_DC_FORMAT_LOCAL)
	{
	    globus_libc_fprintf(stderr, 
		"ERROR: attempt to send MPI_PACKED with "
		"illegal embedded format\n");
	    *error_code = MPI_ERR_INTERN;
	    goto fn_exit;
	} /* endif */

	sreq->cancel_issued  = GLOBUS_FALSE;
	MPID_Type_validate_vmpi(datatype);
	dest = comm->vgrank_to_vlrank[VMPI_GRank_to_VGRank[dest_grank]];
	*error_code = vmpi_error_to_mpich_error(
	    mp_isend((void *) (((char *) buf) + adj), 
		     count-adj, 
		     datatype->vmpi_type,
		     dest,
		     tag,
		     comm->vmpi_comm,
		     sreq->vmpi_req));
    }
#   endif
    else if (proto == tcp)
    {
	struct tcpsendreq *sr;
	int row;

	/* NICK: inefficient to init/destroy lock/condvar each time */
	sreq->cancel_issued  = GLOBUS_FALSE;
	sreq->needs_ack      = GLOBUS_FALSE;
	sreq->ack_arrived    = GLOBUS_FALSE;
	sreq->data_sent      = GLOBUS_FALSE;
	sreq->dest_grank     = dest_grank;
	/* unique msg id for potential cancels */
	if ((row = get_channel_rowidx(MPID_MyWorldRank, 
				    &(sreq->msg_id_commworld_displ))) == -1)
	{
	    DEBUG_PRINTF(DEBUG_MODULE_SEND, DEBUG_INFO_FAILURE,
			 ("ERROR - proc %d got row -1 "
			  " for my own commworldrank\n",
			  MPID_MyWorldRank));
#	    if DEBUG_CHECK(DEBUG_MODULE_SEND, DEBUG_INFO_FAILURE)
	    {
		print_channels();
	    }
#	    endif

	    *error_code = MPI_ERR_INTERN;
	    goto fn_exit;
	} /* endif */
	memcpy(sreq->msg_id_commworld_id, 
		CommWorldChannelsTable[row].name, 
		COMMWORLDCHANNELSNAMELEN);
	get_unique_msg_id(&(sreq->msg_id_sec), 
			    &(sreq->msg_id_usec), 
			    &(sreq->msg_id_ctr));

	g_malloc(sr, struct tcpsendreq *, sizeof(struct tcpsendreq));
	sr->type          = user_data;
	sr->buff          = buf;
	sr->count         = count;
	sr->datatype      = MPIR_Type_dup(datatype);
	sr->src_lrank     = src_lrank;
	sr->tag           = tag;
	sr->context_id    = context_id;
	sr->sreq          = sreq;
	sr->dest_grank    = sreq->dest_grank;
	
	sreq->my_sp    = sr;

	if (enqueue_tcp_send(sr))
	{
	    *error_code = MPI_ERR_INTERN;
	}
	else
	{
	    *error_code = 0;
	} /* endif */
    }
    else
    {
	DEBUG_PRINTF(DEBUG_MODULE_SEND, DEBUG_INFO_FAILURE,
		     ("ERROR - proc %d selected proto for dest %d "
		      "has unrecognizable proto type %d\n",
		      MPID_MyWorldRank,
		      dest_grank,
		      proto));
#	if DEBUG_CHECK(DEBUG_MODULE_SEND, DEBUG_INFO_FAILURE)
	{
	    print_channels();
	} /* endif */
#	endif

	*error_code = MPI_ERR_INTERN;
    } /* endif */

  fn_exit:
    DEBUG_FN_EXIT(DEBUG_MODULE_SEND);

} /* end MPID_IsendDatatype() */

/*
 * MPID_SsendDatatype()
 */
#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME MPID_SsendDatatype
void MPID_SsendDatatype(struct MPIR_COMMUNICATOR *comm,
                        void *buf,
                        int count,
                        struct MPIR_DATATYPE *datatype,
                        int src_lrank,
                        int tag,
                        int context_id,
                        int dest_grank,
                        int *error_code)
{
    int proto;

    DEBUG_FN_ENTRY(DEBUG_MODULE_SEND);
    DEBUG_PRINTF(DEBUG_MODULE_SEND, DEBUG_INFO_ARGS,
		 ("dest_grank %d type %d count %d context %d tag %d\n",
		  dest_grank,
		  datatype->dte_type,
		  count,
		  context_id,
		  tag));

    if ((proto = proto_from_valid_send(buf, count, datatype, dest_grank)) < 0)
    {
        *error_code = MPI_ERR_BUFFER;
    }
#   if defined(VMPI)
    else if (proto == mpi)
    {
	int		dest;
	globus_bool_t	tcp_outstanding_recv_reqs;

	MPID_Type_validate_vmpi(datatype);
	dest = comm->vgrank_to_vlrank[VMPI_GRank_to_VGRank[dest_grank]];

	tcp_outstanding_recv_reqs = 
	    (TcpOutstandingRecvReqs > 0) ? GLOBUS_TRUE : GLOBUS_FALSE;

	if (MpiPostedQueue.head == NULL && !tcp_outstanding_recv_reqs)
	{
            /* 
             * NOTE: under the assumption that vendor's implement 'packing'
             *       by simply copying the data into the buffer, we 
             *       simply strip our single 'format' byte we inserted
             *       during _our_ packing process from the front
             *       of the buffer when sending over vMPI.  this
             *       allows the receiver to receive the data as
             *       either packed or the basic datatype.
             *
             *       also, the user should have called MPID_Pack_size
             *       to get the value for 'count'.  we add sizeof(unsigned char)
             *       to the buffer size to account for the format byte
             *       we prepend ... we subtract that here if sending
             *       packed data.
             */

            int adj = (datatype->dte_type == MPIR_PACKED 
                    ? sizeof(unsigned char) : 0);

            if (datatype->dte_type == MPIR_PACKED 
                && *((unsigned char *) buf) != GLOBUS_DC_FORMAT_LOCAL)
            {
                globus_libc_fprintf(stderr, 
                    "ERROR: attempt to send MPI_PACKED with "
                    "illegal embedded format\n");
                *error_code = MPI_ERR_INTERN;
                goto fn_exit;
            } /* endif */

	    *error_code = vmpi_error_to_mpich_error(
		mp_ssend((void *) (((char *) buf) + adj),
			 count-adj, 
			 datatype->vmpi_type,
			 dest,
			 tag,
			 comm->vmpi_comm));
	}
	else
	{
	    ssend_datatype(comm,
			   buf,
			   count,
			   datatype,
			   src_lrank,
			   tag,
			   context_id,
			   dest_grank,
			   error_code);
	}
    }
#   endif
    else if (proto == tcp)
    {
	ssend_datatype(comm,
		       buf,
		       count,
		       datatype,
		       src_lrank,
		       tag,
		       context_id,
		       dest_grank,
		       error_code);
    }
    else
    {
	DEBUG_PRINTF(DEBUG_MODULE_SEND, DEBUG_INFO_FAILURE,
		     ("ERROR - proc %d selected proto for dest %d "
		      "has unrecognizable proto type %d\n",
		      MPID_MyWorldRank,
		      dest_grank,
		      proto));
#	if DEBUG_CHECK(DEBUG_MODULE_SEND, DEBUG_INFO_FAILURE)
	{
	    print_channels();
	}
#	endif

        *error_code = MPI_ERR_INTERN;
    } /* endif */

#if defined(VMPI)
  fn_exit:
#endif
    DEBUG_FN_EXIT(DEBUG_MODULE_SEND);
} /* end MPID_SsendDatatype() */

#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME MPID_IssendDatatype
void MPID_IssendDatatype(struct MPIR_COMMUNICATOR *comm,
                         void *buf,
                         int count,
                         struct MPIR_DATATYPE *datatype,
                         int src_lrank,
                         int tag,
                         int context_id,
                         int dest_grank,
                         MPI_Request request,
                         int *error_code)
{
    MPIR_SHANDLE *sreq = (MPIR_SHANDLE *) request;
    int proto;

    DEBUG_FN_ENTRY(DEBUG_MODULE_SEND);
    DEBUG_PRINTF(DEBUG_MODULE_SEND, DEBUG_INFO_ARGS,
		 ("dest_grank %d type %d count %d context %d tag %d\n",
		  dest_grank,
		  datatype->dte_type,
		  count,
		  context_id,
		  tag));

    proto = proto_from_valid_send(buf, count, datatype, dest_grank);
    sreq->req_src_proto = proto;
    sreq->is_complete = GLOBUS_FALSE;
    
    if (proto < 0)
    {
        *error_code = MPI_ERR_BUFFER;
    }
#   if defined(VMPI)
    else if (proto == mpi)
    {
	/* 
	 * NOTE: under the assumption that vendor's implement 'packing'
	 *       by simply copying the data into the buffer, we 
	 *       simply strip our single 'format' byte we inserted
	 *       during _our_ packing process from the front
	 *       of the buffer when sending over vMPI.  this
	 *       allows the receiver to receive the data as
	 *       either packed or the basic datatype.
	 *
	 *       also, the user should have called MPID_Pack_size
	 *       to get the value for 'count'.  we add sizeof(unsigned char)
	 *       to the buffer size to account for the format byte
	 *       we prepend ... we subtract that here if sending
	 *       packed data.
	 */

	int adj = (datatype->dte_type == MPIR_PACKED 
		? sizeof(unsigned char) : 0);
	int dest;

	if (datatype->dte_type == MPIR_PACKED 
	    && *((unsigned char *) buf) != GLOBUS_DC_FORMAT_LOCAL)
	{
	    globus_libc_fprintf(stderr, 
		"ERROR: attempt to send MPI_PACKED with "
		"illegal embedded format\n");
	    *error_code = MPI_ERR_INTERN;
	    goto fn_exit;
	} /* endif */

	sreq->cancel_issued  = GLOBUS_FALSE;
	MPID_Type_validate_vmpi(datatype);
	dest = comm->vgrank_to_vlrank[VMPI_GRank_to_VGRank[dest_grank]];
	*error_code = vmpi_error_to_mpich_error(
	    mp_issend((void *) (((char *) buf) + adj),
		      count-adj, 
		      datatype->vmpi_type,
		      dest,
		      tag,
		      comm->vmpi_comm,
		      sreq->vmpi_req));
    }
#   endif
    else if (proto == tcp)
    {
	struct tcpsendreq *sr;
	int row;
	
	/* NICK: inefficient to init/destroy lock/condvar each time */
	sreq->cancel_issued  = GLOBUS_FALSE;
	sreq->needs_ack      = GLOBUS_TRUE;
	sreq->ack_arrived    = GLOBUS_FALSE;
	sreq->data_sent      = GLOBUS_FALSE;
	sreq->dest_grank     = dest_grank;
	/* unique msg id for potential cancels */
	if ((row = get_channel_rowidx(MPID_MyWorldRank, 
				    &(sreq->msg_id_commworld_displ))) == -1)
	{
	    DEBUG_PRINTF(DEBUG_MODULE_SEND, DEBUG_INFO_FAILURE,
			 ("ERROR - proc %d got row -1 "
			  " for my own commworldrank\n",
			  MPID_MyWorldRank));
#	    if DEBUG_CHECK(DEBUG_MODULE_SEND, DEBUG_INFO_FAILURE)
	    {
		print_channels();
	    }
#	    endif

	    *error_code = MPI_ERR_INTERN;
	    goto fn_exit;
	} /* endif */
	memcpy(sreq->msg_id_commworld_id, 
		CommWorldChannelsTable[row].name, 
		COMMWORLDCHANNELSNAMELEN);
	get_unique_msg_id(&(sreq->msg_id_sec), 
			    &(sreq->msg_id_usec), 
			    &(sreq->msg_id_ctr));

	g_malloc(sr, struct tcpsendreq *, sizeof(struct tcpsendreq));
	sr->type          = user_data;
	sr->buff          = buf;
	sr->count         = count;
	sr->datatype      = MPIR_Type_dup(datatype);
	sr->src_lrank     = src_lrank;
	sr->tag           = tag;
	sr->context_id    = context_id;
	sr->sreq          = sreq;
	sr->dest_grank    = sreq->dest_grank;

	sreq->my_sp    = sr;
	
	if (enqueue_tcp_send(sr))
	{
	    *error_code = MPI_ERR_INTERN;
	}
	else
	{
	    *error_code = 0;
	} /* endif */
    }
    else
    {
	DEBUG_PRINTF(DEBUG_MODULE_SEND, DEBUG_INFO_FAILURE,
		     ("ERROR - proc %d selected proto for dest %d "
		      "has unrecognizable proto type %d\n",
		      MPID_MyWorldRank,
		      dest_grank,
		      proto));
#	if DEBUG_CHECK(DEBUG_MODULE_SEND, DEBUG_INFO_FAILURE)
	{
	    print_channels();
	}
#	endif
	
        *error_code = MPI_ERR_INTERN;
    } /* endif */

  fn_exit:
    DEBUG_FN_EXIT(DEBUG_MODULE_SEND);

} /* end MPID_IssendDatatype() */

#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME MPID_SendComplete
void MPID_SendComplete(MPI_Request request, int *error_code)
{
    int done;

    DEBUG_FN_ENTRY(DEBUG_MODULE_SEND);

    do
    {
	done = MPID_SendIcomplete(request, error_code);
    } 
    while (*error_code == 0 && !done);
    
    DEBUG_FN_EXIT(DEBUG_MODULE_SEND);
} /* end MPID_SendComplete() */


#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME MPID_SendIcomplete
int MPID_SendIcomplete(MPI_Request request, int *error_code)
{
    MPIR_SHANDLE *sreq = (MPIR_SHANDLE *) request;
    int rc;

    DEBUG_FN_ENTRY(DEBUG_MODULE_SEND);

    rc = sreq->is_complete;

    if (rc)
    {
	goto fn_exit;
    }

#  if defined(VMPI)
    {
	/*
	 * If we sent the message using MPI, then check MPI for the status of
	 * our request
	 */
	if (sreq->req_src_proto == mpi)
	{
	    MPI_Status status;
	    
	    /* normally relaxed RC semantics would require a lock here to
               acquire the shared sreq data, but we already did an acquire
               above */
	    *error_code = vmpi_error_to_mpich_error(
		mp_test(sreq->vmpi_req,
			&rc,
			STATUS_INFO_GET_VMPI_PTR(status)));

	    if (rc)
	    {
		/* if the send has completed then let MPICH know */
		sreq->is_complete = GLOBUS_TRUE;

		goto fn_exit;
	    } /* endif */

	}
    }
#   endif


    /* give all protos that are waiting for something a nudge */
    MPID_DeviceCheck(MPID_NOTBLOCKING);

    /* all protos tried ... tabulate results */
    rc = sreq->is_complete;

    *error_code = 0;

  fn_exit:
    DEBUG_PRINTF(DEBUG_MODULE_SEND, DEBUG_INFO_RC,
		 ("rc=%d\n", rc));
    DEBUG_FN_EXIT(DEBUG_MODULE_SEND);
    return rc;

} /* end MPID_SendIcomplete() */

#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME MPID_SendCancel
void MPID_SendCancel(MPI_Request request, int *error_code )
{
    MPIR_SHANDLE *sreq = (MPIR_SHANDLE *) request;

    DEBUG_FN_ENTRY(DEBUG_MODULE_SEND);

    sreq->cancel_issued = GLOBUS_TRUE;
    
    if (sreq->req_src_proto == tcp)
    {
	struct tcpsendreq *sr = sreq->my_sp;

	if (!sr || sr->write_started)
	{
	    /* data already sent or currently being sent */
	    /* need to enqueue 'cancel' node */

	    sreq->cancel_complete = sreq->is_cancelled = GLOBUS_FALSE;
	    if (enqueue_cancel_tcp_data(sreq))
		*error_code = MPI_ERR_INTERN;
	    else
		*error_code = 0;
	}
	else
	{
	    /* data not sent yet, must remove from queue */
	    struct channel_t *cp;

	    if ((cp = get_channel(sreq->dest_grank)) != NULL)
	    {
		struct tcp_miproto_t *tp = (struct tcp_miproto_t *) 
		    (cp->selected_proto)->info;

		(sr->prev)->next = sr->next;
		if (sr->next)
		    (sr->next)->prev = sr->prev;
		else
		    tp->send_tail = sr->prev;
		TcpOutstandingSendReqs --;

		if (sr->src != sr->buff)
		    g_free((void *) (sr->src));
		MPIR_Type_free(&(sr->datatype));
		g_free((void *) sr);
		sreq->my_sp = (struct tcpsendreq *) NULL;

		sreq->is_complete = 
		    sreq->cancel_complete = 
		    sreq->is_cancelled    = GLOBUS_TRUE;
		sreq->s.MPI_TAG = MPIR_MSG_CANCELLED;

		*error_code = 0;
	    }
	    else
	    {
		globus_libc_fprintf(stderr, 
		    "ERROR: MPID_SendCancel(): failed get_channel() "
		    "for grank %d\n",  
		    sreq->dest_grank);
		print_channels();
		*error_code = MPI_ERR_INTERN;
	    } /* endif */

	} /* endif */
    }
#   if defined(VMPI)
    else if (sreq->req_src_proto == mpi)
    {
	*error_code = vmpi_error_to_mpich_error(mp_cancel(sreq->vmpi_req));
    }
#   endif
    else
    {
	DEBUG_PRINTF(DEBUG_MODULE_SEND, DEBUG_INFO_FAILURE,
    ("INTERNAL ERROR - MPID_SendCancel encountered invalid req_src_proto %d\n", 
	    sreq->req_src_proto));
	*error_code = MPI_ERR_INTERN;
	goto fn_exit;
    } /* endif */

    /* 
     * need to unconditionally set active=flase with persistent send,
     * independent of is_cancelled because MPI_Waitall will only set
     * status->MPI_TAG = MPIR_MSG_CANCELLED under the condition 
     * that active == false.
     */
    /* if (sreq->is_cancelled && sreq->handle_type == MPIR_PERSISTENT_SEND) */
    if (sreq->handle_type == MPIR_PERSISTENT_SEND)
	((MPIR_PSHANDLE *) sreq)->active = GLOBUS_FALSE;

  fn_exit:
    DEBUG_FN_EXIT(DEBUG_MODULE_SEND);

} /* end MPID_SendCancel() */

/*
 * returns 1 iff cancel send was successfully, waits here if necessary
 */
#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME MPID_SendRequestCancelled
int MPID_SendRequestCancelled(MPI_Request request)
{
    MPIR_SHANDLE *sreq = (MPIR_SHANDLE *) request;
    int rc;

    DEBUG_FN_ENTRY(DEBUG_MODULE_SEND);

    if (sreq->s.MPI_TAG == MPIR_MSG_CANCELLED)
	rc = 1;
    else if (sreq->req_src_proto == tcp)
    {
	if (sreq->cancel_issued)
	{
	    if (sreq->cancel_complete)
		rc = sreq->is_cancelled;
	    else
	    {
		/* 
		 * NICK: hate to have to do this here, but ...  
		 *
		 * calling MPID_SendComplete here is a clear violation
		 * of the MPI standard which states that MPI_Cancel
		 * should not be a blocking operation.  unfortunately,
		 * the way the mpich layer has implemented MPI_Wait
		 * forces us to know the result of a cancel send request
		 * at the time MPI_Wait is called.  this forces to 
		 * wait here for the answer.
		 */
		int error_code;

		MPID_SendComplete(request, &error_code);
		rc = sreq->is_cancelled;
	    } /* endif */
	}
	else
	    /* a cancel was never issued on this req */
	    rc = 0;
    }
#   if defined(VMPI)
    else if (sreq->req_src_proto == mpi)
    {
	if (sreq->cancel_issued)
	{
	    MPI_Status status;

	    mp_wait(sreq->vmpi_req, STATUS_INFO_GET_VMPI_PTR(status));
	    mp_test_cancelled(STATUS_INFO_GET_VMPI_PTR(status), &rc);
	    if (rc)
	    {
		sreq->s.MPI_TAG = MPIR_MSG_CANCELLED;
	    }
	}
	else
	{
	    /* a cancel was never issued on this req */
	    rc = 0;
	}
    }
#   endif
    else
    {
	DEBUG_PRINTF(DEBUG_MODULE_SEND, 
		    DEBUG_INFO_FAILURE,
		    ("INTERNAL ERROR - MPID_SendRequestCancelled encountered "
		    "invalid req_src_proto %d\n", 
			sreq->req_src_proto));
	rc = 0;
    } /* endif */

  /* fn_exit: */
    DEBUG_FN_EXIT(DEBUG_MODULE_SEND);

    return rc;

} /* end MPID_SendRequestCancelled() */

/***************************/
/* Local Utility Functions */
/***************************/

static int proto_from_valid_send(void *buf, 
				int count, 
				struct MPIR_DATATYPE *datatype, 
				int dest_grank)
{
    int rc;
    struct channel_t *cp;

     /* Make sure the send is valid */
    if (buf == NULL && count > 0 && datatype->is_contig)
        rc = -1;
    else if (!(cp = get_channel(dest_grank)))
    {
        globus_libc_fprintf(stderr,
	    "ERROR: proto_from_valid_send: proc %d: failed get_channel "
	    "grank %d\n",
            MPID_MyWorldRank, dest_grank); 
        print_channels();
	rc = -1;
    }
    else if (!(cp->selected_proto))
    {
        globus_libc_fprintf(stderr,
	    "ERROR: proto_from_valid_send: proc %d does not have "
	    "selected proto for dest %d\n",
            MPID_MyWorldRank, dest_grank); 
        print_channels();
	rc = -1;
    } 
    else
	rc = (cp->selected_proto)->type;

    return rc;

} /* end proto_from_valid_send */

static int enqueue_cancel_tcp_data(MPIR_SHANDLE *sreq)
{
    int rc;
    struct channel_t *cp;

    if ((cp = get_channel(sreq->dest_grank)) != NULL)
    {
	struct tcpsendreq *sr;
	struct tcp_miproto_t *tp = (struct tcp_miproto_t *) 
				    (cp->selected_proto)->info;

	g_malloc(sr, struct tcpsendreq *, sizeof(struct tcpsendreq));
	sr->next = (struct tcpsendreq *) NULL;
	sr->sreq = sreq;

	if (tp->cancel_tail)
	{
	    /* there are other cancels before me */
	    (tp->cancel_tail)->next = sr;
	    sr->prev = tp->cancel_tail;
	    tp->cancel_tail = sr;
	    TcpOutstandingSendReqs ++;
	    rc = 0;
	}
	else
	{
	    /* there were no other cancels before me*/
	    sr->prev = (struct tcpsendreq *) NULL;
	    tp->cancel_head = tp->cancel_tail = sr;
	    TcpOutstandingSendReqs ++;

	    if (tp->send_head)
		/* there is a data send in progress */
		rc = 0;
	    else
		/* there are no data sends in progress, start cancel now */
		rc = write_all_tcp_cancels(tp);
	} /* endif */
    }
    else
    {
        globus_libc_fprintf(stderr,
	    "ERROR: enqueue_cancel_tcp_data: proc %d: failed get_channel "
	    "grank %d\n",
            MPID_MyWorldRank, sreq->dest_grank); 
        print_channels();
	rc = -1;
    } /* endif */

    return rc;

} /* end enqueue_cancel_tcp_data() */

/*
 * it has been determined that the tcp_miproto_t pointed at by 'tp'
 * has outstanding cancel requests AND that it is now time to 
 * send them all out in succession.
 *
 * this function returns 0 if ALL writes went OK, otherwise returns -1
 */
static int write_all_tcp_cancels(struct tcp_miproto_t *tp)
{
    enum header_type type = cancel_send;
    struct tcpsendreq *sr;
    MPIR_SHANDLE *sreq;
    globus_byte_t *cp;
    globus_size_t nbytes_sent;
    int rc;

    /* NICK: do i need an RC mutex here to flush volatile value of handlep? */
    if (!(tp->whandle))
    {
	/* the only reason we should have to send a cancel message */
	/* is because it has already gone out the door ... which means */
	/* that the line should already be primed.  something terribly */
	/* wrong has happened if we get to this point ... print err    */
	/* message and abort.                                          */
	globus_libc_fprintf(stderr, 
	    "ERROR: write_all_tcp_cancels: detected NULL tp->whandle, "
	    "should have already been primed\n");
	return -1;
    }  /* endif */

    rc = 0;
    while (tp->cancel_head)
    {
	sr   = tp->cancel_head;
	sreq = sr->sreq;
	cp   = tp->header;

	/* packing header =
	 * type = cancel_send,
	 *       msgid_src_commworld_id,msgid_src_commworld_displ,
	 *       msgid_sec,msgid_usec,msgid_ctr,liba
	 */
	globus_dc_put_int(&cp,    &type,              1);
	globus_dc_put_char(&cp,                           /* msgid stuff */
			sreq->msg_id_commworld_id,  
			COMMWORLDCHANNELSNAMELEN); 
	globus_dc_put_int(&cp,                            /* msgid stuff */
			&sreq->msg_id_commworld_displ,  
			1); 
	globus_dc_put_long(&cp,   &sreq->msg_id_sec,  1); /* msgid stuff */
	globus_dc_put_long(&cp,   &sreq->msg_id_usec, 1); /* msgid stuff */
	globus_dc_put_u_long(&cp, &sreq->msg_id_ctr,  1); /* msgid stuff */
	memcpy((void *) cp, &sreq, sizeof(MPIR_SHANDLE *));

	/* sending header */
	if (globus_io_write(&(((struct tcp_rw_handle_t *)(tp->handlep))
				 ->handle),
			    tp->header,
			    Headerlen,
			    &nbytes_sent) != GLOBUS_SUCCESS)
	{
	    globus_libc_fprintf(stderr,
		"ERROR: write_all_tcp_cancels: write header failed\n"); 
	    rc = -1;
	} 
	else
	{
	    TcpOutstandingSendReqs --;
	    TcpOutstandingRecvReqs ++;
	} /* endif */
/* globus_libc_fprintf(stderr, "NICK: %d: write_all_tcp_cancels: after write header cwid %s cwdisp %d msgid_sec %ld msgid_usec %ld msgid_ctr %ld\n", MPID_MyWorldRank, sreq->msg_id_commworld_id, sreq->msg_id_commworld_displ, sreq->msg_id_sec, sreq->msg_id_usec, sreq->msg_id_ctr);  */

	/* removing and continuing */
	if ((tp->cancel_head = sr->next) != NULL)
	    (tp->cancel_head)->prev = (struct tcpsendreq *) NULL;
	else
	    tp->cancel_tail = (struct tcpsendreq *) NULL;
	g_free((void *) sr);
    } /* endwhile */

    return rc;

} /* end write_all_tcp_cancels() */

/*
 * called by anyone doing TCP communication.  places 'sr' on the 
 * end of this channel's queue, and if this is the only 'sr' on 
 * that queue, starts the TCP write.
 *
 */
int enqueue_tcp_send(struct tcpsendreq *sr)
{
    struct channel_t *cp;
    int rc;

/* globus_libc_fprintf(stderr, "NICK: %d: enter enqueue_tcp_send: dest_grank %d\n", MPID_MyWorldRank, sr->dest_grank); */
    if ((cp = get_channel(sr->dest_grank)) != NULL)
    {
	struct tcp_miproto_t *tp = (struct tcp_miproto_t *) 
	    (cp->selected_proto)->info;

	sr->write_started = GLOBUS_FALSE;
	sr->next          = (struct tcpsendreq *) NULL;
	sr->src           = (globus_byte_t *) NULL;
/* globus_libc_fprintf(stderr, "NICK: %d: enter enqueue_tcp_send: tp->send_tail %x tp->cancel_head %x\n", MPID_MyWorldRank, tp->send_tail, tp->cancel_head); */
	
	if (tp->send_tail)
	{
	    /* 
	     * this tcp channel has prior unfinished sends.  place this
	     * one at the end of the queue.
	     */
	     sr->prev = tp->send_tail;
	     (tp->send_tail)->next = sr;
	     tp->send_tail = sr;
	     rc = 0;
	}
	else
	{
	    /* no other tcp sends before me on this channel. */
	    sr->prev = (struct tcpsendreq *) NULL;
	    tp->send_head = tp->send_tail = sr;
	    if (!(tp->cancel_head))
		/* no other TCP activity going on right now, start the write */
		rc = start_tcp_send(sr);
	    else
		/* there are some cancel requests ahead of us, they must */
		/* always be processed first                             */
		rc = 0;
	} /* endif */
	TcpOutstandingSendReqs ++;
    }
    else
    {
        globus_libc_fprintf(stderr,
	    "ERROR: enqueue_tcp_send: proc %d: failed get_channel grank %d\n",
            MPID_MyWorldRank, sr->dest_grank); 
        print_channels();
	rc = -1;
    } /* endif */

    return rc;

} /* end enqueue_tcp_send() */

/*
 * it is assumed that upon entrance to this function:
 *    - 'sr' is sitting at the head of it's 'my_tp' send queue
 *
 * NOTE: there is one more datatype found in datatype.h ... MPIR_FORT_INT
 *       it has been explained to me by bill that we do not have to
 *       support an explicit case for that type because it is a
 *       synonym for one of the other types we already have a case
 *       statement for (which type it is a synonym for is architecture 
 *       dependent and determined during mpich configuration).
 */
static int start_tcp_send(struct tcpsendreq *sr)
{
    MPIR_SHANDLE *sreq    = sr->sreq;
    int dest_grank        = sr->dest_grank;
    enum header_type type = user_data;
    globus_byte_t *cp;
    struct channel_t *chp;
    struct tcp_miproto_t *tp;
    int bufflen;
    int ssend_flag;
    int packed_flag;
    globus_size_t nbytes_sent;
    int rc = 0;

    /* NICK: do i need an RC mutex here to flush volatile value of handlep? */
    if (!(chp = get_channel(dest_grank)))
    {
	globus_libc_fprintf(stderr, 
	    "ERROR: enqueue_tcp_send: proc %d: failed get_channel grank %d\n",
	    MPID_MyWorldRank, dest_grank);
	print_channels();
	remove_and_continue(sr);
	if (sr->type == user_data)
	{
	    free_and_mark_sreq(sr, GLOBUS_FALSE);
	}
	else
	{
	    g_free(sr->liba);
	    g_free(sr);
	}
	MPID_Abort(NULL,
		   0,
		   "MPICH-G2 (internal error)",
		   "start_tcp_send()");
    } /* endif */

    tp = (chp->selected_proto)->info;
    cp = tp->header;

    if (!(tp->whandle))
    {
	/* should only have to be done once */
	prime_the_line(tp, dest_grank);

	if (!(tp->whandle))
	{
	    globus_libc_fprintf(stderr, 
		"ERROR: start_tcp_send: proc %d: dest_grank %d: "
		"after call to prime_the_line tp->whandle is still NULL\n", 
		MPID_MyWorldRank, dest_grank);
	    print_channels();
	    remove_and_continue(sr);
	    if (sr->type == user_data)
	    {
		free_and_mark_sreq(sr, GLOBUS_FALSE);
	    }
	    else
	    {
		g_free(sr->liba);
		g_free(sr);
	    } /* endif */
	    MPID_Abort(NULL, 
			0, 
			"MPICH-G2 (internal error)",
		       "start_tcp_send()");
	} /* endif */
    } /* endif */

    switch(sr->type)
    {
      case cancel_result:
	{
	    globus_result_t rc2;
	    
            globus_dc_put_int(&cp,    &sr->type,       1);
            globus_dc_put_int(&cp,    &sr->result,     1);
            globus_dc_put_char(&cp,   
			    sr->msgid_commworld_id, 
			    COMMWORLDCHANNELSNAMELEN);
            globus_dc_put_int(&cp,    &sr->msgid_commworld_displ, 1);
            globus_dc_put_long(&cp,   &sr->msgid_sec,  1);
            globus_dc_put_long(&cp,   &sr->msgid_usec, 1);
            globus_dc_put_u_long(&cp, &sr->msgid_ctr,  1);
            memcpy((void *) cp, sr->liba, sr->libasize); 

            /* sending header */
	    rc2 =
		globus_io_write(
		    &(((struct tcp_rw_handle_t *)(tp->handlep))->handle), 
		    tp->header, 
		    Headerlen, 
		    &nbytes_sent);
/* globus_libc_fprintf(stderr, "NICK: %d: start_tcp_send: cancel_result: after write header result %d cwid %s cwdisp %d msgid_sec %ld msgid_usec %ld msgid_ctr %ld\n", MPID_MyWorldRank, sr->result, sr->msgid_commworld_id, sr->msgid_commworld_displ, sr->msgid_sec, sr->msgid_usec, sr->msgid_ctr);  */
	    
	    remove_and_continue(sr);
	    g_free(sr->liba);
	    g_free(sr);
	    
            if (rc2 != GLOBUS_SUCCESS)
            {
                globus_libc_fprintf(stderr,
                    "ERROR: send_cancel_result_over_tcp: "
		    "write header failed\n"); 
		rc = -1;
		goto fn_exit;
            } /* endif */
	}
      break;
      
      case ack:
	{
	    globus_result_t rc2;
	    
	    globus_dc_put_int(&cp, &sr->type, 1);
	    memcpy((void *) cp, sr->liba, sr->libasize); 

	    /* sending header */
	    rc2 =
		globus_io_write(
		    &(((struct tcp_rw_handle_t *)(tp->handlep))->handle), 
		    tp->header, 
		    Headerlen, 
		    &nbytes_sent);

	    remove_and_continue(sr);
	    g_free(sr->liba);
	    g_free(sr);

	    if (rc2 != GLOBUS_SUCCESS)
	    {
		globus_libc_fprintf(stderr,
		    "ERROR: start_tcp_send: write header failed\n"); 
		rc = -1;
		goto fn_exit;
	    } /* endif */
	}
      break;

      case user_data:
	{
	    /* calculating bufflen */
	    if ((bufflen = local_size(sr->count, sr->datatype)) < 0)
	    {
		globus_libc_fprintf(stderr,
				    "ERROR: start_tcp_send: rcvd "
				    "invalid %d from local_size\n", 
				    bufflen);
		remove_and_continue(sr);
		free_and_mark_sreq(sr, GLOBUS_FALSE);
		rc = 1;
		goto fn_exit;
	    } /* endif */

	    /* 
	     * packing header = type==user_data,
	     *       src,tag,contextid,dataoriginbuffsize,
	     *       ssend_flag,packed_flag,
	     *       msgid_src_commworld_id,msgid_src_commworld_displ,
	     *       msgid_sec,msgid_usec,msgid_ctr,liba
	     */
	    ssend_flag  = (sreq->needs_ack ? GLOBUS_TRUE : GLOBUS_FALSE);
	    packed_flag = ((sr->datatype->dte_type == MPIR_PACKED)
			   ? GLOBUS_TRUE : GLOBUS_FALSE);
	    globus_dc_put_int(&cp,    &type,              1);
	    globus_dc_put_int(&cp,    &(sr->src_lrank),   1);
	    globus_dc_put_int(&cp,    &(sr->tag),         1);
	    globus_dc_put_int(&cp,    &(sr->context_id),  1);
	    globus_dc_put_int(&cp,    &bufflen,           1);
	    globus_dc_put_int(&cp,    &ssend_flag,        1);
	    globus_dc_put_int(&cp,    &packed_flag,       1);
	    globus_dc_put_char(&cp,                           /* msgid stuff */
			    sreq->msg_id_commworld_id,  
			    COMMWORLDCHANNELSNAMELEN); 
	    globus_dc_put_int(&cp,                            /* msgid stuff */
			    &sreq->msg_id_commworld_displ,  
			    1); 
	    globus_dc_put_long(&cp,   &sreq->msg_id_sec,  1); /* msgid stuff */
	    globus_dc_put_long(&cp,   &sreq->msg_id_usec, 1); /* msgid stuff */
	    globus_dc_put_u_long(&cp, &sreq->msg_id_ctr,  1); /* msgid stuff */
	    memcpy(cp, &sreq, sizeof(MPIR_SHANDLE *));
	    if (sreq->needs_ack) 
	    {
		TcpOutstandingRecvReqs ++;
	    } /* endif */
    
	    /* sending header */
	    if (globus_io_write(tp->whandle, 
				tp->header, 
				Headerlen, 
				&nbytes_sent) != GLOBUS_SUCCESS)
	    {
		globus_libc_fprintf(stderr,
				   "ERROR: start_tcp_send: "
				   "write header failed\n"); 
		remove_and_continue(sr);
		free_and_mark_sreq(sr, GLOBUS_FALSE);
		rc = -1;
		goto fn_exit;
	    } /* endif */
/* globus_libc_fprintf(stderr, "NICK: %d: start_tcp_send: user_data: after write header src_lrank %d tag %d context %d cwid %s cwdisp %d msgid_sec %ld msgid_usec %ld msgid_ctr %ld\n", MPID_MyWorldRank, sr->src_lrank, sr->tag, sr->context_id, sreq->msg_id_commworld_id, sreq->msg_id_commworld_displ, sreq->msg_id_sec, sreq->msg_id_usec, sreq->msg_id_ctr);  */
    
	    sr->write_started = GLOBUS_TRUE;

	    if (bufflen) /* only send data if there is a payload */
	    {
		/* now the payload */

		/* basic types may send directly from user buff, */
		/* must pack complex types                       */
		switch ((sr->datatype)->dte_type)
		{
		    /* basic datatypes may be sent directly from user buffer */
		  case MPIR_CHAR:    case MPIR_UCHAR:       case MPIR_PACKED: 
		  case MPIR_BYTE:    case MPIR_SHORT:       case MPIR_USHORT: 
		  case MPIR_LOGICAL: case MPIR_INT:         case MPIR_UINT:   
		  case MPIR_LONG:    case MPIR_LONGLONGINT: case MPIR_ULONG:      
		  case MPIR_FLOAT:   case MPIR_DOUBLE:     case MPIR_LONGDOUBLE:
		  case MPIR_UB:      case MPIR_LB:          case MPIR_COMPLEX:    
		  case MPIR_DOUBLE_COMPLEX: 
		    sr->src = (globus_byte_t *) sr->buff; break;

		    /* complex data types, need to malloc'd and packed */
		  case MPIR_CONTIG:  case MPIR_VECTOR:   case MPIR_HVECTOR:
		  case MPIR_INDEXED: case MPIR_HINDEXED: case MPIR_STRUCT:
		    {
			int position   = 0;
			int error_code = 0;

			g_malloc(sr->src, globus_byte_t *, bufflen);

			mpich_globus2_pack_data(sr->buff,
						sr->count,
						sr->datatype,
						(void *) (sr->src),
						&position,
						&error_code);
			if (error_code)
			{
			    globus_libc_fprintf(stderr,
					    "ERROR: start_tcp_send: "
					   "could not pack complex datatype\n");
			    rc = -1;
			    goto fn_exit;
			} /* endif */
		    }
		  break;
		  default:
		    globus_libc_fprintf(stderr,
				    "ERROR: start_tcp_send: "
				   "encountered unrecognizable data type %d\n",
				   (sr->datatype)->dte_type); 
		    rc = -1;
		    goto fn_exit;
		    break;
		} /* end switch() */

                /* START GRIDFTP */
                if (tp->use_grid_ftp)
                {
                    g_ftp_user_args_t ua;
                    globus_result_t res;

                    g_ftp_monitor_reset(&(tp->write_monitor));

                    ua.monitor                = &(tp->write_monitor);
                    ua.buffer                 = sr->src;
                    ua.nbytes                 = bufflen;
                    /* START NEWGRIDFTP */
                    /* ua.max_outstanding_writes = tp->max_outstanding_writes; */
                    ua.gftp_tcp_buffsize = tp->gftp_tcp_buffsize;
                    /* END NEWGRIDFTP */
                    /****************/
                    /* WRITE BUFFER */
                    /****************/

                    /*
                     * this can be used over and over again ...
                     * it simply opens connections and deploys callback when
                     * ready to go
                     */
                    res = globus_ftp_control_data_connect_write(
                            &(tp->ftp_handle_w),
                            gridftp_setup_sockets_callback,
                            &ua);
                    if (res != GLOBUS_SUCCESS)
                    {
                        globus_libc_fprintf(
                            stderr,
                            "ERROR: start_tcp_send: "
                            "register gridftp write payload %d failed\n",
                            bufflen);
                        remove_and_continue(sr);
                        free_and_mark_sreq(sr, GLOBUS_FALSE);
                        rc = -1;
                        goto fn_exit;
                    } /* endif */

		    while (!(tp->write_monitor.done))
		    {
			G2_WAIT
		    } /* endwhile */

                    remove_and_continue(sr);
                    free_and_mark_sreq(sr, GLOBUS_TRUE);
                }
                else
                /* END GRIDFTP */

		if (globus_io_register_write(tp->whandle, 
					     sr->src, 
					     (globus_size_t) bufflen, 
					     write_callback,
					     (void *) sr) != GLOBUS_SUCCESS)
		{
		    globus_libc_fprintf(stderr, 
			"ERROR: start_tcp_send: "
			"register write payload %d failed (nbytes_sent=%ld)\n",
			bufflen, 
			nbytes_sent); 
		    remove_and_continue(sr);
		    free_and_mark_sreq(sr, GLOBUS_FALSE);
		    rc = -1;
		    goto fn_exit;
		} /* endif */
	    } 
	    else
	    {
		/* 
		 * empty payload, this message is done. mark it complete,
		 * remove it from queue, and move on to the next (if there
		 * are any more).
		 */
		remove_and_continue(sr);
		free_and_mark_sreq(sr, GLOBUS_TRUE);
	    } /* endif */
	} /* esac user_data */
      break;

      /* START GRIDFTP */
      case gridftp_port:
        {
            globus_result_t rc2;

            globus_dc_put_int(&cp, &sr->type,                    1);
            globus_dc_put_int(&cp, &sr->gridftp_partner_grank,   1);
            globus_dc_put_int(&cp, &sr->gridftp_port,            1);

            /* sending header */
            rc2 = globus_io_write(
                    &(((struct tcp_rw_handle_t *)(tp->handlep))->handle),
                    tp->header,
                    Headerlen,
                    &nbytes_sent);
            remove_and_continue(sr);
            g_free(sr);

            if (rc2 != GLOBUS_SUCCESS)
            {
                globus_libc_fprintf(stderr,
                    "ERROR: start_tcp_send: write header failed\n");
                rc = -1;
                goto fn_exit;
            } /* endif */
        }
      break;
      /* END GRIDFTP */

      case cancel_send: break; /* here only to get rid of 
                                  annoying compiler warning */
      
    } /* end switch */

  fn_exit:
    return rc;

} /* end start_tcp_send() */

/* 
 * NICK THREAD: check thread-safety of globus_error_get, 
 * globus_object_printable_to_string, globus_libc_printf
 */
/* called by G2_POLL when previously registered write has completed */
static void write_callback(void *arg, 
			    globus_io_handle_t *handle, 
			    globus_result_t result, 
			    globus_byte_t *buff, 
			    globus_size_t nbytes)
{
    struct tcpsendreq *sr = (struct tcpsendreq *) arg;

    if (result != GLOBUS_SUCCESS)
    {
	globus_object_t * err;
	char * errstring;

	err = globus_error_get(result);
	errstring = globus_object_printable_to_string(err);

	globus_libc_fprintf(stderr, "ERROR(%d): write_callback: "
			    "write payload failed: %s\n",
			   MPID_MyWorldRank,
			   errstring);
	MPID_Abort(NULL,
		   0,
		   "MPICH-G2 (internal error)",
		   "write_callback()");
    } /* endif */
    
    remove_and_continue(sr);

    /*
     * NICK THREAD: free_and_mark_sreq is not necessarily thread-safe
     * because it decrements shared dataype counters without locking
     * anything.  it may suffice to simply put the call to 
     * free_and_mark_sreq inside the mutex lock above, but then 
     * we need to check that deadlock is not possible because
     * free_and_mark_sreq acquires sreq->lock before setting stuff.
     * one easy solution might be to remove sreq->lock and rely
     * on the lock above.
     */
    free_and_mark_sreq(sr, GLOBUS_TRUE);

} /* end write_callback */

/* START GRIDFTP */
/*
 * this gets called when sockets are all setup and 
 * you're ready to start writing.
 */
static void gridftp_setup_sockets_callback(void *callback_arg,
                                struct globus_ftp_control_handle_s *handle,
                                unsigned int stripe_ndx,
                                globus_bool_t reuse,
                                globus_object_t *error)
{
    g_ftp_perf_monitor_t                        monitor;
    int                                         nsent;
    globus_bool_t                               eof;
    globus_result_t                             res;
    g_ftp_user_args_t *ua = (g_ftp_user_args_t *) callback_arg;
    g_ftp_perf_monitor_t *done_monitor = ua->monitor;
    globus_byte_t *next_write_start;
    int bytes_per_write;

    g_ftp_monitor_init(&monitor);
 
/* START NEWGRIDFTP */
#if 0
    if ((bytes_per_write = ua->nbytes/ua->max_outstanding_writes) < 1)
        bytes_per_write = 1;
#else
    bytes_per_write = ua->gftp_tcp_buffsize;
#endif 
/* END NEWGRIDFTP */

    nsent = 0;
    next_write_start = ua->buffer;
    eof = GLOBUS_FALSE;
    while (!eof)
    {
        if (nsent + bytes_per_write >= ua->nbytes)
        {
            eof = GLOBUS_TRUE;
            bytes_per_write = ua->nbytes - nsent;
        } /* endif */

        res = globus_ftp_control_data_write(handle, /* same ftp_handle */
                                  next_write_start,    /* buff */
                                  bytes_per_write,     /* buffsize */
                                  nsent,               /* offset into payload */
                                  eof,                 /* flag */
                                  gridftp_write_callback,
                                  (void *) &monitor);  /* user arg */
        /* test_result(res, "connect_write_callback:data_write", __LINE__); */

        next_write_start += bytes_per_write;
        nsent += bytes_per_write;
        monitor.count++;
    } /* endwhile */

    /* wait for all the callbacks to return */
    while(monitor.count != 0)
    {
	G2_WAIT
    } /* endwhile */

    /* signalling that write of entire payload is complete */
    done_monitor->done = GLOBUS_TRUE;
    G2_SIGNAL

} /* end gridftp_setup_sockets_callback() */

static void gridftp_write_callback(void *callback_arg,
                                    globus_ftp_control_handle_t *handle,
                                    globus_object_t *error,
                                    globus_byte_t *buffer,
                                    globus_size_t length,
                                    globus_off_t offset,
                                    globus_bool_t eof)
{
    g_ftp_perf_monitor_t *                   monitor;

    monitor = (g_ftp_perf_monitor_t *)callback_arg;

    if(error != GLOBUS_NULL)
    {
        assert(GLOBUS_FALSE);
    } /* endif */

    monitor->count--;
    G2_SIGNAL

} /* end gridftp_write_callback() */

void g_ftp_monitor_init(g_ftp_perf_monitor_t *monitor)
{
    g_ftp_monitor_reset(monitor);
} /* end g_ftp_monitor_init() */

void g_ftp_monitor_reset(g_ftp_perf_monitor_t *monitor)
{
    monitor->done = GLOBUS_FALSE;
    monitor->count = 0;
} /* end g_ftp_monitor_reset() */

/* END GRIDFTP */


/*
 * it is assumed that upon entrance to this function:
 *    - 'sr' is sitting at the head of it's 'my_tp' send queue
 *
 * called when tcp send has completed.  removes it from the head of 
 * it's my_tp queue and if there are more in the queue, starts the next one
 */
static void remove_and_continue(struct tcpsendreq *sr)
{
    struct channel_t *cp;
    struct tcp_miproto_t *tp;
    
    if (!(cp = get_channel(sr->dest_grank)))
    {
	globus_libc_fprintf(stderr, 
	    "ERROR: remove_and_continue: proc %d: failed "
	    "get_channel grank %d\n",
	    MPID_MyWorldRank, sr->dest_grank);
	print_channels();
	MPI_Abort(MPI_COMM_WORLD, 1);
    } /* endif */

    tp = (struct tcp_miproto_t *) (cp->selected_proto)->info;

    /* 
     * removing this sr from tp's list (better be at the head of the list)
     * and if there are others, starting the next one.
     */
    if (tp->send_head != sr)
    {
	globus_libc_fprintf(stderr, "FATAL ERROR: remove_and_continue: "
	    "called with sr not at head of queue\n");
	MPI_Abort(MPI_COMM_WORLD, 1);
    } /* endif */

    if (sr->type == user_data)
    {
	(sr->sreq)->my_sp = (struct tcpsendreq *) NULL;
    }

    TcpOutstandingSendReqs --;
    if ((tp->send_head = sr->next) != NULL)
    {
	(tp->send_head)->prev = (struct tcpsendreq *) NULL;
    }
    else
    {
	tp->send_tail = (struct tcpsendreq *) NULL;
    } /* endif */
    
    if (tp->cancel_head)
	write_all_tcp_cancels(tp);

    if (tp->send_head)
	start_tcp_send(tp->send_head);
} /* end remove_and_continue() */

static void free_and_mark_sreq(struct tcpsendreq *sr, globus_bool_t data_sent)
{
    MPIR_SHANDLE *sreq = sr->sreq;
    globus_bool_t free_sreq = GLOBUS_FALSE;

    if (sr->src != sr->buff)
    {
	g_free((void *) (sr->src));
    }
    MPIR_Type_free(&(sr->datatype));
    g_free((void *) sr);

    sreq->data_sent = data_sent;
    if (sreq->cancel_issued)
    {
	sreq->is_complete = sreq->cancel_complete;
    }
    
    else if (sreq->data_sent)
    {
	sreq->is_complete = !(sreq->needs_ack) | sreq->ack_arrived;
    }

    if (sreq->is_complete && ((MPI_Request) sreq)->chandle.ref_count <= 0)
    {
	free_sreq = GLOBUS_TRUE;
    }

    if (free_sreq)
    {
	/* an orphaned req that we have to free ourselves right here */
	MPID_SendFree(sreq);
    }
}
/* end free_and_mark_sreq() */

/*
 * send_datatype
 */
#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME send_datatype
static void send_datatype(
    struct MPIR_COMMUNICATOR *		comm,
    void *				buf,
    int					count,
    struct MPIR_DATATYPE *		datatype,
    int					src_lrank,
    int					tag,
    int					context_id,
    int					dest_grank,
    int *				error_code)
{
    /* allocate req to pass to MPID_IsendDatatype ... */
    /* code copied from mpich/src/pt2pt/isend.c       */

    MPI_Request request;
    MPIR_SHANDLE *shandle;

    DEBUG_FN_ENTRY(DEBUG_MODULE_SEND);
    
    MPID_SendAlloc(shandle);
    if (!shandle)
    {
	DEBUG_PRINTF(DEBUG_MODULE_SEND, DEBUG_INFO_FAILURE,
		     ("ERROR - could not malloc shandle\n"));
	*error_code = MPI_ERR_INTERN;
	goto fn_exit;
    } /* endif */
    MPID_Request_init(shandle, MPIR_SEND);
    request = (MPI_Request) shandle;

    MPID_IsendDatatype(comm,
		       buf,
		       count,
		       datatype,
		       src_lrank,
		       tag,
		       context_id,
		       dest_grank,
		       request,
		       error_code);

    if (*error_code == 0) /* everything still ok */
    {
	MPID_SendComplete(request, error_code);
    }

    MPID_SendFree(shandle);
    
  fn_exit:
    DEBUG_FN_EXIT(DEBUG_MODULE_SEND);

} /* end send_datatype() */
    
/*
 * ssend_datatype
 */
#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME ssend_datatype
static void ssend_datatype(
    struct MPIR_COMMUNICATOR *		comm,
    void *				buf,
    int					count,
    struct MPIR_DATATYPE *		datatype,
    int					src_lrank,
    int					tag,
    int					context_id,
    int					dest_grank,
    int *				error_code)
{
    /* allocate req to pass to MPID_IssendDatatype ... */
    /* code copied from mpich/src/pt2pt/issend.c       */

    MPI_Request request;
    MPIR_SHANDLE *shandle;

    DEBUG_FN_ENTRY(DEBUG_MODULE_SEND);
    
    MPID_SendAlloc(shandle);
    if (!shandle)
    {
	DEBUG_PRINTF(DEBUG_MODULE_SEND, DEBUG_INFO_FAILURE,
		     ("ERROR - could not malloc shandle\n"));
	*error_code = MPI_ERR_INTERN;
	goto fn_exit;
    } /* endif */
    MPID_Request_init(shandle, MPIR_SEND);
    request = (MPI_Request) shandle;

    MPID_IssendDatatype(comm,
		       buf,
		       count,
		       datatype,
		       src_lrank,
		       tag,
		       context_id,
		       dest_grank,
		       request,
		       error_code);

    if (*error_code == 0) /* everything still ok */
    {
	MPID_SendComplete(request, error_code);
    }

    MPID_SendFree(shandle);
    
  fn_exit:
    DEBUG_FN_EXIT(DEBUG_MODULE_SEND);

} /* end ssend_datatype() */

static void get_unique_msg_id(long *sec, long *usec, unsigned long *ctr)
{
    *sec  = LastTimeILookedAtMyWatch.tv_sec;
    *usec = LastTimeILookedAtMyWatch.tv_usec;
    *ctr  = NextMsgIdCtr ++;

    if (!NextMsgIdCtr)
    {
	/* counter rolled over, need to look at my watch again */
	if (gettimeofday(&LastTimeILookedAtMyWatch, (void *) NULL))
	{
	    MPID_Abort(NULL,
		       0,
		       "MPICH-G2 (internal error)",
		       "get_unique_msg_id() - get_unique_msg_id: "
                       "failed gettimeofday()");
	} /* endif */
    } /* endif */

} /* end get_unique_msg_id() */
