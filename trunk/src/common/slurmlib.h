/* 
 * slurmlib.h - Descriptions of SLURM APIs
 * See slurm.h for documentation on external functions and data structures
 *
 * Author: Moe Jette, jette@llnl.gov
 */

#define MAX_NAME_LEN	16
#define BUILD_SIZE	128
#define FEATURE_SIZE	1024
#define SLURMCTLD_HOST	"134.9.55.42"
#define SLURMCTLD_PORT	1543



/*
 * Allocate - Allocate nodes for a job with supplied contraints. 
 * Input: Spec - Specification of the job's constraints
 *        NodeList - Place into which a node list pointer can be placed
 * Output: NodeList - List of allocated nodes
 *         Returns 0 if no error, EINVAL if the request is invalid, 
 *			EAGAIN if the request can not be satisfied at present
 * NOTE: Acceptable specifications include: JobName=<name> NodeList=<list>, 
 *	Features=<features>, Groups=<groups>, Partition=<part_name>, Contiguous, 
 *	TotalCPUs=<number>, TotalNodes=<number>, MinCPUs=<number>, 
 *	MinMemory=<number>, MinTmpDisk=<number>, Key=<number>, Shared=<0|1>
 * NOTE: The calling function must free the allocated storage at NodeList[0]
 */
extern int Allocate(char *Spec, char **NodeList);

/*
 * Free_Build_Info - Free the build information buffer (if allocated)
 */
extern void Free_Build_Info(void);

/*
 * Free_Node_Info - Free the node information buffer (if allocated)
 */
extern void Free_Node_Info(void);

/*
 * Free_Part_Info - Free the partition information buffer (if allocated)
 */
extern void Free_Part_Info(void);

/*
 * Load_Build - Update the build information buffer for use by info gathering APIs
 * Output: Returns 0 if no error, EINVAL if the buffer is invalid, ENOMEM if malloc failure
 */
extern int Load_Build();

/* 
 * Load_Build_Name - Load the state information about the named build parameter
 * Input: Req_Name - Name of the parameter for which information is requested
 *		     if "", then get info for the first parameter in list
 *        Next_Name - Location into which the name of the next parameter is 
 *                   stored, "" if no more
 *        Value - Pointer to location into which the information is to be stored
 * Output: Req_Name - The parameter's name is stored here
 *         Next_Name - The name of the next parameter in the list is stored here
 *         Value - The parameter's state information
 *         Returns 0 on success, ENOENT if not found, or EINVAL if buffer is bad
 * NOTE:  Req_Name, Next_Name, and Value must be declared by caller with have 
 *        length BUILD_SIZE or larger
 */
extern int Load_Build_Name(char *Req_Name, char *Next_Name, char *Value);
 
/*
 * Load_Node - Load the supplied node information buffer for use by info gathering APIs if
 *	node records have changed since the time specified. 
 * Input: Buffer - Pointer to node information buffer
 *        Buffer_Size - size of Buffer
 * Output: Returns 0 if no error, EINVAL if the buffer is invalid, ENOMEM if malloc failure
 */
extern int Load_Node(time_t *Last_Update_Time);
 
/* 
 * Load_Node_Config - Load the state information about the named node
 * Input: Req_Name - Name of the node for which information is requested
 *		     if "", then get info for the first node in list
 *        Next_Name - Location into which the name of the next node is 
 *                   stored, "" if no more
 *        CPUs, etc. - Pointers into which the information is to be stored
 * Output: Next_Name - Name of the next node in the list
 *         CPUs, etc. - The node's state information
 *         Returns 0 on success, ENOENT if not found, or EINVAL if buffer is bad
 * NOTE:  Req_Name, Next_Name, Partition, and NodeState must be declared by the 
 *        caller and have length MAX_NAME_LEN or larger
 *        Features must be declared by the caller and have length FEATURE_SIZE or larger
 */
extern int Load_Node_Config(char *Req_Name, char *Next_Name, int *CPUs, 
	int *RealMemory, int *TmpDisk, int *Weight, char *Features,
	char *Partition, char *NodeState);

/*
 * Load_Part - Update the partition information buffer for use by info gathering APIs if 
 *	partition records have changed since the time specified. 
 * Input: Last_Update_Time - Pointer to time of last buffer
 * Output: Last_Update_Time - Time reset if buffer is updated
 *         Returns 0 if no error, EINVAL if the buffer is invalid, ENOMEM if malloc failure
 */
int Load_Part(time_t *Last_Update_Time);

/* 
 * Load_Part_Name - Load the state information about the named partition
 * Input: Req_Name - Name of the partition for which information is requested
 *		     if "", then get info for the first partition in list
 *        Next_Name - Location into which the name of the next partition is 
 *                   stored, "" if no more
 *        MaxTime, etc. - Pointers into which the information is to be stored
 * Output: Req_Name - The partition's name is stored here
 *         Next_Name - The name of the next partition in the list is stored here
 *         MaxTime, etc. - The partition's state information
 *         Returns 0 on success, ENOENT if not found, or EINVAL if buffer is bad
 * NOTE:  Req_Name and Next_Name must be declared by caller with have length MAX_NAME_LEN or larger
 *        Nodes and AllowGroups must be declared by caller with length of FEATURE_SIZE or larger
 */
int Load_Part_Name(char *Req_Name, char *Next_Name, int *MaxTime, int *MaxNodes, 
	int *TotalNodes, int *TotalCPUs, int *Key, int *StateUp, int *Shared, int *Default,
	char *Nodes, char *AllowGroups);

/* 
 * Parse_Node_Name - Parse the node name for regular expressions and return a sprintf format 
 * generate multiple node names as needed.
 * Input: NodeName - Node name to parse
 * Output: Format - sprintf format for generating names
 *         Start_Inx - First index to used
 *         End_Inx - Last index value to use
 *         Count_Inx - Number of index values to use (will be zero if none)
 *         return 0 if no error, error code otherwise
 * NOTE: The calling program must execute free(Format) when the storage location is no longer needed
 */
extern int Parse_Node_Name(char *NodeName, char **Format, int *Start_Inx, int *End_Inx, int *Count_Inx);

/* 
 * Reconfigure - _ Request that slurmctld re-read the configuration files
 * Output: Returns 0 on success, errno otherwise
 */
extern int Reconfigure();

/* 
 * Update_Config - _ Request that slurmctld update its configuration per request
 * Input: A line containing configuration information per the configuration file format
 * Output: Returns 0 on success, errno otherwise
 */
extern int Update_Config(char *Spec);
