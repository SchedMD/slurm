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

#define MAX_NAME_LEN 32
#define MAX_OS_LEN 20
#define MAX_PARTITION 32
#define MAX_PART_LEN 16

/* Last entry must be "END" */
enum Node_State {
	STATE_UNKNOWN, 		/* Node's initial state, unknown */
	STATE_IDLE, 		/* Node idle and available for use */
	STATE_BUSY,		/* Node allocated to a job */
	STATE_DOWN, 		/* Node unavailable */
	STATE_DRAINED, 		/* Node idle and not to be allocated future work */
	STATE_DRAINING,		/* Node in use, but not to be allocated future work */
	STATE_END };		/* LAST ENTRY IN TABLE */

/* NOTE: Change NODE_STRUCT_VERSION value whenever the contents of "struct Node_Record" change */
#define NODE_STRUCT_VERSION 1
struct Node_Record {
    char Name[MAX_NAME_LEN];
    char OS[MAX_OS_LEN];
    int CPUs;
    float Speed;
    int RealMemory;
    int VirtualMemory;
    long TmpDisk;
    unsigned Partition:MAX_PARTITION;	/* Bit Mask */
    enum Node_State NodeState;
    time_t LastResponse;
};

/* NOTE: Change PART_STRUCT_VERSION value whenever the contents of "struct Node_Record" change */
#define PART_STRUCT_VERSION 1
struct Part_Record {
    char Name[MAX_PART_LEN];
    int  Number;
    unsigned RunBatch:1;
    unsigned RunInteractive:1;
    unsigned Available:1;
    int MaxTime;		/* -1 if unlimited */
    int MaxCpus;		/* -1 if unlimited */
    char *AllowUsers;		/* NULL indicates ALL */
    char *DenyUsers;		/* NULL indicates NONE */
};

/* 
 * Dump_Node_Records - Raw dump of NODE_STRUCT_VERSION value and all Node_Record structures 
 * Input: File_Name - Name of the file to be created and written to
 */
int Dump_Node_Records (char *File_Name);

/* 
 * Dump_Part_Records - Raw dump of PART_STRUCT_VERSION value and all Part_Record structures 
 * Input: File_Name - Name of the file to be created and have Part_Record written to 
 *        File_Name_UserList - Name of the file to be created and have AllowUsers 
 *                             and DenyUsers written to 
 */
int Dump_Part_Records (char *File_Name, char *File_Name_UserList);

/* 
 * Find_Valid_Parts - Determine which partitions the job specification can execute on
 * Input: Specification - Standard configuration file input line
 *        Partition - Location into which the bit-map of valid partitions is placed
 * Output: Partition is filled in
 *         Returns 0 if satisfactory, errno otherwise
 */
int Find_Valid_Parts (char *Specification, unsigned *Partition);

/*
 * Read_Node_Spec_Conf - Load the node specification information from the specified file 
 * NOTE: Call this routine only once at daemon startup, otherwise call Update_Node_Spec_Conf
 * Input: File_Name - Name of the file containing node specification
 * Output: return - 0 if no error, otherwise errno
 */
int Read_Node_Spec_Conf (char *File_Name);

/*
 * Read_Part_Spec_Conf - Load the partition specification information from the specified file 
 * NOTE: Call this routine only once at daemon startup, otherwise call Update_Part_Spec_Conf
 * Input: File_Name - Name of the file containing node specification
 * Output: return - 0 if no error, otherwise errno
 */
int Read_Part_Spec_Conf (char *File_Name);

/*
 * Show_Node_Record - Dump the record for the specified node
 * Input: Node_Name - Name of the node for which data is requested
 *        Node_Record - Location into which the information is written
 *        Buf_Size - Size of Node_Record in bytes
 * Output: Node_Record is filled in
 *         return - 0 if no error, otherwise errno
 */
int Show_Node_Record (char *Node_Name, char *Node_Record, int Buf_Size);

/*
 * Show_Part_Record - Dump the record for the specified node
 * Input: Part_Name - Name of the node for which data is requested
 *        Part_Record - Location into which the information is written
 *        Buf_Size - Size of Node_Record in bytes
 * Output: Part_Record is filled in
 *         return - 0 if no error, otherwise errno
 */
int Show_Part_Record (char *Part_Name, char *Part_Record, int Buf_Size);

/*
 * Update_Node_Spec_Conf - Update the configuration for the given node, create record as needed 
 *	NOTE: To delete a record, specify CPUs=0 in the configuration
 * Input: Specification - Standard configuration file input line
 * Output: return - 0 if no error, otherwise errno
 */
int Update_Node_Spec_Conf (char *Specification);

/*
 * Update_Part_Spec_Conf - Update the configuration for the given partition, create record as needed 
 *	NOTE: To delete a record, specify State=DELETE in the configuration
 * Input: Specification - Standard configuration file input line
 * Output: return - 0 if no error, otherwise errno
 */
int Update_Part_Spec_Conf (char *Specification);

/* 
 * Validate_Node_Spec - Determine if the supplied node specification satisfies 
 *	the node record specification (all values at least as high). Note we 
 *	ignore partition and the OS level strings are just run through strcmp
 * Output: Returns 0 if satisfactory, errno otherwise
 */
int Validate_Node_Spec (char *Specification);

/*
 * Will_Job_Run - Determine if the given job specification can be initiated now
 * Input: Job_Spec - Specifications for the job
 * Output: Returns node list, NULL if can not be initiated
 *
 * NOTE: The value returned MUST be freed to avoid memory leak
 */
char *Will_Job_Run(char *Specification);

/*
 * Write_Node_Spec_Conf - Dump the node specification information into the specified file 
 * Input: File_Name - Name of the file containing node specification
 *        Full_Dump - Full node record dump if equal to zero, can be used for restore on daemon failure
 * Output: return - 0 if no error, otherwise an error code
 */
int Write_Node_Spec_Conf (char *File_Name, int Full_Dump);

/*
 * Write_Part_Spec_Conf - Dump the partition specification information into the specified file 
 * Input: File_Name - Name of the file containing node specification
 * Output: return - 0 if no error, otherwise an error code
 */
int Write_Part_Spec_Conf (char *File_Name);
