                                          /*#NONGET_START#*/
#ifdef MPID_ADI_MUST_SENDSELF
/****************************************************************************
  MPID_CH_Post_send_local

  Description
    Some low-level devices do not support sending a message to yourself.  
    This function notifies the soft layer that a message has arrived,
    then copies the body of the message the dmpi handle.  Currently,
    we post (copy) the sent message directly to the unexpected message
    queue or the expected message queue.

  This code was taken from mpid/t3d/t3dsend.c 

  This code is relatively untested.  If the matching receive has not
  been posted, it copies the message rather than defering the copy.
  This may cause problems for some rendevous-based implementations.
 ***************************************************************************/
int MPID_CH_Post_send_local( dmpi_send_handle, mpid_send_handle, len )
MPIR_SHANDLE *dmpi_send_handle;
MPID_SHANDLE *mpid_send_handle;
int           len;
{
    MPIR_RHANDLE    *dmpi_recv_handle;
    int              is_posted;
    int              err = MPI_SUCCESS;

    DEBUG_PRINT_MSG("S Send to self")

    MPID_Msg_arrived( dmpi_send_handle->lrank, dmpi_send_handle->tag, 
                      dmpi_send_handle->contextid, 
                      &dmpi_recv_handle, &is_posted );

    if (is_posted) {

        dmpi_recv_handle->totallen = len;
        
        /* Copy message if needed and mark the receive as completed */
        if (len > 0) 
            MEMCPY( dmpi_recv_handle->dev_rhandle.start, 
                    dmpi_send_handle->dev_shandle.start,
                    len ); 
        DMPI_mark_recv_completed(dmpi_recv_handle);

	/* Mark the send as completed. */
	DMPI_mark_send_completed( dmpi_send_handle );

        return (err);
    }
    else {

        MPID_RHANDLE *mpid_recv_handle;
        char         *address;
        
        /* initialize mpid handle */
        mpid_recv_handle                  = &dmpi_recv_handle->dev_rhandle;
        mpid_recv_handle->bytes_as_contig = len;
        mpid_recv_handle->mode            = 0;   
        /* This could be -1 to indicate message from self */
        mpid_recv_handle->from            = MPID_MyWorldRank; 
        
        /* copy the message */
        if (len > 0) {
            mpid_recv_handle->temp = (char *)MALLOC(len);
            if ( ! mpid_recv_handle->temp ) {
		(*MPID_ErrorHandler)( 1, 
			 "No more memory for storing unexpected messages"  );
			     return MPI_ERR_EXHAUSTED; 
		}
            MEMCPY( mpid_recv_handle->temp, 
                    dmpi_send_handle->dev_shandle.start, 
                    len );
        }
        DMPI_mark_recv_completed(dmpi_recv_handle);

	/* Mark the send as completed. */
	DMPI_mark_send_completed( dmpi_send_handle );

        return (err);
    }

} /* MPID_CH_Post_send_local */
#endif
