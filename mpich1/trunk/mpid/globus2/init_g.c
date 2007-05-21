/* On IRIX, INET6 must be defined so that struct sockaddr_in6 gets defined
   by the IRIX header files. */
#if !defined(INET6)
#   define INET6
#endif

#include "chconfig.h"
#include "globus_duroc_runtime.h"
#include "globus_duroc_bootstrap.h"
#include "globus_gram_myjob.h"
#include "globus_gram_client.h"

#include <strings.h>   /* for index */
#include <sys/time.h> /* for gettimeofday() */

/* allow the user to access the underlying topology */
#include "topology_access.h"

extern int MPICHX_PARALLELSOCKETS_PARAMETERS; /* GRIDFTP */

#include "globdev.h"
#include "reqalloc.h"
#include "sendq.h"
#include "queue.h"

/* Include files to discover the appropriate network interface */
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#if defined(HAVE_SYS_IOCTL_H)
#   include <sys/ioctl.h>
#endif
#if defined(HAVE_SYS_SOCKIO_H)
#   include <sys/sockio.h>
#endif
#include <errno.h>

#define MPIDPATCHLEVEL 2.0
#define MPIDTRANSPORT "globus"
#define MPICH_GLOBUS2_IFREQ_ALLOC_COUNT 10

/* inter- and intra-subjob message tags */
#define SUBJOB_MASTER_TO_SUBJOB0_MASTER_T "subjob mstr to subjob0 mstr topology"
#define SUBJOB0_MASTER_TO_SUBJOB_MASTER_T "subjob0 mstr to subjob mstr topology"
#define SUBJOB_MASTER_TO_SLAVE_T          "subjob mstr to slave topology"

#define SUBJOB_SLAVE_TO_MASTER_D          "subjob slave to master data"
#define SUBJOB_MASTER_TO_SUBJOB_MASTER_D  "subjob master to subjob master data"
#define SUBJOB_MASTER_TO_SLAVE_D          "subjob master to slave data"

/********************/
/* Global Variables */
/********************/

#ifdef GLOBUS_CALLBACK_GLOBAL_SPACE
globus_callback_space_t MpichG2Space;    
#endif

extern volatile int TcpOutstandingRecvReqs;
extern volatile int TcpOutstandingSendReqs;
extern int          MpichGlobus2TcpBufsz;
static struct channel_t *CommworldChannels;
static globus_byte_t *MyGlobusGramJobContact;
static globus_byte_t **GramJobcontactsVector;
/* 
 * for unique msg id's (used in conjunction with MPID_MyWorldRank) 
 * the NextMsgIdCtr must have at least enough bits so as not to rollover     
 * within the resolution of our clock (in our case usecs).  also, every time 
 * NextMsgIdCtr rolls over we have to call gettimeofday, so the more bits it
 * has, the fewer times we have to make that expensive system call.          
 */
struct timeval LastTimeILookedAtMyWatch;
unsigned long NextMsgIdCtr;

#if defined(VMPI)
extern struct mpi_posted_queue MpiPostedQueue;

int   VMPI_MyWorldSize     = 0;
int   VMPI_MyWorldRank     = -1;
int  *VMPI_VGRank_to_GRank = NULL;
int  *VMPI_GRank_to_VGRank = NULL;
void *VMPI_Internal_Comm   = NULL;
#endif

/* TCP proto Global Variables */
globus_size_t      Headerlen;
globus_io_handle_t Handle; 

/* Home for these globals ... required of all mpich devices */
int MPID_MyWorldSize, MPID_MyWorldRank;
int MPID_Print_queues = 0;
MPID_SBHeader MPIR_rhandles;
MPID_SBHeader MPIR_shandles;
int MPID_IS_HETERO = GLOBUS_FALSE;

/**************************/
/* begin MPI-2 extensions */
/**************************/

int CommworldChannelsTableSize = 0;
int CommworldChannelsTableNcommWorlds = 0;
struct commworldchannels *CommWorldChannelsTable = 
					    (struct commworldchannels *) NULL;

/************************/
/* end MPI-2 extensions */
/************************/

/***************************************/
/* local utility function declarations */
/***************************************/

static int globus_init(int *argc, char ***argv);
static void create_my_miproto(globus_byte_t **my_miproto, int *nbytes);
static void build_vmpi_maps();
static void free_vmpi_maps();

static void get_topology(int rank_in_my_subjob, 
			int my_subjob_size, 
			int **subjob_addresses,
			int *nprocs, 
			int *nsubjobs, 
			int *my_grank);

static void distribute_byte_array(globus_byte_t *inbuff,
				int inbufflen,
				int rank_in_my_subjob,
				int my_subjob_size,
				int *subjob_addresses,
				int nprocs,
				int nsubjobs,
				int my_grank,
				globus_byte_t **outbuffs,
				int *outbufflens);

#if !defined(VMPI)
static void intra_subjob_send(int dest, char *tag_base, int nbytes,char *buff);
static void intra_subjob_receive(char *tag_base, int *rcvd_nbytes,char **buff);
#endif

static void extract_byte_arrays(char *rbuff, 
				int *nbuffs_p,  /* optional */
				globus_byte_t **outbuffs, 
				int *outbufflens);

#if !defined(VMPI)
static void intra_subjob_bcast(int rank_in_my_subjob, 
				int my_subjob_size, 
				char *tag_base, 
				int *rcvd_nbytes, 
				char **buff);
static void intra_subjob_gather(int rank_in_my_subjob,
				int my_subjob_size,
				char *inbuff,
				int inbufflen,
				char *tag_base, 
				int *rcvd_nbytes, /* subjob master only */
				char **buff);     /* subjob master only */
#endif
static void print_CommWorldChannelsTable_row(struct commworldchannels *cp);

void MPID_Init(int *argc, char ***argv, void *config, int *error_code)
{
    /* required of all mpich devices */
    MPIR_shandles = MPID_SBinit(sizeof(MPIR_PSHANDLE), 100, 100);
    MPIR_rhandles = MPID_SBinit(sizeof(MPIR_PRHANDLE), 100, 100);
    MPID_InitQueue();

    *error_code = 0;
    if (globus_init(argc, argv))
    {
	*error_code = MPI_ERR_INTERN;
	globus_libc_fprintf(stderr, "ERROR: MPID_Init: failed globus_init()\n");
	goto fn_exit;
    } /* endif */

    /* Initialization for generating unique message id's */
    if (gettimeofday(&LastTimeILookedAtMyWatch, (void *) NULL))
    {
	*error_code = MPI_ERR_INTERN;
	globus_libc_fprintf(stderr, 
	    "ERROR: MPID_Init: failed gettimeofday()\n");
    } /* endif */
    NextMsgIdCtr = 0;

    /*
     * Call the vendor implementation of MPI_Init().  See pr_mp_g.c for a
     * discussion on startup and shutdown constraints.
     */
#   if defined(VMPI)
    {
	if (mp_init(argc, argv))
	{
	    *error_code = MPI_ERR_INTERN;
	    globus_libc_fprintf(stderr, 
		"ERROR: MPID_Init: failed mp_init()\n");
	    goto fn_exit;
	} /* endif */

	VMPI_Internal_Comm = (void *) globus_libc_malloc(mp_comm_get_size());
	if (VMPI_Internal_Comm == NULL)
	{
	    *error_code = MPI_ERR_INTERN;
	    globus_libc_fprintf(stderr, "MPID_Init(): failed malloc\n");
	    goto fn_exit;
	} /* endif */

	if (mp_comm_dup(NULL, VMPI_Internal_Comm) != VMPI_SUCCESS)
	{
	    *error_code = MPI_ERR_INTERN;
	    globus_libc_fprintf(stderr, "MPID_Init(): failed mp_comm_dup()\n");
	    goto fn_exit;
	} /* endif */
    } /* endifdef */
#   endif

    create_topology_access_keys();
  fn_exit: 
    /* to supress compile warnings */ ;
} /* end MPID_Init() */

/* 
 * this about MPI_Abort from the MPI 1.1 standard:
 *   "This routine makes a ``best attempt'' to abort all tasks in the group 
 *    of comm. This function does not require that the invoking environment 
 *    take any action with the error code. However, a Unix or POSIX environment 
 *    should handle this as a return errorcode from the main program or an 
 *    abort(errorcode). 
 *    MPI implementations are required to define the behavior of MPI_ABORT at 
 *    least for a comm of MPI_COMM_WORLD. MPI implementations may ignore the 
 *    comm argument and act as if the comm was MPI_COMM_WORLD."
 *
 * we have chosen to ignore the comm arg and kill everything in MPI_COMM_WORLD.
 * also, we do NOT propogate the 
 */
void MPID_Abort(struct MPIR_COMMUNICATOR *comm,
		int error_code,
		char *facility, /* optional, used to indicate who called the
				   routine; for example, the user (MPI_Abort)
				   or the MPI implementation. */
		char *string) /* optional, use if provided else print default */
{
    int i;
    globus_byte_t *last_contact = (globus_byte_t *) NULL;

    if (facility && *facility)
	globus_libc_fprintf(stderr, "%s: ", facility); 

    if (string && *string)
	globus_libc_fprintf(stderr, "%s\n", string); 
    else
	/* default message */
	globus_libc_fprintf(stderr, "Aborting with code %d\n", error_code); 

    i = globus_module_activate(GLOBUS_GRAM_CLIENT_MODULE);
    if (i != GLOBUS_SUCCESS)
    {
	globus_libc_fprintf(stderr, 
	    "MPID_Abort: failed "
	    "globus_module_activate(GLOBUS_GRAM_CLIENT_MODULE)");
	abort();
    } /* endif */

    /* loop to send ONE kill message to all gatekeepers OTHER than mine */
    for (i = 0; i < MPID_MyWorldSize; i ++)
    {
	if ((!last_contact || strcmp((const char *) last_contact, 
				    (const char *) GramJobcontactsVector[i]))
	    && strcmp((const char *) MyGlobusGramJobContact, 
			(const char *) GramJobcontactsVector[i]))
	{
	    last_contact = GramJobcontactsVector[i];
	    if (globus_gram_client_job_cancel((char *) GramJobcontactsVector[i])
		!= GLOBUS_SUCCESS)
	    {
		globus_libc_fprintf(stderr, 
		    "ERROR: MPID_Abort: failed remote "
		    "globus_gram_client_job_cancel to job contact >%s<\n", 
		    GramJobcontactsVector[i]);
	    } /* endif */
	} /* endif */
    } /* endfor */

    /* now killing MY subjob */
    if (globus_gram_client_job_cancel((char *) MyGlobusGramJobContact)
	    !=GLOBUS_SUCCESS)
    {
	globus_libc_fprintf(stderr, 
	    "ERROR: MPID_Abort: failed local globus_gram_client_job_cancel "
	    "to job contact >%s<\n", 
	    MyGlobusGramJobContact);
    } /* endif */

} /* end MPID_Abort() */

#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME MPID_End
void MPID_End(void)
{
    int i, j;

    DEBUG_FN_ENTRY(DEBUG_MODULE_INIT);

#   if defined(GLOBUS_CALLBACK_GLOBAL_SPACE)
    {
        globus_result_t result;
        result = globus_callback_space_destroy(MpichG2Space);
        if (result != GLOBUS_SUCCESS)
        {
            globus_object_t* err = globus_error_get(result);
            char *errstring = globus_object_printable_to_string(err);
            globus_libc_fprintf(stderr, 
                "WARNING: MPID_End: failed globus_callback_space_destroy "
                "during shutdown: %s\n", 
                errstring);
        } /* endif */
    } 
#endif

    destroy_topology_access_keys();
    /* START GRIDFTP */
    if ( MPICHX_PARALLELSOCKETS_PARAMETERS != MPI_KEYVAL_INVALID )
        MPI_Keyval_free(&MPICHX_PARALLELSOCKETS_PARAMETERS);
    /* END GRIDFTP */

#   if defined(VMPI)
    {
	struct mpircvreq *mp;

	if (MpiPostedQueue.head)
	{
	    DEBUG_PRINTF(DEBUG_MODULE_INIT | DEBUG_MODULE_RECV,
			 DEBUG_INFO_WARNING,
			 ("WARNING: MPI_COMM_WORLD_RANK %d found residual "
			  "nodes in MpiPostedQueue\n",
			  MPID_MyWorldRank));
	} /* endif */

	while ((mp = MpiPostedQueue.head) != NULL)
	{
	    MpiPostedQueue.head = mp->next;
	    g_free(mp);
	} /* endwhile */
    }
#   endif

    /* freeing CommWorldChannelsTable */
    for (i = 0; i < CommworldChannelsTableNcommWorlds; i ++)
    {
	struct channel_t *cp = CommWorldChannelsTable[i].channels;

	for (j = 0; j < CommWorldChannelsTable[i].nprocs; j ++)
	{
	    struct miproto_t *mp;
	    while ((mp = cp[j].proto_list) != NULL)
	    {
		cp[j].proto_list = mp->next;
		if (mp->type == tcp)
		{
		    struct tcp_miproto_t *tp = (struct tcp_miproto_t *) 
						mp->info;
		    struct tcpsendreq *tmp;

		    if (tp->handlep)
		    {
			struct tcp_rw_handle_t *rwp;

			rwp = (struct tcp_rw_handle_t *) tp->handlep;
			globus_io_close(&(rwp->handle));
			g_free(tp->handlep);
		    }
		    
		    for (tmp = tp->cancel_head; tmp; tmp = tp->cancel_head)
		    {
			tp->cancel_head = tmp->next;
			g_free((void *) tmp);
		    } /* endfor */
		    for (tmp = tp->send_head; tmp; tmp = tp->send_head)
		    {
			tp->send_head = tmp->next;
			g_free((void *) tmp);
		    } /* endfor */

                    g_free((void *) tp->globus_lan_id);
		    g_free((void *) tp->header);
		} /* endif */
		g_free((void *) mp->info);
		g_free((void *) mp);
	    } /* endwhile */
	} /* endfor */
	g_free((void *) cp);
    } /* endfor */
    g_free((void *) CommWorldChannelsTable);

    /* freeing GramJobcontactsVector */
    for (i = 0; i < MPID_MyWorldSize; i ++)
	g_free((void *) GramJobcontactsVector[i]);
    g_free((void *) GramJobcontactsVector);

    free_vmpi_maps();
    
    globus_module_deactivate(GLOBUS_NEXUS_MODULE);
    globus_module_deactivate(GLOBUS_IO_MODULE);
    globus_module_deactivate(GLOBUS_FTP_CONTROL_MODULE); /* GRIDFTP */
    globus_module_deactivate(GLOBUS_COMMON_MODULE);

    /*
     * Call the vendor version of MPI_Finalize()
     */
#   if defined(VMPI)
    {
	mp_finalize();
    }
#   endif

    DEBUG_FN_EXIT(DEBUG_MODULE_INIT);

} /* end MPID_End() */

/*
 * MPID_DeviceCheck
 *
 * NICK: for now just call G2_POLL and return 1.
 *       need to understand if it's OK to call globus_poll_blocking
 *       when 'is_blocking != 0'
 */
int MPID_DeviceCheck(MPID_BLOCKING_TYPE is_blocking)
{

#   if defined(VMPI)
    {
	/* nudge MPI */
	struct mpircvreq *curr, *next;

	/* 
	 * take one pass through the MpiPostedQueue trying to 
	 * satisfy each req.
	 */
	for (curr = MpiPostedQueue.head; curr; curr = next)
	{
	    /* 
	     *if curr->req was satisfied, then mpi_recv_or_post
	     * removes it from MpiPostedQueue and sets curr->next=NULL
	     */
	    next = curr->next;
	    mpi_recv_or_post(curr->req, (int *) NULL);
	} /* endfor */
    }
#   endif

    /* nudge TCP */
    {
	globus_bool_t outstanding_tcp_reqs;
	outstanding_tcp_reqs = ((TcpOutstandingRecvReqs > 0 
					|| TcpOutstandingSendReqs > 0)
					    ? GLOBUS_TRUE : GLOBUS_FALSE);
	if (outstanding_tcp_reqs)
	{
	    G2_POLL
	} /* endif */
    }

    return 1;

} /* end MPID_DeviceCheck() */

int MPID_Complete_pending(void)
{
    MPID_Abort( (struct MPIR_COMMUNICATOR *)0, 1, "MPI internal", 
		"MPID_Complete_pending not implemented yet" );
    return 1;
} /* end MPID_Complete_pending() */

int MPID_WaitForCompleteSend(MPIR_SHANDLE *request)
{
    MPID_Abort( (struct MPIR_COMMUNICATOR *)0, 1, "MPI internal", 
		"MPID_WaitForCompleteSend not implemented yet" );
    return 1;
#if 0
    while (!request->is_complete)
    {
	MPID_DeviceCheck(MPID_BLOCKING);
    }
    return MPI_SUCCESS;
#endif
} /* end MPID_WaitForCompleteSend() */

int MPID_WaitForCompleteRecv(MPIR_RHANDLE *request)
{
    MPID_Abort( (struct MPIR_COMMUNICATOR *)0, 1, "MPI internal", 
		"MPID_WaitForCompleteRecv not implemented yet" );
    return 1;
#if 0
    while (!request->is_complete)
    {
	MPID_DeviceCheck(MPID_BLOCKING);
    }
    return MPI_SUCCESS;
#endif
} /* end MPID_WaitForCompleteRecv() */

void MPID_SetPktSize(int len)
{
    /* do nothing */
} /* end MPID_SetPktSize() */

void MPID_Version_name(char *name)
{
    sprintf(name, "ADI version %4.2f - transport %s",
    		MPIDPATCHLEVEL, MPIDTRANSPORT);
} /* end MPID_Version_name() */

/*
 * this function gets called by MPI_Reqeust_free in 
 * mpich/src/pt2pt/commreq_free.c ONLY when the is_complete 
 * of request is FALSE.
 */
void MPID_Request_free(MPI_Request request)
{

    MPI_Request rq = request;
    int error_code;

    switch (rq->handle_type) 
    {
	case MPIR_SEND:
	    {
#               if defined(VMPI)
		{
		    MPIR_SHANDLE *sreq = (MPIR_SHANDLE *) rq;

		    if (sreq->req_src_proto == mpi)
		    {
			/* MPI */
			error_code = vmpi_error_to_mpich_error(
				    mp_request_free(sreq->vmpi_req));
			MPIR_FORGET_SEND(&rq->shandle);
			MPID_SendFree(rq);
			return;
		    } /* endif */
		}
#               endif

		/* TCP */
		if (MPID_SendIcomplete(rq, &error_code)) 
		{
		    MPIR_FORGET_SEND(&rq->shandle);
		    MPID_SendFree(rq);
		    rq = 0;
		} /* endif */
	    }
	    break;
	case MPIR_RECV:
	    if (MPID_RecvIcomplete(rq, (MPI_Status *)0, &error_code)) 
	    {
		MPID_RecvFree(rq);
		rq = 0;
	    }
	    break;
	case MPIR_PERSISTENT_SEND:
	    MPID_Abort( (struct MPIR_COMMUNICATOR *)0, 1, "MPI internal", 
			"Unimplemented operation - persistent send free" );
	    break;
	case MPIR_PERSISTENT_RECV:
	    MPID_Abort( (struct MPIR_COMMUNICATOR *)0, 1, "MPI internal", 
			"Unimplemented operation - persistent recv free" );
	    break;
	default:
	    break;
    } /* end switch() */

    MPID_DeviceCheck( MPID_NOTBLOCKING );
    /* 
     * If we couldn't complete it, decrement it's reference count
     * and forget about it.  This requires that the device detect
     * orphaned requests when they do complete, and process them
     * independent of any wait/test.
     */
    if (rq)
    {
        rq->chandle.ref_count--;
    }
} /* end MPID_Request_free() */


void MPID_ZeroStatusCount(
    MPI_Status *			status)
{
    (status)->count = 0;
    STATUS_INFO_SET_COUNT_NONE(*status);
}


/* Temp fix for MPI_Status_set_elements, needed in Romio */
void MPID_Status_set_bytes(
    MPI_Status *                        status,
    int                                 bytes)
{
    status->count = bytes;
    STATUS_INFO_SET_COUNT_LOCAL(*status);
}

/****************************/
/* public utility functions */
/****************************/

void build_channels(int nprocs,
		    globus_byte_t **mi_protos_vector,
		    struct channel_t **channels)
{
    int i, j;
    int dummy;
    int nprotos;
    char *cp;
    struct miproto_t *mp;

    g_malloc(*channels, struct channel_t *, nprocs*sizeof(struct channel_t));

    for (i = 0; i < nprocs; i ++)
    {
        (*channels)[i].selected_proto = (struct miproto_t *)  NULL;

        cp = (char *) mi_protos_vector[i];
        sscanf(cp, "%d ", &nprotos);
        cp = index(cp, ' ')+1;

        for (j = 0, mp = (struct miproto_t *) NULL; j < nprotos; j ++)
        {
            if (mp)
            {
                /* not the first proto for this proc */
                g_malloc(mp->next,
			 struct miproto_t *,
			 sizeof(struct miproto_t));
                mp = mp->next;
            }
            else
            {
                /* first proto for this proc */
		g_malloc((*channels)[i].proto_list, 
			struct miproto_t *, 
			sizeof(struct miproto_t));
                mp = (*channels)[i].proto_list;
            } /* endif */
            mp->next = (struct miproto_t *) NULL;
            sscanf(cp, "%d ", &dummy);
	    mp->type = dummy;
            cp = index(cp, ' ')+1;
            switch (mp->type)
            {
                case tcp:
                {
                    struct tcp_miproto_t *tp;
                    int lan_id_lng;

		    g_malloc(tp, 
			struct tcp_miproto_t *, 
			sizeof(struct tcp_miproto_t));
		    mp->info = tp;
                    tp->handlep = (struct tcp_rw_handle_t *) NULL;
                    tp->whandle = (globus_io_handle_t *) NULL;
		    tp->cancel_head = tp->cancel_tail = 
			tp->send_head = tp->send_tail = 
			    (struct tcpsendreq *) NULL;
                    tp->recvd_partner_port = GLOBUS_FALSE; /* GRIDFTP */
                    tp->use_grid_ftp       = GLOBUS_FALSE; /* GRIDFTP */
		    g_malloc(tp->header, globus_byte_t *, Headerlen);
                    sscanf(cp, "%s %d %d", tp->hostname, &dummy,
                                           &lan_id_lng);
                    tp->port = (unsigned short) dummy;
                    cp = index(cp, ' ')+1;
                    cp = index(cp, ' ')+1;
                    cp = index(cp, ' ')+1;
                    g_malloc(tp->globus_lan_id, char *,
                             sizeof(char) * lan_id_lng);
                    /* '\0' already included in 'lan_id_lng' */
                    sscanf(cp, "%s %d", tp->globus_lan_id, &(tp->localhost_id));
                    cp = index(cp, ' ')+1;
                    cp = index(cp, ' ')+1;
                }
                    break;
                case mpi:
                    g_malloc(mp->info, void *, sizeof(struct mpi_miproto_t));
                    sscanf(cp, "%s %d", 
                ((struct mpi_miproto_t *) (mp->info))->unique_session_string, 
                        &(((struct mpi_miproto_t *) (mp->info))->rank));
                    cp = index(cp, ' ')+1;
                    cp = index(cp, ' ')+1;
                    break;
                default:
		    {
			char err[1024];
			sprintf(err, 
			    "ERROR: build_channles() - encountered "
			    "unrecognized proto type %d", 
			    mp->type);
			MPID_Abort( (struct MPIR_COMMUNICATOR *)0, 
				    1, "MPICH-G2", err);
		    }
                    break;
            } /* end switch() */
        } /* endfor */
    } /* endfor */
    
} /* end build_channels() */

/*
 * selects protocols for each channel in 'channels'
 * based on CommworldChannels[MPID_MyWorldRank]
 */
void select_protocols(int nprocs, struct channel_t *channels)
{

    struct miproto_t *my_mp, *dest_mp;
    int i;

    for (i = 0; i < nprocs; i ++)
    {
        channels[i].selected_proto = (struct miproto_t *) NULL;

        for (my_mp = CommworldChannels[MPID_MyWorldRank].proto_list; 
            !(channels[i].selected_proto) && my_mp; 
                my_mp = my_mp->next)
        {
            for (dest_mp = channels[i].proto_list; 
                !(channels[i].selected_proto) && dest_mp; 
                    dest_mp = dest_mp->next)
            {
                if (my_mp->type == dest_mp->type)
                {
                    switch (my_mp->type)
                    {
                        case tcp: /* auto-match */
                            channels[i].selected_proto = dest_mp; 
                        break; 
#                       if defined(VMPI)
			    case mpi:
				if (!strcmp(
            ((struct mpi_miproto_t *) (my_mp->info))->unique_session_string,
            ((struct mpi_miproto_t *) (dest_mp->info))->unique_session_string))
				    channels[i].selected_proto = dest_mp; 
			    break;
#                       endif
                        default:
			    {
				char err[1024];

				globus_libc_sprintf(err,
				    "select_protocols(): unrecognizable "
				    "proto type %d", 
				    my_mp->type);
				MPID_Abort( (struct MPIR_COMMUNICATOR *)0, 
					1, "MPICH-G2", err);
			    }
                        break;
                    } /* endif */
                } /* endif */
            } /* endfor */
        } /* endfor */
        if (!(channels[i].selected_proto))
        {
            globus_libc_fprintf(stderr,
		"ERROR: select_protocols(): proc %d could not select proto "
		"to proc %d\n", 
		MPID_MyWorldRank, 
		i);
            print_channels();
	    MPID_Abort( (struct MPIR_COMMUNICATOR *)0, 
				    1, "MPICH-G2", "");
        } /* endif */

    } /* endfor */

} /* end select_protocols() */

void print_channels()
{

    int i;

    globus_libc_fprintf(stderr, 
	"%d: *** START print_channels(): table currently has %d "
	"commworlds (rows)\n", 
	MPID_MyWorldRank, CommworldChannelsTableNcommWorlds);

    for (i = 0; i < CommworldChannelsTableNcommWorlds; i ++)
    {
	globus_libc_fprintf(stderr, "    %d: ### START commworld (i.e., row) "
	    "%d of %d name >%s< nprocs %d\n", 
	    MPID_MyWorldRank, 
	    i+1, 
	    CommworldChannelsTableNcommWorlds,
	    CommWorldChannelsTable[i].name,
	    CommWorldChannelsTable[i].nprocs);

	print_CommWorldChannelsTable_row(CommWorldChannelsTable+i);

	globus_libc_fprintf(stderr, "    %d: ### END commworld (i.e., row) "
	    "%d of %d name >%s< nprocs %d\n", 
	    MPID_MyWorldRank, 
	    i+1, 
	    CommworldChannelsTableNcommWorlds,
	    CommWorldChannelsTable[i].name,
	    CommWorldChannelsTable[i].nprocs);

    } /* endfor */

    globus_libc_fprintf(stderr, 
	"%d: *** END print_channels(): table currently has %d "
	"commworlds (rows)\n", 
	MPID_MyWorldRank, CommworldChannelsTableNcommWorlds);

} /* end print_channels() */

struct channel_t *get_channel(int grank)
{
    struct channel_t *rc;
    int row, displ;

    row = get_channel_rowidx(grank, &displ);

    if (row == -1)
	rc = (struct channel_t *) NULL;
    else
	rc = &(CommWorldChannelsTable[row].channels[displ]);

    return rc;

} /* end get_channel() */

/*
 * upon successful completion returns rowidx into CommWorldChannelsTable
 * for specified 'grank', otherwise returns -1
 *
 * if optional 'displ' is supplied, also returns displ into that row
 * associated with 'grank'
 */
int get_channel_rowidx(int grank, int *displ /* optional */)
{

    int rc;
    int row, highest_rank_of_last_row;

    for (row = 0, highest_rank_of_last_row = -1;
	row < CommworldChannelsTableNcommWorlds 
	&& highest_rank_of_last_row+CommWorldChannelsTable[row].nprocs < grank; 
	    row ++) 
		highest_rank_of_last_row += CommWorldChannelsTable[row].nprocs;

    if (row < CommworldChannelsTableNcommWorlds)
    {
	rc = row;
	if (displ)
	    *displ = grank-(highest_rank_of_last_row+1);
    }
    else
	rc = -1;

    return rc;

} /* end get_channel_rowidx() */

/*
 * upon successful completion returns rowidx into CommWorldChannelsTable
 * for specified 'name', otherwise returns -1
 *
 */
int commworld_name_to_rowidx(char *name)
{

    int rc;

    if (name)
    {
	for (rc = 0; rc < CommworldChannelsTableNcommWorlds 
	    && strcmp(name, CommWorldChannelsTable[rc].name); 
		rc ++) 
	;

	if (rc == CommworldChannelsTableNcommWorlds)
	    rc = -1;
    }
    else
	rc = -1;

    return rc;

} /* end commworld_name_to_rowidx() */

/*
 * upon successful completion returns grank 
 * for specified <'name','displ'> otherwise returns -1
 */
int commworld_name_displ_to_grank(char *name, int displ)
{

    int row;
    int rc;

    if (name && displ >= 0)
    {
	rc = 0;
	for (row = 0; 
	    row < CommworldChannelsTableNcommWorlds 
	    && strcmp(name, CommWorldChannelsTable[row].name); 
		row ++) 
		    rc += CommWorldChannelsTable[row].nprocs;

	if (row == CommworldChannelsTableNcommWorlds)
	    rc = -1; /* could not find name */
	else if (displ > CommWorldChannelsTable[row].nprocs)
	    rc = -1; /* invalid displ for commworld with 'name' */
	else
	    rc += displ;
    }
    else
	rc = -1;

    return rc;

} /* end commworld_name_displ_to_grank() */

/***************************/
/* Local Utility Functions */
/***************************/

static int globus_init(int *argc, char ***argv)
{
    int i;
    globus_byte_t *my_miproto;
    globus_byte_t **mi_protos_vector;
    int *mi_protos_vector_lengths;
    int nbytes;
    char * tmpstr;

    int rank_in_my_subjob;
    int my_subjob_size;    
    int nsubjobs;          /* used by subjobmasters only */
    int *subjob_addresses; /* used by subjobmasters only */

    int rc;

    /* 
     * using GLOBUS_CALLBACK_GLOBAL_SPACE as cheap test
     * to see if MPICH-G2 is being configured against
     * Globus v2.2 or later ... GLOBUS_CALLBACK_GLOBAL_SPACE
     * was introduced in GLobus v2.2
     */
#if defined(GLOBUS_CALLBACK_GLOBAL_SPACE)
    globus_module_set_args(argc, argv);
#endif

    rc = globus_module_activate(GLOBUS_DUROC_RUNTIME_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
	globus_libc_fprintf(stderr, 
	    "globus_init: failed "
	    "globus_module_activate(GLOBUS_DUROC_RUNTIME_MODULE)\n");
	abort();
    } /* endif */
	
    globus_duroc_runtime_barrier();
    
    rc = globus_module_deactivate(GLOBUS_DUROC_RUNTIME_MODULE);    
    if (rc != GLOBUS_SUCCESS)
    {
	globus_libc_fprintf(stderr, 
	    "globus_init: failed "
	    "globus_module_deactivate(GLOBUS_DUROC_RUNTIME_MODULE)\n");
	abort();
    } /* endif */

    rc = globus_module_activate(GLOBUS_COMMON_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
	globus_libc_fprintf(stderr, 
	    "globus_init: failed "
	    "globus_module_activate(GLOBUS_COMMON_MODULE)\n");
	abort();
    } /* endif */

    rc = globus_module_activate(GLOBUS_IO_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
	globus_libc_fprintf(stderr, 
	    "globus_init: failed "
	    "globus_module_activate(GLOBUS_IO_MODULE)\n");
	abort();
    } /* endif */

    /* START GRIDFTP */
    rc = globus_module_activate(GLOBUS_FTP_CONTROL_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
        globus_libc_fprintf(stderr, 
            "globus_init: failed "
            "globus_module_activate(GLOBUS_FTP_CONTROL_MODULE)\n");
        abort();
    } /* endif */
    /* END GRIDFTP */

    /* 
     * we have to activate the nexus and disable fault tolerance
     * because duroc bootstrap uses nexus AND insists on keeping
     * nexus activated during the entire computation (even though
     * duroc only uses nexus when we use duroc, that is, during
     * initialization).  the problem here is that when a remote
     * proc dies a bunch of nexus error messages gets generated
     * (because fd's in nexus ep's are being closed) AND our proc
     * is forced to abort because nexus_fatal gets called too.  
     * by registering NULL nexus error handlers we not only prevent
     * the annoying nexus error messages, but we also prevent our
     * proc terminating just becase a remote proc terminated.
     */
    rc = globus_module_activate(GLOBUS_NEXUS_MODULE);
    if (rc != GLOBUS_SUCCESS)
    {
	globus_libc_fprintf(stderr, 
	    "globus_init: failed "
	    "globus_module_activate(GLOBUS_NEXUS_MODULE)\n");
	abort();
    } /* endif */
    nexus_enable_fault_tolerance (NULL, NULL);

    /*
     * Find out if the user wants to increase the socket buffer size
     */
    tmpstr = globus_module_getenv("MPICH_GLOBUS2_TCP_BUFFER_SIZE");
    if (tmpstr != GLOBUS_NULL)
    {
	MpichGlobus2TcpBufsz = atoi(tmpstr);
	if (MpichGlobus2TcpBufsz < 0)
	{
	    MpichGlobus2TcpBufsz = 0;
	} /* endif */
    } /* endif */

    /****************************************************************/
    /* making sure there's enough room in a ulong to hold a pointer */
    /* ... this is REQUIRED for liba of our TCP headers.            */
    /****************************************************************/

    if (sizeof(MPIR_SHANDLE *) > globus_dc_sizeof_u_long(1))
    {
	globus_libc_fprintf(stderr, 
	    "ERROR: globus_init: detected that sizeof pointer %ld is greater "
	    "than sizeof(ulong) %ld ... cannot run\n", 
	    (long) sizeof(MPIR_SHANDLE *), 
	    (long) globus_dc_sizeof_u_long(1));
	return 1;
    } /* endif */

    /*************************************************************************/
    /* making sure there's enough room in G2_MAXHOSTNAMELEN to hold hostname */
    /*************************************************************************/

    if (G2_MAXHOSTNAMELEN < MAXHOSTNAMELEN)
    {
	globus_libc_fprintf(stderr, 
	    "ERROR: globus_init: detected that the MPICH-G2-defined value\n"
	    "       G2_MAXHOSTNAMELEN %d is less OS-defined value "
	    "MAXHOSTNAMELEN %d\n."
	    "       The solution is to increase the value of G2_MAXHOSTNAMELEN "
	    "(defined in\n"
	    "       a header file in <mpichdir>/mpid/globus2 directory) so that"
	    "it is at\n"
	    "       it least %d and re-build/install MPICH-G2.\n"
	    "NOTE: If you change the value of G2_MAXHOSTNAMELEN on this" 
	    "system then\n"
	    "      you _MUST_ also change it to the exact same value on all "
	    "systems you plan\n"
	    "      to run your application on.  This will require a "
	    "re-build/install of\n"
	    "      MPICH-G2 on those systems as well.\n"
	    "      Within a single computation, the value of G2_MAXHOSTNAMELEN "
	    "must be\n"
	    "      identical in all MPICH-G2 installations.\n\n",
	    G2_MAXHOSTNAMELEN,
	    MAXHOSTNAMELEN,
	    MAXHOSTNAMELEN); 
	return 1;
    } /* endif */

    /*********************************/
    /* initializing global variables */
    /*********************************/

#   if defined(GLOBUS_CALLBACK_GLOBAL_SPACE)
    {
        globus_result_t result;
        result = globus_callback_space_init(&MpichG2Space, GLOBUS_NULL_HANDLE);
        if (result != GLOBUS_SUCCESS)
        {
            globus_object_t* err = globus_error_get(result);
            char *errstring = globus_object_printable_to_string(err);
            globus_libc_fprintf(stderr, 
                "ERROR: globus_init: failed globus_callback_space_init: %s\n", 
                errstring);
            return 1;
        } /* endif */
    } 
#endif
 
#   if defined(VMPI)
    {
        MpiPostedQueue.head = MpiPostedQueue.tail = (struct mpircvreq *) NULL;
    }
#   endif

    /* initializing TCP proto global variables */
    /* tcphdr = src,tag,context,dataoriginbuffsize,ssend_flag,liba (ulong) */
    Headerlen = (globus_size_t) LOCAL_HEADER_LEN;

    /* 
     * getting topology info, including global variables
     * MPID_MyWorldSize and MPID_MyWorldRank 
     */
		
    globus_duroc_runtime_intra_subjob_rank(&rank_in_my_subjob);
    globus_duroc_runtime_intra_subjob_size(&my_subjob_size);

    get_topology(rank_in_my_subjob,
		my_subjob_size,
		&subjob_addresses,
		&MPID_MyWorldSize,
		&nsubjobs,
		&MPID_MyWorldRank);

    /**************************************************/
    /* creating and all-to-all distributing mi_protos */
    /**************************************************/

    create_my_miproto(&my_miproto, &nbytes);

    if (!(mi_protos_vector =
        (globus_byte_t **) 
	    globus_libc_malloc(MPID_MyWorldSize*sizeof(globus_byte_t *))))
    {
        globus_libc_fprintf(stderr,
            "ERROR: failed malloc of %d bytes\n",
            MPID_MyWorldSize*sizeof(globus_byte_t *));
        exit(1);
    } /* endif */

    if (!(mi_protos_vector_lengths = 
	    (int *) globus_libc_malloc(MPID_MyWorldSize*sizeof(int))))
    {
        globus_libc_fprintf(stderr,
            "ERROR: failed malloc of %d bytes\n", 
	    MPID_MyWorldSize*sizeof(int));
        exit(1);
    } /* endif */

/* globus_libc_fprintf(stderr, "%d: before distribute_byte_array %d bytes\n", MPID_MyWorldRank, nbytes); */
    distribute_byte_array(my_miproto,
			    nbytes,
			    rank_in_my_subjob,
			    my_subjob_size,
			    subjob_addresses,
			    MPID_MyWorldSize,
			    nsubjobs,
			    MPID_MyWorldRank,
			    mi_protos_vector,
			    mi_protos_vector_lengths);
/* globus_libc_fprintf(stderr, "%d: after distribute_byte_array %d bytes\n", MPID_MyWorldRank, nbytes); */
    g_free((void *) my_miproto);

    mpich_globus2_debug_init();

    build_channels(MPID_MyWorldSize,
                    mi_protos_vector,
                    &CommworldChannels);

    g_free((void *) mi_protos_vector_lengths);
    for (i = 0; i < MPID_MyWorldSize; i ++)
    {
        g_free((void *) mi_protos_vector[i]);
    } /* endfor */
    g_free((void *) mi_protos_vector);

    select_protocols(MPID_MyWorldSize, CommworldChannels);
/* globus_libc_fprintf(stderr, "%d: after select_protocols\n", MPID_MyWorldRank); */

    build_vmpi_maps();
    
    /********************************************************/
    /* rank 0 creating and bcasting universally unique name */
    /* for CommworldChannels ... needed for MPI_Connect     */
    /********************************************************/
    {
	int *vector_lengths;
	globus_byte_t my_commworld_id[COMMWORLDCHANNELSNAMELEN];
	globus_byte_t **my_commworld_id_vector;
	int i;

	if (MPID_MyWorldRank == 0)
	{
	    /* creating universally unique name = {hostname,pid} for rank 0 */
	    char hostname[G2_MAXHOSTNAMELEN];

	    if (globus_libc_gethostname(hostname, G2_MAXHOSTNAMELEN))
	    {
		    globus_libc_fprintf(stderr,
		    "ERROR: globus_init(): failed globus_libc_gethostname()\n");
		    exit(1);
	    } /* endif */

	    sprintf((char *) my_commworld_id, "%s %d", 
		/* hostname, atoi(globus_libc_getenv("G2_PID"))); */
		hostname, globus_libc_getpid());
	}
	else
	    my_commworld_id[0] = '\0';

	if (!(my_commworld_id_vector = (globus_byte_t **)
		globus_libc_malloc(MPID_MyWorldSize*sizeof(globus_byte_t *))))
	{
	    globus_libc_fprintf(stderr,
		"ERROR: failed malloc of %d bytes\n",
		MPID_MyWorldSize*sizeof(globus_byte_t *));
	    exit(1);
	} /* endif */

	if (!(vector_lengths =
		(int *) globus_libc_malloc(MPID_MyWorldSize*sizeof(int))))
	{
	    globus_libc_fprintf(stderr,
		"ERROR: failed malloc of %d bytes\n",
		MPID_MyWorldSize*sizeof(int));
	    exit(1);
	} /* endif */

	distribute_byte_array(my_commworld_id,
				COMMWORLDCHANNELSNAMELEN,
				rank_in_my_subjob,
				my_subjob_size,
				subjob_addresses,
				MPID_MyWorldSize,
				nsubjobs,
				MPID_MyWorldRank,
				my_commworld_id_vector,
				vector_lengths);
	g_free((void *) vector_lengths);

	if (!(CommWorldChannelsTable =
	    (struct commworldchannels *)
		globus_libc_malloc(
	    COMMWORLDCHANNELS_TABLE_STEPSIZE*sizeof(struct commworldchannels))))
	{
	    globus_libc_fprintf(stderr,
		"ERROR: failed malloc of %d bytes\n",
	    COMMWORLDCHANNELS_TABLE_STEPSIZE*sizeof(struct commworldchannels));
	    exit(1);
	} /* endif */

	CommworldChannelsTableSize        = COMMWORLDCHANNELS_TABLE_STEPSIZE;
	CommworldChannelsTableNcommWorlds = 1;

	CommWorldChannelsTable[0].nprocs   = MPID_MyWorldSize;
	memcpy(CommWorldChannelsTable[0].name, 
		(char *) my_commworld_id_vector[0], 
		COMMWORLDCHANNELSNAMELEN);
	CommWorldChannelsTable[0].channels = CommworldChannels;

	for (i = 0; i < MPID_MyWorldSize; i ++)
	    g_free((void *) my_commworld_id_vector[i]);
	g_free((void *) my_commworld_id_vector);

    }

    /* print_channels(); */

    /**********************************************************************/
    /* discovering and all-to-all distributing jobstrings (for MPI_Abort) */
    /**********************************************************************/

    {
	int *vector_lengths;

	if (!(MyGlobusGramJobContact = 
		(globus_byte_t *)globus_libc_getenv("GLOBUS_GRAM_JOB_CONTACT")))
	{
	    globus_libc_fprintf(stderr, 
		"ERROR: could not read env variable GLOBUS_GRAM_JOB_CONTACT\n");
	    return 1;
	} /* endif */

	if (!(GramJobcontactsVector =
	    (globus_byte_t **)
		globus_libc_malloc(MPID_MyWorldSize*sizeof(globus_byte_t *))))
	{
	    globus_libc_fprintf(stderr,
		"ERROR: failed malloc of %d bytes\n",
		MPID_MyWorldSize*sizeof(globus_byte_t *));
	    exit(1);
	} /* endif */

	if (!(vector_lengths =
		(int *) globus_libc_malloc(MPID_MyWorldSize*sizeof(int))))
	{
	    globus_libc_fprintf(stderr,
		"ERROR: failed malloc of %d bytes\n",
		MPID_MyWorldSize*sizeof(int));
	    exit(1);
	} /* endif */

	distribute_byte_array(MyGlobusGramJobContact,
				strlen((const char *) MyGlobusGramJobContact)+1,
				rank_in_my_subjob,
				my_subjob_size,
				subjob_addresses,
				MPID_MyWorldSize,
				nsubjobs,
				MPID_MyWorldRank,
				GramJobcontactsVector,
				vector_lengths);
	g_free((void *) vector_lengths);

    }

    /************/
    /* clean-up */
    /************/

    if (!rank_in_my_subjob)
	g_free((void *) subjob_addresses);
    return 0;

} /* end globus_init() */

/*
 * mpich_globus2_get_interface_address()
 *
 * Note: this is not IPv6 ready; it does, however, attempt to play nicely with
 * systems that have IPv6.
 */
static
int
mpich_globus2_get_interface_address(
    struct in_addr * net_addr_p,
    struct in_addr * net_mask_p,
    struct in_addr * if_addr_p)
{
    int					fd;
    char *				buf_ptr;
    int					buf_len;
    int					buf_len_prev;
    char *				ptr;
    
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
	MPID_Abort(GLOBUS_NULL,
		   MPI_ERR_INTERN,
		   "MPICH-G2",
		   "mpich_globus2_get_interface_address() - "
		   "failed to acquire a socket");
    }

    /*
     * Obtain the interface information from the operating system
     *
     * Note: much of this code is borrowed from W. Richard Stevens' book
     * entitled "UNIX Network Programming", Volume 1, Second Edition.  See
     * section 16.6 for details.
     */
    buf_len = MPICH_GLOBUS2_IFREQ_ALLOC_COUNT * sizeof(struct ifreq);
    buf_len_prev = 0;

    for(;;)
    {
	struct ifconf			ifconf;
	int				rc;

	buf_ptr = (char *) globus_malloc(buf_len);
	if (buf_ptr == GLOBUS_NULL)
	{
	    MPID_Abort(GLOBUS_NULL,
		       MPI_ERR_INTERN,
		       "MPICH-G2",
		       "mpich_globus2_get_interface_address() - "
		       "failed to allocate temporary buffer space");
	}
	
	ifconf.ifc_buf = buf_ptr;
	ifconf.ifc_len = buf_len;

	rc = ioctl(fd, SIOCGIFCONF, &ifconf);
	if (rc < 0)
	{
	    if (errno != EINVAL || buf_len_prev != 0)
	    {
		MPID_Abort(GLOBUS_NULL,
			   MPI_ERR_INTERN,
			   "MPICH-G2",
			   "mpich_globus2_get_interface_address() - "
			   "failed to acquire interface information");
	    }
	}
        else
	{
	     
	    if (ifconf.ifc_len == buf_len_prev)
	    {
		buf_len = ifconf.ifc_len;
		break;
	    }

	    buf_len_prev = ifconf.ifc_len;
	}
	
	free(buf_ptr);
	buf_len += MPICH_GLOBUS2_IFREQ_ALLOC_COUNT * sizeof(struct ifreq);
    }
	
    /*
     * Now that we've got the interface information, we need to run through
     * the interfaces and see if any of their addresses match the network
     * address the user gave us.
     */
    ptr = buf_ptr;
    if_addr_p->s_addr = htonl(0);

    while(ptr < buf_ptr + buf_len)
    {
	struct ifreq *			ifreq;

	ifreq = (struct ifreq *) ptr;
	
	if (ifreq->ifr_addr.sa_family == AF_INET)
	{
	    struct in_addr		addr;

	    addr = ((struct sockaddr_in *) &(ifreq->ifr_addr))->sin_addr;
	    
	    if ((addr.s_addr & net_mask_p->s_addr) ==
		(net_addr_p->s_addr & net_mask_p->s_addr))
	    {
		*if_addr_p = addr;
		break;
	    }
	}

	/*
	 *  Increment pointer to the next ifreq; some adjustment may be
	 *  required if the address is an IPv6 address
	 */
	ptr += sizeof(struct ifreq);
	
#	if defined(AF_INET6)
	{
	    if (ifreq->ifr_addr.sa_family == AF_INET6)
	    {
		ptr += sizeof(struct sockaddr_in6) - sizeof(struct sockaddr);
	    }
	}
#	endif
    }

    globus_free(buf_ptr);
    close(fd);

    return (if_addr_p->s_addr != htonl(0)) ? GLOBUS_SUCCESS : GLOBUS_FAILURE;
}
/* mpich_globus2_get_interface_address() */


/*
 * mpich_globus2_get_network_address_and_mask()
 *
 * Note: this is not IPv6 ready.
 */
static
int
mpich_globus2_get_network_address_and_mask(
    char *				str,
    struct in_addr *			net_addr,
    struct in_addr *			net_mask)
{
    int					rc;
    int					i;
    unsigned int			addr_octets[4];
    unsigned int			mask_octets[4];
    char				misc_chars[2];
    
    if (str == GLOBUS_NULL)
    {
	goto fn_error;
    }

    rc = sscanf(str, "%u.%u.%u.%u%c%u.%u.%u.%u%c",
		addr_octets + 3, addr_octets + 2,
		addr_octets + 1, addr_octets + 0,
		misc_chars + 0,
		mask_octets + 3, mask_octets + 2,
		mask_octets + 1, mask_octets + 0,
		misc_chars + 1);

    if (rc < 4 || rc == 5 || rc == 7 || rc == 8 || rc > 9)
    {
	goto fn_error;
    }

    if (rc > 4 && misc_chars[0] != '/')
    {
	goto fn_error;
    }

    /*
     *  Get the network address
     */
    net_addr->s_addr = 0;
    for (i = 3; i >= 0; i--)
    {
	if (addr_octets[i] > 255)
	{
	    goto fn_error;
	}
	
	net_addr->s_addr |= addr_octets[i] << (i * 8);
    }

    /*
     *  Get the network mask
     */
    if (rc == 4)
    {
	/* No mask was specified */
	net_mask->s_addr = 0;
	net_mask->s_addr = ~net_mask->s_addr;
    }
    else if (rc == 6)
    {
	/* The number of "on" bits were specified */
	if (mask_octets[3] > 32)
	{
	    goto fn_error;
	}

	/* The number of "on" bits were specified */
	net_mask->s_addr = (mask_octets[3] == 0) ? 0 :
	    (0xffffffff << (32 - mask_octets[3])) ;
    }
    else
    {
	/* The mask was specified as four octets */
	net_mask->s_addr = 0;
	for (i = 3; i >= 0; i--)
	{
	    if (mask_octets[i] > 255)
	    {
		goto fn_error;
	    }
	
	    net_mask->s_addr |= mask_octets[i] << (i * 8);
	}
    }

    /*
     * Convert the address and mask to network byte order
     */
    net_addr->s_addr = htonl(net_addr->s_addr);
    net_mask->s_addr = htonl(net_mask->s_addr);
    
    return GLOBUS_SUCCESS;

  fn_error:
    net_addr->s_addr = htonl(0);
    net_mask->s_addr = htonl(0);
    
    return GLOBUS_FAILURE;
} /* end mpich_globus2_get_network_address_and_mask() */

static void create_my_miproto(globus_byte_t **my_miproto, int *nbytes)
{
    char hostname[G2_MAXHOSTNAMELEN];
    char s_port[10];
    char s_tcptype[10];
    char s_nprotos[10];
    globus_io_attr_t attr;
    unsigned short port;
    char *cp;
    int nprotos;
    char * net_if_str;
    char *lan_id, *duroc_subjob;
    int lan_id_lng, lan_id_lng_lng;
    int localhost_id;
#   if defined(VMPI)
    int mpi_nbytes;
    char *mpi_miproto;
#   endif

    *nbytes = 0;
    nprotos = 0;

    /*******/
    /* MPI */
    /*******/

#   if defined(VMPI)
    {
	nprotos ++;
	mp_create_miproto(&mpi_miproto, &mpi_nbytes);
	(*nbytes) += mpi_nbytes; /* mpi_nbytes already included 1 for '\0' */
    }
#   endif

    /*******/
    /* TCP */
    /*******/

    nprotos ++;

    if (globus_libc_gethostname(hostname, G2_MAXHOSTNAMELEN))
    {
	MPID_Abort( (struct MPIR_COMMUNICATOR *)0, 1, "MPICH-G2", 
		    "create_my_miproto() - failed globus_libc_gethostname()" );
    } /* endif */
    globus_io_tcpattr_init(&attr);
#   if defined(GLOBUS_CALLBACK_GLOBAL_SPACE)
    { 
        globus_result_t result;
        result = globus_io_attr_set_callback_space(&attr, MpichG2Space);
        if (result != GLOBUS_SUCCESS)
        {
            globus_object_t* err = globus_error_get(result);
            char *errstring = globus_object_printable_to_string(err);
            char msg[1024];
            globus_libc_sprintf(msg, 
                "ERROR: create_my_miproto: "
		"failed globus_io_attr_set_callback_space:"
                " %s", 
                errstring);
            MPID_Abort((struct MPIR_COMMUNICATOR *)0, 1, "MPICH-G2", msg);
        } /* endif */
    }
#endif
    
    net_if_str = globus_libc_getenv("MPICH_GLOBUS2_USE_NETWORK_INTERFACE");

    if (net_if_str != GLOBUS_NULL)
    {
	struct in_addr			net_addr;
	struct in_addr			net_mask;
	struct in_addr			if_addr;
	int				rc;
	
	rc = mpich_globus2_get_network_address_and_mask(net_if_str,
							&net_addr,
							&net_mask);

	if (rc != GLOBUS_SUCCESS)
	{
	    globus_libc_fprintf(
		stderr,
		"MPICH-G2 WARNING - unable to parse the address/netmask "
		"specified in MPICH_GLOBUS2_USE_NETWORK_INTERFACE\n");
	    goto block_end;
	} /* endif */

	rc = mpich_globus2_get_interface_address(&net_addr,
						 &net_mask,
						 &if_addr);

	if (rc != GLOBUS_SUCCESS)
	{
	    globus_libc_fprintf(
		stderr,
		"MPICH-G2 WARNING - unable to located network interface "
		"specified in MPICH_GLOBUS2_USE_NETWORK_INTERFACE; using "
		"default interface\n");
	    goto block_end;    
	} /* endif */

	globus_libc_sprintf(hostname,
			    "%d.%d.%d.%d",
			    (ntohl(if_addr.s_addr) & 0xff000000) >> 24,
			    (ntohl(if_addr.s_addr) & 0x00ff0000) >> 16,
			    (ntohl(if_addr.s_addr) & 0x0000ff00) >> 8,
			    (ntohl(if_addr.s_addr) & 0x000000ff) >> 0);

	globus_libc_fprintf(
	    stderr,
	    "MPICH-G2 NOTE - using the network interface bound to %s\n",
	    hostname);

      block_end: ;
    } /* endif */

    /*
     * We need to set the tcp send and receive buffer sizes to something large
     * to deal with the large bandwidth - delay product associated with today's
     * WAN.
     */
    if (MpichGlobus2TcpBufsz > 0)
    {
	globus_io_attr_set_socket_sndbuf(&attr, MpichGlobus2TcpBufsz);
	globus_io_attr_set_socket_rcvbuf(&attr, MpichGlobus2TcpBufsz);
    } /* endif */

    /*
     * Don't delay small messages; avoiding the extra latency incurred by this
     * delay is probably far more important than saving the little bit of
     * bandwidth eaten by an extra TCP/IP header
     */
    globus_io_attr_set_tcp_nodelay(&attr, GLOBUS_TRUE);
    
    port = 0; /* must be 0 so it will be assigned */
              /* by globus_io_tcp_create_listener */
    globus_io_tcp_create_listener(&port, /* gets assigned */
                                    -1,  /* backlog, same as for listen() */
                                         /* ... whatever that means?      */
                                         /* specifying -1 here results in */
                                         /* a backlog of SOMAXCONN.       */
                                &attr,
                                &Handle); /* gets assigned */
    globus_io_tcpattr_destroy(&attr);

    /* when client connects to socket specified by    */
    /* 'Handle', the callback function will be called */
    globus_io_tcp_register_listen(&Handle,
                                listen_callback,
                                (void *) NULL); /* optional user arg */
                                                /* to be passed to callback */
    globus_libc_sprintf(s_port, "%d", (int) port);
    globus_libc_sprintf(s_tcptype, "%d", tcp);
    (*nbytes) += ((size_t) (strlen(s_tcptype) + 1
                            + strlen(hostname) + 1
                            + strlen(s_port) + 1));

    /* Make the difference between WAN-TCP & LAN-TCP using the
     * environment variable "GLOBUS_LAN_ID" provided by the user */

    duroc_subjob = globus_libc_getenv("GLOBUS_DUROC_SUBJOB_INDEX");
    if ( !duroc_subjob )
        MPID_Abort((struct MPIR_COMMUNICATOR *)0, 1, "MPICH-G2",
                   "create_my_miproto() - GLOBUS_DUROC_SUBJOB_INDEX undefined");
    {
        char *prim_lan_id;
        int tmp_lng;

        prim_lan_id = globus_libc_getenv("GLOBUS_LAN_ID");
        if ( !prim_lan_id )
        {
            lan_id_lng = strlen("GLOBUS_DUROC_SUBJOB_INDEX") + 2
                         + strlen(duroc_subjob);   /* +2 for _ and \0 */
            g_malloc(prim_lan_id, char*, lan_id_lng * sizeof(char));
            globus_libc_sprintf(prim_lan_id, "%s_%s",
                                "GLOBUS_DUROC_SUBJOB_INDEX", duroc_subjob);
        }
        else   /* check there are no white space in GLOBUS_LAN_ID variable */
        {
            if ( index(prim_lan_id, ' ') != NULL
                 || index(prim_lan_id, '\t') != NULL )
                MPID_Abort((struct MPIR_COMMUNICATOR *)0, 1, "MPICH-G2",
                           "white spaces and tabs are "
                           "not allowed in the variable GLOBUS_LAN_ID");
        }

        lan_id_lng = strlen(prim_lan_id);
        (*nbytes) += ((size_t) (lan_id_lng + 1));
        lan_id_lng++;
        g_malloc(lan_id, char*, lan_id_lng * sizeof(char));
        /* "lan_id_lng + 1" for '\0' */
        strcpy(lan_id, prim_lan_id);
        if ( !globus_libc_getenv("GLOBUS_LAN_ID") )
            g_free(prim_lan_id);
        /* determine the number of digits of the decimal representation of
         * the length of the string 'lan_id' ('\0' included) */
        lan_id_lng_lng = 0;
        tmp_lng = lan_id_lng;   /* can never be 0 */
        while ( tmp_lng > 0 )
        {
            lan_id_lng_lng++;
            tmp_lng /= 10;
        }
        (*nbytes) += (size_t) (lan_id_lng_lng + 1);
    }

    /* make the difference between localhost-TCP and LAN/WAN-TCP */
    /* at this point, we're sure that GLOBUS_DUROC_SUBJOB_INDEX is defined */
    /* in the envrionment variables (see test above with MPID_Abort): */
    /* duroc_subjob != NULL */

    localhost_id = atoi(duroc_subjob);
    (*nbytes) += (size_t) (strlen(duroc_subjob) + 1);

    /************************************************************************/
    /* acquire my_miproto info for other protos here ... increasing *nbytes */
    /************************************************************************/

    /**************************************/
    /* allocating and filling my_miproto */
    /**************************************/

    globus_libc_sprintf(s_nprotos, "%d", nprotos);
    (*nbytes) += ((size_t) (strlen(s_nprotos) + 1));

    g_malloc(*my_miproto, globus_byte_t *, *nbytes);

    cp = (char *) *my_miproto;
    globus_libc_sprintf(cp, "%s ", s_nprotos);
    cp += (strlen(s_nprotos) + 1);

    /* copying MPI */
#   if defined(VMPI)
    {
        globus_libc_sprintf(cp, "%s ", mpi_miproto);
	cp += mpi_nbytes; /* mpi_nbytes already included 1 for '\0' */
	g_free(mpi_miproto);
    }
#   endif
    
    /* copying TCP */
    globus_libc_sprintf(cp, "%s %s %s %d %s %d", s_tcptype, hostname, s_port,
                                                  lan_id_lng, lan_id,
                                                  localhost_id);
    cp += strlen(s_tcptype) + 1  + strlen(hostname) + 1  + strlen(s_port) + 1
           + lan_id_lng_lng + 1 + strlen(lan_id) + 1 + strlen(duroc_subjob) + 1;
    g_free(lan_id);

    /*****************************************************************/
    /* appending miproto for other protos to end of *my_miproto here */
    /*****************************************************************/

} /* end create_my_miproto() */

/*
 * build_vmpi_maps()
 *
 * Construct mapping between the global rank within the vendor MPI's MPI
 * MPI_COMM_WORLD and MPICH's MPI_COMM_WORLD, and vice versa.
 */
static void build_vmpi_maps()
{
#   if defined(VMPI)
    {
	struct channel_t *			channel;
	struct mpi_miproto_t *		mpi_miproto;
	struct miproto_t *			miproto;
	int					i;

	channel = &(CommworldChannels[MPID_MyWorldRank]);
	if ((channel->selected_proto) == NULL)
	{
	    globus_libc_fprintf(stderr, 
		"build_vmpi_maps: (channel->selected_proto) == NULL\n");
	    abort();
	} /* endif */
    
	mpi_miproto = NULL;
	if (channel->selected_proto->type == mpi)
	{
	    mpi_miproto = (struct mpi_miproto_t *)
		(channel->selected_proto->info);
	}
	else
	{
	    /*
	     * At this point we know that the selected proto to myself is NOT
	     * MPI.  In a world in which TCP and MPI are the only protos, this
	     * would be an error; however, later we may add other protos which
	     * are better than MPI (e.g., shm) in which case this would NOT be
	     * an error condition.
	     */
	    miproto = channel->proto_list;
	    while(miproto != NULL && miproto->type != mpi)
	    {
		miproto = miproto->next;
	    }

	    if (miproto != NULL)
	    {
		mpi_miproto = (struct mpi_miproto_t *) (miproto->info);
	    }
	}

	/*
	 * If we can't communicate using the vendor MPI, then we are done
	 */
	if (mpi_miproto == NULL)
	{
	    VMPI_MyWorldSize = 0;
	    VMPI_MyWorldRank = -1;
	    VMPI_GRank_to_VGRank = NULL;
	    VMPI_VGRank_to_GRank = NULL;

	    return;
	}
    
	VMPI_MyWorldRank = mpi_miproto->rank;
	VMPI_MyWorldSize = 0;

	/*
     * construct the mapping from MPICH to VMPI
     */
	g_malloc(VMPI_GRank_to_VGRank, int *, MPID_MyWorldSize * sizeof(int));
    
	for (i = 0; i < MPID_MyWorldSize; i++)
	{
	    miproto = CommworldChannels[i].selected_proto;

	    if (i == MPID_MyWorldRank)
	    {
		/* We might be using a different protocol for communicating
		   with ourself (such as a local buffer copy), but we still
		   need to be included in the map.  Actually, I'm not sure MPI
		   supports communication with oneself, but we include this for
		   consistency anyway */
		VMPI_GRank_to_VGRank[i] = VMPI_MyWorldRank;
		VMPI_MyWorldSize++;
	    }
	    else if (miproto->type == mpi)
	    {
		VMPI_GRank_to_VGRank[i] =
		    ((struct mpi_miproto_t *) (miproto->info))->rank;
		VMPI_MyWorldSize++;
	    }
	    else
	    {
		VMPI_GRank_to_VGRank[i] = -1;
	    }
	}

	/*
	 * construct the mapping from VMPI to MPICH
	 */
	g_malloc(VMPI_VGRank_to_GRank, int *, VMPI_MyWorldSize * sizeof(int));
    
	for (i = 0; i < MPID_MyWorldSize; i++)
	{
	    miproto = CommworldChannels[i].selected_proto;

	    if (VMPI_GRank_to_VGRank[i] >= 0)
	    {
		if (VMPI_GRank_to_VGRank[i] >= VMPI_MyWorldSize)
		{
		    globus_libc_fprintf(stderr, 
			"build_vmpi_maps: "
			"VMPI_GRank_to_VGRank[i] < VMPI_MyWorldSize\n");
		    abort();
		} /* endif */
		VMPI_VGRank_to_GRank[VMPI_GRank_to_VGRank[i]] = i;
	    }
	}
    }
#   endif /* defined(VMPI) */
} /* end build_vmpi_maps() */

void free_vmpi_maps()
{
#   if defined(VMPI)
    {
	g_free(VMPI_GRank_to_VGRank);
	g_free(VMPI_VGRank_to_GRank);
    }
#   endif /* defined(VMPI) */
} /* end free_vmpi_maps() */

/*
 * void get_topology(int rank_in_my_subjob, 
 *                   int my_subjob_size, 
 *                   int **subjob_addresses,
 *                   int *nprocs, 
 *                   int *nsubjobs, 
 *                   int *my_grank)
 *
 * MUST be called by EVERY proc, each supplying rank_in_my_subjob
 * rank_in_my_subjob==0 -> subjobmaster, else subjobslave
 * and my_subjob_size.
 *
 * fills the remaining args:
 *     subjob_addresses - malloc'd and filled for OTHER subjobmasters only
 *                        inter_subjob_addr's of other subjobmasters
 *                        my subjob_addr NOT included (so njobs-1)
 *     nsubjobs - populated for subjobmasters only.
 *                total number of procs
 *     nprocs - total number of procs
 *     my_grank - my rank in [0, nprocs-1]
 *     
 */

/* NICK: should return 'globus-like' rc ... not 'void' */

static void get_topology(int rank_in_my_subjob, 
			int my_subjob_size, 
			int **subjob_addresses,
			int *nprocs, 
			int *nsubjobs, 
			int *my_grank)
{
    char topology_buff[GRAM_MYJOB_MAX_BUFFER_LENGTH];
    char *buff;
    int bufflen;
    int i;
    static unsigned int call_idx = 0;
    int sj0_master_idx;    /* used by subjobmasters only */
    int *job_sizes;        /* used by subjobmaster 0 only */
    int *g_ranks;          /* used by subjobmaster 0 only */

    call_idx ++;

    if (rank_in_my_subjob)
    {
	/* subjob slave */
#       if defined(VMPI)
        {
	    int rc;
	    int v[2];

	    /* MPI_Bcast(v, 2, MPI_INT, 0, MPI_COMM_WORLD); */
	    rc = vmpi_error_to_mpich_error(
		    mp_bootstrap_bcast((void *) v, /* buff */
					2,         /* count */
			                0));       /* type, 0 == vMPI_INT */
	    if (rc != MPI_SUCCESS)
	    {
		globus_libc_fprintf(stderr, 
		    "ERROR: get_topology(): erroneous rc = %d from "
		    "mp_bootstrap_bcast (non-root)\n",
		    rc);
		exit(1);
	    } /* endif */

	    /* 
	     * note: setting my_grank as i do below works BECAUSE 
	     *       rank_in_my_subjob == rank in vMPI_COMM_WORLD.
	     *       we know this because DUROC uses vMPI for 
	     *       intra-subjob messaging, and a side-effect of
	     *       that is it sets rank_in_my_subjob to the 
	     *       rank in the vMPI_COMM_WORLD it creates.
	     *
	     *       we also know that the rank that DUROC assigns
	     *       us is the one we will be using because vMPI_Init
	     *       is called only once ... we test if mp_initialized
	     *       and don't call vMPI_Init again.
	     * 
	     */
	    
	    *nprocs   = v[0];
	    *my_grank = v[1] + rank_in_my_subjob; /* v[1] == subjobmstr grank */
        }
#       else
        {
	    char *rbuff;
	    char tag[200];

	    sprintf(tag, "%s%d", SUBJOB_MASTER_TO_SLAVE_T, call_idx);

	    intra_subjob_receive(tag,     /* tag */
				&bufflen, /* nbytes received? */
				&rbuff);  /* message */

	    sscanf(rbuff, "%d %d", nprocs, my_grank);
	    globus_libc_free(rbuff);
        }
#       endif
    }
    else
    {
	/* subjob master */
	int duroc_subjobmaster_rank;
	int my_subjob_addr;
	int rsl_subjob_rank;
	char *rsl_subjob_rank_env_var;

	globus_duroc_runtime_inter_subjob_structure(&my_subjob_addr,
						    nsubjobs,
						    subjob_addresses);
	/* finding index of master subjob 0, he */
	/* is the one with the lowest address   */
	for (i = 0, sj0_master_idx = -1, duroc_subjobmaster_rank = 0; 
	    i < *nsubjobs; i ++)
	{
	    if ((sj0_master_idx == -1 
		    && (*subjob_addresses)[i] < my_subjob_addr)
	    || (sj0_master_idx != -1 
		    && (*subjob_addresses)[i] 
				< (*subjob_addresses)[sj0_master_idx]))
		sj0_master_idx = i;
	    if ((*subjob_addresses)[i] < my_subjob_addr)
		duroc_subjobmaster_rank ++;
	} /* endfor */
	/* globus_duroc_runtime_inter_subjob_structure reports the */
	/* number of REMOTE subjobs (*other* than the subjob i'm   */
	/* master of).  to get the TOTAL number of subjobs in this */
	/* run i must increment the value reported by              */
	/* globus_duroc_runtime_inter_subjob_structure             */
	(*nsubjobs) ++;

	/* NICK: should not exit here ... should set globus-like rc. */
	if (!(rsl_subjob_rank_env_var=getenv("GLOBUS_DUROC_SUBJOB_INDEX")))
	{
	    globus_libc_fprintf(stderr, 
		"ERROR: required environment variable "
		"GLOBUS_DUROC_SUBJOB_INDEX not set.\n");
	    globus_libc_fprintf(stderr, 
		"       Each subjob in envoking RSL must have "
		"GLOBUS_DUROC_SUBJOB_INDEX\n");
	    globus_libc_fprintf(stderr, 
		"       set to rank (0, 1, 2, ...) of subjob as it "
		"appears in the envoking RSL.\n");
	    exit(1);
	} /* endif */
	rsl_subjob_rank = atoi(rsl_subjob_rank_env_var);
	if (rsl_subjob_rank < 0 || rsl_subjob_rank >= *nsubjobs)
	{
	    globus_libc_fprintf(stderr, 
		"ERROR: env variable GLOBUS_DUROC_SUBJOB_INDEX "
		"%d must be >= 0 and\n", 
		rsl_subjob_rank);
	    globus_libc_fprintf(stderr, 
		"ERROR: less than the number of subjobs %d for this run.\n", 
		*nsubjobs);
	    exit(1);
	} /* endif */

	if (duroc_subjobmaster_rank)
	{
	    /* NOT master of subjob 0 */

	    sprintf(topology_buff,
		    "%d %d %d",
		    duroc_subjobmaster_rank,
		    rsl_subjob_rank,
		    my_subjob_size);

	    {
		char tag[200];

		sprintf(tag, "%s%d", 
		    SUBJOB_MASTER_TO_SUBJOB0_MASTER_T, call_idx);
		globus_duroc_runtime_inter_subjob_send(
		    (*subjob_addresses)[sj0_master_idx], /* dest */
		    tag,                                 /* tag */
		    strlen(topology_buff)+1,             /* nbytes */
		    (globus_byte_t *) topology_buff);    /* data */

		sprintf(tag, "%s%d", 
		    SUBJOB0_MASTER_TO_SUBJOB_MASTER_T, call_idx);
		globus_duroc_runtime_inter_subjob_receive(
		    tag,                       /* tag */
		    &bufflen,                  /* nbytes received? */
		    (globus_byte_t **) &buff); /* message */
	    }

	    sscanf(buff, "%d %d", nprocs, my_grank);

	    globus_libc_free(buff);
	}
	else
	{
	    /* master of subjob 0 */

	    int j;
	    int temp;
	    /* vectors len nsubjobs, all indexed by duroc_subjobmaster_rank */
	    int *rsl_ranks; /* received from other subjob masters */

	    /* NICK: exiting on these failed mallocs is not right thing */
	    /*       to do.  should set error rc and return. fix that   */
	    /*       later when i learn more about globus rc stuff.     */
	    if (!(rsl_ranks = 
		(int *) globus_libc_malloc(*nsubjobs*sizeof(int))))
	    {
		globus_libc_fprintf(stderr, 
		    "ERROR: failed malloc of %d bytes\n", 
		    *nsubjobs*sizeof(int));
		exit(1);
	    } /* endif */
	    if (!(job_sizes = 
		(int *) globus_libc_malloc(*nsubjobs*sizeof(int))))
	    {
		globus_libc_fprintf(stderr, 
		    "ERROR: failed malloc of %d bytes\n", 
		    *nsubjobs*sizeof(int));
		exit(1);
	    } /* endif */
	    if (!(g_ranks = (int *) globus_libc_malloc(*nsubjobs*sizeof(int))))
	    {
		globus_libc_fprintf(stderr, 
		    "ERROR: failed malloc of %d bytes\n", 
		    *nsubjobs*sizeof(int));
		exit(1);
	    } /* endif */

	    /* need to sort subjob_addresses so that i may associate */
	    /* (using incoming duroc_subjobmaster_rank) incoming     */
	    /* rsl_subjob_rank and my_subjob_size with dest addr     */
	    /* in subjob_addresses                                   */
	    for (i = 1; i < *nsubjobs-1; i ++)
	    {
		for (j = i; j > 0; j --)
		{
		    if ((*subjob_addresses)[j] < (*subjob_addresses)[j-1])
		    {
			temp = (*subjob_addresses)[j];
			(*subjob_addresses)[j] = (*subjob_addresses)[j-1];
			(*subjob_addresses)[j-1] = temp;
		    } /* endif */
		} /* endfor */
	    } /* endfor */

	    /* rsl_ranks[] and job_sizes[] are indexed by  */
	    /* duroc_subjobmaster_rank, and i know that my */
	    /* duroc_subjobmaster_rank==0                  */
	    rsl_ranks[0] = rsl_subjob_rank;
	    job_sizes[0] = my_subjob_size;

	    {
		char tag[200];

		sprintf(tag, "%s%d", 
		    SUBJOB_MASTER_TO_SUBJOB0_MASTER_T, call_idx);

		for (i = 1; i < *nsubjobs; i ++)
		{
		    int ranks, sizes;
		    
		    /*
		     * receiving 3 longs from other subjob master
		     *    duroc_subjobmaster_rank (used to index job_sizes[] 
		     *                             and rsl_ranks[])
		     *    rsl_subjob_rank 
		     *    my_subjob_size
		     */

		    globus_duroc_runtime_inter_subjob_receive(
			tag,                       /* tag */
			&bufflen,                  /* nbytes received? */
			(globus_byte_t **) &buff); /* message */

		    sscanf(buff, "%d %d %d", &j, &ranks, &sizes);
		    rsl_ranks[j] = ranks;
		    job_sizes[j] = sizes;
		    
		    globus_libc_free(buff);
		} /* endfor */
	    }

	    /* calculating nprocs and everyones' g_rank based */
	    /* on rsl_rank and job_sizes ...                  */
	    /* mygrank = sum job_size for all rsl_ranks       */
	    /*           that are less than mine              */
	    for (i = 0, *nprocs = 0; i < *nsubjobs; i ++)
	    {
		(*nprocs) += job_sizes[i];
		for (g_ranks[i] = 0, j = 0; j < *nsubjobs; j ++)
		if (rsl_ranks[i] > rsl_ranks[j])
		    g_ranks[i] += job_sizes[j];
	    } /* endfor */
	    *my_grank = g_ranks[0];

	    {
		char tag[200];

		sprintf(tag, "%s%d", 
		    SUBJOB0_MASTER_TO_SUBJOB_MASTER_T, call_idx);

		/* sending other subjob masters nprocs and their g_rank */
		for (i = 0; i < *nsubjobs-1; i ++)
		{
		    sprintf(topology_buff, "%d %d", *nprocs, g_ranks[i+1]);
		    globus_duroc_runtime_inter_subjob_send(
			(*subjob_addresses)[i],           /* dest */
			tag,                              /* tag */
			strlen(topology_buff)+1,          /* nbytes */
			(globus_byte_t *) topology_buff); /* data */
		} /* endfor */
	    }

	    globus_libc_free(rsl_ranks);
	    globus_libc_free(job_sizes);
	    globus_libc_free(g_ranks);

	} /* endif */

	/* all subjob masters sending nprocs and their g_rank to their slaves */
#       if defined(VMPI)
	{
	    int v[2];
	    int rc;

	    v[0] = *nprocs;
	    v[1] = *my_grank;

	    /* MPI_Bcast(v, 2, MPI_INT, 0, MPI_COMM_WORLD); */
	    rc = vmpi_error_to_mpich_error(
		    mp_bootstrap_bcast((void *) v, /* buff */
					2,          /* count */
					0));        /* type, 0 == vMPI_INT */
	    if (rc != MPI_SUCCESS)
	    {
		globus_libc_fprintf(stderr, 
		    "ERROR: get_topology(): erroneous rc = %d from "
		    "mp_bootstrap_bcast (root)\n",
		    rc);
		exit(1);
	    } /* endif */
	}
#       else
        {
	    {
		char tag[200];

		sprintf(tag, "%s%d", SUBJOB_MASTER_TO_SLAVE_T, call_idx);

		for (i = 1; i < my_subjob_size; i ++)
		{
		    sprintf(topology_buff, "%d %d", *nprocs, (*my_grank) + i);
		    intra_subjob_send(i,                     /* dest */
				    tag,                     /* tag */
				    strlen(topology_buff)+1, /* nbytes */
				    topology_buff);          /* data */
		} /* endfor */
	    }
        }
#       endif
    } /* endif */

} /* end get_topology() */

static void distribute_byte_array(globus_byte_t *inbuff,
				    int inbufflen,
				    int rank_in_my_subjob,
				    int my_subjob_size,
				    int *subjob_addresses,
				    int nprocs,
				    int nsubjobs,
				    int my_grank,
				    globus_byte_t **outbuffs,
				    int *outbufflens)
{

    int i;
    char *buff;
    int bufflen;
    static unsigned int call_idx = 0;

/* globus_libc_fprintf(stderr, "%d: enter distribute_byte_array: rank_in_my_subjob %d %d bytes\n", MPID_MyWorldRank, rank_in_my_subjob, inbufflen); */
    call_idx ++;

    /* initialization */
    for (i = 0; i < nprocs; i ++)
    {
	outbuffs[i]    = (globus_byte_t *) NULL;
	outbufflens[i] = 0;
    } /* endfor */

    if (rank_in_my_subjob)
    {
/* globus_libc_fprintf(stderr, "%d: distribute_byte_array: i am subjob slave\n", MPID_MyWorldRank); */
	/* subjob slave */
#       if defined(VMPI)
        {
	    int rc;

	    /* MPI_Gather(&inbufflen,  */
			/* 1, */
			/* MPI_INT, */
			/* (void *) NULL,  */
			/* 1, */
			/* MPI_INT, */
			/* 0, */
			/* MPI_COMM_WORLD); */
	    rc = vmpi_error_to_mpich_error(
		    mp_bootstrap_gather((void *) &inbufflen,   /* sendbuff */
				1,                   /* sendcount */
				(void *) NULL,       /* rcvbuff, significant 
					                at root only */
				1));                  /* rcvcount, significant
					                at root only */
	    if (rc != MPI_SUCCESS)
	    {
		globus_libc_fprintf(stderr, 
		    "ERROR: distribute_byte_array(): erroneous rc = %d from "
		    "mp_bootstrap_gather (non-root)\n",
		    rc);
		exit(1);
	    } /* endif */

	    /* MPI_Gatherv((char *) inbuff,  */
			/* inbufflen,  */
			/* MPI_CHAR,  */
			/* (void *) NULL,  */
			/* (int *) NULL,  */
			/* (int *) NULL,  */
			/* MPI_CHAR, */
			/* 0, */
			/* MPI_COMM_WORLD); */
	    rc = vmpi_error_to_mpich_error(
		    mp_bootstrap_gatherv((void *) inbuff,  /* sendbuff */
				inbufflen,           /* sendcount */
				(void *) NULL,       /* rcvbuff, significant 
					                at root only */
				(int *) NULL,        /* rcvcounts, significant
					                at root only */
				(int *) NULL));      /* displs, significant
					                at root only */
	    if (rc != MPI_SUCCESS)
	    {
		globus_libc_fprintf(stderr, 
		    "ERROR: distribute_byte_array(): erroneous rc = %d from "
		    "mp_bootstrap_gatherv (non-root)\n",
		    rc);
		exit(1);
	    } /* endif */

        }
#       else
        {
	    char *bigbuff;
	    char *t;
	    char tagged_intrabuff[GRAM_MYJOB_MAX_BUFFER_LENGTH];

	    if (sizeof(tagged_intrabuff) < (2*HEADERLEN)+inbufflen)
	    {
		if (!(bigbuff = 
		    (char *) globus_libc_malloc((2*HEADERLEN)+inbufflen)))
		{
		    globus_libc_fprintf(stderr, 
			"ERROR: failed malloc of %d bytes\n", 
			(2*HEADERLEN)+inbufflen);
		    exit(1);
		} /* endif */
		t = bigbuff;
	    }
	    else
	    {
		t = tagged_intrabuff;
		bigbuff = (char *) NULL;
	    } /* endif */

	    /* tagging and copying my byte array for distribution */
	    sprintf(t,              "%d ",  my_grank);
	    sprintf(t+HEADERLEN,    "%d ",  inbufflen);
	    memcpy(t+(2*HEADERLEN), inbuff, inbufflen);

	    {
		char tag[200];
		sprintf(tag, "%s%d", SUBJOB_SLAVE_TO_MASTER_D, call_idx);
		/* send my byte array to my master */
		intra_subjob_gather(rank_in_my_subjob,
				    my_subjob_size,
				    t,                        /* data */
				    (2*HEADERLEN)+inbufflen,  /* nbytes */
				    tag,                      /* tag */
				    (int *) NULL,         /* subjob mstr only */
				    (char **) NULL);      /* subjob mstr only */
	    }

	    if (bigbuff)
		globus_libc_free(bigbuff);
	}
#       endif
	
	/* receiving all other byte arrays from my master */
	i = 0; 
	while (i < nprocs)
	{
	    char *rbuff;
	    int nbuffs;
/* globus_libc_fprintf(stderr, "%d: distribute_byte_array: subjob slave: top of while loop i %d nprocs %d\n", MPID_MyWorldRank, i, nprocs); */

#           if defined(VMPI)
            {
		int bsize;
		int rc;

		/* MPI_Bcast(&bsize, 1, MPI_INT, 0, MPI_COMM_WORLD); */
		rc = vmpi_error_to_mpich_error(
			/* NICK */
			mp_bootstrap_bcast((void *) &bsize, /* buff */
					    1,   /* count */
					    0)); /* type, 0 == vMPI_INT */
		if (rc != MPI_SUCCESS)
		{
		    globus_libc_fprintf(stderr, 
			"ERROR: distribute_byte_array(): erroneous rc = %d "
			"from mp_bootstrap_bcast (non-root, int)\n",
			rc);
		    exit(1);
		} /* endif */

		if (!(rbuff = (char *) globus_libc_malloc(bsize)))
		{
		    globus_libc_fprintf(stderr, 
			"ERROR: distribute_byte_array(): "
			"failed malloc of %d bytes\n", 
			bsize);
		    exit(1);
		} /* endif */

		/* MPI_Bcast(rbuff, bsize, MPI_CHAR, 0, MPI_COMM_WORLD); */
		rc = vmpi_error_to_mpich_error(
			mp_bootstrap_bcast((void *) rbuff, /* buff */
					    bsize, /* count */
					    1));   /* type, 1 == vMPI_CHAR */
		if (rc != MPI_SUCCESS)
		{
		    globus_libc_fprintf(stderr, 
			"ERROR: distribute_byte_array(): erroneous rc = %d "
			"from mp_bootstrap_bcast (non-root, char)\n",
			rc);
		    exit(1);
		} /* endif */

	    }
#           else
            {
		char tag[200];
		sprintf(tag, "%s%d", SUBJOB_MASTER_TO_SLAVE_D, call_idx);
/* globus_libc_fprintf(stderr, "%d: distribute_byte_array: subjob slave: before intra_subjob_bcast\n", MPID_MyWorldRank); */
		intra_subjob_bcast(rank_in_my_subjob,
				    my_subjob_size,
				    tag,              /* tag */
				    &bufflen,         /* nbytes rcvd? */
				    &rbuff);          /* message */
/* globus_libc_fprintf(stderr, "%d: distribute_byte_array: subjob slave: after intra_subjob_bcast\n", MPID_MyWorldRank); */
	    }
#           endif

	    extract_byte_arrays(rbuff, &nbuffs, outbuffs, outbufflens);
	    globus_libc_free(rbuff);
	    i += nbuffs;

	} /* endwhile */
    }
    else 
    {
	/* subjob master */
	char *my_subjob_buff;
	int my_subjob_buffsize;
/* globus_libc_fprintf(stderr, "%d: distribute_byte_array: i am subjob master\n", MPID_MyWorldRank); */

#       if defined(VMPI)
        {
	    char *temp_buff;
	    char *dest;
	    int *rcounts;
	    int *displs;
	    int rc;

	    if (!(rcounts = 
		    (int *) globus_libc_malloc(my_subjob_size*sizeof(int))))
	    {
		globus_libc_fprintf(stderr, 
		    "ERROR: failed malloc of %d bytes\n", 
		    (int) (my_subjob_size*sizeof(int)));
		exit(1);
	    } /* endif */
	    if (!(displs = 
		    (int *) globus_libc_malloc(my_subjob_size*sizeof(int))))
	    {
		globus_libc_fprintf(stderr, 
		    "ERROR: failed malloc of %d bytes\n", 
		    (int) (my_subjob_size*sizeof(int)));
		exit(1);
	    } /* endif */

	    /* MPI_Gather(&inbufflen,  */
			/* 1,  */
			/* MPI_INT,  */
			/* rcounts,  */
			/* 1, */
			/* MPI_INT, */
			/* 0,  */
			/* MPI_COMM_WORLD); */
	    rc = vmpi_error_to_mpich_error(
		    mp_bootstrap_gather((void *) &inbufflen,   /* sendbuff */
				1,                   /* sendcount */
				(void *) rcounts,    /* rcvbuff  */
				1));                 /* rcvcount */

	    if (rc != MPI_SUCCESS)
	    {
		globus_libc_fprintf(stderr, 
		    "ERROR: distribute_byte_array(): erroneous rc = %d from "
		    "mp_bootstrap_gather (root)\n",
		    rc);
		exit(1);
	    } /* endif */

	    my_subjob_buffsize = displs[0] = 0;
	    for (i = 0; i < my_subjob_size; i ++)
	    {
		my_subjob_buffsize += rcounts[i];
		if (i)
		    displs[i] = displs[i-1] + rcounts[i-1];
	    } /* endfor */
	    if (!(temp_buff = (char *) globus_libc_malloc(my_subjob_buffsize)))
	    {
		globus_libc_fprintf(stderr, 
		    "ERROR: failed malloc of %d bytes\n", my_subjob_buffsize);
		exit(1);
	    } /* endif */

	    /* MPI_Gatherv((char *) inbuff,  */
			/* inbufflen,  */
			/* MPI_CHAR,  */
			/* temp_buff,  */
			/* rcounts,  */
			/* displs,  */
			/* MPI_CHAR,  */
			/* 0,  */
			/* MPI_COMM_WORLD); */
	    rc = vmpi_error_to_mpich_error(
		    mp_bootstrap_gatherv((void *) inbuff,   /* sendbuff */
				inbufflen,            /* sendcount */
				(void *) temp_buff,   /* rcvbuff */
				rcounts,              /* rcvcounts */
				displs));             /* displacements */

	    if (rc != MPI_SUCCESS)
	    {
		globus_libc_fprintf(stderr, 
		    "ERROR: distribute_byte_array(): erroneous rc = %d from "
		    "mp_bootstrap_gatherv (root)\n",
		    rc);
		exit(1);
	    } /* endif */

	    my_subjob_buffsize += (HEADERLEN+(my_subjob_size*2*HEADERLEN));
	    if (!(my_subjob_buff = 
		    (char *) globus_libc_malloc(my_subjob_buffsize)))
	    {
		globus_libc_fprintf(stderr, 
		    "ERROR: failed malloc of %d bytes\n", my_subjob_buffsize);
		exit(1);
	    } /* endif */
	    sprintf(my_subjob_buff,               "%d ",  my_subjob_size);
	    sprintf(my_subjob_buff+HEADERLEN,     "%d ",  my_grank);
	    sprintf(my_subjob_buff+(2*HEADERLEN), "%d ",  inbufflen);

	    memcpy(my_subjob_buff+(3*HEADERLEN),  inbuff, inbufflen);

	    dest = my_subjob_buff + (3*HEADERLEN) + inbufflen;

	    /* filling the rest of my_subjob_buff */
	    for (i = 1; i < my_subjob_size; i ++)
	    {
		sprintf(dest, "%d ", my_grank+i);
		sprintf(dest+HEADERLEN, "%d ", rcounts[i]);
		memcpy(dest+(2*HEADERLEN), temp_buff+displs[i], rcounts[i]);
		dest += ((2*HEADERLEN)+rcounts[i]);
	    } /* endfor */

	    globus_libc_free(temp_buff);
	    globus_libc_free(rcounts);
	    globus_libc_free(displs);
	}
#       else
	{
	    /* 
	     * constructing inter-subjob message MY subjob 
	     * to pass around ring of subjob masters 
	     */
	    
	    char *t;
	    int t_len = (3*HEADERLEN)+inbufflen;

	    if (!(t = (char *) globus_libc_malloc(t_len)))
	    {
		globus_libc_fprintf(stderr, 
		    "ERROR: failed malloc of %d bytes\n", t_len);
		exit(1);
	    } /* endif */

	    sprintf(t,               "%d ",  my_subjob_size);
	    sprintf(t+HEADERLEN,     "%d ",  my_grank);
	    sprintf(t+(2*HEADERLEN), "%d ",  inbufflen);
	    memcpy(t+(3*HEADERLEN),  inbuff, inbufflen);

	    {
		char tag[200];
		sprintf(tag, "%s%d", SUBJOB_SLAVE_TO_MASTER_D, call_idx);

		intra_subjob_gather(rank_in_my_subjob,
				    my_subjob_size,
				    t,
				    t_len,
				    tag,                 /* tag */
				    &my_subjob_buffsize, /* nbytes rcvd? */
				    &my_subjob_buff);    /* message */
	    }
	    globus_libc_free(t);
	}
#       endif

	extract_byte_arrays(my_subjob_buff, (int *) NULL, outbuffs,outbufflens);

#       if defined(VMPI)
        {
	    int rc;

	    /* MPI_Bcast(&my_subjob_buffsize, 1, MPI_INT, 0, MPI_COMM_WORLD); */
	    rc = vmpi_error_to_mpich_error(
		    mp_bootstrap_bcast((void *) &my_subjob_buffsize, /* buff */
					1,   /* count */
					0)); /* type, 0 == vMPI_INT */
	    if (rc != MPI_SUCCESS)
	    {
		globus_libc_fprintf(stderr, 
		    "ERROR: distribute_byte_array(): erroneous rc = %d "
		    "from mp_bootstrap_bcast (root, int)\n",
		    rc);
		exit(1);
	    } /* endif */

	    /* MPI_Bcast(my_subjob_buff,  */
			/* my_subjob_buffsize,  */
			/* MPI_CHAR,  */
			/* 0,  */
			/* MPI_COMM_WORLD); */
	    rc = vmpi_error_to_mpich_error(
		    mp_bootstrap_bcast((void *) my_subjob_buff, /* buff */
					my_subjob_buffsize,     /* count */
					1)); /* type, 0 == vMPI_CHAR */
	    if (rc != MPI_SUCCESS)
	    {
		globus_libc_fprintf(stderr, 
		    "ERROR: distribute_byte_array(): erroneous rc = %d "
		    "from mp_bootstrap_bcast (root, char)\n",
		    rc);
		exit(1);
	    } /* endif */

	}
#       else
        {
	    char tag[200];

	    /* sending inter-subjob message for MY subjob to all my slaves */
	    sprintf(tag, "%s%d", SUBJOB_MASTER_TO_SLAVE_D, call_idx);
/* globus_libc_fprintf(stderr, "%d: distribute_byte_array: subjob master: before intra_subjob_bcast our subjob\n", MPID_MyWorldRank); */
	    intra_subjob_bcast(rank_in_my_subjob,
				my_subjob_size,
				tag,                 /* tag */
				&my_subjob_buffsize, /* nbytes bcast */
				&my_subjob_buff);    /* message */
/* globus_libc_fprintf(stderr, "%d: distribute_byte_array: subjob master: after intra_subjob_bcast our subjob\n", MPID_MyWorldRank); */
	}
#       endif

	{
	    char tag[200];
	    sprintf(tag, "%s%d", SUBJOB_MASTER_TO_SUBJOB_MASTER_D, call_idx);

	    /* 
	     * sending inter-subjob message for MY subjob 
	     * to other subjob masters 
	     */
	    for (i = 0; i < nsubjobs-1; i ++)
	    {
		globus_duroc_runtime_inter_subjob_send( 
		    subjob_addresses[i],                 /* dest */ 
		    tag,                                 /* tag */
		    my_subjob_buffsize,                  /* nbytes */
		    (globus_byte_t *) my_subjob_buff);   /* data */
	    } /* endfor */
	}

	globus_libc_free(my_subjob_buff);

	/* receiving subjob byte arrays from other subjob masters */
	for (i = 0; i < nsubjobs-1; i ++)
	{
/* globus_libc_fprintf(stderr, "%d: distribute_byte_array: subjob master: top of for loop i %d nsubjobs-1 %d\n", MPID_MyWorldRank, i, nsubjobs-1); */
	    {
		char tag[200];
		sprintf(tag, "%s%d", 
		    SUBJOB_MASTER_TO_SUBJOB_MASTER_D, call_idx);

		globus_duroc_runtime_inter_subjob_receive(
		    tag,                        /* tag */
		    &bufflen,                   /* nbytes received? */
		    (globus_byte_t **) &buff);  /* message */
	    }

#           if defined(VMPI)
	    {
		int rc;

		/* MPI_Bcast(&bufflen, 1, MPI_INT, 0, MPI_COMM_WORLD); */
		rc = vmpi_error_to_mpich_error(
			mp_bootstrap_bcast((void *) &bufflen, /* buff */
					    1,                /* count */
					    0)); /* type, 0 == vMPI_INT */
		if (rc != MPI_SUCCESS)
		{
		    globus_libc_fprintf(stderr, 
			"ERROR: distribute_byte_array(): erroneous rc = %d "
			"from mp_bootstrap_bcast (root, int, 2)\n",
			rc);
		    exit(1);
		} /* endif */

		/* MPI_Bcast(buff, bufflen, MPI_CHAR, 0, MPI_COMM_WORLD); */
		rc = vmpi_error_to_mpich_error(
			mp_bootstrap_bcast((void *) buff, /* buff */
					    bufflen,      /* count */
					    1)); /* type, 1 == vMPI_CHAR */
		if (rc != MPI_SUCCESS)
		{
		    globus_libc_fprintf(stderr, 
			"ERROR: distribute_byte_array(): erroneous rc = %d "
			"from mp_bootstrap_bcast (root, char, 2)\n",
			rc);
		    exit(1);
		} /* endif */

	    }
#           else
	    {
		/* bcasting inter-subjob message to all my slaves */
		char tag[200];
		sprintf(tag, "%s%d", SUBJOB_MASTER_TO_SLAVE_D, call_idx);
/* globus_libc_fprintf(stderr, "%d: distribute_byte_array: subjob master: before intra_subjob_bcast other subjob\n", MPID_MyWorldRank); */
		intra_subjob_bcast(rank_in_my_subjob,
				    my_subjob_size,
				    tag,              /* tag */
				    &bufflen,         /* nbytes bcast */
				    &buff);           /* message */
/* globus_libc_fprintf(stderr, "%d: distribute_byte_array: subjob master: after intra_subjob_bcast other subjob\n", MPID_MyWorldRank); */
	    }
#           endif

	    extract_byte_arrays(buff, (int *) NULL, outbuffs, outbufflens);
	    globus_libc_free(buff);
	} /* endfor */
    } /* endif */

/* globus_libc_fprintf(stderr, "%d: exit distribute_byte_array: rank_in_my_subjob %d %d bytes\n", MPID_MyWorldRank, rank_in_my_subjob, inbufflen); */

} /* end distribute_byte_array() */

#if !defined(VMPI)
static void intra_subjob_send(int dest, char *tag_base, int nbytes, char *buff)
{
    char tag[100];
    char *bigtag;
    char *t;
    int i;
    /* NICK: This is a hack because globus_duroc_runtime_intra_subjob_send
     *       dictates that the tag+message must fit into a buffer the
     *       size of GRAM_MYJOB_MAX_BUFFER_LENGTH-10 and they ain't 
     *       likely gonna fix this Globus code ever ... they've moved on 
     *       to Web-services and have abandonded all this DUROC code for good.
     */
    /* char send_buff[GRAM_MYJOB_MAX_BUFFER_LENGTH]; */
    char send_buff[GRAM_MYJOB_MAX_BUFFER_LENGTH-15];
    int max_payload_size = GRAM_MYJOB_MAX_BUFFER_LENGTH - 10 
			    - strlen(tag_base) - 5;
    char *src;
    int bytes_sent;
    int ncpy;


    if (strlen(tag_base)+5 > sizeof(tag))
    {
	if (!(bigtag = (char *) globus_libc_malloc(strlen(tag_base)+5)))
	{
	    globus_libc_fprintf(stderr,
		"ERROR: failed malloc of %d bytes\n", 
		((int) strlen(tag_base))+5);
	    exit(1);
	} /* endif */
	t = bigtag;
    }
    else
    {
	bigtag = (char *) NULL;
	t = tag;
    } /* endif */

    /* sending as much as i can in the first buffer */
    sprintf(send_buff, "%d ", nbytes);
    ncpy = max_payload_size-HEADERLEN < nbytes 
	    ? max_payload_size-HEADERLEN 
	    : nbytes;

    memcpy(send_buff+HEADERLEN, buff, ncpy);

    sprintf(t, "%s0", tag_base);
    globus_duroc_runtime_intra_subjob_send(
	dest,                         /* dest */
	t,                            /* tag */
	HEADERLEN+ncpy,               /* nbytes */
	(globus_byte_t *) send_buff); /* data */

    /* pushing out remaining data */
    for (i = 1, bytes_sent = ncpy, src = buff+ncpy; bytes_sent < nbytes; i ++)
    {
	ncpy = max_payload_size < nbytes-bytes_sent
		? max_payload_size
		: nbytes-bytes_sent;
	memcpy(send_buff, src, ncpy);
	sprintf(t, "%s%d", tag_base, i);
	globus_duroc_runtime_intra_subjob_send(
	    dest,                         /* dest */
	    t,                            /* tag */
	    ncpy,                         /* nbytes */
	    (globus_byte_t *) send_buff); /* data */

	bytes_sent += ncpy;
	src        += ncpy;
    } /* endfor */

    /* clean-up */
    if (bigtag)
	globus_libc_free(bigtag);

} /* end intra_subjob_send() */

static void intra_subjob_receive(char *tag_base, int *rcvd_nbytes, char **buff)
{
    char tag[100];
    char *bigtag;
    char *t;
    int i;
    char rcv_buff[GRAM_MYJOB_MAX_BUFFER_LENGTH];
    int nr;
    char *dest;
    int bytes_rcvd;

    if (strlen(tag_base) > sizeof(tag)+5)
    {
	if (!(bigtag = (char *) globus_libc_malloc(strlen(tag_base)+5)))
	{
	    globus_libc_fprintf(stderr,
		"ERROR: failed malloc of %d bytes\n", 
		((int) strlen(tag_base))+5);
	    exit(1);
	} /* endif */
	t = bigtag;
    }
    else
    {
	bigtag = (char *) NULL;
	t = tag;
    } /* endif */

    /* rcv as much as i can in the first buffer */
    sprintf(t, "%s0", tag_base);
    globus_duroc_runtime_intra_subjob_receive(
	t,                           /* tag */
	&nr,                         /* nbytes received? */
	(globus_byte_t *) rcv_buff); /* message */
    sscanf(rcv_buff, "%d ", rcvd_nbytes);
    if (!(*buff = (char *) globus_libc_malloc(*rcvd_nbytes)))
    {
	globus_libc_fprintf(stderr,
	    "ERROR: failed malloc of %d bytes\n", *rcvd_nbytes);
	exit(1);
    } /* endif */

    memcpy(*buff, rcv_buff+HEADERLEN, nr-HEADERLEN);

    /* receiving remaining data */
    for (i = 1, bytes_rcvd = nr-HEADERLEN, dest = *buff+(nr-HEADERLEN); 
	bytes_rcvd < *rcvd_nbytes; i ++)
    {
	sprintf(t, "%s%d", tag_base, i);
	globus_duroc_runtime_intra_subjob_receive(
	    t,                           /* tag */
	    &nr,                         /* nbytes received? */
	    (globus_byte_t *) rcv_buff); /* message */
	memcpy(dest, rcv_buff, nr);

	bytes_rcvd += nr;
	dest       += nr;

    } /* endfor */

    /* clean-up */
    if (bigtag)
	globus_libc_free(bigtag);

} /* end intra_subjob_receive() */
#endif /* !defined(VMPI) */

static void extract_byte_arrays(char *rbuff, 
				int *nbuffs_p,  /* optional */
				globus_byte_t **outbuffs, 
				int *outbufflens)
{
    char *src;
    int nbuffs;
    int j;

    src = rbuff + HEADERLEN;
    sscanf(rbuff, "%d ", &nbuffs);
    if (nbuffs_p)
	*nbuffs_p = nbuffs;

    for (j = 0; j < nbuffs; j ++)
    {
	int id;

	sscanf(src, "%d ", &id);
/* globus_libc_fprintf(stderr, "%d: extract_byte_arrays: FOO extracting rank %d\n", MPID_MyWorldRank, id); */

	if (outbuffs[id])
	{
	    globus_libc_fprintf(stderr, 
		"ERROR(%d): just rcvd second byte array from %d\n", 
		MPID_MyWorldRank, id);
	    exit(1);
	} /* endif */

	sscanf(src+HEADERLEN, "%d ", outbufflens+id);
	if (!(outbuffs[id] = 
	    (globus_byte_t *) globus_libc_malloc(outbufflens[id])))
	{
	    globus_libc_fprintf(stderr, 
		"ERROR: failed malloc of %d bytes\n", outbufflens[id]);
	    exit(1);
	} /* endif */

	memcpy(outbuffs[id], src+(2*HEADERLEN), outbufflens[id]);

	src += ((2*HEADERLEN)+outbufflens[id]);
    } /* endfor */

} /* end extract_byte_arrays() */

#if !defined(VMPI)
/*
 *
 * NOTE: both bcast/gather assumes root is always rank_in_my_subjob=0
 *
 * for non-vMPI environments this function uses globus functions
 * to distribute information (both bcast/gather from/to subjob masters).
 * all such bcasts/gathers are done by configuring the procs within a
 * subjob in a binomial tree.  def of binomial tree:
 *   A binomial tree, B_k (k>=0), is an ordered tree of order k.  The root
 *   has k children (ordered from left to right) where each child is
 *   binomial tree B_(k-1), B_(k-2), ..., B_0.
 * note a B_k tree 
 *     - has 2**k nodes
 *     - height = k
 *     - there are k choose i nodes at dept i
 ****************************************************************************
 * ex
 *     B_0    root            =     0
 *
 *     B_1    root            =     0
 *             |                    | 
 *            B_0                   0
 *
 *     B_2    root            =          0
 *             |                         |
 *        +----+----+               +----+----+
 *        |         |               |         |
 *       B_1       B_0              0         0
 *                                  |      
 *                                  0
 *     B_3    root            =             0
 *             |                            |
 *        +----+----+               +-------+----+
 *        |    |    |               |       |    |
 *       B_2  B_1  B_0              0       0    0
 *                                  |       |      
 *                              +---+---+   |
 *                              |       |   |
 *                              0       0   0
 *                              |
 *                              0
 ****************************************************************************
 *   This uses a fairly basic recursive subdivision algorithm.
 *   The root sends to the process size/2 away; the receiver becomes
 *   a root for a subtree and applies the same process. 
 *
 *   So that the new root can easily identify the size of its
 *   subtree, the (subtree) roots are all powers of two (relative to the root)
 *   If m = the first power of 2 such that 2^m >= the size of the
 *   communicator, then the subtree at root at 2^(m-k) has size 2^k
 *   (with special handling for subtrees that aren't a power of two in size).
 *
 *
 * basically, consider every rank as a binary number.  find the least
 * significant (rightmost) '1' bit (LSB) in each such rank.  so, for each
 * rank you have the following bitmask:
 *
 *                 xxxx100...0
 *                 ^^^^^^^^^^^
 *                 |   |   |
 *         0 or more  LSB  +---- followed by zero or more 0's
 *          leading
 *          bits
 *  or
 *                 0.........0 (root only)
 *
 * each non-root node receives from the src who has the same rank
 * BUT has the LSB=0, so, 4 (100), 2 (01), and 1 (1) all rcv from 0.
 * 5 (101) and 6 (110) both rcv from 4 (100).
 * the sends are ordered from left->right by sending first to the
 * bit to the immediate right of the LSB.  so, 4 (100) sends first
 * to 6 (110) and then to 5 (101).  
 *
 * note that under this scheme the root (0) receives from nobody
 * and all the leaf nodes (sending to nobody) are the odd-numbered nodes.
 *   
 */

static void intra_subjob_bcast(int rank_in_my_subjob, 
				int my_subjob_size, 
				char *tag_base, 
				int *rcvd_nbytes, 
				char **buff)
{
    int mask;

    /*
     *   Do subdivision.  There are two phases:
     *   1. Wait for arrival of data.  Because of the power of two nature
     *      of the subtree roots, the source of this message is alwyas the
     *      process whose relative rank has the least significant bit CLEARED.
     *      That is, process 4 (100) receives from process 0, process 7 (111) 
     *      from process 6 (110), etc.   
     *   2. Forward to my subtree
     *
     *   Note that the process that is the tree root is handled automatically
     *   by this code, since it has no bits set.
     */

    /* loop to find LSB */
    for (mask = 0x1;
	mask < my_subjob_size && !(rank_in_my_subjob & mask);
	    mask <<= 1)
    ;

    if (rank_in_my_subjob & mask) 
    {
	/* i am not root */
	intra_subjob_receive(tag_base,    /* tag */
			    rcvd_nbytes,  /* nbytes rcvd? */
			    buff);        /* message */
    } /* endif */

    /*
     *   This process is responsible for all processes that have bits set from
     *   the LSB upto (but not including) mask.  Because of the "not including",
     *   we start by shifting mask back down one.
     */
    mask >>= 1;
    while (mask > 0) 
    {
	if (rank_in_my_subjob + mask < my_subjob_size) 
	{
	    intra_subjob_send(rank_in_my_subjob+mask, /* dest */
			    tag_base,                 /* tag */
			    *rcvd_nbytes,             /* nbytes */
			    *buff);                   /* data */
	} /* endif */
	mask >>= 1;
    } /* endwhile */

} /* end intra_subjob_bcast() */

static void intra_subjob_gather(int rank_in_my_subjob,
				int my_subjob_size,
				char *inbuff,
				int inbufflen,
				char *tag_base, 
				int *rcvd_nbytes, /* subjob master only */
				char **buff)      /* subjob master only */
{

    int mask;
    int my_subjob_buffsize;
    char *my_subjob_buff;
    char *dest;
    char *tag;

/* printf("rank_in_my_subjob %d: enter intra_subjob_gather\n", rank_in_my_subjob); fflush(stdout); */
    if (!(tag = (char *) globus_libc_malloc(strlen(tag_base)+10)))
    {
	globus_libc_fprintf(stderr, 
	    "ERROR: failed tag malloc of %d bytes\n", strlen(tag_base)+10);
	exit(1);
    } /* endif */

    /* 
     * take a guess of how big my_subjob_buff needs to be 
     * based on my inbuff size and the size of my subjob
     */

    my_subjob_buffsize = my_subjob_size*inbufflen+100;
    if (!(my_subjob_buff = (char *) globus_libc_malloc(my_subjob_buffsize)))
    {
	globus_libc_fprintf(stderr, 
	    "ERROR: failed my_subjob_buff malloc of %d bytes\n", 
	    my_subjob_buffsize);
	exit(1);
    } /* endif */

    memcpy(my_subjob_buff,  inbuff, inbufflen);
    dest = my_subjob_buff + inbufflen;

    for (mask = 0x1; 
	mask < my_subjob_size && !(rank_in_my_subjob & mask); 
	    mask <<= 1)
    {
	if (rank_in_my_subjob+mask < my_subjob_size)
	{
	    int bufflen;
	    char *rbuff;

	    sprintf(tag, "%s%d", tag_base, rank_in_my_subjob+mask);
	    intra_subjob_receive(tag,      /* tag */
				&bufflen,  /* nbytes received? */
				&rbuff);   /* message */

	    if (my_subjob_buffsize-(dest-my_subjob_buff) < bufflen)
	    {
		/* have to re-alloc */
		int disp = dest-my_subjob_buff;

		my_subjob_buffsize += (my_subjob_size*inbufflen+100);

		if (!(my_subjob_buff = 
			(char *) globus_libc_realloc(my_subjob_buff, 
						    my_subjob_buffsize)))
		{
		    globus_libc_fprintf(stderr, 
			"ERROR: failed realloc of %d bytes\n", 
			my_subjob_buffsize);
		    exit(1);
		} /* endif */

		dest = my_subjob_buff+disp;

	    } /* endif */

	    memcpy(dest, rbuff, bufflen);
	    dest += bufflen;

	    globus_libc_free(rbuff);
	} /* endif */
    } /* endfor */

    if (rank_in_my_subjob)
    {
	/* subjob slave */
	sprintf(tag, "%s%d", tag_base, rank_in_my_subjob);
	intra_subjob_send(rank_in_my_subjob - mask, /* dest */
			tag,                        /* tag */
			dest-my_subjob_buff,        /* nbytes */
			my_subjob_buff);            /* data */
	globus_libc_free(my_subjob_buff);
    } 
    else
    {
	/* subjob master */
	*rcvd_nbytes = dest-my_subjob_buff;
	*buff        = my_subjob_buff;
    } /* endif */

    globus_libc_free(tag);

} /* end intra_subjob_gather() */

#endif /* !defined(VMPI) */

static void print_CommWorldChannelsTable_row(struct commworldchannels *cp)
{
    if (cp)
    {
	int nprocs = cp->nprocs; 
	struct channel_t *channels = cp->channels;
	int i;
	struct miproto_t *mp;

	for (i = 0; i < nprocs; i ++)
	{
	    globus_libc_fprintf(stderr, 
		"        %d: channel(s) for proc %d\n", 
		MPID_MyWorldRank, i); 
	    for (mp = channels[i].proto_list; mp; mp = mp->next)
	    {
		switch (mp->type)
		{
		    case tcp:
			globus_libc_fprintf(stderr, 
			    "            %d: TCP: host >%s< port %d"
                            " lan_id >%s< localhost_id %d", 
			    MPID_MyWorldRank,
			    ((struct tcp_miproto_t *) (mp->info))->hostname, 
			    (int) ((struct tcp_miproto_t *) (mp->info))->port,
                            ((struct tcp_miproto_t *)(mp->info))->globus_lan_id,
                            ((struct tcp_miproto_t *)(mp->info))->localhost_id);
			break;
		    case mpi:
			globus_libc_fprintf(stderr,
			    "            %d: MPI: unique_string >%s< "
			    "rank %d", 
			    MPID_MyWorldRank,
			    ((struct mpi_miproto_t *) 
				(mp->info))->unique_session_string, 
			    ((struct mpi_miproto_t *) (mp->info))->rank);
			break;
		    default:
			globus_libc_fprintf(stderr,
			    "            %d: ERROR: encountered "
			    "unrecognized proto type %d", 
			    MPID_MyWorldRank, mp->type);
			break;
		} /* end switch() */
		if (mp == channels[i].selected_proto)
		    globus_libc_fprintf(stderr, " (selected)");
		globus_libc_fprintf(stderr, "\n");
	    } /* endfor */
	} /* endfor */
    } /* endif */

} /* end print_CommWorldChannelsTable_row() */

