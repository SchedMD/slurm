#define MPID_FLOW_MEM_OK(len,dest_grank) \
     (! MPID_BUF_READY_IS_SET(&(MPID_destready[dest_grank].buf_ready)))
