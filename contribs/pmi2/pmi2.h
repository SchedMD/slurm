/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2007 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#ifndef PMI2_H_INCLUDED
#define PMI2_H_INCLUDED

#ifndef USE_PMI2_API
/*#error This header file defines the PMI2 API, but PMI2 was not selected*/
#endif

#define PMI2_MAX_KEYLEN 64
#define PMI2_MAX_VALLEN 1024
#define PMI2_MAX_ATTRVALUE 1024
#define PMI2_ID_NULL -1

#define PMII_COMMANDLEN_SIZE 6
#define PMII_MAX_COMMAND_LEN (64*1024)

#if defined(__cplusplus)
extern "C" {
#endif

static const char FULLINIT_CMD[]          = "fullinit";
static const char FULLINITRESP_CMD[]      = "fullinit-response";
static const char FINALIZE_CMD[]          = "finalize";
static const char FINALIZERESP_CMD[]      = "finalize-response";
static const char ABORT_CMD[]             = "abort";
static const char JOBGETID_CMD[]          = "job-getid";
static const char JOBGETIDRESP_CMD[]      = "job-getid-response";
static const char JOBCONNECT_CMD[]        = "job-connect";
static const char JOBCONNECTRESP_CMD[]    = "job-connect-response";
static const char JOBDISCONNECT_CMD[]     = "job-disconnect";
static const char JOBDISCONNECTRESP_CMD[] = "job-disconnect-response";
static const char KVSPUT_CMD[]            = "kvs-put";
static const char KVSPUTRESP_CMD[]        = "kvs-put-response";
static const char KVSFENCE_CMD[]          = "kvs-fence";
static const char KVSFENCERESP_CMD[]      = "kvs-fence-response";
static const char KVSGET_CMD[]            = "kvs-get";
static const char KVSGETRESP_CMD[]        = "kvs-get-response";
static const char GETNODEATTR_CMD[]       = "info-getnodeattr";
static const char GETNODEATTRRESP_CMD[]   = "info-getnodeattr-response";
static const char PUTNODEATTR_CMD[]       = "info-putnodeattr";
static const char PUTNODEATTRRESP_CMD[]   = "info-putnodeattr-response";
static const char GETJOBATTR_CMD[]        = "info-getjobattr";
static const char GETJOBATTRRESP_CMD[]    = "info-getjobattr-response";
static const char NAMEPUBLISH_CMD[]       = "name-publish";
static const char NAMEPUBLISHRESP_CMD[]   = "name-publish-response";
static const char NAMEUNPUBLISH_CMD[]     = "name-unpublish";
static const char NAMEUNPUBLISHRESP_CMD[] = "name-unpublish-response";
static const char NAMELOOKUP_CMD[]        = "name-lookup";
static const char NAMELOOKUPRESP_CMD[]    = "name-lookup-response";

static const char PMIJOBID_KEY[]          = "pmijobid";
static const char PMIRANK_KEY[]           = "pmirank";
static const char SRCID_KEY[]             = "srcid";
static const char THREADED_KEY[]          = "threaded";
static const char RC_KEY[]                = "rc";
static const char ERRMSG_KEY[]            = "errmsg";
static const char PMIVERSION_KEY[]        = "pmi-version";
static const char PMISUBVER_KEY[]         = "pmi-subversion";
static const char RANK_KEY[]              = "rank";
static const char SIZE_KEY[]              = "size";
static const char APPNUM_KEY[]            = "appnum";
static const char SPAWNERJOBID_KEY[]      = "spawner-jobid";
static const char DEBUGGED_KEY[]          = "debugged";
static const char PMIVERBOSE_KEY[]        = "pmiverbose";
static const char ISWORLD_KEY[]           = "isworld";
static const char MSG_KEY[]               = "msg";
static const char JOBID_KEY[]             = "jobid";
static const char KVSCOPY_KEY[]           = "kvscopy";
static const char KEY_KEY[]               = "key";
static const char VALUE_KEY[]             = "value";
static const char FOUND_KEY[]             = "found";
static const char WAIT_KEY[]              = "wait";
static const char NAME_KEY[]              = "name";
static const char PORT_KEY[]              = "port";
static const char THRID_KEY[]             = "thrid";
static const char INFOKEYCOUNT_KEY[]      = "infokeycount";
static const char INFOKEY_KEY[]           = "infokey%d";
static const char INFOVAL_KEY[]           = "infoval%d";

static const char TRUE_VAL[]              = "TRUE";
static const char FALSE_VAL[]             = "FALSE";

/* Local types */

/* Parse commands are in this structure.  Fields in this structure are
   dynamically allocated as necessary */
typedef struct PMI2_Keyvalpair {
    const char *key;
    const char *value;
    int         valueLen;  /* Length of a value (values may contain nulls, so
                              we need this) */
    int         isCopy;    /* The value is a copy (and will need to be freed)
                              if this is true, otherwise,
                              it is a null-terminated string in the original
                              buffer */
} PMI2_Keyvalpair;

typedef struct PMI2_Command {
    int               nPairs;   /* Number of key=value pairs */
    char             *command;  /* Overall command buffer */
    PMI2_Keyvalpair **pairs;    /* Array of pointers to pairs */
    int               complete;
} PMI2_Command;


/*D
PMI2_CONSTANTS - PMI2 definitions

Error Codes:
+ PMI2_SUCCESS - operation completed successfully
. PMI2_FAIL - operation failed
. PMI2_ERR_NOMEM - input buffer not large enough
. PMI2_ERR_INIT - PMI not initialized
. PMI2_ERR_INVALID_ARG - invalid argument
. PMI2_ERR_INVALID_KEY - invalid key argument
. PMI2_ERR_INVALID_KEY_LENGTH - invalid key length argument
. PMI2_ERR_INVALID_VAL - invalid val argument
. PMI2_ERR_INVALID_VAL_LENGTH - invalid val length argument
. PMI2_ERR_INVALID_LENGTH - invalid length argument
. PMI2_ERR_INVALID_NUM_ARGS - invalid number of arguments
. PMI2_ERR_INVALID_ARGS - invalid args argument
. PMI2_ERR_INVALID_NUM_PARSED - invalid num_parsed length argument
. PMI2_ERR_INVALID_KEYVALP - invalid keyvalp argument
. PMI2_ERR_INVALID_SIZE - invalid size argument
- PMI2_ERR_OTHER - other unspecified error

D*/
#define PMI2_SUCCESS                0
#define PMI2_FAIL                   -1
#define PMI2_ERR_INIT               1
#define PMI2_ERR_NOMEM              2
#define PMI2_ERR_INVALID_ARG        3
#define PMI2_ERR_INVALID_KEY        4
#define PMI2_ERR_INVALID_KEY_LENGTH 5
#define PMI2_ERR_INVALID_VAL        6
#define PMI2_ERR_INVALID_VAL_LENGTH 7
#define PMI2_ERR_INVALID_LENGTH     8
#define PMI2_ERR_INVALID_NUM_ARGS   9
#define PMI2_ERR_INVALID_ARGS       10
#define PMI2_ERR_INVALID_NUM_PARSED 11
#define PMI2_ERR_INVALID_KEYVALP    12
#define PMI2_ERR_INVALID_SIZE       13
#define PMI2_ERR_OTHER              14

/* This is here to allow spawn multiple functions to compile.  This
   needs to be removed once those functions are fixed for pmi2 */
/*
typedef struct PMI_keyval_t
{
    char * key;
    char * val;
} PMI_keyval_t;
*/

/*@
  PMI2_Connect_comm_t - connection structure used when connecting to other jobs

  Fields:
  + read - Read from a connection to the leader of the job to which
    this process will be connecting. Returns 0 on success or an MPI
    error code on failure.
  . write - Write to a connection to the leader of the job to which
    this process will be connecting. Returns 0 on success or an MPI
    error code on failure.
  . ctx - An anonymous pointer to data that may be used by the read
    and write members.
  - isMaster - Indicates which process is the "master"; may have the
    values 1 (is the master), 0 (is not the master), or -1 (neither is
    designated as the master). The two processes must agree on which
    process is the master, or both must select -1 (neither is the
    master).

  Notes:
  A typical implementation of these functions will use the read and
  write calls on a pre-established file descriptor (fd) between the
  two leading processes. This will be needed only if the PMI server
  cannot access the KVS spaces of another job (this may happen, for
  example, if each mpiexec creates the KVS spaces for the processes
  that it manages).
  
@*/
typedef struct PMI2_Connect_comm {
    int (*read)( void *buf, int maxlen, void *ctx );
    int (*write)( const void *buf, int len, void *ctx );
    void *ctx;
    int  isMaster;
} PMI2_Connect_comm_t;


/*S
  MPID_Info - Structure of an MPID info

  Notes:
  There is no reference count because 'MPI_Info' values, unlike other MPI
  objects, may be changed after they are passed to a routine without
  changing the routine''s behavior.  In other words, any routine that uses
  an 'MPI_Info' object must make a copy or otherwise act on any info value
  that it needs.

  A linked list is used because the typical 'MPI_Info' list will be short
  and a simple linked list is easy to implement and to maintain.  Similarly,
  a single structure rather than separate header and element structures are
  defined for simplicity.  No separate thread lock is provided because
  info routines are not performance critical; they may use the single
  critical section lock in the 'MPIR_Process' structure when they need a
  thread lock.

  This particular form of linked list (in particular, with this particular
  choice of the first two members) is used because it allows us to use
  the same routines to manage this list as are used to manage the
  list of free objects (in the file 'src/util/mem/handlemem.c').  In
  particular, if lock-free routines for updating a linked list are
  provided, they can be used for managing the 'MPID_Info' structure as well.

  The MPI standard requires that keys can be no less that 32 characters and
  no more than 255 characters.  There is no mandated limit on the size
  of values.

  Module:
  Info-DS
  S*/
typedef struct MPID_Info {
    int                 handle;
    int                 pobj_mutex;
    int                 ref_count;
    struct MPID_Info    *next;
    char                *key;
    char                *value;
} MPID_Info;

#define PMI2U_Info MPID_Info

/*@
  PMI2_Init - initialize the Process Manager Interface

  Output Parameter:
  + spawned - spawned flag
  . size - number of processes in the job
  . rank - rank of this process in the job
  - appnum - which executable is this on the mpiexec commandline
  
  Return values:
  Returns 'MPI_SUCCESS' on success and an MPI error code on failure.
  
  Notes:
  Initialize PMI for this process group. The value of spawned indicates whether
  this process was created by 'PMI2_Spawn_multiple'.  'spawned' will be non-zero
  iff this process group has a parent.

@*/
int PMI2_Init(int *spawned, int *size, int *rank, int *appnum);

/*@
  PMI2_Finalize - finalize the Process Manager Interface
  
  Return values:
  Returns 'MPI_SUCCESS' on success and an MPI error code on failure.
  
  Notes:
  Finalize PMI for this job.
  
@*/
int PMI2_Finalize(void);

/*@
  PMI2_Initialized - check if PMI has been initialized

  Return values:
  Non-zero if PMI2_Initialize has been called successfully, zero otherwise.
  
@*/
int PMI2_Initialized(void);

/*@
  PMI2_Abort - abort the process group associated with this process
  
  Input Parameters:
  + flag - non-zero if all processes in this job should abort, zero otherwise
  - error_msg - error message to be printed
  
  Return values:
  If the abort succeeds this function will not return.  Returns an MPI
  error code otherwise.

@*/
int PMI2_Abort(int flag, const char msg[]);

/*@
  PMI2_Spawn - spawn a new set of processes

  Input Parameters:
  + count - count of commands
  . cmds - array of command strings
  . argcs - size of argv arrays for each command string
  . argvs - array of argv arrays for each command string
  . maxprocs - array of maximum processes to spawn for each command string
  . info_keyval_sizes - array giving the number of elements in each of the 
    'info_keyval_vectors'
  . info_keyval_vectors - array of keyval vector arrays
  . preput_keyval_size - Number of elements in 'preput_keyval_vector'
  . preput_keyval_vector - array of keyvals to be pre-put in the spawned keyval space
  - jobIdSize - size of the buffer provided in jobId

  Output Parameter:
  + jobId - job id of the spawned processes
  - errors - array of errors for each command

  Return values:
  Returns 'MPI_SUCCESS' on success and an MPI error code on failure.

  Notes:
  This function spawns a set of processes into a new job.  The 'count'
  field refers to the size of the array parameters - 'cmd', 'argvs', 'maxprocs',
  'info_keyval_sizes' and 'info_keyval_vectors'.  The 'preput_keyval_size' refers
  to the size of the 'preput_keyval_vector' array.  The 'preput_keyval_vector'
  contains keyval pairs that will be put in the keyval space of the newly
  created job before the processes are started.  The 'maxprocs' array
  specifies the desired number of processes to create for each 'cmd' string.  
  The actual number of processes may be less than the numbers specified in
  maxprocs.  The acceptable number of processes spawned may be controlled by
  ``soft'' keyvals in the info arrays.  The ``soft'' option is specified by
  mpiexec in the MPI-2 standard.  Environment variables may be passed to the
  spawned processes through PMI implementation specific 'info_keyval' parameters.
@*/
int PMI2_Job_Spawn(int count, const char * cmds[],
                   int argcs[], const char ** argvs[],
                   const int maxprocs[],
                   const int info_keyval_sizes[],
                   const struct MPID_Info *info_keyval_vectors[],
                   int preput_keyval_size,
                   const struct MPID_Info *preput_keyval_vector[],
                   char jobId[], int jobIdSize,
                   int errors[]);


/*@
  PMI2_Job_GetId - get job id of this job 

  Input parameters:
  . jobid_size - size of buffer provided in jobid

  Output parameters:
  . jobid - the job id of this job
  
  Return values:
  Returns 'MPI_SUCCESS' on success and an MPI error code on failure.

@*/
int PMI2_Job_GetId(char jobid[], int jobid_size);

/*@
  PMI2_Job_GetRank - get rank of this job

  Output parameters:
  . rank - the rank of this job

  Return values:
  Returns 'PMI2_SUCCESS' on success and an PMI error code on failure.

@*/
int PMI2_Job_GetRank(int* rank);

/*@
  PMI2_Info_GetSize - get the number of processes on the node

  Output parameters:
  . size - the number of processes on the node

  Return values:
  Returns 'PMI2_SUCCESS' on success and an PMI error code on failure.
@*/
int PMI2_Info_GetSize(int* size);

/*@
  PMI2_Job_Connect - connect to the parallel job with ID jobid

  Input parameters:
  . jobid - job id of the job to connect to

  Output parameters:
  . conn - connection structure used to establish communication with
    the remote job
  
  Return values:
  Returns 'MPI_SUCCESS' on success and an MPI error code on failure.

  Notes:
  This just "registers" the other parallel job as part of a parallel
  program, and is used in the PMI2_KVS_xxx routines (see below). This
  is not a collective call and establishes a connection between all
  processes that are connected to the calling processes (on the one
  side) and that are connected to the named jobId on the other
  side. Processes that are already connected may call this routine.

@*/
int PMI2_Job_Connect(const char jobid[], PMI2_Connect_comm_t *conn);

/*@
  PMI2_Job_Disconnect - disconnects from the job with ID jobid

  Input parameters:
  . jobid - job id of the job to connect to

  Return values:
  Returns 'MPI_SUCCESS' on success and an MPI error code on failure.

@*/
int PMI2_Job_Disconnect(const char jobid[]);

/*@
  PMI2_KVS_Put - put a key/value pair in the keyval space for this job

  Input Parameters:
  + key - key
  - value - value
  
  Return values:
  Returns 'MPI_SUCCESS' on success and an MPI error code on failure.

  Notes:
  If multiple PMI2_KVS_Put calls are made with the same key between
  calls to PMI2_KVS_Fence, the behavior is undefined. That is, the
  value returned by PMI2_KVS_Get for that key after the PMI2_KVS_Fence
  is not defined.

@*/
int PMI2_KVS_Put(const char key[], const char value[]);
/*@
  PMI2_KVS_Fence - commit all PMI2_KVS_Put calls made before this fence

  Return values:
  Returns 'MPI_SUCCESS' on success and an MPI error code on failure.

  Notes:
  This is a collective call across the job.  It has semantics that are
  similar to those for MPI_Win_fence and hence is most easily
  implemented as a barrier across all of the processes in the job.
  Specifically, all PMI2_KVS_Put operations performed by any process in
  the same job must be visible to all processes (by using PMI2_KVS_Get)
  after PMI2_KVS_Fence completes.  However, a PMI implementation could
  make this a lazy operation by not waiting for all processes to enter
  their corresponding PMI2_KVS_Fence until some process issues a
  PMI2_KVS_Get. This might be appropriate for some wide-area
  implementations.
  
@*/
int PMI2_KVS_Fence(void);

/*@
  PMI2_KVS_Get - returns the value associated with key in the key-value
      space associated with the job ID jobid

  Input Parameters:
  + jobid - the job id identifying the key-value space in which to look
    for key.  If jobid is NULL, look in the key-value space of this job.
  . src_pmi_id - the pmi id of the process which put this keypair.  This
    is just a hint to the server.  PMI2_ID_NULL should be passed if no
    hint is provided.
  . key - key
  - maxvalue - size of the buffer provided in value

  Output Parameters:
  + value - value associated with key
  - vallen - length of the returned value, or, if the length is longer
    than maxvalue, the negative of the required length is returned
  
  Return values:
  Returns 'MPI_SUCCESS' on success and an MPI error code on failure.

@*/
int PMI2_KVS_Get(const char *jobid, int src_pmi_id, const char key[], char value [], int maxvalue, int *vallen);

/*@
  PMI2_Info_GetNodeAttr - returns the value of the attribute associated
      with this node

  Input Parameters:
  + name - name of the node attribute
  . valuelen - size of the buffer provided in value
  - waitfor - if non-zero, the function will not return until the
    attribute is available

  Output Parameters:
  + value - value of the attribute
  - found - non-zero indicates that the attribute was found
  
  Return values:
  Returns 'MPI_SUCCESS' on success and an MPI error code on failure.

  Notes:
  This provides a way, when combined with PMI2_Info_PutNodeAttr, for
  processes on the same node to share information without requiring a
  more general barrier across the entire job.

  If waitfor is non-zero, the function will never return with found
  set to zero.

  Predefined attributes:
  + memPoolType - If the process manager allocated a shared memory
    pool for the MPI processes in this job and on this node, return
    the type of that pool. Types include sysv, anonmmap and ntshm.
  . memSYSVid - Return the SYSV memory segment id if the memory pool
    type is sysv. Returned as a string.
  . memAnonMMAPfd - Return the FD of the anonymous mmap segment. The
    FD is returned as a string.
  - memNTName - Return the name of the Windows NT shared memory
    segment, file mapping object backed by system paging
    file.  Returned as a string.

@*/
int PMI2_Info_GetNodeAttr(const char name[], char value[], int valuelen, int *found, int waitfor);

/*@
  PMI2_Info_GetNodeAttrIntArray - returns the value of the attribute associated
      with this node.  The value must be an array of integers.

  Input Parameters:
  + name - name of the node attribute
  - arraylen - number of elements in array

  Output Parameters:
  + array - value of attribute
  . outlen - number of elements returned
  - found - non-zero if attribute was found
  
  Return values:
  Returns 'MPI_SUCCESS' on success and an MPI error code on failure.

  Notes:
  Notice that, unlike PMI2_Info_GetNodeAttr, this function does not
  have a waitfor parameter, and will return immediately with found=0
  if the attribute was not found.

  Predefined array attribute names:
  + localRanksCount - Return the number of local ranks that will be
    returned by the key localRanks.
  . localRanks - Return the ranks in MPI_COMM_WORLD of the processes
    that are running on this node.
  - cartCoords - Return the Cartesian coordinates of this process in
    the underlying network topology. The coordinates are indexed from
    zero. Value only if the Job attribute for physTopology includes
    cartesian.

@*/
int PMI2_Info_GetNodeAttrIntArray(const char name[], int array[], int arraylen, int *outlen, int *found);

/*@
  PMI2_Info_PutNodeAttr - stores the value of the named attribute
  associated with this node

  Input Parameters:
  + name - name of the node attribute
  - value - the value of the attribute

  Return values:
  Returns 'MPI_SUCCESS' on success and an MPI error code on failure.

  Notes:
  For example, it might be used to share segment ids with other
  processes on the same SMP node.
  
@*/
int PMI2_Info_PutNodeAttr(const char name[], const char value[]);

/*@
  PMI2_Info_GetJobAttr - returns the value of the attribute associated
  with this job

  Input Parameters:
  + name - name of the job attribute
  - valuelen - size of the buffer provided in value

  Output Parameters:
  + value - value of the attribute
  - found - non-zero indicates that the attribute was found
  
  Return values:
  Returns 'MPI_SUCCESS' on success and an MPI error code on failure.

@*/
int PMI2_Info_GetJobAttr(const char name[], char value[], int valuelen, int *found);

/*@
  PMI2_Info_GetJobAttrIntArray - returns the value of the attribute associated
      with this job.  The value must be an array of integers.

  Input Parameters:
  + name - name of the job attribute
  - arraylen - number of elements in array

  Output Parameters:
  + array - value of attribute
  . outlen - number of elements returned
  - found - non-zero if attribute was found
  
  Return values:
  Returns 'MPI_SUCCESS' on success and an MPI error code on failure.

  Predefined array attribute names:

  + universeSize - The size of the "universe" (defined for the MPI
    attribute MPI_UNIVERSE_SIZE

  . hasNameServ - The value hasNameServ is true if the PMI2 environment
    supports the name service operations (publish, lookup, and
    unpublish).
    
  . physTopology - Return the topology of the underlying network. The
    valid topology types include cartesian, hierarchical, complete,
    kautz, hypercube; additional types may be added as necessary. If
    the type is hierarchical, then additional attributes may be
    queried to determine the details of the topology. For example, a
    typical cluster has a hierarchical physical topology, consisting
    of two levels of complete networks - the switched Ethernet or
    Infiniband and the SMP nodes. Other systems, such as IBM BlueGene,
    have one level that is cartesian (and in virtual node mode, have a
    single-level physical topology).

  . physTopologyLevels - Return a string describing the topology type
    for each level of the underlying network. Only valid if the
    physTopology is hierarchical. The value is a comma-separated list
    of physical topology types (except for hierarchical). The levels
    are ordered starting at the top, with the network closest to the
    processes last. The lower level networks may connect only a subset
    of processes. For example, for a cartesian mesh of SMPs, the value
    is cartesian,complete. All processes are connected by the
    cartesian part of this, but for each complete network, only the
    processes on the same node are connected.
    
  . cartDims - Return a string of comma-separated values describing
    the dimensions of the Cartesian topology. This must be consistent
    with the value of cartCoords that may be returned by
    PMI2_Info_GetNodeAttrIntArray.

    These job attributes are just a start, but they provide both an
    example of the sort of external data that is available through the
    PMI interface and how extensions can be added within the same API
    and wire protocol. For example, adding more complex network
    topologies requires only adding new keys, not new routines.
    
  . isHeterogeneous - The value isHeterogeneous is true if the
    processes belonging to the job are running on nodes with different
    underlying data models.

@*/
int PMI2_Info_GetJobAttrIntArray(const char name[], int array[], int arraylen, int *outlen, int *found);

/*@
  PMI2_Nameserv_publish - publish a name 

  Input parameters:
  + service_name - string representing the service being published
  . info_ptr -
  - port - string representing the port on which to contact the service

  Return values:
  Returns 'MPI_SUCCESS' on success and an MPI error code on failure.

@*/
int PMI2_Nameserv_publish(const char service_name[], const struct MPID_Info *info_ptr, const char port[]);

/*@
  PMI2_Nameserv_lookup - lookup a service by name

  Input parameters:
  + service_name - string representing the service being published
  . info_ptr -
  - portLen - size of buffer provided in port
  
  Output parameters:
  . port - string representing the port on which to contact the service

  Return values:
  Returns 'MPI_SUCCESS' on success and an MPI error code on failure.

@*/
int PMI2_Nameserv_lookup(const char service_name[], const struct MPID_Info *info_ptr,
                        char port[], int portLen);
/*@
  PMI2_Nameserv_unpublish - unpublish a name

  Input parameters:
  + service_name - string representing the service being unpublished
  - info_ptr -

  Return values:
  Returns 'MPI_SUCCESS' on success and an MPI error code on failure.

@*/
int PMI2_Nameserv_unpublish(const char service_name[], 
                           const struct MPID_Info *info_ptr);



#if defined(__cplusplus)
}
#endif

#endif /* PMI2_H_INCLUDED */
