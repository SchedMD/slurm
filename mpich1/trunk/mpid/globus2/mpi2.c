#include "chconfig.h"
#include "globdev.h"
#include "mpiimpl.h"
#include "mpimem.h"

/*************************/
/* Local Data Structures */
/*************************/

struct accepted_connection_t
{
    globus_io_handle_t handle;
    volatile globus_bool_t connected;
}; /* end struct accepted_connection_t */

struct open_port_t
{
    struct open_port_t *next;
    char port_name[MPI_MAX_PORT_NAME];
    globus_io_handle_t listen_handle;
}; /* end struct open_port_t */

/********************/
/* Global Variables */
/********************/

#ifdef GLOBUS_CALLBACK_GLOBAL_SPACE
extern globus_callback_space_t MpichG2Space;   
#endif
extern int MPID_MyWorldSize, MPID_MyWorldRank;
extern int CommworldChannelsTableSize;
extern int CommworldChannelsTableNcommWorlds;
extern struct commworldchannels *CommWorldChannelsTable;

static struct open_port_t *OpenPorts = (struct open_port_t *) NULL;

/*******************/
/* Local Functions */
/*******************/

static int connect_server_handshake(globus_io_handle_t *handle, 
				    MPIR_CONTEXT *remote_context,
				    MPIR_CONTEXT local_context,
				    struct MPIR_COMMUNICATOR *comm_ptr,
				    int *nprocs,
				    globus_byte_t **map);
static int connect_client_handshake(globus_io_handle_t *handle,
				    MPIR_CONTEXT *remote_context,
				    MPIR_CONTEXT local_context,
				    struct MPIR_COMMUNICATOR *comm_ptr,
				    int *nprocs,
				    globus_byte_t **map);
static int mark_commworldchannels_to_send(int *nsend, 
					globus_bool_t *sendflags, 
					struct MPIR_COMMUNICATOR *comm_ptr,
					int remote_ncommworlds,
					globus_byte_t *remote_namesbuff);
static int send_commworldchannels(globus_io_handle_t* handle, 
				    int nsend, 
				    int nflags, 
				    globus_bool_t *sendflags);
static int pack_world(struct commworldchannels *cwp, 
			globus_byte_t **buff, 
			int *buffsize, 
			int *packsize);
static int channel_pack_size(struct channel_t *chp);
static int pack_channel(char **buff, struct channel_t *cp);
static int recv_commworldchannels(globus_io_handle_t *handle);
static int send_rankmap(globus_io_handle_t *handle,
			struct MPIR_COMMUNICATOR *comm_ptr);
static int recv_rankmap(globus_io_handle_t *handle,
			int *nprocs, 
			globus_byte_t **map);
static int distribute_info_to_slaves(MPI_Comm comm, 
				    int root,
				    MPIR_CONTEXT remote_context,
				    int nmapprocs,
				    globus_byte_t *map);
static int receive_info_from_master(MPI_Comm comm, 
				    int root,
				    MPIR_CONTEXT *remote_context,
				    int *nmapprocs,
				    globus_byte_t **map);
static int build_new_intercommunicator(struct MPIR_COMMUNICATOR *comm_ptr,
					MPI_Comm *newcomm, 
					MPIR_CONTEXT remote_context,
					MPIR_CONTEXT local_context,
					int nmapprocs,
					globus_byte_t *map);

/* callback routines */

static void connect_listen_callback(void *callback_arg,         
                                    globus_io_handle_t *handle, 
                                    globus_result_t result);

/*******************/
/* Server Routines */
/*******************/

/*
 * it is assumed that this function is called by one proc per created port_name
 * which will be the SAME proc that 
 *     (a) is the root to subsequent calls to MPI_Comm_accept and 
 *     (b) calls MPI_Close_port
 */
int MPI_Open_port(MPI_Info info, char *port_name) 
{
    char hostname[G2_MAXHOSTNAMELEN];
    unsigned short port;
    globus_io_attr_t attr;
    struct open_port_t *p;

    if (globus_libc_gethostname(hostname, G2_MAXHOSTNAMELEN))
    {
        globus_libc_fprintf(stderr, 
	    "ERROR: MPI_Open_port: failed globus_libc_gethostname()\n");
        return MPI_ERR_INTERN;
    } /* endif */

    if (!(p = globus_libc_malloc(sizeof(struct open_port_t))))
    {
        globus_libc_fprintf(stderr, 
            "ERROR: MPI_Open_port: failed malloc %d bytes\n", 
            sizeof(struct open_port_t));
        return MPI_ERR_INTERN;
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
            globus_libc_fprintf(stderr,
                "ERROR: MPI_Open_port: failed "
                "globus_io_attr_set_callback_space: %s",
                errstring);
            return MPI_ERR_INTERN;
        } /* endif */
    } 
#endif

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
                                &(p->listen_handle)); /* gets assigned */
    globus_io_tcpattr_destroy(&attr);

    globus_libc_sprintf(port_name, "%s %d", hostname, port);

    globus_libc_sprintf(p->port_name, "%s %d", hostname, port);
    p->next = OpenPorts;
    OpenPorts = p;

    return MPI_SUCCESS;

} /* end MPI_Open_port() */

/*
 * it is assumed that this function is called by one proc per created port_name
 * which will be the SAME proc that created the port with MPI_Open_port
 */
int MPI_Close_port(char *port_name) 
{
    int rc;
    struct open_port_t *prev, *removed;

    if (OpenPorts)
    {
        if (strcmp(port_name, OpenPorts->port_name))
        {
            /* was not first in list */
            for (prev = OpenPorts; 
                prev->next && strcmp(port_name, (prev->next)->port_name); 
                    prev= prev->next)
            ;
            if (prev->next)
            {
                removed    = prev->next;
                prev->next = removed->next;
            }
            else
                removed = (struct open_port_t *) NULL;
        }
        else
        {
            /* was first in list */
            removed   = OpenPorts;
            OpenPorts = removed->next;
        } /* endif */
    } 
    else
	removed = (struct open_port_t *) NULL;

    if (removed)
    {
	globus_io_close(&(removed->listen_handle));
	g_free(removed);
	rc = MPI_SUCCESS;
    } 
    else
    {
        globus_libc_fprintf(stderr, 
            "ERROR: MPI_Close_port: could not find port >%s<\n", port_name);
        rc = MPI_ERR_INTERN;
    } /* endif */

    return rc;

} /* end MPI_Close_port() */

int MPI_Comm_accept(char *port_name, 
		    MPI_Info info, 
		    int root, 
		    MPI_Comm comm, 
		    MPI_Comm *newcomm) 
{
    struct MPIR_COMMUNICATOR *comm_ptr;
    int root_grank;
    MPIR_CONTEXT remote_context;
    MPIR_CONTEXT local_context;
    int nprocs;
    globus_byte_t *map = (globus_byte_t *) NULL;

    comm_ptr  = MPIR_GET_COMM_PTR(comm);

    if (comm  == MPI_COMM_NULL) 
    {
	static char myname[]="MPI_COMM_ACCEPT";
	int mpi_errno = MPIR_Err_setmsg(MPI_ERR_COMM, 
					MPIR_ERR_LOCAL_COMM, 
					myname, 
				"Intra communicator must not be MPI_COMM_NULL",
					(char *)0);
	return MPIR_ERROR(comm_ptr, mpi_errno, myname);
    } /* endif */

    if (root < 0 || root >= comm_ptr->np)
    {
        globus_libc_fprintf(stderr, 
            "ERROR: MPI_Comm_accept: invalid root %d for communicator "
	    "with %d procs\n", 
	    root, comm_ptr->np);
        return MPI_ERR_INTERN;
    } /* endif */

    root_grank = comm_ptr->lrank_to_grank[root];
 
    /* 'root' in 'comm' must map to one of the procs in my MPI_COMM_WORLD */
    if (root_grank < 0 || root_grank >= MPID_MyWorldSize)
    {
        globus_libc_fprintf(stderr, 
            "ERROR: MPI_Comm_accept: root %d mapped to root_grank %d "
	    "with MPID_MyWorldSize %d\n", 
	    root, root_grank, MPID_MyWorldSize);
        return MPI_ERR_INTERN;
    } /* endif */

    /* Allocate send context, inter-coll context and intra-coll context */
    MPIR_Context_alloc (comm_ptr, 3, &local_context);

    if (root_grank == MPID_MyWorldRank)
    {
	/* 
	 * only the root of 'accept' handshakes with 
	 * client-side root of 'connect'
	 */

	struct open_port_t *p;
	struct accepted_connection_t *cp;
	int rc;

	/*************************************************/
	/* finding port that this proc previously opened */
	/*************************************************/

	for (p = OpenPorts; p && strcmp(port_name, p->port_name); p = p->next)
	;

	if (!p)
	{
	    globus_libc_fprintf(stderr, 
		"ERROR: MPI_Comm_accept() could not find open port name >%s<\n",
		port_name);
	    return MPI_ERR_INTERN;
	} /* endif */

	/**********************************************************/
	/* allocating, listening, and establishing new connection */
	/**********************************************************/

	if (!(cp = globus_libc_malloc(sizeof(struct accepted_connection_t))))
	{
	    globus_libc_fprintf(stderr, 
		"ERROR: MPI_Comm_accept: failed malloc %d bytes\n", 
		sizeof(struct accepted_connection_t));
	    return MPI_ERR_INTERN;
	} /* endif */

	cp->connected = GLOBUS_FALSE;

	/* when client connects to socket specified by    */
	/* 'ConnectHandle', the callback function will be called */
	globus_io_tcp_register_listen(&(p->listen_handle),
				    connect_listen_callback,
				    (void *) cp); /* optional user arg */
						/* to be passed to callback */
	/****************************************/
	/* wait for connect on from client side */
	/****************************************/
	while (!(cp->connected))
	{
	    G2_WAIT
	} /* endwhile */

	/* handshaking over new connection */
	rc = connect_server_handshake(&(cp->handle), 
					&remote_context, 
					local_context,
					comm_ptr, 
					&nprocs, 
					&map);

	/* closing new connection */
	globus_io_close(&(cp->handle));

	if (rc)
	{
	    g_free(map);
	    return MPI_ERR_INTERN;
	} /* endif */

	if (distribute_info_to_slaves(comm, root, remote_context, nprocs, map))
	{
	    g_free(map);
	    return MPI_ERR_INTERN;
	} /* endif */
    } 
    else
    {
        if (receive_info_from_master(comm,root, &remote_context, &nprocs, &map))
        {
            g_free(map);
            return MPI_ERR_INTERN;
        } /* endif */
    } /* endif */

    build_new_intercommunicator(comm_ptr, 
				newcomm, 
				remote_context, 
				local_context, 
				nprocs, 
				map);

    g_free(map);

    return MPI_SUCCESS;

} /* end MPI_Comm_accept() */

/*******************/
/* Client Routines */
/*******************/

int MPI_Comm_connect(char *port_name, 
		    MPI_Info info, 
		    int root,
		    MPI_Comm comm, 
		    MPI_Comm *newcomm) 
{
    struct MPIR_COMMUNICATOR *comm_ptr;
    int root_grank;
    MPIR_CONTEXT remote_context;
    MPIR_CONTEXT local_context;
    int nprocs;
    globus_byte_t *map = (globus_byte_t *) NULL;

    comm_ptr  = MPIR_GET_COMM_PTR(comm);

    if (comm  == MPI_COMM_NULL) 
    {
	static char myname[]="MPI_COMM_CONNECT";
	int mpi_errno = MPIR_Err_setmsg(MPI_ERR_COMM, 
					MPIR_ERR_LOCAL_COMM, 
					myname, 
				"Intra communicator must not be MPI_COMM_NULL",
					(char *)0);
	return MPIR_ERROR(comm_ptr, mpi_errno, myname);
    } /* endif */

    if (root < 0 || root >= comm_ptr->np)
    {
        globus_libc_fprintf(stderr, 
            "ERROR: MPI_Comm_connect: invalid root %d for communicator "
	    "with %d procs\n", 
	    root, comm_ptr->np);
        return MPI_ERR_INTERN;
    } /* endif */

    root_grank = comm_ptr->lrank_to_grank[root];
 
    /* 'root' in 'comm' must map to one of the procs in my MPI_COMM_WORLD */
    if (root_grank < 0 || root_grank >= MPID_MyWorldSize)
    {
        globus_libc_fprintf(stderr, 
            "ERROR: MPI_Comm_connect: root %d mapped to root_grank %d "
	    "with MPID_MyWorldSize %d\n", 
	    root, root_grank, MPID_MyWorldSize);
        return MPI_ERR_INTERN;
    } /* endif */

    /* Allocate send context, inter-coll context and intra-coll context */
    MPIR_Context_alloc (comm_ptr, 3, &local_context);

    if (root_grank == MPID_MyWorldRank)
    {
	char hostname[G2_MAXHOSTNAMELEN];
	unsigned short port;
	globus_io_attr_t attr;
	globus_io_handle_t handle;
	int rc;

	sscanf(port_name, "%s %d", hostname, &rc);
	port = (unsigned short) rc;
	globus_io_tcpattr_init(&attr);
#if     defined(GLOBUS_CALLBACK_GLOBAL_SPACE)
        {
            globus_result_t result;
            result = globus_io_attr_set_callback_space(&attr, MpichG2Space);
            if (result != GLOBUS_SUCCESS)
            {
                globus_object_t* err = globus_error_get(result);
                char *errstring = globus_object_printable_to_string(err);
                globus_libc_fprintf(stderr,
                    "ERROR: MPI_Comm_connect: failed "
                    "globus_io_attr_set_callback_space: %s",
                    errstring);
                return MPI_ERR_INTERN;
            } /* endif */
        } 
#endif
	/*
	* Don't delay small messages; avoiding the extra latency 
	* incurred by this delay is probably far more important 
	* than saving the little bit of bandwidth eaten by an extra 
	* TCP/IP header
	*/
	globus_io_attr_set_tcp_nodelay(&attr, GLOBUS_TRUE);
	if (globus_io_tcp_connect(hostname,
				    port,
				    &attr,
				    &handle) != GLOBUS_SUCCESS)
	{
	    globus_libc_fprintf(stderr, 
		"ERROR: MPI_Comm_connect: failed globus_io_tcp_connect\n");
	    return MPI_ERR_INTERN;
	} /* endif */
	globus_io_tcpattr_destroy(&attr);

	/* handshaking over new connection */
	rc = connect_client_handshake(&handle, 
				    &remote_context, 
				    local_context,
				    comm_ptr, 
				    &nprocs, 
				    &map);

	/* closing new connection */
	globus_io_close(&handle);

	if (rc)
	{
	    /* error message already sent to stderr */
	    g_free(map);
	    return MPI_ERR_INTERN;
	} /* endif */

	if (distribute_info_to_slaves(comm, root, remote_context, nprocs, map))
	{
	    g_free(map);
	    return MPI_ERR_INTERN;
	} /* endif */
    } 
    else
    {
	if (receive_info_from_master(comm,root, &remote_context, &nprocs, &map))
	{
	    g_free(map);
	    return MPI_ERR_INTERN;
	} /* endif */
    } /* endif */

    build_new_intercommunicator(comm_ptr, 
				newcomm, 
				remote_context, 
				local_context, 
				nprocs, 
				map);

    g_free(map);

    return MPI_SUCCESS;

} /* end MPI_Comm_connect() */

/*******************/
/* Local Functions */
/*******************/

/*
 * returns 0 upon successful completion, otherwise returns non-zero
 */
static int connect_server_handshake(globus_io_handle_t *handle, 
				    MPIR_CONTEXT *remote_context,
				    MPIR_CONTEXT local_context,
				    struct MPIR_COMMUNICATOR *comm_ptr,
				    int *nprocs,
				    globus_byte_t **map)
{
    globus_size_t nbytes;
    globus_byte_t hdr[HEADERLEN];
    int i;
    int alloc_bytes;
    char *cp;
    int nsend;
    globus_bool_t *sendflags;
    int remote_ncommworlds;
    globus_byte_t *namesbuff;
    int context_i;

    /****************************************************************/
    /* Phase I - exchanging all the names in CommWorldChannelsTable */
    /****************************************************************/

    namesbuff = (globus_byte_t *) NULL;

    alloc_bytes = HEADERLEN 
		+ CommworldChannelsTableNcommWorlds*COMMWORLDCHANNELSNAMELEN;

    if (!(namesbuff = (globus_byte_t *) globus_libc_malloc(alloc_bytes)))
    {
	globus_libc_fprintf(stderr, 
	    "ERROR: connect_server_handshake: failed malloc %d bytes\n", 
	    alloc_bytes);
	return -1;
    } /* endif */

    globus_libc_sprintf((char *) namesbuff, "%d ", CommworldChannelsTableNcommWorlds);
    for (i = 0, cp = (char *) (namesbuff+HEADERLEN); 
	i < CommworldChannelsTableNcommWorlds; 
	    i ++, cp += COMMWORLDCHANNELSNAMELEN)
		memcpy(cp, 
		    CommWorldChannelsTable[i].name, 
		    COMMWORLDCHANNELSNAMELEN);

    globus_io_write(handle,
		    namesbuff,
		    (globus_size_t) alloc_bytes,
		    &nbytes);

    globus_io_read(handle,
		    namesbuff,
		    (globus_size_t) HEADERLEN,
		    (globus_size_t) HEADERLEN,
		    &nbytes);

    sscanf((char *) namesbuff, "%d ", &remote_ncommworlds);

    if (alloc_bytes < remote_ncommworlds*COMMWORLDCHANNELSNAMELEN)
    {
	/* need more room in namesbuff to recv all remote commworld names */
	alloc_bytes = remote_ncommworlds*COMMWORLDCHANNELSNAMELEN;
	if (!(namesbuff = (globus_byte_t *) globus_libc_realloc(
						(char *) namesbuff,
						alloc_bytes)))
	{
	    globus_libc_fprintf(stderr, 
		"ERROR: connect_server_handshake: failed realloc %d bytes\n", 
		alloc_bytes);
	    return -1;
	} /* endif */
    } /* endif */

    globus_io_read(handle,
		    namesbuff,
		(globus_size_t) remote_ncommworlds*COMMWORLDCHANNELSNAMELEN,
		(globus_size_t) remote_ncommworlds*COMMWORLDCHANNELSNAMELEN,
		    &nbytes);

    /**********************************************************************/
    /* Phase II - identifying which of my channel vectors need to be sent */
    /**********************************************************************/

    if (!(sendflags = (globus_bool_t *) globus_libc_malloc(
		    CommworldChannelsTableNcommWorlds*sizeof(globus_bool_t))))
    {
	globus_libc_fprintf(stderr, 
	    "ERROR: connect_server_handshake: failed malloc %d bytes\n", 
	    CommworldChannelsTableNcommWorlds*sizeof(globus_bool_t));
	return -1;
    } /* endif */

    if (mark_commworldchannels_to_send(&nsend, 
					sendflags, 
					comm_ptr, 
					remote_ncommworlds, 
					namesbuff))
    {
	g_free(sendflags);
	return -1;
    } /* endif */

    /*************************************************************************/
    /* Phase III - exchaning only those channel vectors that need to be sent */
    /*************************************************************************/

    if (send_commworldchannels(handle, 
				nsend, 
				CommworldChannelsTableNcommWorlds, 
				sendflags))
    {
	g_free(sendflags);
	return -1;
    } /* endif */

    g_free(sendflags);
	
    if (recv_commworldchannels(handle))
    {
	return -1;
    } /* endif */

    /***************************************************************/
    /* Phase IV - exchanging map info that maps comm's local ranks */
    /*            to channel vectors (map = <worldname,disp>)      */
    /***************************************************************/

    if (send_rankmap(handle, comm_ptr))
    {
	return -1;
    } /* endif */

    if (recv_rankmap(handle, nprocs, map))
    {
	return -1;
    } /* endif */

    /***************************************/
    /* Phase V - exchanging local_contexts */
    /***************************************/

    /* 
     * cheating here by exploiting the fact that we know that 
     * an MPIR_CONTEXT is _really_ nothing more than an int
     */

    context_i = (int) local_context;
    globus_libc_sprintf((char *) hdr, "%d ", context_i);
    globus_io_write(handle,
		    hdr,
		    (globus_size_t) HEADERLEN,
		    &nbytes);

    globus_io_read(handle,
		    hdr,
		    (globus_size_t) HEADERLEN,
		    (globus_size_t) HEADERLEN,
		    &nbytes);

    sscanf((char *) hdr, "%d ", &context_i);
    *remote_context = (MPIR_CONTEXT) context_i;

    return 0;

} /* end connect_server_handshake() */

/*
 * returns 0 upon successful completion, otherwise non-zero
 */
static int connect_client_handshake(globus_io_handle_t *handle,
				    MPIR_CONTEXT *remote_context,
				    MPIR_CONTEXT local_context,
				    struct MPIR_COMMUNICATOR *comm_ptr,
				    int *nprocs,
				    globus_byte_t **map)
{
    globus_size_t nbytes;
    globus_byte_t hdr[HEADERLEN];
    int i;
    int alloc_bytes;
    globus_byte_t *outbuff;
    char *cp;
    int nsend;
    globus_bool_t *sendflags;
    int remote_ncommworlds;
    globus_byte_t *namesbuff;
    int context_i;

    /****************************************************************/
    /* Phase I - exchanging all the names in CommWorldChannelsTable */
    /****************************************************************/

    /* reading server's names */

    namesbuff = (globus_byte_t *) NULL;

    globus_io_read(handle,
		    hdr,
		    (globus_size_t) HEADERLEN,
		    (globus_size_t) HEADERLEN,
		    &nbytes);

    sscanf((char *) hdr, "%d ", &remote_ncommworlds);
    alloc_bytes = remote_ncommworlds*COMMWORLDCHANNELSNAMELEN;

    if (!(namesbuff = (globus_byte_t *) globus_libc_malloc(alloc_bytes)))
    {
	globus_libc_fprintf(stderr, 
	    "ERROR: connect_client_handshake: failed malloc %d bytes\n", 
	    alloc_bytes);
	return -1;
    } /* endif */

    globus_io_read(handle,
		    namesbuff,
		    (globus_size_t) alloc_bytes,
		    (globus_size_t) alloc_bytes,
		    &nbytes);

    /* writing my names */
    alloc_bytes = HEADERLEN
		+CommworldChannelsTableNcommWorlds*COMMWORLDCHANNELSNAMELEN;

    if (!(outbuff = (globus_byte_t *) globus_libc_malloc(alloc_bytes)))
    {
	globus_libc_fprintf(stderr, 
	    "ERROR: connect_client_handshake: failed malloc %d bytes\n", 
	    alloc_bytes);
	return -1;
    } /* endif */

    globus_libc_sprintf((char *) outbuff, "%d ", CommworldChannelsTableNcommWorlds);
    for (i = 0, cp = (char *) outbuff+HEADERLEN; 
	i < CommworldChannelsTableNcommWorlds; 
	    i ++, cp += HEADERLEN)
		memcpy(cp, 
		    CommWorldChannelsTable[i].name, 
		    COMMWORLDCHANNELSNAMELEN);

    globus_io_write(handle,
		    outbuff,
		    (globus_size_t) alloc_bytes,
		    &nbytes);
    g_free(outbuff);

    /**********************************************************************/
    /* Phase II - identifying which of my channel vectors need to be sent */
    /**********************************************************************/

    if (!(sendflags = (globus_bool_t *) globus_libc_malloc(
		    CommworldChannelsTableNcommWorlds*sizeof(globus_bool_t))))
    {
	globus_libc_fprintf(stderr, 
	    "ERROR: connect_client_handshake: failed malloc %d bytes\n", 
	    CommworldChannelsTableNcommWorlds*sizeof(globus_bool_t));
	return -1;
    } /* endif */

    if (mark_commworldchannels_to_send(&nsend, 
					sendflags, 
					comm_ptr, 
					remote_ncommworlds, 
					namesbuff))
    {
	g_free(sendflags);
	return -1;
    } /* endif */

    /*************************************************************************/
    /* Phase III - exchaning only those channel vectors that need to be sent */
    /*************************************************************************/

    {
	int nrowsbeforerecv = CommworldChannelsTableNcommWorlds;

	if (recv_commworldchannels(handle))
	{
	    g_free(sendflags);
	    return -1;
	} /* endif */

	if (send_commworldchannels(handle, nsend, nrowsbeforerecv, sendflags))
	{
	    g_free(sendflags);
	    return -1;
	} /* endif */
    }

    g_free(sendflags);

    /***************************************************************/
    /* Phase IV - exchanging map info that maps comm's local ranks */
    /*            to channel vectors (map = <worldname,disp>)      */
    /***************************************************************/

    if (recv_rankmap(handle, nprocs, map))
    {
	return -1;
    } /* endif */

    if (send_rankmap(handle, comm_ptr))
    {
	return -1;
    } /* endif */

    /***************************************/
    /* Phase V - exchanging local_contexts */
    /***************************************/

    /* 
     * cheating here by exploiting the fact that we know that 
     * an MPIR_CONTEXT is _really_ nothing more than an int
     */

    globus_io_read(handle,
		    hdr,
		    (globus_size_t) HEADERLEN,
		    (globus_size_t) HEADERLEN,
		    &nbytes);

    sscanf((char *) hdr, "%d ", &context_i);
    *remote_context = (MPIR_CONTEXT) context_i;

    context_i = (int) local_context;
    globus_libc_sprintf((char *) hdr, "%d ", context_i);
    globus_io_write(handle,
		    hdr,
		    (globus_size_t) HEADERLEN,
		    &nbytes);

    return 0;

} /* end connect_client_handshake() */

/* 
 * it is assumed that 'sendflags' is a vector of 
 * CommworldChannelsTableNcommWorlds globus_bool_t's, 
 * i.e., one for each * row of our CommWorldChannelsTable
 *
 * it is also assumed that 'remote_namesbuff' is a single vector 
 * of 'remote_ncommworlds' names ... where each name is a char string
 * residing in a slot that is exactly COMMWORLDCHANNELSNAMELEN bytes wide.
 *
 * upon successful completion returns 0, otherwise non-zero 
 */

static int mark_commworldchannels_to_send(int *nsend, 
					globus_bool_t *sendflags, 
					struct MPIR_COMMUNICATOR *comm_ptr,
					int remote_ncommworlds,
					globus_byte_t *remote_namesbuff)
{
    int i;

    /* initialization */

    *nsend = 0;
    for (i = 0; i < CommworldChannelsTableNcommWorlds; i ++)
	sendflags[i] = GLOBUS_FALSE;

    for (i = 0; i < comm_ptr->np; i ++)
    {
	int row = get_channel_rowidx(comm_ptr->lrank_to_grank[i],(int *) NULL);

	if (row == -1)
	    return -1;

	if (!sendflags[row])
	{
	    /* we have not already marked this one for sending ...   */
	    /* must check to see if remote side already has this one */

	    int j;
	    char *cp = (char *) remote_namesbuff;

	    for (j = 0; 
		j < remote_ncommworlds 
		&& strcmp(CommWorldChannelsTable[row].name, cp); 
		    j ++, cp += COMMWORLDCHANNELSNAMELEN)
	    ;

	    if (j == remote_ncommworlds)
	    {
		/* this name not found on remote side, must mark for sending */
		sendflags[row] = GLOBUS_TRUE;
		*nsend = *nsend + 1;
	    } /* endif */
	} /* endif */

    } /* endfor */

    return 0;

} /* end mark_commworldchannels_to_send() */

/* 
 * it is assumed that 'sendflags' corresponds to the 
 * first 'nflags' rows of the CommWorldChannelsTable
 *
 * upon successful completion returns 0, otherwise non-zero 
 */
static int send_commworldchannels(globus_io_handle_t *handle, 
				    int nsend, 
				    int nflags, 
				    globus_bool_t *sendflags)
{
    globus_size_t nbytes;
    globus_byte_t hbuff[HEADERLEN];
    int i;
    int worldsize;
    int nsent;
    int rc;
    int *channellens = (int *) NULL;
    int buffsize = 0;
    globus_byte_t *buff = (globus_byte_t *) NULL;

    if (nflags > CommworldChannelsTableNcommWorlds)
    {
	globus_libc_fprintf(stderr, 
	    "ERROR: send_commworldchannels passed nflags %d "
	    "which is > CommworldChannelsTableNcommWorlds %d \n", 
	    nflags, CommworldChannelsTableNcommWorlds);
	return -1;
    } /* endif */

    globus_libc_sprintf((char *) hbuff, "%d ", nsend);
    globus_io_write(handle, hbuff, (globus_size_t) HEADERLEN, &nbytes);

    for (i = 0, nsent = 0; nsent < nsend && i < nflags; i ++)
    {
	if (sendflags[i])
	{
	    /* have to send this world */
	    if (pack_world(&(CommWorldChannelsTable[i]), 
			    &buff, 
			    &buffsize, 
			    &worldsize))
	    {
		g_free(buff);
		return -1;
	    } /* endif */
	    /* sending this world */
	    globus_io_write(handle, buff, (globus_size_t) worldsize, &nbytes);
	    nsent ++;

	} /* endif */
    } /* endfor */

    if (nsent != nsend)
    {
	globus_libc_fprintf(stderr, 
	    "ERROR: send_commworldchannels found only %d "
	    "of the %d it was expected to send\n", 
	    nsent, nsend);
	rc = -1;
    } 
    else
	rc = 0;

    g_free(channellens);
    g_free(buff);

    return rc;
    
} /* end send_commworldchannels() */

/*
 * packs the channels of the comworld pointed at by 'cwp' into 'buff'.
 * if there is not enough room in 'buff' realloc enough updating 'buffsize'.
 * returns packsize in 'packsize'
 *
 * packs world in following format (nprocs=cwp->nprocs, n=nprocs-1):
 * | nprocs | worldname | nbytes ch_0 | ch_0 | ... | nbytes ch_n | ch_n |
 *   ^^^^^^   ^^^^^^^^^   ^^^^^^^^^^^  ^^^^^^
 *     |          |            |         |
 *     |          |            |         +- width=nbytes ch_0
 *     |          |            |
 *     |          |            +- width=HEADERLEN
 *     |          |
 *     |          +- width=COMMWORLDCHANNELSNAMELEN
 *     |
 *     +- width=HEADERLEN
 *
 * | ------------ total width returned in 'packsize' -------------------|
 *
 * upon successful completion this function returns 0, otherwise -1.
 * 
 */
static int pack_world(struct commworldchannels *cwp, 
			globus_byte_t **buff, 
			int *buffsize, 
			int *packsize)
{
    int j;
    int *channellens;
    char *cp;

    /* first computing size needed to send this world */
    *packsize = (HEADERLEN+COMMWORLDCHANNELSNAMELEN);

    if (!(channellens = (int *) globus_libc_malloc(cwp->nprocs*sizeof(int))))
    {
	globus_libc_fprintf(stderr, 
	    "ERROR: pack_world: failed alloc %d bytes\n", 
	    cwp->nprocs*sizeof(int));
	return -1;
    } /* endif */

    for (j = 0; j < cwp->nprocs; j ++)
    {
	if ((channellens[j] = channel_pack_size(&(cwp->channels[j]))) == -1)
	{
	    g_free(channellens);
	    return -1;
	} /* endif */

	*packsize = *packsize+(HEADERLEN+channellens[j]);
    } /* endfor */

    if (*buffsize < *packsize)
    {
	*buffsize = *packsize;
	if (!(*buff = (globus_byte_t *) globus_libc_realloc(
					(void *) *buff, *buffsize)))
	{
	    globus_libc_fprintf(stderr, 
		"ERROR: pack_world: failed realloc %d bytes\n", 
		*buffsize);
	    g_free(channellens);
	    return -1;
	} /* endif */
    } /* endif */

    /* filling buff with this world */
    cp = (char *) *buff;
    globus_libc_sprintf(cp, "%d ", cwp->nprocs);
    cp += HEADERLEN;
    strcpy(cp, cwp->name);
    cp += COMMWORLDCHANNELSNAMELEN;

    for (j = 0; j < cwp->nprocs; j ++)
    {
	globus_libc_sprintf(cp, "%d ", channellens[j]);
	cp += HEADERLEN;
	if (pack_channel(&cp, &(cwp->channels[j])))
	{
	    g_free(channellens);
	    return -1;
	} /* endif */
    } /* endfor */

    g_free(channellens);
    return 0;

} /* end pack_world() */

/* 
 * returns the number of bytes to linearize ('pack') 
 * the channel pointed at by 'chp'
 * if something went wrong, returns -1
 */
static int channel_pack_size(struct channel_t *chp)
{
    struct miproto_t *mp;
    char proto_type[10];
    char temp[10];
    int rc;
    int nprotos;

    for (rc = nprotos = 0, mp = chp->proto_list; rc != -1 && mp; mp = mp->next)
    {
	nprotos ++;

	switch (mp->type)
	{
	    case tcp:
	    {
		struct tcp_miproto_t *p = (struct tcp_miproto_t *) (mp->info);
		int lan_id_len = strlen(p->globus_lan_id);
		char lan_id_len_str[10];
		char localhost_id[10];

		globus_libc_sprintf(proto_type, "%d", tcp);
		globus_libc_sprintf(temp, "%d", (int) (p->port));
		globus_libc_sprintf(lan_id_len_str, "%d", lan_id_len);
		globus_libc_sprintf(localhost_id, "%d", p->localhost_id);

		rc += (strlen(proto_type)+1
			+ strlen(p->hostname)+1
			+ strlen(temp)+1
			+ strlen(lan_id_len_str)+1
			+ lan_id_len+1
			+ strlen(localhost_id)+1);
	    }
		break;
	    case mpi:
	    {
		struct mpi_miproto_t *p = (struct mpi_miproto_t *) (mp->info);

		globus_libc_sprintf(proto_type, "%d", mpi);
		globus_libc_sprintf(temp, "%d", (int) (p->rank));

		rc += (strlen(proto_type)+1
			+ strlen(p->unique_session_string)+1
			+ strlen(temp)+1);
	    }
		break;
	    default:
		globus_libc_fprintf(stderr, 
		    "ERROR: channel_pack_size encountered unrecongnizable "
		    "protocol type %d\n", mp->type);
		rc = -1;
		break;
	} /* end switch() */
    } /* endfor */

    if (rc != -1)
    {
	globus_libc_sprintf(temp, "%d ", nprotos);
	rc += strlen(temp);
    } /* endif */

    return rc;

} /* end channel_pack_size() */

/* 
 * linearizes (packs) the channel pointed at by 'chp'
 * into the buffer pointed at by '*buff' updating '*buff'
 * as we go along.
 *
 * upon successfull completion returns 0, otherwise non-zero
 */
static int pack_channel(char **buff, struct channel_t *chp)
{
    struct miproto_t *mp;
    char proto_type[10];
    char temp[10];
    int nprotos;

    for (nprotos = 0, mp = chp->proto_list; mp; mp = mp->next)
	nprotos ++;
    globus_libc_sprintf(temp, "%d ", nprotos);
    strcpy(*buff, temp);
    *buff = *buff + strlen(temp);

    /* now linearize this channel */
    for (mp = chp->proto_list; mp; mp = mp->next)
    {
	switch (mp->type)
	{
	    case tcp:
	    {
		struct tcp_miproto_t *p = (struct tcp_miproto_t *) (mp->info);
		int lan_id_len = strlen(p->globus_lan_id);
		char lan_id_len_str[10];
		char localhost_id[10];

		globus_libc_sprintf(proto_type, "%d", tcp);
		globus_libc_sprintf(temp, "%d", (int) (p->port));
		globus_libc_sprintf(lan_id_len_str, "%d", lan_id_len);
		globus_libc_sprintf(localhost_id, "%d", p->localhost_id);

		globus_libc_sprintf(*buff, 
				    "%s %s %s %s %s %s", 
				    proto_type, 
				    p->hostname, 
				    temp,
				    lan_id_len_str,
				    p->globus_lan_id,
				    localhost_id);

		*buff = *buff + (strlen(proto_type)+1
			    + strlen(p->hostname)+1
			    + strlen(temp)+1
			    + strlen(lan_id_len_str)+1
			    + lan_id_len+1
			    + strlen(localhost_id)+1);
	    }
		break;
	    case mpi:
	    {
		struct mpi_miproto_t *p = (struct mpi_miproto_t *) (mp->info);

		globus_libc_sprintf(proto_type, "%d", mpi);
		globus_libc_sprintf(temp, "%d", (int) (p->rank));

		globus_libc_sprintf(*buff, "%s %s %s ", 
		    proto_type, p->unique_session_string, temp);

		*buff = *buff + (strlen(proto_type)+1
				+ strlen(p->unique_session_string)+1
				+ strlen(temp)+1);
	    }
		break;
	    default:
		globus_libc_fprintf(stderr, 
		    "ERROR: pack_channel encountered unrecongnizable "
		    "protocol type %d\n", mp->type);
		return -1;
		break;
	} /* end switch() */
    } /* endfor */

    return 0;

} /* end pack_channel() */

/* upon successful completion returns 0, otherwise non-zero */
static int recv_commworldchannels(globus_io_handle_t *handle)
{
    globus_size_t nbytes;
    globus_byte_t hbuff[HEADERLEN+COMMWORLDCHANNELSNAMELEN];
    int nrecv;
    int i;
    int j;

    globus_io_read(handle,
		    hbuff,
		    (globus_size_t) HEADERLEN,
		    (globus_size_t) HEADERLEN,
		    &nbytes);
    sscanf((char *) hbuff, "%d ", &nrecv);

    /* making sure there's enough room in CommWorldChannelsTable */
    /* to accomodate incoming comm worlds                        */
    if (CommworldChannelsTableNcommWorlds + nrecv > CommworldChannelsTableSize)
    {
	/* have to increase size of CommWorldChannelsTable */

	while (CommworldChannelsTableNcommWorlds + nrecv 
		> CommworldChannelsTableSize)
	    CommworldChannelsTableSize += COMMWORLDCHANNELS_TABLE_STEPSIZE;

	if (!(CommWorldChannelsTable = (struct commworldchannels *) 
		globus_libc_realloc((void *) CommWorldChannelsTable,
		CommworldChannelsTableSize*sizeof(struct commworldchannels))))
	{
		globus_libc_fprintf(stderr, 
		    "ERROR: recv_commworldchannels failed realloc for "
		    "CommWorldChannelsTable up to %d rows\n", 
		    CommworldChannelsTableSize);
		return -1;
	} /* endif */
    } /* endif */

    for (i = 0; i < nrecv; i ++, CommworldChannelsTableNcommWorlds ++)
    {
	struct commworldchannels *cwp = 
		    CommWorldChannelsTable+CommworldChannelsTableNcommWorlds;
	globus_byte_t **miproto_vectors;

	globus_io_read(handle,
			hbuff,
			(globus_size_t) HEADERLEN+COMMWORLDCHANNELSNAMELEN,
			(globus_size_t) HEADERLEN+COMMWORLDCHANNELSNAMELEN,
			&nbytes);

	sscanf((char *) hbuff, "%d ", &(cwp->nprocs));
	strcpy(cwp->name, (char *) (hbuff+HEADERLEN));

	if (!(miproto_vectors = (globus_byte_t **) 
		    globus_libc_malloc(cwp->nprocs*sizeof(globus_byte_t *))))
	{
		globus_libc_fprintf(stderr, 
		    "ERROR: recv_commworldchannels failed alloc %d "
		    "bytes for miproto_vectors\n", 
		    cwp->nprocs*sizeof(globus_byte_t *));
		return -1;
	} /* endif */

	for (j = 0; j < cwp->nprocs; j ++)
	{
	    int miproto_len;

	    globus_io_read(handle,
			    hbuff,
			    (globus_size_t) HEADERLEN,
			    (globus_size_t) HEADERLEN,
			    &nbytes);
	    sscanf((char *) hbuff, "%d ", &miproto_len);

	    if (!(miproto_vectors[j] = (globus_byte_t *) 
			globus_libc_malloc(miproto_len)))
	    {
		    globus_libc_fprintf(stderr, 
			"ERROR: recv_commworldchannels failed alloc %d "
			"bytes for one of the miproto_vectors\n", 
			miproto_len);
		    return -1;
	    } /* endif */
	    globus_io_read(handle,
			    miproto_vectors[j],
			    (globus_size_t) miproto_len,
			    (globus_size_t) miproto_len,
			    &nbytes);
	} /* endfor */

	build_channels(cwp->nprocs, miproto_vectors, &(cwp->channels));

	for (j = 0; j < cwp->nprocs; j ++)
	    g_free(miproto_vectors[j]);
	g_free(miproto_vectors);

	select_protocols(cwp->nprocs, cwp->channels);
    } /* endfor */

    return 0;

} /* end recv_commworldchannels() */

/*
 * returns 0 upon successful completion, otherwise non-zero
 */
static int send_rankmap(globus_io_handle_t *handle,
			struct MPIR_COMMUNICATOR *comm_ptr)
{
    int i;
    globus_byte_t *buff;
    globus_size_t buffsize;
    globus_size_t nbytes;
    char *cp;

    buffsize = (globus_size_t) 
	    (HEADERLEN + (comm_ptr->np*(COMMWORLDCHANNELSNAMELEN+HEADERLEN)));

    if (!(buff = (globus_byte_t *) globus_libc_malloc((int) buffsize)))
    {
	    globus_libc_fprintf(stderr, 
		"ERROR: send_rankmap failed alloc %d bytes\n",
		buffsize);
	    return -1;
    } /* endif */

    cp = (char *) buff;
    globus_libc_sprintf(cp, "%d ", comm_ptr->np);
    cp += HEADERLEN;

    for (i = 0; i < comm_ptr->np; i ++)
    {
	int displ;
	int row = get_channel_rowidx(comm_ptr->lrank_to_grank[i], &displ);

	if (row == -1)
	{
	    g_free(buff);
	    return -1;
	} /* endif */

	strcpy(cp, CommWorldChannelsTable[row].name);
	cp += COMMWORLDCHANNELSNAMELEN;
	globus_libc_sprintf(cp, "%d ", displ);
	cp += HEADERLEN;

    } /* endfor */

    globus_io_write(handle, buff, buffsize, &nbytes);

    g_free(buff);

    return 0;

} /* end send_rankmap() */

/*
 * returns 0 upon successful completion, otherwise non-zero
 */
static int recv_rankmap(globus_io_handle_t *handle,
			int *nprocs, 
			globus_byte_t **map)
{
    globus_byte_t temp[HEADERLEN];
    globus_size_t nbytes;
    globus_size_t buffsize;

    globus_io_read(handle,
		    temp,
		    (globus_size_t) HEADERLEN,
		    (globus_size_t) HEADERLEN,
		    &nbytes);
    sscanf((char *) temp, "%d ", nprocs);

    buffsize = (globus_size_t) (*nprocs*(COMMWORLDCHANNELSNAMELEN+HEADERLEN));

    if (!(*map = (globus_byte_t *) globus_libc_malloc((int) buffsize)))
    {
	    globus_libc_fprintf(stderr, 
		"ERROR: recv_rankmap failed alloc %d bytes\n",
		buffsize);
	    return -1;
    } /* endif */

    globus_io_read(handle, *map, buffsize, buffsize, &nbytes);

    return 0;

} /* end recv_rankmap() */

/*
 * 'map' is a vector of 'nmapprocs' (one for each proc in the
 * remote intra-communicator) binary tuples <commworldname,displ>
 * s.t. foreach proc_i the binary tuple <commworldname_i,displ_i>
 * names the commworld and displ within that commworld that proc_i
 * belongs to.  we must bcast 'map' to all our slaves.
 *
 * returns 0 upon successful completion, otherwise returns non-zero 
 *
 */
static int distribute_info_to_slaves(MPI_Comm comm, 
				    int root,
				    MPIR_CONTEXT remote_context,
				    int nmapprocs,
				    globus_byte_t *map)
{
    int i;
    char *cp;
    int buffsize = 0;
    char *buff = (char *) NULL;
    int *sendflags = (int *) NULL;
    int nsend;
    int nsent;

    /**************************/
    /* bcasting map to slaves */
    /**************************/

    MPI_Bcast(&nmapprocs, 1, MPI_INT, root, comm);
    MPI_Bcast((char *) map, 
	    nmapprocs*(COMMWORLDCHANNELSNAMELEN+HEADERLEN), 
	    MPI_CHAR, 
	    root, 
	    comm);

    /*************************************/
    /* bcasting remote_context to slaves */
    /*************************************/

    MPI_Bcast(&remote_context, 1, MPIR_CONTEXT_TYPE, root, comm);

    /*********************************/
    /* bcasting commworlds to slaves */
    /*********************************/

    /* 
     * rather than waiting for each slave to tell the master which
     * commworld(s) he needs and then respond to that, it is probably 
     * a lot faster to have the master simply bcast all the commworlds
     * to all the slaves and then each slave can simply throw out
     * those commworlds it already had.  
     * i think this approach scales much better as we get into
     * 1000's of procs.
     */

    /* 
     * figuring which of the commworlds i need to send ... i need to 
     * bcast to my slaves all those commworlds that appear in map
     */
    if (!(sendflags = (int *) 
	    globus_libc_malloc(CommworldChannelsTableNcommWorlds*sizeof(int))))
    {
	    globus_libc_fprintf(stderr, 
		"ERROR: distribute_info_to_slaves failed alloc %d bytes\n",
		CommworldChannelsTableNcommWorlds*sizeof(int));
	    return -1;
    } /* endif */

    for (i = 0; i < CommworldChannelsTableNcommWorlds; i ++)
	sendflags[i] = GLOBUS_FALSE;

    for (i = 0, cp = (char *) map, nsend = 0; 
	i < nmapprocs; 
	    i ++, cp += (COMMWORLDCHANNELSNAMELEN+HEADERLEN))
    {
	int row = commworld_name_to_rowidx(cp);

	if (row == -1)
	{
	    globus_libc_fprintf(stderr, 
		"ERROR: distribute_info_to_slaves() could not find "
		"commworld named >%s< in CommWorldChannelsTable\n", 
		cp);
	    g_free(buff);
	    g_free(sendflags);
	    return -1;
	} /* endif */

	if (!sendflags[row])
	{
	    sendflags[row] = GLOBUS_TRUE;
	    nsend ++;
	} /* endif */
    } /* endfor */

    /* bcasting those commworlds that appear in map */

    MPI_Bcast(&nsend, 1, MPI_INT, root, comm);
    for (i = 0, nsent = 0; i < CommworldChannelsTableNcommWorlds; i ++)
    {
	if (sendflags[i])
	{
	    /* need to send this one */
	    int worldsize;

	    if (pack_world(&(CommWorldChannelsTable[i]), 
			    (globus_byte_t **) &buff, 
			    &buffsize, 
			    &worldsize))
	    {
		g_free(buff);
		g_free(sendflags);
		return -1;
	    } /* endif */

	    MPI_Bcast(&worldsize, 1, MPI_INT, root, comm);
	    MPI_Bcast(buff, worldsize, MPI_CHAR, root, comm);

	    nsent ++;
	} /* endif */
    } /* endfor */

    if (nsent != nsend)
    {
	globus_libc_fprintf(stderr, 
	    "ERROR: distribute_info_to_slaves(): sent only %d commworlds "
	    "of the %d i was expecting to send\n",
	    nsent, nsend);
	g_free(sendflags);
	g_free(buff);
	return -1;
    } /* endif */

    g_free(sendflags);
    g_free(buff);

    return 0;

} /* end distribute_info_to_slaves() */

/* returns 0 upon successful completion, otherwise returns non-zero */
static int receive_info_from_master(MPI_Comm comm, 
				    int root,
				    MPIR_CONTEXT *remote_context,
				    int *nmapprocs,
				    globus_byte_t **map)
{
    int i;
    int nremotecommworlds;
    int buffsize = 0;
    char *buff = (char *) NULL;
    int nmiproto_vectors = 0;
    globus_byte_t **miproto_vectors = (globus_byte_t **) NULL;

    /********************************/
    /* rcving bcast map from master */
    /********************************/

    MPI_Bcast(nmapprocs, 1, MPI_INT, root, comm);

    if (!(*map = (globus_byte_t *) 
	globus_libc_malloc(*nmapprocs*(COMMWORLDCHANNELSNAMELEN+HEADERLEN))))
    {
	globus_libc_fprintf(stderr, 
	    "ERROR: receive_info_from_master failed alloc %d bytes\n",
	    *nmapprocs*(COMMWORLDCHANNELSNAMELEN+HEADERLEN));
	return -1;
    } /* endif */

    MPI_Bcast((char *) *map, 
	    *nmapprocs*(COMMWORLDCHANNELSNAMELEN+HEADERLEN), 
	    MPI_CHAR, 
	    root, 
	    comm);

    /*******************************************/
    /* rcving bcast remote_context from master */
    /*******************************************/

    MPI_Bcast(remote_context, 1, MPIR_CONTEXT_TYPE, root, comm);

    /**************************************************************************/
    /* receiving bcast commworlds from master, tossing the ones i already had */
    /**************************************************************************/

    /* 
     * rather than waiting for each slave to tell the master which
     * commworld(s) he needs and then respond to that, it is probably 
     * a lot faster to have the master simply bcast all the commworlds
     * to all the slaves and then each slave can simply throw out
     * those commworlds it already had.  
     * i think this approach scales much better as we get into
     * 1000's of procs.
     */

    MPI_Bcast(&nremotecommworlds, 1, MPI_INT, root, comm);
    for (i = 0; i < nremotecommworlds; i ++)
    {
	int worldsize;

	MPI_Bcast(&worldsize, 1, MPI_INT, root, comm);

	if (buffsize < worldsize)
	{
	    buffsize = worldsize;
	    if (!(buff = (char *) globus_libc_realloc((void *) buff, buffsize)))
	    {
		globus_libc_fprintf(stderr, 
		    "ERROR: receive_info_from_master failed realloc for "
		    "%d bytes\n", 
		    buffsize);
		return -1;
	    } /* endif */
	} /* endif */

	MPI_Bcast(buff, worldsize, MPI_CHAR, root, comm);

	if (commworld_name_to_rowidx(buff+HEADERLEN) == -1)
	{
	    /* 
	     * i do not have this commworld ... must unpack, 
	     * add to table, build channels, and select protos
	     */
/*
 *
 * buff is packed in the follwing format by pack_world() 
 *
 * packs world in following format (nprocs=cwp->nprocs, n=nprocs-1):
 * | nprocs | worldname | nbytes ch_0 | ch_0 | ... | nbytes ch_n | ch_n |
 *   ^^^^^^   ^^^^^^^^^   ^^^^^^^^^^^  ^^^^^^
 *     |          |            |         |
 *     |          |            |         +- width=nbytes ch_0
 *     |          |            |
 *     |          |            +- width=HEADERLEN
 *     |          |
 *     |          +- width=COMMWORLDCHANNELSNAMELEN
 *     |
 *     +- width=HEADERLEN
 *
 */
	    struct commworldchannels *cwp;
	    char *cp;
	    int j;

	    if (CommworldChannelsTableSize 
		< CommworldChannelsTableNcommWorlds+1 )
	    {
		CommworldChannelsTableSize += COMMWORLDCHANNELS_TABLE_STEPSIZE;
		if (!(CommWorldChannelsTable = (struct commworldchannels *)
			globus_libc_realloc((void *) CommWorldChannelsTable,
		CommworldChannelsTableSize*sizeof(struct commworldchannels))))
		{
		    globus_libc_fprintf(stderr,
			"ERROR: receive_info_from_master failed realloc "
			"of %d bytes\n",
		CommworldChannelsTableSize*sizeof(struct commworldchannels));
		    g_free(buff);
		    g_free(miproto_vectors);
		    return -1;
		} /* endif */
	    } /* endif */

	    cwp = &(CommWorldChannelsTable[CommworldChannelsTableNcommWorlds]);

	    sscanf(buff, "%d ", &(cwp->nprocs));
	    strcpy(cwp->name, buff+HEADERLEN);

	    if (nmiproto_vectors < cwp->nprocs)
	    {
		nmiproto_vectors = cwp->nprocs;
		if (!(miproto_vectors = (globus_byte_t **)
			globus_libc_realloc((globus_byte_t **) miproto_vectors,
			nmiproto_vectors*sizeof(globus_byte_t *))))
		{
		    globus_libc_fprintf(stderr,
			"ERROR: receive_info_from_master failed realloc "
			"of %d bytes\n",
			nmiproto_vectors*sizeof(globus_byte_t *));
		    g_free(buff);
		    return -1;
		} /* endif */
	    } /* endif */

	    cp = buff+COMMWORLDCHANNELSNAMELEN+HEADERLEN;
	    for (j = 0; j < cwp->nprocs; j ++)
	    {
		int nchannelbytes;

		sscanf(cp, "%d ", &nchannelbytes);

		if (!(miproto_vectors[j] = (globus_byte_t *) 
				globus_libc_malloc(nchannelbytes)))
		{
		    int k;

		    globus_libc_fprintf(stderr, 
			"ERROR: receive_info_from_master failed malloc "
			"%d bytes\n", 
			nchannelbytes);

		    for (k = 0; k < j; k ++)
			g_free(miproto_vectors[k]);
		    g_free(miproto_vectors);
		    g_free(buff);

		    return -1;

		} /* endif */

		cp += HEADERLEN;
		memcpy((void *) miproto_vectors[j], 
			(void *) cp,  
			(size_t) nchannelbytes);
		cp += nchannelbytes;

	    } /* endfor */

	    build_channels(cwp->nprocs, miproto_vectors, &(cwp->channels));

	    for (j = 0; j < cwp->nprocs; j ++)
		g_free(miproto_vectors[j]);

	    select_protocols(cwp->nprocs, cwp->channels);

	    CommworldChannelsTableNcommWorlds ++;
	} /* endif */
    } /* endfor */

    g_free(buff);
    g_free(miproto_vectors);

    return 0;

} /* end receive_info_from_master() */

/*
 * most of the code of this function was written based on Bill Gropp's
 * implemenation of MPI_Intercomm_create in mpich/src/context/ic_create.c
 *
 * 'map' is a vector of 'nmapprocs' (one for each proc in the
 * remote intra-communicator) binary tuples <commworldname,displ>
 * s.t. foreach proc_i the binary tuple <commworldname_i,displ_i>
 * names the commworld and displ within that commworld that proc_i
 * belongs to.  we must bcast 'map' to all our slaves.
 *
 * returns 0 upon successful completion, otherwise returns non-zero 
 */
static int build_new_intercommunicator(struct MPIR_COMMUNICATOR *comm_ptr,
					MPI_Comm *newcomm, 
					MPIR_CONTEXT remote_context,
					MPIR_CONTEXT local_context,
					int nmapprocs,
					globus_byte_t *map)
{

    struct MPIR_GROUP *remote_group_ptr;
    struct MPIR_COMMUNICATOR *new_comm;
    int i;
    char *cp;
    int rc;

    /* create remote group */
    remote_group_ptr = MPIR_CreateGroup(nmapprocs);
    remote_group_ptr->self = (MPI_Group) MPIR_FromPointer(remote_group_ptr);

    /* I must populate remote_group_ptr->lrank_to_grank from map */
    for (i = 0, cp = (char *) map; 
	i < nmapprocs; 
	    i ++, cp += (COMMWORLDCHANNELSNAMELEN+HEADERLEN))
    {
	int row;
	int j;

	if ((row = commworld_name_to_rowidx(cp)) == -1)
	{
	    globus_libc_fprintf(stderr, 
		"ERROR: build_new_intercommunicator: could not find "
		"commworld name >%s<\n", 
		cp);
	    return -1;
	} /* endif */

	sscanf(cp+COMMWORLDCHANNELSNAMELEN, 
		"%d ", 
		&(remote_group_ptr->lrank_to_grank[i]));
	for (j = 0; j < row; j ++)
	    remote_group_ptr->lrank_to_grank[i] += 
					CommWorldChannelsTable[j].nprocs;
    } /* endfor */

    /* 
     * We all now have all the information necessary ...
     * start building the inter-communicator 
     */
    MPIR_ALLOC(new_comm,
		NEW(struct MPIR_COMMUNICATOR),
		comm_ptr,
		MPI_ERR_EXHAUSTED,
		"build_new_intercommunicator");
    MPIR_Comm_init(new_comm, comm_ptr, MPIR_INTER);
    *newcomm = new_comm->self;
    new_comm->group = remote_group_ptr;
    MPIR_Group_dup(comm_ptr->group, &(new_comm->local_group));
    new_comm->local_rank     = new_comm->local_group->local_rank;
    new_comm->lrank_to_grank = new_comm->group->lrank_to_grank;
    new_comm->np             = new_comm->group->np;
    new_comm->send_context   = remote_context;
    new_comm->recv_context   = local_context;
    new_comm->comm_name      = 0;

    /* Build the collective inter-communicator */
    if ((rc = MPID_CommInit(comm_ptr, new_comm)))
	return rc;
    (void) MPIR_Attr_create_tree(new_comm);

    /* Build the collective inter-communicator */
    MPIR_Comm_make_coll(new_comm, MPIR_INTER);

    /* 
     * Build the collective intra-communicator.  Note that we require
     * an intra-communicator for the "coll_comm" so that MPI_COMM_DUP
     * can use it for some collective operations (do we need this
     * for MPI-2 with intercommunicator collective?)
     *
     * Note that this really isn't the right thing to do; we need to replace
     * _all_ of the Mississippi state collective code.
     */
    MPIR_Comm_make_coll(new_comm->comm_coll, MPIR_INTRA);

    /* Remember it for the debugger */
    MPIR_Comm_remember (new_comm);

    return 0;

} /* end build_new_intercommunicator() */

/* callback routines */

static void connect_listen_callback(void *callback_arg,         
                                    globus_io_handle_t *handle, 
                                    globus_result_t result)
{
    struct accepted_connection_t *cp = (struct accepted_connection_t *) 
                                        callback_arg;

    if (result != GLOBUS_SUCCESS)
    {
        /* things are very bad */
        fprintf(stderr, 
            "ERROR: connect_listen_callback rcvd result != GLOBUS_SUCCESS\n");
        exit(1);
    } /* endif */

    globus_io_tcp_accept(handle,  /* should be handle passed to this callback */
                        (globus_io_attr_t *) GLOBUS_NULL, /* use atr passed */
                                        /* to globus_io_tcp_create_listener() */
                        &(cp->handle)); /* handle for new socket */
                                        /* created as a result of this accept */

    /* signalling MPI_Accept() that client has 'MPI_Connect'ed to us */
    cp->connected = GLOBUS_TRUE;
    G2_SIGNAL

} /* end connect_listen_callback() */
