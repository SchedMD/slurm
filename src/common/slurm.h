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

#include <time.h>
#include "list.h"

#define MAX_NAME_LEN 32		/* Maximum length of partition or node name */

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

char *ControlMachine;		/* Name of computer acting as SLURM controller */
char *BackupController;		/* Name of computer acting as SLURM backup controller */

/* NOTE: Change JOB_STRUCT_VERSION value whenever the contents of "struct Job_Record" change */
#define JOB_STRUCT_VERSION 1
struct Job_Record {
    int Job_Id;
    int User_Id;
    int MaxTime;		/* -1 if unlimited */
};

/* NOTE: Change CONFIG_STRUCT_VERSION value whenever the contents of "struct Config_Record" change */
#define CONFIG_STRUCT_VERSION 1
struct Config_Record {
    int CPUs;			/* Count of CPUs running on the node */
    int RealMemory;		/* Megabytes of real memory on the node */
    int TmpDisk;		/* Megabytes of total storage in TMP_FS file system */
    char *Feature;		/* Arbitrary list of features associated with a node */
    char *Nodes;		/* Names of nodes in partition configuration record */
    char *NodeBitMap;		/* Bitmap of nodes in configuration record */
};
List Config_List;		/* List of Config_Record entries */

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
char *Node_State_String[] = {"UNKNOWN", "IDLE", "STAGE_IN", "BUSY", "STAGE_OUT", "DOWN", "DRAINED", "DRAINING", "END"};

/* NOTE: Change NODE_STRUCT_VERSION value whenever the contents of "struct Node_Record" change */
#define NODE_STRUCT_VERSION 1
time_t Last_Node_Update;		/* Time of last update to Node Records */
struct Node_Record {
    char Name[MAX_NAME_LEN];		/* Name of the node. A NULL name indicates defunct node */
    enum Node_State NodeState;		/* State of the node */
    time_t LastResponse;		/* Last response from the node */
    struct Config_Record *Config_Ptr;	/* Configuration specification for this node */
};
struct Node_Record *Node_Record_Table_Ptr;	/* Location of the node records */
int	Node_Record_Count;		/* Count of records in the Node Record Table */
int	*Hash_Table;			/* Table of hashed indicies into Node_Record */
char 	*Up_NodeBitMap;			/* Bitmap of nodes are UP */
char 	*Idle_NodeBitMap;		/* Bitmap of nodes are IDLE */

/* NOTE: Change PART_STRUCT_VERSION value whenever the contents of "struct Node_Record" change */
#define PART_STRUCT_VERSION 1
struct Part_Record {
    char Name[MAX_NAME_LEN];	/* Name of the partition */
    int MaxTime;		/* -1 if unlimited */
    int MaxNodes;		/* -1 if unlimited */
    char *Nodes;		/* Names of nodes in partition */
    char *AllowGroups;		/* NULL indicates ALL */
    unsigned Interactive:1;	/* 1 if interactive (non-queued) jobs may run here */
    unsigned StateUp:1;		/* 1 if state is UP */
    char *NodeBitMap;		/* Bitmap of nodes in partition */
};
List Part_List;			/* List of Part_Record entries */
char Default_Part_Name[MAX_NAME_LEN];	/* Name of default partition */
struct Part_Record *Default_Part_Loc;	/* Location of default partition */

/* 
 * Create_Node_Record - Create a node record
 * Input: Location to store error value in
 * Output: Error_Code is set to zero if no error, errno otherwise
 *         Returns a pointer to the record or NULL if error
 * NOTE The record's values are initialized to those of Default_Record
 */
struct Node_Record *Create_Node_Record(int *Error_Code);

/* 
 * Create_Part_Record - Create a partition record
 * Input: Location to store error value in
 * Output: Error_Code is set to zero if no error, errno otherwise
 *         Returns a pointer to the record or NULL if error
 * NOTE The record's values are initialized to those of Default_Part
 */
struct Part_Record *Create_Part_Record(int *Error_Code);

/* 
 * Find_Node_Record - Find a record for node with specified name,
 * Input: name - name of the desired node 
 * Output: return pointer to node record or NULL if not found
 */
struct Node_Record *Find_Node_Record(char *name);

/* 
 * Find_Part_Record - Find a record for partition with specified name,
 * Input: name - name of the desired partition 
 * Output: return pointer to node partition or NULL if not found
 */
struct Part_Record *Find_Part_Record(char *name);

/* 
 * Init_SLURM_Conf - Initialize the SLURM configuration values and data structures. 
 * This should be called once at startup of SLURM daemons.
 */
int Init_SLURM_Conf();

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
int Parse_Node_Name(char *NodeName, char **Format, int *Start_Inx, int *End_Inx, int *Count_Inx);

/*
 * Read_SLURM_Conf - Load the SLURM configuration from the specified file 
 * Call Init_SLURM_Conf before ever calling Read_SLURM_Conf.  
 * Read_SLURM_Conf can be called more than once if so desired.
 * Input: File_Name - Name of the file containing SLURM configuration information
 * Output: return - 0 if no error, otherwise an error code
 */
int Read_SLURM_Conf (char *File_Name);

