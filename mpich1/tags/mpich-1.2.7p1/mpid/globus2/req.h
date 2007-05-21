#ifndef MPIR_REQUEST_COOKIE

#include "mpi2.h"
#include "datatype.h"

/*
 * Modified definitions of the request.  The "device" handle has been 
 * integrated.
 * 
 * Should consider separating persistent from non-persistent requests.
 * Note that much of the information a send request is not needed
 * once the send envelope is sent, unless it is a persistent request.
 * This includes the contextid, dest, tag, and datatype.  The comm
 * might be needed for error handling, but maybe not.
 *
 * If persistent is a handle_type, we save another store.
 */

/* MPIR_COMMON is a subset of the handle structures that contains JUST
   the handle type and the Cookie.
 */
/* User not yet supported */
typedef enum {
    MPIR_SEND,
    MPIR_RECV,
    MPIR_PERSISTENT_SEND,
    MPIR_PERSISTENT_RECV /*,
    MPIR_USER */
} MPIR_OPTYPE;

#define MPIR_REQUEST_COOKIE 0xe0a1beaf
typedef struct {
    MPIR_OPTYPE handle_type;
    MPIR_COOKIE                 /* Cookie to help detect valid item */
    int is_complete;
    int self_index;             /* Used when mapping to/from indices */
    int ref_count;              /* Used to handle freed (by user) but
				   not complete */
    } MPIR_COMMON;

/*
   In the case of a send handle, the information is provided separately.
   All we need is enough information to dispatch the message and to 
   deliver the data (in a rendezvous/get setting).  Some sophisticated
   devices may be able to use an MPI_Datatype directly; they should
   add an MPI_Datatype field if they need it.
 */
typedef struct _MPIR_SHANDLE MPIR_SHANDLE;
struct _MPIR_SHANDLE {
    MPIR_OPTYPE  handle_type;    
    MPIR_COOKIE                   /* Cookie to help detect valid item */
    volatile int is_complete;     /* Indicates if complete */
    int          self_index;      /* Used when mapping to/from indices */
    int          ref_count;       /* Used to handle freed (by user) but
				     not complete */
    int          is_cancelled;    /* Indicates message is cancelled */
    int          cancel_complete; /* Indicates when cancel call has 
				     completed */
    int          partner;         /* Holds rank of process - used to 
				     cancel isend requests */
    int          errval;          /* Holds any error code; 0 for none */
    MPI_Comm     comm;            /* Do we need this?  */
    MPI_Status   s;               /* Status of data */

    /* Device data */
    /* START GLOBUS DEVICE */
    /* NOTE: definition of is_complete is as follows
     * if (cancel_issued)
     *    is_complete = cancel_complete
     * else
     *    is_complete = [(!needs_ack | ack_arrived) && data_sent]
     * endif
     */
    globus_bool_t  needs_ack;
    globus_bool_t  ack_arrived;
    globus_bool_t  data_sent;
    int            req_src_proto;
    int            dest_grank;
    /* start msg id stuff for cancelling TCP messages */
    globus_bool_t cancel_issued;
    char msg_id_commworld_id[COMMWORLDCHANNELSNAMELEN];
    int msg_id_commworld_displ;
    long msg_id_sec;
    long msg_id_usec;
    unsigned long msg_id_ctr;
    /* end msg id stuff for cancelling TCP messages */
#if VENDOR_MPI_REQUEST_SIZE > 0
    globus_byte_t  vmpi_req[VENDOR_MPI_REQUEST_SIZE];
#endif
    struct tcpsendreq *my_sp; 
#ifdef VMPI
    struct mpircvreq *my_mp; /* not used here ... just placed here so 
				that MPID_Request_init will compile */
#endif
    /* END   GLOBUS DEVICE */
    int           is_non_blocking;
        /* The following describes the buffer to be sent.  This may 
	   be ignored, depending on the device.
	 */
    void          *start;
    int           bytes_as_contig;
        /* Rest of data */
    ASYNCSendId_t sid;              /* Id of non-blocking send, if used.
				       0 if no non-blocking send used, 
				       or if non-blocking send has 
				       completed */
    MPID_RNDV_T   recv_handle;      /* Holds 'transfer' handle for RNDV 
				       operations */

    /* New fields are the functions to call */
    /* Is test a field or a function ? */
    int (*test)   (MPIR_SHANDLE *);
    int (*push)   (MPIR_SHANDLE *);
    int (*wait)   (MPIR_SHANDLE *);
    int (*cancel) (MPIR_SHANDLE *);
    int (*finish) (MPIR_SHANDLE *);
};

/* 
 * A receive request is VERY different from a send.  We need to 
 * keep the information about the message we want in the request,
 * as well as how to accept it.
 */
typedef struct _MPIR_RHANDLE MPIR_RHANDLE;
struct _MPIR_RHANDLE {
    MPIR_OPTYPE  handle_type;    
    MPIR_COOKIE                /* Cookie to help detect valid item */
    volatile int is_complete;  /* Indicates is complete */
    int          self_index;   /* Used when mapping to/from indices */
    int          ref_count;    /* Used to handle freed (by user) but
				  not complete */
    MPI_Status   s;            /* Status of data */
    int          contextid;    /* context id -- why? */
    void         *buf;         /* address of buffer */
    int          len;          /* length of buffer at bufadd in bytes */
    int          partner;      /* Holds rank of process - used for 
				  rendevous unexpected messages */

    /* Device data */
    int           is_non_blocking;
    /* START GLOBUS DEVICE */
    int            src_format;
    int		   packed_flag;
    int            needs_ack;
    int            req_src_proto;
    int            req_count;
    int            libasize; /* of remote side ... not 8 bytes of next field */
    char           liba[8]; /* LocallyInterpretedByteArray, addr on send side 
                               ... hardcoding 8 bytes for as workaround for 
			       32- and 64-bit runs.  Correct solution is 
			       to maintain tables of LIBAs on all machines
			       and pass around fix-sized (2byte?) indexes
			       into tables ... will be done in enxt release. */
#ifdef VMPI
    int               req_rank;
    int               req_tag;
    int               req_context_id;
    struct mpircvreq *my_mp;
#endif
    /* start msg id stuff for cancelling TCP messages, */
    /* values all come from message origination side   */
    int msg_id_src_grank; /* src rank w.r.t. MPI_COMM_WORLD */
    char msg_id_commworld_id[COMMWORLDCHANNELSNAMELEN];
    int msg_id_commworld_displ;
    long msg_id_sec;
    long msg_id_usec;
    unsigned long msg_id_ctr;
    /* end msg id stuff for cancelling TCP messages */
    /* END GLOBUS DEVICE */
        /* Rest of data */
    ASYNCRecvId_t rid;              /* Id of non-blocking recv, if used.
				       0 if no non-blocking recv used.*/
    MPID_Aint     send_id;          /* Used for rendevous send; this is
				       needed when the incoming message
				       is unexpected. */
    MPID_RNDV_T   recv_handle;      /* Holds 'transfer' handle for RNDV 
				       operations */
    char          *unex_buf;        /* Holds body of unexpected message */
    int           from;             /* Absolute process number that sent
				       message; used only in rendevous 
				       messages */


        /* The following describes the user buffer to be received */
    void         *start;
    int          bytes_as_contig;
    int          count;
    struct MPIR_DATATYPE *datatype;   /* basic or derived datatype */
    struct MPIR_COMMUNICATOR *comm;
    MPID_Msgrep_t msgrep;     /* Message representation; used to indicate
				XDR, sender, receiver used */

    /* New fields are the functions to call */
    /* Is test a field or a function ? */
    int (*test)   (MPIR_RHANDLE *);
    /* Push is called to advance the completion; the second arg may be
       a pkt or, in the case of an unexpected receive, the saved request.
       In the unexpected case, the saved request may already be complete */
    int (*push)   (MPIR_RHANDLE *, void *);
    /* Status is saved in the request */
    int (*wait)   (MPIR_RHANDLE *);
    int (*cancel) (MPIR_RHANDLE *);
    int (*finish) (MPIR_RHANDLE *);
};

typedef struct {
    MPIR_RHANDLE rhandle;
    int          active;
    int          perm_tag, perm_source, perm_count;
    void         *perm_buf;
    struct MPIR_DATATYPE *perm_datatype;
    struct MPIR_COMMUNICATOR *perm_comm;
    } MPIR_PRHANDLE;

typedef struct {
    MPIR_SHANDLE shandle;
    int          active;
    int          perm_tag, perm_dest, perm_count;
    void         *perm_buf;
    struct MPIR_DATATYPE *perm_datatype;
    struct MPIR_COMMUNICATOR *perm_comm;
    void         (*send) (struct MPIR_COMMUNICATOR *, void *, int, 
				    struct MPIR_DATATYPE *,
				    int, int, int, int, MPI_Request, int *);
                    /* IsendDatatype, IssendDatatype, Ibsend, IrsendDatatype */
    } MPIR_PSHANDLE;
	
/* This is an "extension" handle and is NOT part of the MPI standard.
   Defining it, however, introduces no problems with the standard, and
   it allows us to easily extent the request types.

   Note that this is not yet compatible with the essential fields of
   MPIR_COMMON.
 */
typedef struct {
    MPIR_OPTYPE handle_type;    
    MPIR_COOKIE                 /* Cookie to help detect valid item */
    int         is_complete;    /* Is request complete? */
    int         self_index;     /* Used when mapping to/from indices */
    int         ref_count;      /* Used to handle freed (by user) but
				   not complete */
    int         active;         /* Should this be ignored? */
    int         (*create_ureq) (MPI_Request);
    int         (*free_ureq)   (MPI_Request);
    int         (*wait_ureq)   (MPI_Request);
    int         (*test_ureq)   (MPI_Request);
    int         (*start_ureq)  (MPI_Request);
    int         (*cancel_ureq) (MPI_Request);
    void        *private_data;
} MPIR_UHANDLE;

#define MPIR_HANDLES_DEFINED

union MPIR_HANDLE {
    MPIR_OPTYPE   handle_type;   
    MPIR_COMMON   chandle;       /* common fields */
    MPIR_SHANDLE  shandle;
    MPIR_RHANDLE  rhandle;
    MPIR_PSHANDLE persistent_shandle;
    MPIR_PRHANDLE persistent_rhandle;
    MPIR_UHANDLE  uhandle;
};

#ifdef STDC_HEADERS
/* Prototype for memset() */
#include <string.h>
#elif defined(HAVE_STRING_H)
#include <string.h>
#elif defined(HAVE_MEMORY_H)
#include <memory.h>
#endif

#ifdef VMPI
#ifdef DEBUG_INIT_MEM
#define MPID_Request_init( ptr, in_type ) { \
		      memset(ptr,0,sizeof(*(ptr)));\
		      (ptr)->handle_type = in_type;\
                      (ptr)->ref_count = 1;\
		      MPIR_SET_COOKIE((ptr),MPIR_REQUEST_COOKIE);\
		      if ((in_type) == MPIR_RECV)\
		      {\
			(ptr)->my_mp = (struct mpircvreq *) GLOBUS_NULL; \
		      }\
		      }
#else
/* For now, turn on deliberate garbaging of memory to catch problems */
/* We should do this, but I want to get the release out */
/* 		      memset(ptr,0xfc,sizeof(*(ptr))); */
#define MPID_Request_init( ptr, in_type ) { \
		      memset(ptr,0,sizeof(*(ptr)));\
		      (ptr)->handle_type = in_type;\
                      (ptr)->ref_count = 1;\
		      MPIR_SET_COOKIE((ptr),MPIR_REQUEST_COOKIE);\
		      if ((in_type) == MPIR_RECV)\
		      {\
			(ptr)->my_mp = (struct mpircvreq *) GLOBUS_NULL; \
		      }\
		      }
#endif

#else /* VMPI */
#ifdef DEBUG_INIT_MEM
#define MPID_Request_init( ptr, in_type ) { \
		      memset(ptr,0,sizeof(*(ptr)));\
		      (ptr)->handle_type = in_type;\
                      (ptr)->ref_count = 1;\
		      MPIR_SET_COOKIE((ptr),MPIR_REQUEST_COOKIE);\
		      }
#else
/* For now, turn on deliberate garbaging of memory to catch problems */
/* We should do this, but I want to get the release out */
/* 		      memset(ptr,0xfc,sizeof(*(ptr))); */
#define MPID_Request_init( ptr, in_type ) { \
		      memset(ptr,0,sizeof(*(ptr)));\
		      (ptr)->handle_type = in_type;\
                      (ptr)->ref_count = 1;\
		      MPIR_SET_COOKIE((ptr),MPIR_REQUEST_COOKIE);\
		      }
#endif
#endif /* VMPI */

/* These have been added to provide a procedural interface to these 
   fields; the Globus device implements these in a different way */
/* #define MPID_SendRequestCancelled(r) (r)->shandle.s.MPI_TAG == MPIR_MSG_CANCELLED */
#define MPID_SendRequestErrval(r) (r)->s.MPI_ERROR

#endif
