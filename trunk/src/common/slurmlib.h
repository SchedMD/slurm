/* 
 * slurmlib.h - Descriptions of SLURM APIs
 * See slurm.h for documentation on external functions and data structures
 *
 * Author: Moe Jette, jette@llnl.gov
 */

#define MAX_NAME_LEN 16		/* Maximum length of partition or node name */


/*
 * Load_Node - Load the supplied node information buffer for use by info gathering APIs
 * Input: Buffer - Pointer to node information buffer
 *        Buffer_Size - size of Buffer
 * Output: Returns 0 if no error, EINVAL if the buffer is invalid
 */
extern int Load_Node(char *Buffer, int Buffer_Size);
 
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
 * NOTE:  Req_Name and Next_Name must have length MAX_NAME_LEN
 */
extern int Load_Node_Config(char *Req_Name, char *Next_Name, int *CPUs, 
	int *RealMemory, int *TmpDisk, int *Weight, char **Features,
	char **Partition, char **NodeState);
  
/* 
 * Load_Nodes_Idle - Load the bitmap of idle nodes
 * Input: NodeBitMap - Location to put bitmap pointer
 *        BitMap_Size - Location into which the byte size of NodeBitMap is to be stored
 * Output: NodeBitMap - Pointer to bitmap
 *         BitMap_Size - Byte size of NodeBitMap
 *         Returns 0 on success or EINVAL if buffer is bad
 */
extern int Load_Nodes_Idle(unsigned **NodeBitMap, int *BitMap_Size);

/* 
 * Load_Nodes_Up - Load the bitmap of up nodes
 * Input: NodeBitMap - Location to put bitmap pointer
 *        BitMap_Size - Location into which the byte size of NodeBitMap is to be stored
 * Output: NodeBitMap - Pointer to bitmap
 *         BitMap_Size - Byte size of NodeBitMap
 *         Returns 0 on success or EINVAL if buffer is bad
 */
extern int Load_Nodes_Up(unsigned **NodeBitMap, int *BitMap_Size);

/*
 * Load_Part - Load the supplied partition information buffer for use by info gathering APIs
 * Input: Buffer - Pointer to partition information buffer
 *        Buffer_Size - size of Buffer
 * Output: Returns 0 if no error, EINVAL if the buffer is invalid
 */
extern int Load_Part(char *Buffer, int Buffer_Size);

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
 *         BitMap_Size - Size of BitMap in bytes
 *         Returns 0 on success, ENOENT if not found, or EINVAL if buffer is bad
 * NOTE:  Req_Name and Next_Name must have length MAX_NAME_LEN
 */
extern int Load_Part_Name(char *Req_Name, char *Next_Name, int *MaxTime, int *MaxNodes, 
	int *TotalNodes, int *TotalCPUs, int *Key, int *StateUp, int *Shared,
	char **Nodes, char **AllowGroups, unsigned **NodeBitMap, int *BitMap_Size);
