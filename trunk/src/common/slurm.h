/* 
 * slurm.h - Definitions for slurm API use
 *
 * Note: The job, node, and partition specifications are all of the 
 * same basic format:
 * If the first character of a line is "#" then it is a comment.
 * Place all information for a single node, partition, or job on a 
 *    single line. 
 * Space delimit collection of keywords and values and separate
 *    the keyword from value with an equal sign (e.g. "CPUs=3"). 
 * List entries should be comma separated (e.g. "Nodes=lx01,lx02").
 * 
 * See the SLURM administrator guide for more details.
 */

#ifndef _HAVE_SLURM_H
#define _HAVE_SLURM_H

#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include "list.h"
#include "slurmlib.h"

#define DEBUG_SYSTEM 1

#define BACKUP_INTERVAL		60
#define BACKUP_LOCATION		"/usr/local/SLURM/Slurm.State"
#define CONTROL_DAEMON  	"/usr/local/SLURM/Slurmd.Control"
#define CONTROLLER_TIMEOUT 	300
#define EPILOG			""
#define HASH_BASE		10
#define HEARTBEAT_INTERVAL	60
#define INIT_PROGRAM		""
#define KILL_WAIT		30
#define	PRIORITIZE		""
#define PROLOG			""
#define SERVER_DAEMON   	"/usr/local/SLURM/Slurmd.Server"
#define SERVER_TIMEOUT  	300
#define SLURM_CONF		"/g/g0/jette/slurm/etc/SLURM.conf2"
#define TMP_FS			"/tmp"

/* NOTE: Change BUILD_STRUCT_VERSION value whenever the contents of BUILD_STRUCT_FORMAT change */
#define BUILD_STRUCT_VERSION 1
#define HEAD_FORMAT "#Time=%lu Version=%d\n"
#define BUILD_STRUCT_FORMAT  "%s %s\n"
#define BUILD_STRUCT2_FORMAT "%s %d\n"

extern char *ControlMachine;	/* Name of computer acting as SLURM controller */
extern char *BackupController;	/* Name of computer acting as SLURM backup controller */

/* NOTE: Change JOB_STRUCT_VERSION value whenever the contents of "struct Job_Record" 
 * change with respect to the API structures  */
#define JOB_STRUCT_VERSION 1
struct Job_Record {
    int Job_Id;
    int User_Id;
    int MaxTime;		/* -1 if unlimited */
};

/* NOTE: Change NODE_STRUCT_VERSION value whenever the contents of NODE_STRUCT_FORMAT change */
#define NODE_STRUCT_VERSION 1
#define NODE_STRUCT_FORMAT "NodeName=%s State=%s CPUs=%d RealMemory=%d TmpDisk=%d Weight=%d Feature=%s #Partition=%s\n"
#define CONFIG_MAGIC 'C'
#define NODE_MAGIC   'N'
struct Config_Record {
#if DEBUG_SYSTEM
    char Magic;			/* Magic cookie to test data integrity */
#endif
    int CPUs;			/* Count of CPUs running on the node */
    int RealMemory;		/* Megabytes of real memory on the node */
    int TmpDisk;		/* Megabytes of total storage in TMP_FS file system */
    int Weight;			/* Arbitrary priority of node for scheduling work on */
    char *Feature;		/* Arbitrary list of features associated with a node */
    char *Nodes;		/* Names of nodes in partition configuration record */
    unsigned *NodeBitMap;	/* Bitmap of nodes in configuration record */
};
extern List Config_List;		/* List of Config_Record entries */

/* Last entry must be STATE_END, keep in sync with Node_State_String    	*/
/* Any value less than or equal to zero is down. If a node was in state 	*/
/* STATE_BUSY and stops responding, its state becomes -(STATE_BUSY), etc.	*/
enum Node_State {
	STATE_DOWN,		/* Node is not responding */
	STATE_UNKNOWN, 		/* Node's initial state, unknown */
	STATE_IDLE, 		/* Node idle and available for use */
	STATE_STAGE_IN, 	/* Node has been allocated to a job, which has not yet begun execution */
	STATE_BUSY,		/* Node allocated to a job and that job is actively running */
	STATE_STAGE_OUT,	/* Node has been allocated to a job, which has completed execution */
	STATE_DRAINED, 		/* Node idle and not to be allocated future work */
	STATE_DRAINING,		/* Node in use, but not to be allocated future work */
	STATE_END };		/* LAST ENTRY IN TABLE */
/* Last entry must be "END", keep in sync with Node_State */
extern char *Node_State_String[];

extern time_t Last_BitMap_Update;	/* Time of last node creation or deletion */
extern time_t Last_Node_Update;		/* Time of last update to Node Records */
struct Node_Record {
#if DEBUG_SYSTEM
    char Magic;				/* Magic cookie to test data integrity */
#endif
    char Name[MAX_NAME_LEN];		/* Name of the node. A NULL name indicates defunct node */
    int NodeState;			/* State of the node, see Node_State above, negative if down */
    time_t LastResponse;		/* Last response from the node */
    int CPUs;				/* Actual count of CPUs running on the node */
    int RealMemory;			/* Actual megabytes of real memory on the node */
    int TmpDisk;			/* Actual megabytes of total storage in TMP_FS file system */
    struct Config_Record *Config_Ptr;	/* Configuration specification for this node */
    struct Part_Record *Partition_Ptr;	/* Partition for this node */
};
extern struct Node_Record *Node_Record_Table_Ptr; /* Location of the node records */
extern int	Node_Record_Count;	/* Count of records in the Node Record Table */
extern int	*Hash_Table;		/* Table of hashed indicies into Node_Record */
extern unsigned *Up_NodeBitMap;		/* Bitmap of nodes are UP */
extern unsigned *Idle_NodeBitMap;	/* Bitmap of nodes are IDLE */
extern struct 	Config_Record Default_Config_Record;
extern struct 	Node_Record Default_Node_Record;

/* NOTE: Change PART_STRUCT_VERSION value whenever the contents of PART_STRUCT_FORMAT change */
#define PART_STRUCT_VERSION 1
#define PART_STRUCT_FORMAT "PartitionName=%s MaxNodes=%d MaxTime=%d Nodes=%s Key=%s Default=%s AllowGroups=%s Shared=%s State=%s #TotalNodes=%d TotalCPUs=%d\n"
#define PART_MAGIC 'P'
extern time_t Last_Part_Update;	/* Time of last update to Part Records */
struct Part_Record {
#if DEBUG_SYSTEM
    char Magic;			/* Magic cookie to test data integrity */
#endif
    char Name[MAX_NAME_LEN];	/* Name of the partition */
    int MaxTime;		/* -1 if unlimited */
    int MaxNodes;		/* -1 if unlimited */
    int TotalNodes;		/* Total number of nodes in the partition */
    int TotalCPUs;		/* Total number of CPUs in the partition */
    unsigned Key:1;		/* 1 if SLURM distributed key is required for use of partition */
    unsigned Shared:2;		/* 1 if more than one job can execute on a node, 2 if required */
    unsigned StateUp:1;		/* 1 if state is UP, 0 if DOWN */
    char *Nodes;		/* Names of nodes in partition */
    char *AllowGroups;		/* NULL indicates ALL */
    unsigned *NodeBitMap;	/* Bitmap of nodes in partition */
};
extern List Part_List;		/* List of Part_Record entries */
extern struct	Part_Record Default_Part;		/* Default configuration values */
extern char Default_Part_Name[MAX_NAME_LEN];	/* Name of default partition */
extern struct Part_Record *Default_Part_Loc;	/* Location of default partition */

/*
 * BitMap2NodeName - Given a bitmap, build a node list representation
 * Input: BitMap - Bitmap pointer
 *        Node_List - Place to put node list
 * Output: Node_List - Set to node list or NULL on error 
 *         Returns 0 if no error, otherwise EINVAL or ENOMEM
 * NOTE: Consider returning the node list as a regular expression if helpful
 * NOTE: The caller must free memory at Node_List when no longer required
 */
extern int BitMap2NodeName(unsigned *BitMap, char **Node_List);

/*
 * BitMapAND - AND two bitmaps together
 * Input: BitMap1 and BitMap2 - The bitmaps to AND
 * Output: BitMap1 is set to the value of BitMap1 & BitMap2
 */
extern void BitMapAND(unsigned *BitMap1, unsigned *BitMap2);

/*
 * BitMapClear - Clear the specified bit in the specified bitmap
 * Input: BitMap - The bit map to manipulate
 *        Position - Postition to clear
 * Output: BitMap - Updated value
 */
extern void BitMapClear(unsigned *BitMap, int Position);

/*
 * BitMapCopy - Create a copy of a bitmap
 * Input: BitMap - The bitmap create a copy of
 * Output: Returns pointer to copy of BitMap or NULL if error (no memory)
 *   The returned value MUST BE FREED by the calling routine
 */
extern unsigned * BitMapCopy(unsigned *BitMap);

/*
 * BitMapCount - Return the count of set bits in the specified bitmap
 * Input: BitMap - The bit map to get count from
 * Output: Returns the count of set bits
 */
extern int BitMapCount(unsigned *BitMap);

/*
 * BitMapFill - Fill the provided bitmap so that all bits between the highest and lowest
 * 	previously set bits are also set (i.e fill in the gaps to make it contiguous)
 * Input: BitMap - Pointer to the bit map to fill in
 * Output: BitMap - The filled in bitmap
 */
extern void BitMapFill(unsigned *BitMap);

/* 
 * BitMapIsSuper - Report if one bitmap's contents are a superset of another
 * Input: BitMap1 and BitMap2 - The bitmaps to compare
 * Output: Return 1 if if all bits in BitMap1 are also in BitMap2, 0 otherwise 
 */
extern int BitMapIsSuper(unsigned *BitMap1, unsigned *BitMap2);

/*
 * BitMapOR - OR two bitmaps together
 * Input: BitMap1 and BitMap2 - The bitmaps to OR
 * Output: BitMap1 is set to the value of BitMap1 | BitMap2
 */
extern void BitMapOR(unsigned *BitMap1, unsigned *BitMap2);

/*
 * BitMapPrint - Convert the specified bitmap into a printable hexadecimal string
 * Input: BitMap - The bit map to print
 * Output: Returns a string
 * NOTE: The returned string must be freed by the calling program
 */
extern char *BitMapPrint(unsigned *BitMap);

/*
 * BitMapSet - Set the specified bit in the specified bitmap
 * Input: BitMap - The bit map to manipulate
 *        Position - Postition to set
 * Output: BitMap - Updated value
 */
extern void BitMapSet(unsigned *BitMap, int Position);

/*
 * BitMapValue - Return the value of specified bit in the specified bitmap
 * Input: BitMap - The bit map to get value from
 *        Position - Postition to get
 * Output: Normally returns the value 0 or 1, returns -1 if given bad BitMap ponter
 */
extern int BitMapValue(unsigned *BitMap, int Position);

/*
 * Create_Config_Record - Create a Config_Record entry, append it to the Config_List, 
 *	and set is values to the defaults in Default_Config_Record.
 * Input: Error_Code - Pointer to an error code
 * Output: Returns pointer to the Config_Record
 *         Error_Code - set to zero if no error, errno otherwise
 * NOTE: The pointer returned is allocated memory that must be freed when no longer needed.
 */
extern struct Config_Record *Create_Config_Record(int *Error_Code);

/* 
 * Create_Node_Record - Create a node record
 * Input: Error_Code - Location to store error value in
 *        Config_Point - Pointer to node's configuration information
 *        Node_Name - Name of the node
 * Output: Error_Code - Set to zero if no error, errno otherwise
 *         Returns a pointer to the record or NULL if error
 * NOTE The record's values are initialized to those of Default_Node_Record, Node_Name and 
 *	Config_Point's CPUs, RealMemory, and TmpDisk values
 * NOTE: Allocates memory that should be freed with Delete_Part_Record
 */
extern struct Node_Record *Create_Node_Record(int *Error_Code, struct Config_Record *Config_Point,
	char *Node_Name);

/* 
 * Create_Part_Record - Create a partition record
 * Input: Error_Code - Location to store error value in
 * Output: Error_Code - Set to zero if no error, errno otherwise
 *         Returns a pointer to the record or NULL if error
 * NOTE: The record's values are initialized to those of Default_Part
 */
extern struct Part_Record *Create_Part_Record(int *Error_Code);

/* 
 * Delete_Node_Record - Delete record for node with specified name
 *   To avoid invalidating the bitmaps and hash table, we just clear the name 
 *   set its state to STATE_DOWN
 * Input: name - name of the desired node 
 * Output: return 0 on success, errno otherwise
 */
extern int Delete_Node_Record(char *name);

/* 
 * Delete_Part_Record - Delete record for partition with specified name
 * Input: name - name of the desired node 
 * Output: return 0 on success, errno otherwise
 */
extern int Delete_Part_Record(char *name);

/* 
 * Dump_Node - Dump all configuration and node information to a buffer
 * Input: Buffer_Ptr - Location into which a pointer to the data is to be stored.
 *                     The data buffer is actually allocated by Dump_Node and the 
 *                     calling function must free the storage.
 *         Buffer_Size - Location into which the size of the created buffer is in bytes
 *         Update_Time - Dump new data only if partition records updated since time 
 *                       specified, otherwise return empty buffer
 * Output: Buffer_Ptr - The pointer is set to the allocated buffer.
 *         Buffer_Size - Set to size of the buffer in bytes
 *         Update_Time - set to time partition records last updated
 *         Returns 0 if no error, errno otherwise
 * NOTE: In this prototype, the buffer at *Buffer_Ptr must be freed by the caller
 * NOTE: This is a prototype for a function to ship data partition to an API.
 */
extern int Dump_Node(char **Buffer_Ptr, int *Buffer_Size, time_t *Update_Time);

/* 
 * Dump_Part - Dump all partition information to a buffer
 * Input: Buffer_Ptr - Location into which a pointer to the data is to be stored.
 *                     The data buffer is actually allocated by Dump_Part and the 
 *                     calling function must free the storage.
 *         Buffer_Size - Location into which the size of the created buffer is in bytes
 *         Update_Time - Dump new data only if partition records updated since time 
 *                       specified, otherwise return empty buffer
 * Output: Buffer_Ptr - The pointer is set to the allocated buffer.
 *         Buffer_Size - Set to size of the buffer in bytes
 *         Update_Time - set to time partition records last updated
 *         Returns 0 if no error, errno otherwise
 * NOTE: In this prototype, the buffer at *Buffer_Ptr must be freed by the caller
 * NOTE: This is a prototype for a function to ship data partition to an API.
 */
extern int Dump_Part(char **Buffer_Ptr, int *Buffer_Size, time_t *Update_Time);

/* 
 * Find_Node_Record - Find a record for node with specified name,
 * Input: name - name of the desired node 
 * Output: return pointer to node record or NULL if not found
 */
extern struct Node_Record *Find_Node_Record(char *name);

/* 
 * Find_Part_Record - Find a record for partition with specified name,
 * Input: name - name of the desired partition 
 * Output: return pointer to node partition or NULL if not found
 */
extern struct Part_Record *Find_Part_Record(char *name);

/* 
 * Init_Node_Conf - Initialize the node configuration values. 
 * This should be called before creating any node or configuration entries.
 * Output: return value - 0 if no error, otherwise an error code
 */
extern int Init_Node_Conf();

/* 
 * Init_Part_Conf - Initialize the partition configuration values. 
 * This should be called before creating any partition entries.
 * Output: return value - 0 if no error, otherwise an error code
 */
extern int Init_Part_Conf();


/* List_Compare_Config - Compare two entry from the config list based upon weight, 
 * see list.h for documentation */
extern int List_Compare_Config(void *Config_Entry1, void *Config_Entry2);

/* List_Delete_Config - Delete an entry from the configuration list, see list.h for documentation */
extern void List_Delete_Config(void *Config_Entry);

/* List_Find_Config - Find an entry in the configuration list, see list.h for documentation 
 * Key is partition name or "UNIVERSAL_KEY" for all configuration */
extern int List_Find_Config(void *Config_Entry, void *key);

/* List_Delete_Part - Delete an entry from the partition list, see list.h for documentation */
extern void List_Delete_Part(void *Part_Entry);

/* List_Find_Part - Find an entry in the partition list, see list.h for documentation 
 * Key is partition name or "UNIVERSAL_KEY" for all partitions */
extern int List_Find_Part(void *Part_Entry, void *key);

/*
 * Load_Integer - Parse a string for a keyword, value pair  
 * Input: *destination - Location into which result is stored
 *        keyword - String to search for
 *        In_Line - String to search for keyword
 * Output: *destination - set to value, No change if value not found, 
 *             Set to 1 if keyword found without value, 
 *             Set to -1 if keyword followed by "UNLIMITED"
 *         In_Line - The keyword and value (if present) are overwritten by spaces
 *         return value - 0 if no error, otherwise an error code
 * NOTE: In_Line is overwritten, DO NOT USE A CONSTANT
 */
extern int Load_Integer(int *destination, char *keyword, char *In_Line);

/*
 * Load_String - Parse a string for a keyword, value pair  
 * Input: *destination - Location into which result is stored
 *        keyword - String to search for
 *        In_Line - String to search for keyword
 * Output: *destination - set to value, No change if value not found, 
 *	     if *destination had previous value, that memory location is automatically freed
 *         In_Line - The keyword and value (if present) are overwritten by spaces
 *         return value - 0 if no error, otherwise an error code
 * NOTE: destination must be free when no longer required
 * NOTE: if destination is non-NULL at function call time, it will be freed 
 * NOTE: In_Line is overwritten, DO NOT USE A CONSTANT
 */
extern int Load_String(char **destination, char *keyword, char *In_Line);

/* Node_Lock - Lock the node and configuration information */
extern void Node_Lock();

/* Node_Unlock - Unlock the node and configuration information */
extern void Node_Unlock();

/*
 * NodeName2BitMap - Given a node list, build a bitmap representation
 * Input: Node_List - List of nodes
 *        BitMap - Place to put bitmap pointer
 * Output: BitMap - Set to bitmap or NULL on error 
 *         Returns 0 if no error, otherwise EINVAL or ENOMEM
 * NOTE: The caller must free memory at BitMap when no longer required
 */
extern int NodeName2BitMap(char *Node_List, unsigned **BitMap);

/* Part_Lock - Lock the partition information */
extern void Part_Lock();

/* Part_Unlock - Unlock the partition information */
extern void Part_Unlock();

/* 
 * Read_Buffer - Read a line from the specified buffer
 * Input: Buffer - Pointer to read buffer, must be allocated by alloc()
 *        Buffer_Offset - Byte offset in Buffer, read location
 *        Buffer_Size - Byte size of Buffer
 *        Line - Pointer to location to be loaded with POINTER TO THE LINE
 * Output: Buffer_Offset - Incremented by  size of size plus the Value size itself
 *         Line - Set to pointer to the line
 *         Returns 0 if no error or EFAULT on end of buffer, EINVAL on bad tag 
 */
extern int Read_Buffer(char *Buffer, int *Buffer_Offset, int Buffer_Size, char **Line);

/*
 * Read_SLURM_Conf - Load the SLURM configuration from the specified file 
 * Call Init_SLURM_Conf before ever calling Read_SLURM_Conf.  
 * Read_SLURM_Conf can be called more than once if so desired.
 * Input: File_Name - Name of the file containing SLURM configuration information
 * Output: Return - 0 if no error, otherwise an error code
 */
extern int Read_SLURM_Conf (char *File_Name);

/* 
 * Report_Leftover - Report any un-parsed (non-whitespace) characters on the
 * configuration input line.
 * Input: In_Line - What is left of the configuration input line.
 *        Line_Num - Line number of the configuration file.
 * Output: NONE
 */
extern void Report_Leftover(char *In_Line, int Line_Num);

/* 
 * Update_Node - Update a node configuration data
 * Input: NodeName - Node name specification (can include real expression)
 *        Spec - The updates to the node's specification 
 * Output:  Return - 0 if no error, otherwise an error code
 */
extern int Update_Node(char *NodeName, char *Spec);

/* 
 * Update_Part - Update a partition's configuration data
 * Input: PartitionName - Partition's name
 *        Spec - The updates to the partition's specification 
 * Output:  Return - 0 if no error, otherwise an error code
 * NOTE: The contents of Spec are overwritten by white space
 */
extern int Update_Part(char *PartitionName, char *Spec);

/*
 * Validate_Node_Specs - Validate the node's specifications as valid, 
 *   if not set state to DOWN, in any case update LastResponse
 * Input: NodeName - Name of the node
 *        CPUs - Number of CPUs measured
 *        RealMemory - MegaBytes of RealMemory measured
 *        TmpDisk - MegaBytes of TmpDisk measured
 * Output: Returns 0 if no error, ENOENT if no such node, EINVAL if values too low
 */ 
extern int Validate_Node_Specs(char *NodeName, 
	int CPUs, int RealMemory, int TmpDisk);

/* 
 * Write_Buffer - Write the specified line to the specified buffer, 
 *               enlarging the buffer as needed
 * Input: Buffer - Pointer to write buffer, must be allocated by alloc()
 *        Buffer_Offset - Byte offset in Buffer, write location
 *        Buffer_Size - Byte size of Buffer
 *        Line - Pointer to data to be writen
 * Output: Buffer - Value is written here, buffer may be relocated by realloc()
 *         Buffer_Offset - Incremented by Value_Size
 *         Returns 0 if no error or errno otherwise 
 */
extern int Write_Buffer(char **Buffer, int *Buffer_Offset, int *Buffer_Size, char *Line);

#endif /* !_HAVE_SLURM_H */
