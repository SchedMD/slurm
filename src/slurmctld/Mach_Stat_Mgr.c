/* 
 * Mach_Stat_Mgr.c - Manage the node specification information of SLURM
 * See slurm.h for documentation on external functions and data structures
 *
 * Author: Moe Jette, jette@llnl.gov
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

#include "list.h"
#include "slurm.h"

#define BUF_SIZE 1024
#define DEBUG_MODULE 0
#define DEBUG_SYSTEM 1
#define SEPCHARS " \n\t"
#define HASH_BASE 10
struct Node_Record **Hash_Table = NULL;
char *Node_State_String[] = {"UNKNOWN", "IDLE", "BUSY", "DOWN", "DRAINED", "DRAINING", "END"};

int	Delete_Node_Record(char *name);
void	Dump_Hash();
int	Hash_Index(char *name);
int 	Parse_Node_Spec(char *Specification, char *My_Name, char *My_OS, 
	int *My_CPUs, int *Set_CPUs, float *My_Speed, int *Set_Speed,
	int *My_RealMemory, int *Set_RealMemory, int *My_VirtualMemory, int *Set_VirtualMemory, 
	long *My_TmpDisk, int *Set_TmpDisk, unsigned int *My_Partition, int *Set_Partition, 
	enum Node_State *NodeState, int *Set_State, time_t *My_LastResponse, int *Set_LastResponse);
void	Partition_String_To_Value (char *partition, unsigned int *Partition_Value, int *Error_Code);
void 	Partition_Value_To_String(unsigned int partition, char *Partition_String, int Partition_String_size, char *node_name);
void	Rehash();
int	Tally_Node_CPUs(char *Node_List);

List   Node_Record_List = NULL;		/* List of Node_Records */

#if DEBUG_MODULE
/* main is used here for testing purposes only */
main(int argc, char * argv[]) {
    int Error_Code;
    char Out_Line[BUF_SIZE];

    if (argc < 4) {
	printf("Usage: %s <in_file> <text_file> <raw_file>\n", argv[0]);
	exit(0);
    } /* if */

    Error_Code = Read_Node_Spec_Conf(argv[1]);
    if (Error_Code != 0) {
	printf("Error %d from Read_Node_Spec_Conf\n", Error_Code);
	exit(1);
    } /* if */
    if (Hash_Table != NULL) Dump_Hash();

    /* Update existing record */
    Error_Code = Update_Node_Spec_Conf("Name=mx01 CPUs=3 TmpDisk=12345");
    if (Error_Code != 0) printf("Error %d from Update_Node_Spec_Conf\n", Error_Code);
    /* Create a new record */
    Error_Code = Update_Node_Spec_Conf("Name=mx03 CPUs=4 TmpDisk=16384 Partition=9 State=IDLE LastResponse=123");
    if (Error_Code != 0) printf("Error %d from Update_Node_Spec_Conf\n", Error_Code);

    Error_Code = Write_Node_Spec_Conf(argv[2], 1);
    if (Error_Code != 0) printf("Error %d from Write_Node_Spec_Conf\n", Error_Code);

    Error_Code = Dump_Node_Records(argv[3]);
    if (Error_Code != 0) printf("Error %d from Dump_Node_Records\n", Error_Code);

    Error_Code = Validate_Node_Spec("Name=mx03 CPUs=4 TmpDisk=22222");
    if (Error_Code != 0) printf("Error %d from Validate_Node_Spec\n", Error_Code);
    Error_Code = Validate_Node_Spec("Name=mx03 CPUs=3");
    if (Error_Code == 0) printf("Error %d from Validate_Node_Spec\n", Error_Code);

    Error_Code = Show_Node_Record("mx03", Out_Line, BUF_SIZE);
    if (Error_Code != 0) printf("Error %d from Show_Node_Record\n", Error_Code);
    if (Error_Code == 0) printf("Show_Node_Record: %s\n", Out_Line);

    Error_Code = Tally_Node_CPUs("mx01,mx03 junk...");
    if (Error_Code != 7) printf("Tally_Node_CPUs returned %d instead of 7\n");

    exit(0);
} /* main */
#endif

/* 
 * Delete_Node_Record - Find a record for node with specified name and delete it
 * Input: name - name of the node
 * Output: returns 0 on no error, otherwise errno
 */
int Delete_Node_Record(char *name) {
    int Error_Code;
    ListIterator Node_Record_Iterator;		/* For iterating through Node_Record_List */
    struct Node_Record *Node_Record_Point;	/* Pointer to Node_Record */

    Node_Record_Iterator = list_iterator_create(Node_Record_List);
    if (Node_Record_Iterator == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Delete_Node_Record: list_iterator_create unable to allocate memory\n");
#else
	syslog(LOG_ERR, "Delete_Node_Record: list_iterator_create unable to allocate memory\n");
#endif
	return ENOMEM;
    }

    Error_Code = ENOENT;
    while (Node_Record_Point = (struct Node_Record *)list_next(Node_Record_Iterator)) {
	if (strcmp(Node_Record_Point->Name, name) == 0) {
	    (void) list_remove(Node_Record_Iterator);
	    Node_Count--;
	    free(Node_Record_Point);
	    Error_Code = 0;
	    break;
	} /* if */
    } /* while */

    list_iterator_destroy(Node_Record_Iterator);
    Rehash();
    return Error_Code;
} /* Delete_Node_Record */


/* Print the Hash_Table contents */
void Dump_Hash() {
    int i;

    if (Hash_Table ==  NULL) return;
    for (i=0; i<Node_Count; i++) {
	if (Hash_Table[i] == NULL) continue;
	printf("Hash:%d:%s\n", i, Hash_Table[i]->Name);
    } /* for */
} /* Dump_Hash */


/*
 * Dump_Node_Records - Raw dump of node specification information into the specified file 
 * Input: File_Name - Name of the file into which the node specification is to be written
 * Output: return - 0 if no error, otherwise an error code
 */
int Dump_Node_Records (char *File_Name) {
    FILE *Node_Spec_File;	/* Pointer to output data file */
    int Error_Code;		/* Error returns from system functions */
    int i;			/* Counter */
    ListIterator Node_Record_Iterator;		/* For iterating through Node_Record_List */
    struct Node_Record *Node_Record_Point;	/* Pointer to Node_Record */

    /* Initialization */
    Error_Code = 0;
    Node_Spec_File = fopen(File_Name, "w");
    if (Node_Spec_File == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Dump_Node_Records error %d opening file %s\n", errno, File_Name);
#else
	syslog(LOG_ERR, "Dump_Node_Records error %d opening file %s\n", errno, File_Name);
#endif
	return errno;
    } /* if */

    Node_Record_Iterator = list_iterator_create(Node_Record_List);
    if (Node_Record_Iterator == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Dump_Node_Records: list_iterator_create unable to allocate memory\n");
#else
	syslog(LOG_ERR, "Dump_Node_Records: list_iterator_create unable to allocate memory\n");
#endif
	return ENOMEM;
    } /* if */

    i = NODE_STRUCT_VERSION;
    if (fwrite((void *)&i, sizeof(i), 1, Node_Spec_File) < 1) {
	Error_Code = ferror(Node_Spec_File);
#if DEBUG_SYSTEM
	fprintf(stderr, "Dump_Node_Records error %d writing to file %s\n", Error_Code, File_Name);
#else
	syslog(LOG_ERR, "Dump_Node_Records error %d writing to file %s\n", Error_Code, File_Name);
#endif
    } /* if */

    /* Process the data file */
    while (Node_Record_Point = (struct Node_Record *)list_next(Node_Record_Iterator)) {
	if (fwrite((void *)Node_Record_Point, sizeof (struct Node_Record), 1, Node_Spec_File) < 1) {
	    if (Error_Code == 0) Error_Code = ferror(Node_Spec_File);
#if DEBUG_SYSTEM
	    fprintf(stderr, "Dump_Node_Records error %d writing to file %s\n", Error_Code, File_Name);
#else
	    syslog(LOG_ERR, "Dump_Node_Records error %d writing to file %s\n", Error_Code, File_Name);
#endif
	} /* if */
    } /* while */

    /* Termination */
    if (fclose(Node_Spec_File) != 0) {
	if (Error_Code == 0) Error_Code = errno;
#if DEBUG_SYSTEM
	fprintf(stderr, "Dump_Node_Records error %d closing file %s\n", errno, File_Name);
#else
	syslog(LOG_NOTICE, "Dump_Node_Records error %d closing file %s\n", errno, File_Name);
#endif
    } /* if */
    list_iterator_destroy(Node_Record_Iterator);
    return Error_Code;
} /* Dump_Node_Records */


/* 
 * Find_Node_Record - Find a record for node with specified name,
 * Input: name - name of the desired node 
 * Output: return pointer to node record or NULL if not found
 */
struct Node_Record *Find_Node_Record(char *name) {
    ListIterator Node_Record_Iterator;		/* For iterating through Node_Record_List */
    struct Node_Record *Node_Record_Point;	/* Pointer to Node_Record */
    int i;

    /* Try to find in hash table first */
    if (Hash_Table != NULL) {
	i = Hash_Index(name);
        if (strcmp(Hash_Table[i]->Name, name) == 0) return Hash_Table[i];
#if DEBUG_SYSTEM
	fprintf(stderr, "Find_Node_Record: Hash table lookup failure for %s\n", name);
#else
	syslog(LOG_DEBUG, "Find_Node_Record: Hash table lookup failure for %s\n", name)
#endif
    } /* if */

#if DEBUG_SYSTEM
    if (Hash_Table != NULL) {
	printf("Find linear for %s\n", name);
	Dump_Hash();
    } /* if */
#endif

    /* Revert to linear search */
    Node_Record_Iterator = list_iterator_create(Node_Record_List);
    if (Node_Record_Iterator == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Find_Node_Record: list_iterator_create unable to allocate memory\n");
#else
	syslog(LOG_ERR, "Find_Node_Record: list_iterator_create unable to allocate memory\n");
#endif
	return NULL;
    }

    while (Node_Record_Point = (struct Node_Record *)list_next(Node_Record_Iterator)) {
	if (strcmp(Node_Record_Point->Name, name) == 0) break;
    } /* while */

    list_iterator_destroy(Node_Record_Iterator);
    return Node_Record_Point;
} /* Find_Node_Record */


/* 
 * Hash_Index - Return a hash table index for the given node name 
 * This code is optimized for names containing a base-ten suffix (e.g. "lx04")
 * Input: The node's name
 * Output: Return code is the hash table index
 */
int Hash_Index(char *name) {
    int i, inx, tmp;

    if (Node_Count == 0) return 0;		/* Degenerate case */
    inx = 0;

#if ( HASH_BASE == 10 )
    for (i=0; ;i++) { 
	tmp = (int) name[i];
	if (tmp == 0) break;			/* end if string */
	if (tmp < (int)'0') continue;
	if (tmp > (int)'9') continue;		
	inx = (inx * 10) + (tmp - (int)'0');
    } /* for */
#elif ( HASH_BASE == 8 )
    for (i=0; ;i++) { 
	tmp = (int) name[i];
	if (tmp == 0) break;			/* end if string */
	if (tmp < (int)'0') continue;
	if (tmp > (int)'7') continue;		
	inx = (inx * 8) + (tmp - (int)'0');
    } /* for */

#else
    for (i=0; i<5;i++) { 
	tmp = (int) name[i];
	if (tmp == 0) break;					/* end if string */
	if ((tmp >= (int)'0') && (tmp <= (int)'9')) {		/* value 0-9 */
	    tmp -= (int)'0';
	} else if ((tmp >= (int)'a') && (tmp <= (int)'z')) {	/* value 10-35 */
	    tmp -= (int)'a';
	    tmp += 10;
	} else if ((tmp >= (int)'A') && (tmp <= (int)'Z')) {	/* value 10-35 */
	    tmp -= (int)'A';
	    tmp += 10;
	} else {
	    tmp = 36;
	}
	inx = (inx * 37) + tmp;
    } /* for */
 #endif

    inx = inx % Node_Count;
    return inx;
} /* Hash_Index */


/* 
 * Parse_Node_Spec - Parse the node input specification, return values and set flags
 * Output: 0 if no error, error code otherwise
 */
int Parse_Node_Spec(char *Specification, char *My_Name, char *My_OS, 
	int *My_CPUs, int *Set_CPUs, float *My_Speed, int *Set_Speed,
	int *My_RealMemory, int *Set_RealMemory, int *My_VirtualMemory, int *Set_VirtualMemory, 
	long *My_TmpDisk, int *Set_TmpDisk, unsigned int *My_Partition, int *Set_Partition, 
	enum Node_State *My_NodeState, int *Set_State, time_t *My_LastResponse, int *Set_LastResponse) {
    char *Scratch;
    char *str_ptr1, *str_ptr2, *str_ptr3;
    int Error_Code, i;

    Error_Code         = 0;
    My_Name[0]         = (char)NULL;
    My_OS[0]           = (char)NULL;
    *Set_CPUs          = 0;
    *Set_Speed         = 0;
    *Set_RealMemory    = 0;
    *Set_VirtualMemory = 0;
    *Set_TmpDisk       = 0;
    *Set_Partition     = 0;
    *Set_State         = 0;
    *Set_LastResponse  = 0;

    if (Specification[0] == '#') return 0;
    Scratch = malloc(strlen(Specification)+1);
    if (Scratch == NULL) {
#if DEBUG_SYSTEM
    	fprintf(stderr, "Parse_Node_Spec: unable to allocate memory\n");
#else
    	syslog(LOG_ERR, "Parse_Node_Spec: unable to allocate memory\n");
#endif
	return ENOMEM;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "Name=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+5);
	str_ptr2 = (char *)strtok_r(Scratch, SEPCHARS, &str_ptr3);
	if (strlen(str_ptr2) < MAX_NAME_LEN) 
	    strcpy(My_Name, str_ptr2);
	else {
#if DEBUG_SYSTEM
    	    fprintf(stderr, "Parse_Node_Spec: Node name too long\n");
#else
    	    syslog(LOG_ERR, "Parse_Node_Spec: Node name too long\n");
#endif
	    free(Scratch);
	    return EINVAL;
	} /* else */
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "OS=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+3);
	str_ptr2 = (char *)strtok_r(Scratch, SEPCHARS, &str_ptr3);
	if (strlen(str_ptr2) < MAX_OS_LEN) 
	    strcpy(My_OS, str_ptr2);
	else {
#if DEBUG_SYSTEM
    	    fprintf(stderr, "Parse_Node_Spec: OS name too long, ignored\n");
#else
    	    syslog(LOG_ERR, "Parse_Node_Spec: OS name too long, ignored\n");
#endif
	} /* else */
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "CPUs=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+5);
	str_ptr2 = (char *)strtok_r(Scratch, SEPCHARS, &str_ptr3);
	*My_CPUs = (int) strtol(str_ptr2, (char **)NULL, 10);
	*Set_CPUs = 1;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "Speed=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+6);
	str_ptr2 = (char *)strtok_r(Scratch, SEPCHARS, &str_ptr3);
	*My_Speed = (float) strtod(str_ptr2, (char **)NULL);
	*Set_Speed = 1;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "RealMemory=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+11);
	str_ptr2 = (char *)strtok_r(Scratch, SEPCHARS, &str_ptr3);
	*My_RealMemory = (int) strtol(str_ptr2, (char **)NULL, 10);
	*Set_RealMemory = 1;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "VirtualMemory=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+14);
	str_ptr2 = (char *)strtok_r(Scratch, SEPCHARS, &str_ptr3);
	*My_VirtualMemory = (int) strtol(str_ptr2, (char **)NULL, 10);
	*Set_VirtualMemory = 1;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "TmpDisk=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+8);
	str_ptr2 = (char *)strtok_r(Scratch, SEPCHARS, &str_ptr3);
	*My_TmpDisk = strtol(str_ptr2, (char **)NULL, 10);
	*Set_TmpDisk = 1;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "Partition=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+10);
	str_ptr2 = (char *)strtok_r(Scratch, SEPCHARS, &str_ptr3);
	Partition_String_To_Value(str_ptr2, My_Partition, &Error_Code);
	*Set_Partition = 1;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "State=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+6);
	str_ptr2 = (char *)strtok_r(Scratch, SEPCHARS, &str_ptr3);
	for (i=0; i<= STATE_END; i++) {
	    if (strcmp(Node_State_String[i], "END") == 0) break;
	    if (strcmp(Node_State_String[i], Scratch) == 0) {
		*My_NodeState = i;
		*Set_State = 1;
		break;
	    } /* if */
	} /* for */
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "LastResponse=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+13);
	str_ptr2 = (char *)strtok_r(Scratch, SEPCHARS, &str_ptr3);
	*My_LastResponse = (time_t) strtol(str_ptr2, (char **)NULL, 10);
	*Set_LastResponse = 1;
    } /* if */

    free(Scratch);
    return Error_Code;
} /* Parse_Node_Spec */


/* 
 * Partition_String_To_Value - Convert a partition list string to the equivalent bit mask
 * Input: partition - the partition list, comma separated numbers in the range of 0 to MAX_PARTITION-1
 *        Partition_Value - Pointer to partition bit mask
 *        Error_Code - place in which to place any error code
 */
void Partition_String_To_Value (char *partition, unsigned int *Partition_Value, int *Error_Code) {
    int i, Partition_Num;
    char *Partition_Ptr;
    char *Sep_Ptr;

    *Error_Code = 0;
    *Partition_Value = 0;
    if (partition == NULL) return;
    if (partition[0] == (char)NULL) return;
    Partition_Ptr = partition;
    for (i=0; i<=MAX_PARTITION; i++) {
	Partition_Num = (int)strtol(Partition_Ptr, &Sep_Ptr, 10);
	if ((Partition_Num < 0) || (Partition_Num > MAX_PARTITION)) {
	    *Error_Code = EINVAL;
	    break;
	} else {
	    *Partition_Value |= (1 << Partition_Num);
	    if ((Sep_Ptr[0] == (char)NULL) || (Sep_Ptr[0] == '\n') ||
	        (Sep_Ptr[0] == ' ')        || (Sep_Ptr[0] == '\t')) break;
	    Partition_Ptr = Sep_Ptr + 1;
	} /* else */
    } /* for */
} /* Partition_String_To_Value */


/* 
 * Partition_Value_To_String - Convert a partition list string to the equivalent bit mask
 * Input: partition - the partition bit mask
 *        Partition_String - the partition list, comma separated numbers in the range of 0 to MAX_PARTITION-1
 *        Partition_String_size - size of Partition_String in bytes, prints warning rather than overflow
 *	  node_name - name of the node, used for error messages
 */
void Partition_Value_To_String(unsigned int partition, char *Partition_String, int Partition_String_size, char *node_name) {
    int i;
    int Max_Partitions; 		/* Maximum partition number we are prepared to process */
    char Tmp_String[7];

    Partition_String[0] = (char)NULL;
    Max_Partitions = MAX_PARTITION;
    if (Max_Partitions > 999999) {
#if DEBUG_SYSTEM
    	fprintf(stderr, "Partition_Value_To_String error MAX_PARTITION configured over too large at %d\n", Max_Partitions);
#else
    	syslog(LOG_ERR, "Partition_Value_To_String error MAX_PARTITION configured over too large at %d\n", Max_Partitions);
#endif
	Max_Partitions = 999999;
    } /* if */

    for (i=0; i<Max_Partitions; i++) {
	if ((partition & (1 << i)) == 0) continue;
	sprintf(Tmp_String, "%d", i);
	if ((strlen(Partition_String)+strlen(Tmp_String)+1) >= Partition_String_size) {
#if DEBUG_SYSTEM
    	    fprintf(stderr, "Partition_Value_To_String Partition string overflow for node Name %s\n", node_name);
#else
    	    syslog(LOG_ERR, "Partition_Value_To_String Partition string overflow for node Name %s\n", node_name);
#endif
	} /* if */
	if (Partition_String[0] != (char)NULL) strcat(Partition_String, ",");
	strcat(Partition_String, Tmp_String);
    } /* for */
} /* Partition_Value_To_String */


/*
 * Read_Node_Spec_Conf - Load the node specification information from the specified file 
 * Input: File_Name - Name of the file containing node specification
 * Output: return - 0 if no error, otherwise an error code
 */
int Read_Node_Spec_Conf (char *File_Name) {
    FILE *Node_Spec_File;	/* Pointer to input data file */
    int Error_Code;		/* Error returns from system functions */
    char In_Line[BUF_SIZE];	/* Input line */
    int Line_Num;		/* Line number in input file */
    struct Node_Record *Node_Record_Point;	/* Pointer to Node_Record */
    char My_Name[MAX_NAME_LEN];
    char My_OS[MAX_OS_LEN];
    int My_CPUs;
    float My_Speed;
    int My_RealMemory;
    int My_VirtualMemory;
    long My_TmpDisk;
    unsigned int My_Partition;
    enum Node_State My_NodeState;
    time_t My_LastResponse;

    struct Node_Record  Default_Record;	/* Default values for node record */
    struct Node_Record  Node_Record_Read;	/* Node record being read */

    int Set_CPUs, Set_Speed, Set_RealMemory, Set_VirtualMemory, Set_TmpDisk;
    int Set_Partition, Set_State, Set_LastResponse;

    /* Initialization */
    Error_Code = 0;
    Node_Spec_File = fopen(File_Name, "r");
    if (Node_Spec_File == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Read_Node_Spec_Conf: error %d opening file %s\n", errno, File_Name);
#else
	syslog(LOG_ALERT, "Read_Node_Spec_Conf: error %d opening file %s\n", errno, File_Name);
#endif
	return errno;
    } /* if */
    strcpy(Default_Record.Name, "DEFAULT");
    strcpy(Default_Record.OS, "UNKNOWN");
    Default_Record.CPUs = 1;
    Default_Record.Speed = 1.0;
    Default_Record.RealMemory = 0;
    Default_Record.VirtualMemory = 0;
    Default_Record.TmpDisk = 0L;
    Default_Record.Partition = 1;
    Default_Record.NodeState= STATE_UNKNOWN;
    Default_Record.LastResponse = 0;
    if (Node_Record_List == NULL) {
	Node_Record_List = list_create(NULL);
	if (Node_Record_List == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Read_Node_Spec_Conf: list_create can not allocate memory\n");
#else
	    syslog(LOG_ERR, "Read_Node_Spec_Conf: list_create can not allocate memory\n");
#endif
	    return ENOMEM;
	} /* if */
    } /* if */

    /* Process the data file */
    Line_Num = 0;
    while (fgets(In_Line, BUF_SIZE, Node_Spec_File) != NULL) {
	Line_Num++;
	if (strlen(In_Line) >= (BUF_SIZE-1)) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Read_Node_Spec_Conf: line %d, of input file %s too long\n", 
		Line_Num, File_Name);
#else
	    syslog(LOG_ALERT, "Read_Node_Spec_Conf: line %d, of input file %s too long\n", 
		Line_Num, File_Name);
#endif
	    Error_Code = E2BIG;
	    break;
	} /* if */
	if (In_Line[0] == '#') continue;
	Error_Code = Parse_Node_Spec(In_Line, My_Name, My_OS, 
	    &My_CPUs, &Set_CPUs, &My_Speed, &Set_Speed,
	    &My_RealMemory, &Set_RealMemory, &My_VirtualMemory, &Set_VirtualMemory, 
	    &My_TmpDisk, &Set_TmpDisk, &My_Partition, &Set_Partition, &My_NodeState, &Set_State,
	    &My_LastResponse, &Set_LastResponse);
	if (Error_Code != 0) break;
	if (strlen(My_Name) == 0) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Read_Node_Spec_Conf: line %d, of input file %s contains no Name\n", 
		Line_Num, File_Name);
#else
	    syslog(LOG_ALERT, "Read_Node_Spec_Conf: line %d, of input file %s contains no Name\n", 
		Line_Num, File_Name);
#endif
	    Error_Code = EINVAL;
	    break;
	} /* if */

	if (strcmp("DEFAULT", My_Name) == 0) {
	    if (strlen(My_OS) != 0)     strcpy(Default_Record.OS, My_OS);
	    if (Set_CPUs != 0)          Default_Record.CPUs=My_CPUs;
	    if (Set_Speed != 0)         Default_Record.Speed=My_Speed;
	    if (Set_RealMemory != 0)    Default_Record.RealMemory=My_RealMemory;
	    if (Set_VirtualMemory != 0) Default_Record.VirtualMemory=My_VirtualMemory;
	    if (Set_TmpDisk != 0)       Default_Record.TmpDisk=My_TmpDisk;
	    if (Set_Partition != 0)     Default_Record.Partition=My_Partition;
	    if (Set_State != 0)         Default_Record.NodeState=My_NodeState;
	} else {
	    Node_Record_Point = Find_Node_Record(Node_Record_Read.Name);
	    if (Node_Record_Point == NULL) {
		Node_Record_Point = (struct Node_Record *)malloc(sizeof(struct Node_Record));
		if (Node_Record_Point == NULL) {
#if DEBUG_SYSTEM
		    fprintf(stderr, "Read_Node_Spec_Conf: malloc failure\n");
#else
		    syslog(LOG_ALERT, "Read_Node_Spec_Conf: malloc failure\n");
#endif
		    Error_Code =  errno;
		    break;
		} /* if */
		memset(Node_Record_Point, 0, (size_t)sizeof(struct Node_Record));
		if (list_append(Node_Record_List, (void *)Node_Record_Point) == NULL) {
#if DEBUG_SYSTEM
		    fprintf(stderr, "Read_Node_Spec_Conf: list_append can not allocate memory\n");
#else
		    syslog(LOG_ALERT, "Read_Node_Spec_Conf: list_append can not allocate memory\n");
#endif
		    Error_Code =  errno;
		    break;
		} /* if */
		Node_Count++;
		strcpy(Node_Record_Point->Name, My_Name);
		strcpy(Node_Record_Point->OS, Default_Record.OS);
		Node_Record_Point->CPUs          = Default_Record.CPUs;
		Node_Record_Point->Speed         = Default_Record.Speed;
		Node_Record_Point->RealMemory    = Default_Record.RealMemory;
		Node_Record_Point->VirtualMemory = Default_Record.VirtualMemory;
		Node_Record_Point->TmpDisk       = Default_Record.TmpDisk;
		Node_Record_Point->Partition     = Default_Record.Partition;
		Node_Record_Point->NodeState     = Default_Record.NodeState;
	    } else {
#if DEBUG_SYSTEM
		fprintf(stderr, "Read_Node_Spec_Conf: duplicate data for %s, using latest information\n", 
		    Node_Record_Read.Name);
#else
		syslog(LOG_NOTICE, "Read_Node_Spec_Conf: duplicate data for %s, using latest information\n", 
		    Node_Record_Read.Name);
#endif
	    } /* else */
	    if (strlen(My_OS) != 0)     strcpy(Node_Record_Point->OS, My_OS);
	    if (Set_CPUs != 0)          Node_Record_Point->CPUs=My_CPUs;
	    if (Set_Speed != 0)         Node_Record_Point->Speed=My_Speed;
	    if (Set_RealMemory != 0)    Node_Record_Point->RealMemory=My_RealMemory;
	    if (Set_VirtualMemory != 0) Node_Record_Point->VirtualMemory=My_VirtualMemory;
	    if (Set_TmpDisk != 0)       Node_Record_Point->TmpDisk=My_TmpDisk;
	    if (Set_Partition != 0)     Node_Record_Point->Partition=My_Partition;
	    if (Set_State != 0)         Node_Record_Point->NodeState=My_NodeState;
	} /* else */
    } /* while */

    /* Termination */
    if (fclose(Node_Spec_File) != 0) {
	if (Error_Code == 0) Error_Code = errno;
#if DEBUG_SYSTEM
	fprintf(stderr, "Read_Node_Spec_Conf error %d closing file %s\n", errno, File_Name);
#else
	syslog(LOG_NOTICE, "Read_Node_Spec_Conf error %d closing file %s\n", errno, File_Name);
#endif
    } /* if */
    Rehash();
    return Error_Code;
} /* Read_Node_Spec_Conf */


/* 
 * Rehash - Build a hash table of the Node_Record entries. This is a large hash table 
 * to permit the immediate finding of a record based only upon its name without regards 
 * to the number. There should be no need for a search. The algorithm is optimized for 
 * node names with a base-ten sequence number suffix. If you have a large cluster and 
 * use a different naming convention, this function and/or the Hash_Index function 
 * should be re-written.
 */
void Rehash() {
    ListIterator Node_Record_Iterator;		/* For iterating through Node_Record_List */
    struct Node_Record *Node_Record_Point;	/* Pointer to Node_Record */
    int i;

    if (Hash_Table ==  NULL)
	Hash_Table = malloc(sizeof(struct Node_Record *) * Node_Count);
    else
	Hash_Table = realloc(Hash_Table, (sizeof(struct Node_Record *) * Node_Count));

    if (Hash_Table == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Rehash: list_append can not allocate memory\n");
#else
	syslog(LOG_ERR, "Rehash: list_append can not allocate memory\n");
#endif
	return;
    } /* if */
    bzero(Hash_Table, (sizeof(struct Node_Record *) * Node_Count));

    Node_Record_Iterator = list_iterator_create(Node_Record_List);
    if (Node_Record_Iterator == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Rehash: list_iterator_create unable to allocate memory\n");
#else
	syslog(LOG_ERR, "Rehash: list_iterator_create unable to allocate memory\n");
#endif
	return;
    }

    while (Node_Record_Point = (struct Node_Record *)list_next(Node_Record_Iterator)) {
	i = Hash_Index(Node_Record_Point->Name);
	Hash_Table[i] = Node_Record_Point;
    } /* while */

    list_iterator_destroy(Node_Record_Iterator);
} /* Rehash */


/*
 * Show_Node_Record - Dump the record for the specified node
 * Input: Node_Name - Name of the node for which data is requested
 *        Node_Record - Location into which the information is written
 *        Buf_Size - Size of Node_Record in bytes
 * Output: Node_Record is filled in
 *         return - 0 if no error, otherwise errno
 */
    int Show_Node_Record (char *Node_Name, char *Node_Record, int Buf_Size) {
    struct Node_Record *Node_Record_Point;
    char Out_Partition[MAX_PARTITION*3], Out_Time[20], Out_Line[BUF_SIZE];
    struct tm *Node_Time;

    Node_Record_Point = Find_Node_Record(Node_Name);
    if (Node_Record_Point == NULL) return ENOENT;
    Partition_Value_To_String(Node_Record_Point->Partition, Out_Partition, sizeof(Out_Partition), Node_Record_Point->Name);
/* Alternate, human readable, formatting shown below and commented out */
/*    Node_Time = localtime(&Node_Record_Point->LastResponse); */
/*    strftime(Out_Time, sizeof(Out_Time), "%a%d%b@%H:%M:%S", Node_Time); */
    sprintf(Out_Time, "%ld", Node_Record_Point->LastResponse);
    if (sprintf(Out_Line, 
	  "Name=%s OS=%s CPUs=%d Speed=%f RealMemory=%d VirtualMemory=%d TmpDisk=%ld Partition=%s State=%s LastResponse=%s",
	  Node_Record_Point->Name, Node_Record_Point->OS, Node_Record_Point->CPUs, 
	  Node_Record_Point->Speed, Node_Record_Point->RealMemory, 
	  Node_Record_Point->VirtualMemory, Node_Record_Point->TmpDisk, Out_Partition,
	  Node_State_String[Node_Record_Point->NodeState], Out_Time) < 1) {
	return EINVAL;
    } /* if */
    if (strlen(Out_Line) >= Buf_Size) return E2BIG;
    strcpy(Node_Record, Out_Line);
    return 0;
} /* Show_Node_Record */

/*
 * Tally_Node_CPUs - Return the count of CPUs in the list provided
 * Input: comma separated list of nodes
 * Output: Count of CPUs in the node list provided
 */
int Tally_Node_CPUs(char *Node_List) {
    ListIterator Node_Record_Iterator;		/* For iterating through Node_Record_List */
    struct Node_Record *Node_Record_Point;	/* Pointer to Node_Record */
    int CPU_Count, i, j, str_size;
    char **str_ptr;

    Node_Record_Iterator = list_iterator_create(Node_Record_List);
    if (Node_Record_Iterator == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Tally_Node_CPUs: list_iterator_create unable to allocate memory\n");
#else
	syslog(LOG_ERR, "Tally_Node_CPUs: list_iterator_create unable to allocate memory\n");
#endif
	return 0;
    }

    str_ptr = malloc(strlen(Node_List)*sizeof(char *));
    if (str_ptr == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Tally_Node_CPUs: unable to allocate memory\n");
#else
	syslog(LOG_ERR, "Tally_Node_CPUs: unable to allocate memory\n");
#endif
	return 0;
    } /* if */

    str_ptr[0] = Node_List;
    j = 1;
    for (i=0; i<=strlen(Node_List); i++) {
	if (Node_List[i] == ',') {
	    str_ptr[j++] = &Node_List[i+1];
	} else if ((Node_List[i] == ' ') || (Node_List[i] == '\t') || (Node_List[i] == '\n')) {
	    str_ptr[j++] = &Node_List[i+1];  /* Used just for computing size */
	    str_ptr[j] = (char *)NULL;
	    break;
	} /* if */
    } /* for */

    CPU_Count = 0;
    while (Node_Record_Point = (struct Node_Record *)list_next(Node_Record_Iterator)) {
	/* Look for this node name, Node_Record_Point->Name, in Node_List */
	str_size = strlen(Node_Record_Point->Name);
	for (i=0; i<=strlen(Node_List); i++) {
	    if (str_ptr[i] == (char *)NULL) break;
	    if ((str_ptr[i+1] != (char *)NULL) && 
	        ((int)(str_ptr[i+1] - str_ptr[i] - 1) != str_size)) continue;
	    if (strncmp(str_ptr[i], Node_Record_Point->Name, str_size) != 0) continue;
	    CPU_Count += Node_Record_Point->CPUs;
	} /* for */
    } /* while */

    list_iterator_destroy(Node_Record_Iterator);
    free(str_ptr);
    return CPU_Count;
} /* Tally_Node_CPUs */


/*
 * Update_Node_Spec_Conf - Update the configuration for the given node, 
 *	create record as needed 
 *	NOTE: To delete a record, specify CPUs=0 in the configuration
 * Input: Specification - Standard configuration file input line
 * Output: return - 0 if no error, otherwise errno
 */
int Update_Node_Spec_Conf (char *Specification) {
    struct Node_Record *Node_Record_Point;	/* Pointer to Node_Record */
    char My_Name[MAX_NAME_LEN];
    char My_OS[MAX_OS_LEN];
    int My_CPUs;
    float My_Speed;
    int My_RealMemory;
    int My_VirtualMemory;
    long My_TmpDisk;
    unsigned int My_Partition;
    enum Node_State My_State;
    time_t My_LastResponse;

    int Set_CPUs, Set_Speed, Set_RealMemory, Set_VirtualMemory, Set_TmpDisk;
    int Set_Partition, Set_State, Set_LastResponse;
    int Error_Code;

    Error_Code = Parse_Node_Spec(Specification, My_Name, My_OS, 
	&My_CPUs, &Set_CPUs, &My_Speed, &Set_Speed,
	&My_RealMemory, &Set_RealMemory, &My_VirtualMemory, &Set_VirtualMemory, 
	&My_TmpDisk, &Set_TmpDisk, &My_Partition, &Set_Partition, &My_State, &Set_State, 
	&My_LastResponse, &Set_LastResponse);
    if (Error_Code != 0) return EINVAL;

    if (strlen(My_Name) == 0) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Update_Node_Spec_Conf invalid input: %s\n", Specification);
#else
	syslog(LOG_ERR, "Update_Node_Spec_Conf invalid input: %s\n", Specification);
#endif
	return EINVAL;
    } /* if */

    Node_Record_Point = Find_Node_Record(My_Name);
    if (Node_Record_Point == NULL) {		/* Create new record as needed */
	Node_Record_Point = (struct Node_Record *)malloc(sizeof(struct Node_Record));
	if (Node_Record_Point == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Update_Node_Spec_Conf malloc failure\n");
#else
	    syslog(LOG_ERR, "Update_Node_Spec_Conf malloc failure\n");
#endif
	    return errno;
	} /* if */
	memset(Node_Record_Point, 0, (size_t)sizeof(struct Node_Record));
	if (list_append(Node_Record_List, (void *)Node_Record_Point) == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Update_Node_Spec_Conf: list_append can not allocate memory\n");
#else
	    syslog(LOG_ERR, "Update_Node_Spec_Conf: list_append can not allocate memory\n");
#endif
	    return errno;
	} /* if */

	/* Set defaults */
	Node_Count++;
	strcpy(Node_Record_Point->Name, My_Name);
	strcpy(Node_Record_Point->OS, "UNKNOWN");
	Node_Record_Point->CPUs = 1;
	Node_Record_Point->Speed = 1.0;
	Node_Record_Point->RealMemory = 0;
	Node_Record_Point->VirtualMemory = 0;
	Node_Record_Point->TmpDisk = 0L;
	Node_Record_Point->Partition = 1;
	Node_Record_Point->NodeState = STATE_UNKNOWN;
	Node_Record_Point->LastResponse = 0;
	Rehash();
    } /* if */

    
    if ((Set_CPUs != 0) && (My_CPUs == 0)) {	/* Delete record */
	return Delete_Node_Record(My_Name);
	return 0;
    } /* if */

    strcpy(Node_Record_Point->Name, My_Name);
    if (strlen(My_OS) != 0)     strcpy(Node_Record_Point->OS, My_OS);
    if (Set_CPUs != 0)          Node_Record_Point->CPUs=My_CPUs;
    if (Set_Speed != 0)         Node_Record_Point->Speed=My_Speed;
    if (Set_RealMemory != 0)    Node_Record_Point->RealMemory=My_RealMemory;
    if (Set_VirtualMemory != 0) Node_Record_Point->VirtualMemory=My_VirtualMemory;
    if (Set_TmpDisk != 0)       Node_Record_Point->TmpDisk=My_TmpDisk;
    if (Set_Partition != 0)     Node_Record_Point->Partition=My_Partition;
    if (Set_State != 0)         Node_Record_Point->NodeState=My_State;
    if (Set_LastResponse != 0)  Node_Record_Point->LastResponse=My_LastResponse;

    return 0;
} /* Update_Node_Spec_Conf */


/* 
 * Validate_Node_Spec - Determine if the supplied node specification satisfies 
 *	the node record specification (all values at least as high). Note we 
 *	ignore partition and the OS level strings are just run through strcmp
 * Output: Returns 0 if satisfactory, errno otherwise
 */
int Validate_Node_Spec (char *Specification) { 
    int Error_Code;
    struct Node_Record *Node_Record_Point;
    char My_Name[MAX_NAME_LEN];
    char My_OS[MAX_OS_LEN];
    int My_CPUs;
    float My_Speed;
    int My_RealMemory;
    int My_VirtualMemory;
    long My_TmpDisk;
    unsigned My_Partition;
    enum Node_State My_NodeState;
    time_t My_LastResponse;
    int Set_CPUs, Set_Speed, Set_RealMemory, Set_VirtualMemory, Set_TmpDisk;
    int Set_Partition, Set_State, Set_LastResponse;

    Error_Code = Parse_Node_Spec(Specification, My_Name, My_OS, 
	&My_CPUs, &Set_CPUs, &My_Speed, &Set_Speed,
	&My_RealMemory, &Set_RealMemory, &My_VirtualMemory, &Set_VirtualMemory, 
	&My_TmpDisk, &Set_TmpDisk, &My_Partition, &Set_Partition, &My_NodeState, &Set_State,
	&My_LastResponse, &Set_LastResponse);
    if (Error_Code != 0) return Error_Code;
    if (My_Name[0] == (char)NULL) return EINVAL;

    Node_Record_Point = Find_Node_Record(My_Name);
    if (Node_Record_Point == NULL) return ENOENT;
    if ((strlen(My_OS) != 0) && 
	(strcmp(Node_Record_Point->OS, My_OS) < 0)) return EINVAL;
    if ((Set_CPUs != 0) && 
	(Node_Record_Point->CPUs > My_CPUs)) return EINVAL;
    if ((Set_Speed != 0)&& 
	(Node_Record_Point->Speed > My_Speed)) return EINVAL;
    if ((Set_RealMemory != 0) && 
	(Node_Record_Point->RealMemory > My_RealMemory)) return EINVAL;
    if ((Set_VirtualMemory != 0) && 
	(Node_Record_Point->VirtualMemory > My_VirtualMemory)) return EINVAL;
    if ((Set_TmpDisk != 0) && 
	(Node_Record_Point->TmpDisk > My_TmpDisk)) return EINVAL;
    return 0;
} /* Validate_Node_Spec */


/*
 * Write_Node_Spec_Conf - Dump the node specification information into the specified file 
 * Input: File_Name - Name of the file into which the node specification is to be written
 *        Full_Dump - Full node record dump if equal to zero
 * Output: return - 0 if no error, otherwise an error code
 */
int Write_Node_Spec_Conf (char *File_Name, int Full_Dump) {
    FILE *Node_Spec_File;	/* Pointer to output data file */
    int Error_Code;		/* Error returns from system functions */
    char Out_Line[MAX_PARTITION*4]; /* Temporary output information storage */
    char Out_Buf[BUF_SIZE];	/* Temporary output information storage */
    int i;			/* Counter */
    time_t now;			/* Current time */
    ListIterator Node_Record_Iterator;		/* For iterating through Node_Record_List */
    struct Node_Record *Node_Record_Point;	/* Pointer to Node_Record */

    /* Initialization */
    Error_Code = 0;
    Node_Spec_File = fopen(File_Name, "w");
    if (Node_Spec_File == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Write_Node_Spec_Conf: error %d opening file %s\n", errno, File_Name);
#else
	syslog(LOG_ERR, "Write_Node_Spec_Conf: error %d opening file %s\n", errno, File_Name);
#endif
	return errno;
    } /* if */

    Node_Record_Iterator = list_iterator_create(Node_Record_List);
    if (Node_Record_Iterator == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Write_Node_Spec_Conf: list_iterator_create unable to allocate memory\n");
#else
	syslog(LOG_ERR, "Write_Node_Spec_Conf: list_iterator_create unable to allocate memory\n");
#endif
	return ENOMEM;
    } /* if */

    (void) time(&now);
    if (fprintf(Node_Spec_File, "#\n# Written by SLURM: %s#\n", ctime(&now)) <= 0) {
	Error_Code = errno;
#if DEBUG_SYSTEM
	fprintf(stderr, "Write_Node_Spec_Conf: error %d printing to file %s\n", errno, File_Name);
#else
	syslog(LOG_ERR, "Write_Node_Spec_Conf: error %d printing to file %s\n", errno, File_Name);
#endif
    } /* if */

    /* Process the data file */
    while (Node_Record_Point = (struct Node_Record *)list_next(Node_Record_Iterator)) {
	Partition_Value_To_String(Node_Record_Point->Partition, Out_Line, sizeof(Out_Line), Node_Record_Point->Name);
	if (Full_Dump == 1) {
	    sprintf(Out_Buf, "State=%s LastResponse=%ld\n", 
		Node_State_String[Node_Record_Point->NodeState], Node_Record_Point->LastResponse); 
	} else {
	    strcpy(Out_Buf, "\n"); 
	} /* else */
        if (fprintf(Node_Spec_File, 
	  "Name=%s OS=%s CPUs=%d Speed=%f RealMemory=%d VirtualMemory=%d TmpDisk=%ld Partition=%s %s",
	  Node_Record_Point->Name, Node_Record_Point->OS, Node_Record_Point->CPUs, 
	  Node_Record_Point->Speed, Node_Record_Point->RealMemory, 
	  Node_Record_Point->VirtualMemory, Node_Record_Point->TmpDisk, Out_Line, Out_Buf) <= 0) {
	    if (Error_Code == 0) Error_Code = errno;
#if DEBUG_SYSTEM
	    fprintf(stderr, "Write_Node_Spec_Conf: error %d printing to file %s\n", errno, File_Name);
#else
	    syslog(LOG_ERR, "Write_Node_Spec_Conf: error %d printing to file %s\n", errno, File_Name);
#endif
	} /* if */
    } /* while */

    /* Termination */
    if (fclose(Node_Spec_File) != 0) {
	if (Error_Code == 0) Error_Code = errno;
#if DEBUG_SYSTEM
	fprintf(stderr, "Write_Node_Spec_Conf: error %d closing file %s\n", errno, File_Name);
#else
	syslog(LOG_NOTICE, "Write_Node_Spec_Conf: error %d closing file %s\n", errno, File_Name);
#endif
    } /* if */
    list_iterator_destroy(Node_Record_Iterator);
    return Error_Code;
} /* Write_Node_Spec_Conf */
