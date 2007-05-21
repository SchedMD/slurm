#ifndef _P4_DEFS_H_
#define _P4_DEFS_H_

/* The -n32 version of the SGI compiler complained about not making the
   public/global values explicitly extern.  So we're using the same
   approach used in p4_global.h .
 */
#ifndef PUBLIC
#ifdef GLOBAL
#define PUBLIC
#else
#define PUBLIC extern
#endif
#endif

struct proc_info {
    int port;
    int switch_port;
    int unix_id;
    int slave_idx;
    int group_id;
    P4BOOL am_rm;
    /* The host_name is the name used for connections.  */
    char host_name[HOSTNAME_LEN];
    /* local_name is the name that the machine knows itself by.  This
       is the same as host_name unless the machine has multiple networks
     */
    char local_name[HOSTNAME_LEN];
#   ifdef CAN_DO_SOCKET_MSGS
    struct sockaddr_in sockaddr;
#ifdef LAZY_GETHOSTBYNAME
    int sockaddr_setup;     /* Used to keep track of lazy initialization
			       of the sockaddr fields */
#endif
#   endif
    char machine_type[16];
};

#define NUMAVAILS 8

struct p4_avail_buff {
    int size;			/* size of message portion */
    struct p4_msg *buff;
};

/*
 * This structure is shared among all processes that share memory on 
 * a single node.  If comm=shared is selected, note that the proctable
 * is SHARED among the local processes.
 */
struct p4_global_data {
#   ifdef SYSV_IPC
    int sysv_num_semids;
    int sysv_semid[P4_MAX_SYSV_SEMIDS];
    int sysv_next_lock;
#   endif
    struct proc_info proctable[P4_MAXPROCS];
    int listener_pid;
    int listener_port;
    P4BOOL local_communication_only;
    int local_slave_count;
    int n_forked_pids;
    /* my_host_name is the name that the system knows itself by */
    char my_host_name[HOSTNAME_LEN];
    struct p4_avail_buff avail_buffs[NUMAVAILS];
    p4_lock_t avail_buffs_lock;
    struct p4_queued_msg *avail_quel;
    p4_lock_t avail_quel_lock;
    struct p4_msg_queue shmem_msg_queues[P4_MAX_MSG_QUEUES];
    int num_in_proctable;
    int num_installed;
    p4_lock_t slave_lock;
    int dest_id[P4_MAXPROCS];
    int listener_fd;
    int max_connections;
    int cube_msgs_out;    /* i860 msgs not yet msgwait'ed on */
    unsigned long reference_time;  /* used in p4_initenv and p4_clock */
    int hi_cluster_id;
    int low_cluster_id;
    P4VOID *cluster_shmem;
    p4_barrier_monitor_t cluster_barrier;
    char application_id[16];
};
PUBLIC struct p4_global_data *p4_global;

struct connection {
    int type;
    int port;
    int switch_port;
    P4BOOL same_data_rep;
};

struct local_data {		/* local to each process */
    int listener_fd;
    int my_id;
#ifdef P4_WITH_MPD
    int my_job;			/* specific to mpd */
    int parent_man_fd;		/* specific to mpd */
#endif
#ifdef THREAD_LISTENER
    /* This lock is used to coordinate access to the conntab between the
       main and listener threads in a process */
    /* pthread_mutex_t conntab_lock; */
    /* We found ways to avoid it, but left it in as a comment in case we
       need it later */
#endif
    int local_commtype;		/* cube or shmem messages */
    struct p4_msg_queue *queued_messages;
    P4BOOL am_bm;
    struct connection *conntab;	/* pointer to array of connections */
    struct p4_procgroup *procgroup;
    int soft_errors;            /* false if errors cause termination */
#ifdef CAN_DO_XDR
    char *xdr_buff;
    XDR xdr_enc;
    XDR xdr_dec;
#endif
    int  in_wait_for_exit;  /* true if in p4_wait_for_exit */
};
PUBLIC struct local_data *p4_local;

struct listener_data {
    int listening_fd;
    int num;  /* of slaves, including big or remote master */
    int *slave_pid;
    int *slave_fd;

};
PUBLIC struct listener_data *listener_info;

/* this struct is similar to a p4_net_msg_hdr;  note that the sum of
   the sizes of the items up to the *msg is equal to some number of 
   double words, which is important on machines like bfly2 if you 
   receive doubles into the msg area.
*/
/* link, orig_len, and pad are for the buffer itself*/
/* next fields are for the current message in the buffer */
struct p4_msg {
    struct p4_msg *link;
    int orig_len;
    int type;                
    int to;
    int from;
    int ack_req;
    int len;
    int msg_id;		        /* for i860 messages */
    int data_type;		/* for use by xdr */
    int pad;
    char *msg;	/* variable length array of characters */
};

struct p4_net_msg_hdr {
    int msg_type:32;
    int to:32;
    int from:32;
    int ack_req:32;
    int msg_len:32;
    int msg_id:32;		/* for i860 messages */
    int data_type:32;		/* for use by xdr */
    int imm_from:32;            /* may differ from "from" in brdcst */
    /*  int pad:32;  */                 /* pad field to word boundary */
};

struct net_initial_handshake {
   int pid:32;
   int rm_num:32;
   /* int pad:32; */
};

struct p4_queued_msg {
    struct p4_msg *qmsg;
    struct p4_queued_msg *next;
};


/* Messages between a listener and any other non-listener */

#define DIE   1
#define SLAVE_DYING   2     /* Unused.  Check for whole data struct. */
#define CONNECTION_REQUEST   3
#define IGNORE_THIS   4
#define KILL_SLAVE   5
#define WAKEUP_SLAVE 6

struct slave_listener_msg {
    int type:32;
    int from:32;
    int to:32;
    int to_pid:32;
    int lport:32;
    int pad:32;
#ifdef P4_WITH_MPD
    char hostname[64];
#endif
};

/* Messages between the bm and a rm at startup */

#define INITIAL_INFO            11
#define REMOTE_LISTENER_INFO    12
#define REMOTE_SLAVE_INFO       13
#define REMOTE_MASTER_INFO      14
#define REMOTE_SLAVE_INFO_END   15
#define PROC_TABLE_ENTRY        16
#define PROC_TABLE_END          17
#define SYNC_MSG                18

#ifndef P4_MAX_PGM_LEN 
#define P4_MAX_PGM_LEN 1024
#endif
struct bm_rm_msg {
    int type:32;

    /* for INITIAL_INFO */
    int numslaves:32;
    int numinproctab:32;
    int memsize:32;
    int rm_num:32;
    int debug_level:32;
    int logging_flag:32;

    /* for REMOTE_LISTENER_INFO */
    int port:32;

    /* for REMOTE_SLAVE_INFO and REMOTE_MASTER_INFO */
    int slave_idx:32;
    int slave_pid:32;
    int am_rm:32;

    /* for PROC_TABLE_ENTRY */
    int unix_id:32;
    int group_id:32;
    int switch_port:32;
    /* int pad:32;  to keep number of 32 bit quantities even */
    char host_name[HOSTNAME_LEN];
    char local_name[HOSTNAME_LEN];

    /* also for INITIAL INFO */
    char pgm[P4_MAX_PGM_LEN];
    char wdir[P4_MAX_PGM_LEN];
    char version[8];
    char outfile[P4_MAX_PGM_LEN];
    char application_id[16];
    char machine_type[16];
};

#define P4_ACK_REQ_MASK   1     /* Masks define bits set for requests */
#define P4_ACK_REPLY_MASK 2
#define P4_BROADCAST_MASK 4
#define P4_CLOSE_MASK     8

struct p4_brdcst_info_struct {
/*
  This structure is initialized by init_p4_brdcst_info() which
  is automatically called by every global operation
*/
  int initialized;             /* True if structure is initialized */
  int up;                      /* Process above me in tree         */
  int left_cluster;            /* Id of left child cluster master  */
  int right_cluster;           /* Id of right child cluster master */
  int left_slave;              /* Id of left child slave           */
  int right_slave;             /* Id of right child slave          */
};
PUBLIC struct p4_brdcst_info_struct p4_brdcst_info;

/* This is used to control error behavior.  Use with extreme care; p4_error
   aborts programs and this allows some uses to not call p4_error.  
 */
/* We make this extern so that we can initialize it in p4_error.c */
extern int p4_hard_errors;
#endif
