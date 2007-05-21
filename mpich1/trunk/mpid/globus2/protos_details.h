#ifndef __globdev_protos_details__
#define __globdev_protos_details__

/*******************/
/* TCP proto stuff */
/*******************/

enum header_type {user_data, ack, cancel_send, cancel_result,
		    gridftp_port}; /* GRIDFTP */

/* header =
 * type==user_data,src,tag,contextid,dataoriginbuffsize,ssendflag,packed_flag,
 *       msgid_src_commworld_id(COMMWORLDCHANNELSNAMELEN),
 *       msgid_src_commworld_displ(int),msgid_sec(long),msgid_usec(long),
 *       msgid_ctr(ulong),liba(ulong)
 * OR 
 * type==ack, liba(ulong) 
 */
#define TCP_HDR_N_INTS   8
#define TCP_HDR_N_LONGS  2
#define TCP_HDR_N_ULONGS 2
#define TCP_HDR_N_CHARS  COMMWORLDCHANNELSNAMELEN
#define LOCAL_HEADER_LEN (globus_dc_sizeof_int(TCP_HDR_N_INTS) + \
    globus_dc_sizeof_long(TCP_HDR_N_LONGS)                     + \
    globus_dc_sizeof_u_long(TCP_HDR_N_ULONGS)                  + \
    globus_dc_sizeof_char(TCP_HDR_N_CHARS))
#define REMOTE_HEADER_LEN(format)                                \
    (globus_dc_sizeof_remote_int(TCP_HDR_N_INTS, (format))     + \
    globus_dc_sizeof_remote_long(TCP_HDR_N_LONGS, (format))    + \
    globus_dc_sizeof_remote_u_long(TCP_HDR_N_ULONGS, (format)) + \
    globus_dc_sizeof_remote_char(TCP_HDR_N_CHARS, (format)))

struct tcpsendreq
{
    struct tcpsendreq *prev;
    struct tcpsendreq *next;
    enum header_type  type;
    globus_bool_t write_started; /* used only for data, not for cancel */
    void *buff;
    globus_byte_t *src;
    int count;
    struct MPIR_DATATYPE *datatype;
    int src_lrank;
    int tag;
    int context_id;
    int result;
    int dest_grank;
    char msgid_commworld_id[COMMWORLDCHANNELSNAMELEN];
    int msgid_commworld_displ;
    long msgid_sec;
    long msgid_usec;
    unsigned long msgid_ctr;
    void * liba;
    int libasize;
    MPIR_SHANDLE *sreq;
    int gridftp_port;          /* GRIDFTP */
    int gridftp_partner_grank; /* GRIDFTP */
}; 

/* 
 * INSTRUCTIONBUFFLEN must be large enough to hold 
 * 2 chars + <commworldID, displ>  
 */
#define INSTRUCTIONBUFFLEN (2+COMMWORLDCHANNELSNAMELEN+HEADERLEN)

/* instructions */
#define FORMAT 'F'
#define PRIME  'P'
enum tcp_read_state {await_instructions,await_format,await_header,await_data};
struct tcp_rw_handle_t
{
    globus_io_handle_t     handle;
    enum tcp_read_state    state;
    globus_byte_t          instruction_buff[INSTRUCTIONBUFFLEN]; /* handshake */
    volatile globus_bool_t recvd_format;                         /* handshake */
    globus_byte_t          remote_format;
    globus_byte_t          *incoming_header;
    globus_size_t          incoming_header_len;
    void *                 liba;
    int                    libasize;
    int                    src;
    int                    tag;
    int                    context_id;
    int                    dataorigin_bufflen;
    int                    ssend_flag;
    int                    packed_flag;
    globus_byte_t          *incoming_raw_data;
    char msg_id_src_commworld_id[COMMWORLDCHANNELSNAMELEN]; /* message id */
    int msg_id_src_commworld_displ;                         /* message id */
    int msg_id_src_grank;                                   /* message id */
    long msg_id_sec;                                        /* message id */
    long msg_id_usec;                                       /* message id */
    unsigned long msg_id_ctr;                               /* message id */
};

/* START GRIDFTP */
typedef struct g_ftp_perf_monitor_s
{
    globus_bool_t done;
    int           count;
} g_ftp_perf_monitor_t;

typedef struct g_ftp_user_args_s
{
    g_ftp_perf_monitor_t                *monitor;
    globus_ftp_control_handle_t *       ftp_handle_r;
    globus_byte_t *                     buffer;
    int                                 nbytes; /* buffer length */
    int                                 gftp_tcp_buffsize;
} g_ftp_user_args_t;
/* END GRIDFTP */

struct tcp_miproto_t
{
    char                            hostname[G2_MAXHOSTNAMELEN];
    unsigned short                  port;
    globus_io_attr_t                attr;
    volatile struct tcp_rw_handle_t *handlep;

    /* 
     * 'to_self' used only when send/rcv to myself 
     * and TCP is the selected proto to myself.
     */
    struct tcp_rw_handle_t          to_self; 

    /* 
     * most of the time 'whandle' will point to &(handlep->handle).
     * where handlep is malloc'd during connection establishment.  
     * there is one case in which whandle will _not_ point there, when 
     * a proc connects to itself and TCP is the selected proto to itself.
     * in this case we need 2 distinct handles, so whandle = &(to_self.handle) 
     * (all reads will still be done using &(handlep->handle).
     */
    globus_io_handle_t              *whandle; 

    /*
     * buffer space for constructing message headers
     */
    globus_byte_t *		    header;
    
    struct tcpsendreq *cancel_head;
    struct tcpsendreq *cancel_tail;
    struct tcpsendreq *send_head;
    struct tcpsendreq *send_tail;

    /* Different levels for TCP: WAN-TCP > LAN-TCP > localhost-TCP */
    char *globus_lan_id;
    int localhost_id;

    /* START GRIDFTP */
    globus_bool_t 		recvd_partner_port;
    globus_bool_t 		use_grid_ftp;
    int 			partner_port;
    int 			gftp_tcp_buffsize;
    globus_ftp_control_handle_t ftp_handle_r;
    globus_ftp_control_handle_t ftp_handle_w;
    g_ftp_perf_monitor_t        read_monitor;
    g_ftp_perf_monitor_t 	write_monitor;
    /* END GRIDFTP */
};

/*******************/
/* MPI proto stuff */
/*******************/

struct mpi_miproto_t
{
    char unique_session_string[G2_MAXHOSTNAMELEN+32];
    int  rank;
};

#endif
