/* 
 * slurmlib.h - descriptions of slurm APIs
 * see slurm.h for documentation on external functions and data structures
 *
 * author: moe jette, jette@llnl.gov
 */

#define BUILD_SIZE	128
#define BUILD_STRUCT_VERSION 1
#define FEATURE_SIZE	1024
#define MAX_ID_LEN	32
#define MAX_NAME_LEN	16
#define NODE_STRUCT_VERSION 1
#define PART_STRUCT_VERSION 1
#define SLURMCTLD_HOST	"134.9.55.42"
#define SLURMCTLD_PORT	1543

/* INFINITE is used to identify unlimited configurations,  */
/* eg. the maximum count of nodes any job may use in some partition */
#define	INFINITE (0xffffffff)

/* last entry must be STATE_END, keep in sync with node_state_string    	*/
/* any value less than or equal to zero is down. if a node was in state 	*/
/* STATE_BUSY and stops responding, its state becomes -(STATE_BUSY), etc.	*/
enum node_states {
	STATE_DOWN,		/* node is not responding */
	STATE_UNKNOWN,		/* node's initial state, unknown */
	STATE_IDLE,		/* node idle and available for use */
	STATE_STAGE_IN,		/* node has been allocated, job not yet running */
	STATE_BUSY,		/* node has been allocated, job currently */
	STATE_DRAINED,		/* node idle and not to be allocated future work */
	STATE_DRAINING,		/* node in use, but not to be allocated future work */
	STATE_END		/* last entry in table */
};

struct build_table {
	uint16_t backup_interval;/* slurmctld save state interval, seconds */
	char *backup_location;	/* pathname of state save directory */
	char *backup_machine;	/* name of slurmctld secondary server */
	char *control_daemon;	/* pathname of slurmctld */
	char *control_machine;	/* name of slurmctld primary server */
	uint16_t controller_timeout; /* seconds for secondary slurmctld to take over */
	char *epilog;		/* pathname of job epilog */
	uint16_t fast_schedule;	/* 1 to *not* check configurations by node 
				 * (only check configuration file, faster) */
	uint16_t hash_base;	/* base used for hashing node table */
	uint16_t heartbeat_interval; /* interval between node heartbeats, seconds */
	char *init_program;	/* pathname of program to complete with exit 0 
 				 * before slurmctld or slurmd start on that node */
	uint16_t kill_wait;	/* seconds from SIGXCPU to SIGKILL on job termination */
	char *prioritize;	/* pathname of program to set initial job priority */
	char *prolog;		/* pathname of job prolog */
	char *server_daemon;	/* pathame of slurmd */
	uint16_t server_timeout;/* how long slurmctld waits for setting node DOWN */
	char *slurm_conf;	/* pathname of slurm config file */
	char *tmp_fs;		/* pathname of temporary file system */
};

struct build_buffer {
	time_t last_update;	/* time of last buffer update */
	void *raw_buffer_ptr;	/* raw network buffer info */
	struct build_table *build_table_ptr;
};

struct node_table {
	char *name;		/* name of the node. a null name indicates defunct node */
	uint32_t node_state;	/* state of the node, see node_states */
	uint32_t cpus;		/* count of processors running on the node */
	uint32_t real_memory;	/* megabytes of real memory on the node */
	uint32_t tmp_disk;	/* megabytes of total disk in TMP_FS */
	uint32_t weight;	/* desirability of use */
	char *features;		/* comma delimited feature list */
	char *partition;	/* partition name */ 
};

struct node_buffer {
	time_t last_update;	/* time of last buffer update */
	int node_count;		/* count of entries in node_table */
	void *raw_buffer_ptr;	/* raw network buffer info */
	struct node_table *node_table_ptr;
};

struct part_table {
	char *name;		/* name of the partition */
	uint32_t max_time;	/* minutes or INFINITE */
	uint32_t max_nodes;	/* per job or INFINITE */
	uint32_t total_nodes;	/* total number of nodes in the partition */
	uint32_t total_cpus;	/* total number of cpus in the partition */
	uint16_t default_part;	/* 1 if if this is default partition */
	uint16_t key;		/* 1 if slurm distributed key is required for use  */
	uint16_t shared;	/* 1 if >1 job can share a node, 2 if required */
	uint16_t state_up;	/* 1 if state is up, 0 if down */
	char *nodes;		/* comma delimited list names of nodes in partition */
	char *allow_groups;	/* comma delimited list of groups, null indicates all */
};

struct part_buffer {
	time_t last_update;	/* time of last buffer update */
	int part_count;		/* count of entries in node_table */
	void *raw_buffer_ptr;	/* raw network buffer info */
	struct part_table *part_table_ptr;
};

/*
 * slurm_allocate - allocate nodes for a job with supplied contraints. 
 * input: spec - specification of the job's constraints
 *        job_id - place into which a job_id pointer can be placed
 * output: job_id - node_list - list of allocated nodes
 *         returns 0 if no error, EINVAL if the request is invalid, 
 *			EAGAIN if the request can not be satisfied at present
 * NOTE: required specifications include: User=<uid> Script=<pathname>
 *	optional specifications include: Contiguous=<YES|NO> 
 *	Distribution=<BLOCK|CYCLE> Features=<features> Groups=<groups>
 *	JobId=<id> JobName=<name> Key=<credential> MinProcs=<count>
 *	MinRealMemory=<MB> MinTmpDisk=<MB> Partition=<part_name>
 *	Priority=<integer> ProcsPerTask=<count> ReqNodes=<node_list>
 *	Shared=<YES|NO> TimeLimit=<minutes> TotalNodes=<count>
 *	TotalProcs=<count>
 * NOTE: the calling function must free the allocated storage at node_list[0]
 */
extern int slurm_allocate (char *spec, char **node_list, char **job_id);

/*
 * slurm_cancel - cancel the specified job 
 * input: job_id - the job_id to be cancelled
 * output: returns 0 if no error, EINVAL if the request is invalid, 
 *			EAGAIN if the request can not be satisfied at present
 */
extern int slurm_cancel (char *job_id);

/*
 * slurm_free_build_info - free the build information buffer (if allocated)
 * NOTE: buffer is loaded by slurm_load_build.
 */
extern void slurm_free_build_info (struct build_buffer *build_buffer_ptr);

/*
 * slurm_free_node_info - free the node information buffer (if allocated)
 * NOTE: buffer is loaded by slurm_load_node.
 */
extern void slurm_free_node_info (struct node_buffer *node_buffer_ptr);

/*
 * slurm_free_part_info - free the partition information buffer (if allocated)
 * NOTE: buffer is loaded by load_part.
 */
extern void slurm_free_part_info (struct part_buffer *part_buffer_ptr);

/*
 * slurm_load_build - load the slurm build information buffer for use by info 
 *	gathering APIs if build info has changed since the time specified. 
 * input: update_time - time of last update
 *	build_buffer_ptr - place to park build_buffer pointer
 * output: build_buffer_ptr - pointer to allocated build_buffer
 *	returns -1 if no update since update_time, 
 *		0 if update with no error, 
 *		EINVAL if the buffer (version or otherwise) is invalid, 
 *		ENOMEM if malloc failure
 * NOTE: the allocated memory at build_buffer_ptr freed by slurm_free_node_info.
 */
extern int slurm_load_build (time_t update_time, 
	struct build_buffer **build_buffer_ptr);


/*
 * slurm_load_node - load the supplied node information buffer for use by info 
 *	gathering APIs if node records have changed since the time specified. 
 * input: update_time - time of last update
 *	node_buffer_ptr - place to park node_buffer pointer
 * output: node_buffer_ptr - pointer to allocated node_buffer
 *	returns -1 if no update since update_time, 
 *		0 if update with no error, 
 *		EINVAL if the buffer (version or otherwise) is invalid, 
 *		ENOMEM if malloc failure
 * NOTE: the allocated memory at node_buffer_ptr freed by slurm_free_node_info.
 */
extern int slurm_load_node (time_t update_time, struct node_buffer **node_buffer_ptr);

/*
 * slurm_load_part - load the supplied partition information buffer for use by info 
 *	gathering APIs if partition records have changed since the time specified. 
 * input: update_time - time of last update
 *	part_buffer_ptr - place to park part_buffer pointer
 * output: part_buffer_ptr - pointer to allocated part_buffer
 *	returns -1 if no update since update_time, 
 *		0 if update with no error, 
 *		EINVAL if the buffer (version or otherwise) is invalid, 
 *		ENOMEM if malloc failure
 * NOTE: the allocated memory at part_buffer_ptr freed by slurm_free_part_info.
 */
extern int slurm_load_part (time_t update_time, struct part_buffer **part_buffer_ptr);

/*
 * slurm_submit - submit/queue a job with supplied contraints. 
 * input: spec - specification of the job's constraints
 *	job_id - place to store id of submitted job
 * output: job_id - the job's id
 *	returns 0 if no error, EINVAL if the request is invalid
 * NOTE: required specification include: Script=<script_path_name>
 *	User=<uid>
 * NOTE: optional specifications include: Contiguous=<YES|NO> 
 *	Distribution=<BLOCK|CYCLE> Features=<features> Groups=<groups>
 *	JobId=<id> JobName=<name> Key=<key> MinProcs=<count> 
 *	MinRealMemory=<MB> MinTmpDisk=<MB> Partition=<part_name>
 *	Priority=<integer> ProcsPerTask=<count> ReqNodes=<node_list>
 *	Shared=<YES|NO> TimeLimit=<minutes> TotalNodes=<count>
 *	TotalProcs=<count>
 */
extern int slurm_submit (char *spec, char **job_id);

/*
 * load_job - load the supplied job information buffer for use by info gathering 
 *	APIs if job records have changed since the time specified. 
 * input: buffer - pointer to job information buffer
 *        buffer_size - size of buffer
 * output: returns 0 if no error, EINVAL if the buffer is invalid,
 *		 ENOMEM if malloc failure
 * NOTE: buffer is used by load_job_config and freed by free_job_info.
 */
extern int load_job (time_t * last_update_time);

/* 
 * parse_node_name - parse the node name for regular expressions and return a sprintf format 
 * generate multiple node names as needed.
 * input: node_name - node name to parse
 * output: format - sprintf format for generating names
 *         start_inx - first index to used
 *         end_inx - last index value to use
 *         count_inx - number of index values to use (will be zero if none)
 *         return 0 if no error, error code otherwise
 * NOTE: the calling program must execute free(format) when the storage location is no longer needed
 */
extern int parse_node_name (char *node_name, char **format, int *start_inx,
			    int *end_inx, int *count_inx);

/* 
 * reconfigure - _ request that slurmctld re-read the configuration files
 * output: returns 0 on success, errno otherwise
 */
extern int reconfigure ();

/* 
 * update_config - _ request that slurmctld update its configuration per request
 * input: a line containing configuration information per the configuration file format
 * output: returns 0 on success, errno otherwise
 */
extern int update_config (char *spec);
