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

#include <time.h>
#include "list.h"

#define MAX_NAME_LEN 16		/* Maximum length of partition or node name */

#define BACKUP_INTERVAL		60
#define BACKUP_LOCATION		"/usr/local/SLURM/Slurm.state"
#define CONTROL_DAEMON  	"/usr/local/SLURM/Slurmd.Control"
#define CONTROLLER_TIMEOUT 	300
#define EPILOG			""
#define HASH_BASE		10
#define HEARTBEAT_INTERVAL	60
#define INIT_PROGRAM		""
#define MASTER_DAEMON   	"/usr/local/SLURM/Slurmd.Master"
#define PROLOG			""
#define SERVER_DAEMON   	"/usr/local/SLURM/Slurmd.Server"
#define SERVER_TIMEOUT  	300
#define SLURM_CONF		"/etc/SLURM.conf"
#define TMP_FS			"/tmp"

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

/* NOTE: Change CONFIG_STRUCT_VERSION value whenever the contents of 
 * "struct Config_Record" or "struct Node_Record" change with respect to the API structures */
#define CONFIG_STRUCT_VERSION 1
struct Config_Record {
    int CPUs;			/* Count of CPUs running on the node */
    int RealMemory;		/* Megabytes of real memory on the node */
    int TmpDisk;		/* Megabytes of total storage in TMP_FS file system */
    int Weight;			/* Arbitrary priority of node for scheduling work on */
    char *Feature;		/* Arbitrary list of features associated with a node */
    char *Nodes;		/* Names of nodes in partition configuration record */
    unsigned *NodeBitMap;	/* Bitmap of nodes in configuration record */
};
extern List Config_List;		/* List of Config_Record entries */

/* Last entry must be "END" */
enum Node_State {
	STATE_UNKNOWN, 		/* Node's initial state, unknown */
	STATE_IDLE, 		/* Node idle and available for use */
	STATE_STAGE_IN, 	/* Node has been allocated to a job, which has not yet begun execution */
	STATE_BUSY,		/* Node allocated to a job and that job is actively running */
	STATE_STAGE_OUT,	/* Node has been allocated to a job, which has completed execution */
	STATE_DOWN, 		/* Node unavailable */
	STATE_DRAINED, 		/* Node idle and not to be allocated future work */
	STATE_DRAINING,		/* Node in use, but not to be allocated future work */
	STATE_END };		/* LAST ENTRY IN TABLE */
extern char *Node_State_String[];

extern time_t Last_Node_Update;		/* Time of last update to Node Records */
struct Node_Record {
    char Name[MAX_NAME_LEN];		/* Name of the node. A NULL name indicates defunct node */
    enum Node_State NodeState;		/* State of the node */
    time_t LastResponse;		/* Last response from the node */
    struct Config_Record *Config_Ptr;	/* Configuration specification for this node */
};
extern struct Node_Record *Node_Record_Table_Ptr; /* Location of the node records */
extern int	Node_Record_Count;	/* Count of records in the Node Record Table */
extern int	*Hash_Table;		/* Table of hashed indicies into Node_Record */
extern unsigned *Up_NodeBitMap;		/* Bitmap of nodes are UP */
extern unsigned *Idle_NodeBitMap;	/* Bitmap of nodes are IDLE */
extern struct 	Config_Record Default_Config_Record;
extern struct 	Node_Record Default_Node_Record;

/* NOTE: Change PART_STRUCT_VERSION value whenever the contents of "struct Node_Record" 
 * change with respect to the API structures */
#define PART_STRUCT_VERSION 1
extern time_t Last_Part_Update;		/* Time of last update to Part Records */
struct Part_Record {
    char Name[MAX_NAME_LEN];	/* Name of the partition */
    int MaxTime;		/* -1 if unlimited */
    int MaxNodes;		/* -1 if unlimited */
    int TotalNodes;		/* Total number of nodes in the partition */
    int TotalCPUs;		/* Total number of CPUs in the partition */
    unsigned Key:1;		/* 1 if SLURM distributed key is required for use of partition */
    unsigned StateUp:1;		/* 1 if state is UP */
    char *Nodes;		/* Names of nodes in partition */
    char *AllowGroups;		/* NULL indicates ALL */
    unsigned *NodeBitMap;	/* Bitmap of nodes in partition */
};
extern List Part_List;		/* List of Part_Record entries */
extern struct	Part_Record Default_Part;		/* Default configuration values */
extern char Default_Part_Name[MAX_NAME_LEN];	/* Name of default partition */
extern struct Part_Record *Default_Part_Loc;	/* Location of default partition */

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
 * Output: Returns the value 0 or 1
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
 * Output: Error_Code - Set to zero if no error, errno otherwise
 *         Returns a pointer to the record or NULL if error
 * NOTE The record's values are initialized to those of Default_Node_Record
 * NOTE: Allocates memory that should be freed with Delete_Part_Record
 */
extern struct Node_Record *Create_Node_Record(int *Error_Code);

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
 * Read_SLURM_Conf - Load the SLURM configuration from the specified file 
 * Call Init_SLURM_Conf before ever calling Read_SLURM_Conf.  
 * Read_SLURM_Conf can be called more than once if so desired.
 * Input: File_Name - Name of the file containing SLURM configuration information
 * Output: return - 0 if no error, otherwise an error code
 */
extern int Read_SLURM_Conf (char *File_Name);

#endif /* !_HAVE_SLURM_H */
