/* 
 * Controller.c - Main control machine daemon for SLURM
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
#define DEBUG_MODULE 1
#define DEBUG_SYSTEM 1
#define SEPCHARS " \n\t"

int OS_Comp(char *OS_1, char *OS_2);
int Parse_Job_Spec(char *Specification, char *My_Name, char *My_OS, 
	int *My_CPUs, int *Set_CPUs, float *My_Speed, int *Set_Speed, int *My_Contiguous,
	int *My_RealMemory, int *Set_RealMemory, int *My_VirtualMemory, int *Set_VirtualMemory, 
	long *My_TmpDisk, int *Set_TmpDisk, int *My_MaxTime, int *Set_MaxTime,  
	int *My_CpuCount, int *Set_CpuCount, int *My_NodeCount, int *Set_NodeCount);
char *Pick_Best_Nodes(struct Node_Record **Node_List_Pos, int *Node_List_Index, int *Node_List_CPUs,
	int Node_List_Count, int Req_CPU_Count, int Req_Node_Count, int Req_Continguous,
	int *Error_Code);
extern List   Node_Record_List;		/* List of Node_Records */
extern List   Part_Record_List;		/* List of Part_Records */

#if DEBUG_MODULE
/* main is used here for testing purposes only */
main(int argc, char * argv[]) {
    int Error_Code, Line_Num;
    char In_Line[BUF_SIZE];	/* Input line */
    char Node_Name[MAX_NAME_LEN];
    char *Node_List;		/* List of nodes assigned to the job */
    FILE *Job_Spec_File;	/* Pointer to input data file */

    if (argc < 3) {
	printf("Usage: %s <slurm_conf_file> <job_specs_file>\n", argv[0]);
	exit(0);
    } /* if */

    Init_SLURM_Conf();
    Error_Code = Read_SLURM_Conf(argv[1]);
    if (Error_Code != 0) {
	printf("Error %d from Read_SLURM_Conf\n", Error_Code);
	exit(1);
    } /* if */

    Error_Code = gethostname(Node_Name, MAX_NAME_LEN);
    if (Error_Code != 0) {
	fprintf(stderr, "Read_SLURM_Conf: gethostname error %d\n", Error_Code);
    } /* if */
    if ((strcmp(Node_Name, ControlMachine) != 0) && 
        (strcmp(Node_Name, BackupController) != 0)) {
	printf("This machine (%s) is not a valid control machine (%s or %s)\n", 
		Node_Name, ControlMachine, BackupController);
	exit(1);
    } /* if */

    Error_Code = Read_Node_Spec_Conf(NodeSpecConf);
    if (Error_Code != 0) {
	printf("Error %d from Read_Node_Spec_Conf\n", Error_Code);
	exit(1);
    } /* if */

    Error_Code = Read_Part_Spec_Conf(PartitionConf);
    if (Error_Code != 0) {
	printf("Error %d from Read_Part_Spec_Conf", Error_Code);
	exit(1);
    } /* if */

    Job_Spec_File = fopen(argv[2], "r");
    if (Job_Spec_File == NULL) {
	fprintf(stderr, "Read_Node_Spec_Conf: error %d opening file %s\n", errno, argv[2]);
	exit(1);
    } /* if */

    Line_Num = 0;
    while (fgets(In_Line, BUF_SIZE, Job_Spec_File) != NULL) {
	Line_Num++;
	if (strlen(In_Line) >= (BUF_SIZE-1)) {
	    fprintf(stderr, "Controller: line %d, of input file %s too long\n", 
		Line_Num, argv[2]);
	    exit(1);
	} /* if */

	if (In_Line[0] == '#') continue;	/* Skip comment lines in input */
	printf("%s", In_Line);
	Node_List = Will_Job_Run(In_Line, &Error_Code);
	if (Node_List == (char *)NULL) {
	    printf("  Job can not be scheduled at this time, error=%d \n\n", Error_Code);
	} else {
	    printf("  Job scheduled on this nodes:\n  %s\n\n", Node_List);
	    free(Node_List);
	} /* else */
    } /* while */

    exit(0);
} /* main */
#endif

/* 
 * OS_Comp - Compare two OS versions 
 * Output: returns an integer less than, equal to, or greater than zero if  OS_1 is found 
 *         respectively to be less than, equal to, or greater than OS_2
 */
int OS_Comp(char *OS_1, char *OS_2) {
    char Tmp_OS_1[MAX_OS_LEN], Tmp_OS_2[MAX_OS_LEN];
    char *OS1_Ptr1, *OS1_Ptr2;
    char *OS2_Ptr1, *OS2_Ptr2;
    int i;

    if (strcmp(OS_1, OS_2) == 0) return 0;

    strcpy(Tmp_OS_1, OS_1);
    strcpy(Tmp_OS_2, OS_2);

    OS1_Ptr1 = (char *)strtok_r(Tmp_OS_1, ".", &OS1_Ptr2);
    OS2_Ptr1 = (char *)strtok_r(Tmp_OS_2, ".", &OS2_Ptr2);
    while (1) {
	i = strcmp(OS1_Ptr1, OS1_Ptr1);
	if (i != 0) return i;
	OS1_Ptr1 = (char *)strtok_r(NULL, ".", &OS1_Ptr2);
	OS2_Ptr1 = (char *)strtok_r(NULL, ".", &OS2_Ptr2);
    } /* while */

#if DEBUG_SYSTEM
     fprintf(stderr, "OS_Comp: OS compare failure:%s:%s:\n", OS_1, OS_2);
#else
     syslog(LOG_ERR, "OS_Comp: OS compare failure:%s:%s:\n", OS_1, OS_2);
#endif
    return 0; /* Punt */
} /* OS_Comp */


/* 
 * Parse_Job_Spec - Parse the job input specification, return values and set flags
 * Output: 0 if no error, error code otherwise
 */
int Parse_Job_Spec(char *Specification, char *My_Name, char *My_OS, 
	int *My_CPUs, int *Set_CPUs, float *My_Speed, int *Set_Speed, int *My_Contiguous,
	int *My_RealMemory, int *Set_RealMemory, int *My_VirtualMemory, int *Set_VirtualMemory, 
	long *My_TmpDisk, int *Set_TmpDisk, int *My_MaxTime, int *Set_MaxTime, 
	int *My_CpuCount, int *Set_CpuCount, int *My_NodeCount, int *Set_NodeCount) {
    char *Scratch;
    char *str_ptr1, *str_ptr2, *str_ptr3;
    int Error_Code, i;

    Error_Code         = 0;
    My_Name[0]         = (char)NULL;
    My_OS[0]           = (char)NULL;
    *Set_CPUs          = 0;
    *Set_Speed         = 0;
    *My_Contiguous     = 0;
    *Set_RealMemory    = 0;
    *Set_VirtualMemory = 0;
    *Set_TmpDisk       = 0;
    *Set_MaxTime       = 0;
    *Set_CpuCount      = 0;
    *Set_NodeCount     = 0;

    if (Specification[0] == '#') return 0;
    Scratch = malloc(strlen(Specification)+1);
    if (Scratch == NULL) {
#if DEBUG_SYSTEM
    	fprintf(stderr, "Parse_Job_Spec: unable to allocate memory\n");
#else
    	syslog(LOG_ERR, "Parse_Job_Spec: unable to allocate memory\n");
#endif
	return ENOMEM;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "User=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+5);
	str_ptr2 = (char *)strtok_r(Scratch, SEPCHARS, &str_ptr3);
	if (strlen(str_ptr2) < MAX_NAME_LEN) 
	    strcpy(My_Name, str_ptr2);
	else {
#if DEBUG_SYSTEM
    	    fprintf(stderr, "Parse_Job_Spec: User name too long\n");
#else
    	    syslog(LOG_ERR, "Parse_Job_Spec: User name too long\n");
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
    	    fprintf(stderr, "Parse_Job_Spec: OS name too long\n");
#else
    	    syslog(LOG_ERR, "Parse_Job_Spec: OS name too long\n");
#endif
	    free(Scratch);
	    return EINVAL;
	} /* else */
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "Continguous=TRUE");
    if (str_ptr1 != NULL) {
	*My_Contiguous = 1;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "MinCpus=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+8);
	str_ptr2 = (char *)strtok_r(Scratch, SEPCHARS, &str_ptr3);
	*My_CPUs = (int) strtol(str_ptr2, (char **)NULL, 10);
	*Set_CPUs = 1;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "MinSpeed=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+9);
	str_ptr2 = (char *)strtok_r(Scratch, SEPCHARS, &str_ptr3);
	*My_Speed = (float) strtod(str_ptr2, (char **)NULL);
	*Set_Speed = 1;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "MinRealMemory=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+14);
	str_ptr2 = (char *)strtok_r(Scratch, SEPCHARS, &str_ptr3);
	*My_RealMemory = (int) strtol(str_ptr2, (char **)NULL, 10);
	*Set_RealMemory = 1;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "MinVirtualMemory=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+17);
	str_ptr2 = (char *)strtok_r(Scratch, SEPCHARS, &str_ptr3);
	*My_VirtualMemory = (int) strtol(str_ptr2, (char **)NULL, 10);
	*Set_VirtualMemory = 1;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "MinTmpDisk=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+11);
	str_ptr2 = (char *)strtok_r(Scratch, SEPCHARS, &str_ptr3);
	*My_TmpDisk = strtol(str_ptr2, (char **)NULL, 10);
	*Set_TmpDisk = 1;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "MaxTime=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+8);
	str_ptr2 = (char *)strtok_r(Scratch, SEPCHARS, &str_ptr3);
	*My_MaxTime = (int) strtol(str_ptr2, (char **)NULL, 10);
	*Set_MaxTime = 1;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "CpuCount=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+9);
	str_ptr2 = (char *)strtok_r(Scratch, SEPCHARS, &str_ptr3);
	*My_CpuCount = (int) strtol(str_ptr2, (char **)NULL, 10);
	*Set_CpuCount = 1;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "NodeCount=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+10);
	str_ptr2 = (char *)strtok_r(Scratch, SEPCHARS, &str_ptr3);
	*My_NodeCount = (int) strtol(str_ptr2, (char **)NULL, 10);
	*Set_NodeCount = 1;
    } /* if */

    free(Scratch);
    return 0;
} /* Parse_Job_Spec */


/*
 * Pick_Best_Nodes - From the list of acceptable nodes, select the "best" ones to use
 * On systems with the Quadrics switch, this means 
 *  1. selecting the set of continguous nodes which is best fit (e.g. for a 4 node job and 
 *     a system with two sets of contiguous nodes of size 4 nodes and 16 nodes, pick the 4 node set).
 *  2. if no single continuous node set is available, select the nodes so as to minimize 
 *     the number of sets (e.g for a 5 node job and a system with available node sets of size 
 *     3, 2, 1, 1, 1, 1, and 1, select the 3 and 2 node set.
 * Input: Node_List_Pos - Pointers to available nodes
 *        Node_List_Index - Index numbers of available nodes (position in list)
 *        Node_List_CPUs - Count of CPUs on each available nodes 
 *        Node_List_Count - Number of entries in Node_List_*
 *        Req_CPU_Count - Required count of CPUs
 *        Req_Node_Count - Required count of nodes
 *        Req_Continguous - Request the nodes be contiguous
 * Output: Error_Code is set
 *         Returns node list, NOTE: This memory space must be freed by whatever calls Pick_Best_Nodes
 */
char *Pick_Best_Nodes(struct Node_Record **Node_List_Pos, int *Node_List_Index, int *Node_List_CPUs,
	int Node_List_Count, int Req_CPU_Count, int Req_Node_Count, int Req_Continguous,
	int *Error_Code) {
    int inx, Node_List_Size;
    int CPU_Tally, Node_Tally;
    int Best_Fit, Best_Fit_CPUs;
    int Biggest_Fit, Biggest_Fit_CPUs;
    int *Node_Set_Pos, *Node_Set_Nodes, *Node_Set_CPUs, Node_Set_Size;
    char *Scratch;

    /* Test for trivial case */
    if (Node_List_Count <= 0) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Pick_Best_Nodes: Internal error, passed null node list\n");
#else
	syslog(LOG_ERR, "Pick_Best_Nodes: Internal error, passed null node list\n");
#endif
	*Error_Code = 0;
	return (char *)NULL;
    } /* if */

    Node_Set_Pos = (int *)malloc(Node_List_Count * sizeof(int *));
    if (Node_Set_Pos == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Pick_Best_Nodes: unable to allocate memory\n");
#else
	syslog(LOG_ERR, "Pick_Best_Nodes: unable to allocate memory\n");
#endif
	*Error_Code =  ENOMEM;
	return (char *)NULL;
    } /* if */

    Node_Set_Nodes = (int *)malloc(Node_List_Count * sizeof(int *));
    if (Node_Set_Nodes == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Pick_Best_Nodes: unable to allocate memory\n");
#else
	syslog(LOG_ERR, "Pick_Best_Nodes: unable to allocate memory\n");
#endif
	*Error_Code =  ENOMEM;
	free(Node_Set_Pos);
	return (char *)NULL;
    } /* if */

    Node_Set_CPUs = (int *)malloc(Node_List_Count * sizeof(int *));
    if (Node_Set_CPUs == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Pick_Best_Nodes: unable to allocate memory\n");
#else
	syslog(LOG_ERR, "Pick_Best_Nodes: unable to allocate memory\n");
#endif
	*Error_Code =  ENOMEM;
	free(Node_Set_Pos);
	free(Node_Set_Nodes);
	return (char *)NULL;
    } /* if */

    Node_Set_Size = -1;
    for (inx=0; inx<Node_List_Count; inx++) {
	if (inx == 0) {
	    Node_Set_Size++;
	    Node_Set_Pos[Node_Set_Size]   = inx;
	    Node_Set_Nodes[Node_Set_Size] = 1;
	    Node_Set_CPUs[Node_Set_Size]  = Node_List_CPUs[inx];
	} else if (Node_List_Index[inx-1] == (Node_List_Index[inx]-1)) {
	    Node_Set_Nodes[Node_Set_Size]++;
	    Node_Set_CPUs[Node_Set_Size]  += Node_List_CPUs[inx];
	} else { 	/* no longer contiguous */
	    Node_Set_Size++;
	    Node_Set_Pos[Node_Set_Size]   = inx;
	    Node_Set_Nodes[Node_Set_Size] = 1;
	    Node_Set_CPUs[Node_Set_Size]  = Node_List_CPUs[inx];
	} /* else */
    } /* for */
    Node_Set_Size++;

    Best_Fit = -1;
    Biggest_Fit = -1;
    for (inx=0; inx<Node_Set_Size; inx++) {
	if ((Biggest_Fit == -1) || (Biggest_Fit_CPUs < Node_Set_CPUs[inx])) {
	    Biggest_Fit = inx;
	    Biggest_Fit_CPUs = Node_Set_CPUs[inx];
	} /* if */
	if (Node_Set_CPUs[inx]  < Req_CPU_Count)  continue;
	if (Node_Set_Nodes[inx] < Req_Node_Count) continue;
	if ((Best_Fit == -1) || (Best_Fit_CPUs > Node_Set_CPUs[inx])) {
	    Best_Fit = inx;
	    Best_Fit_CPUs = Node_Set_CPUs[inx];
	} /* if */
    } /* for */

    if (Best_Fit != -1) { /* Return this list */
	Node_List_Size = BUF_SIZE;
	Scratch = (char *)malloc(Node_List_Size);
	if (Scratch == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Pick_Best_Nodes: unable to allocate memory\n");
#else
	    syslog(LOG_ERR, "Pick_Best_Nodes: unable to allocate memory\n");
#endif
	    *Error_Code =  ENOMEM;
	    free(Node_Set_Pos);
	    free(Node_Set_Nodes);
	    free(Node_List_CPUs);
	    return (char *)NULL;
	} /* if */
	Scratch[0] = (char)NULL;
	CPU_Tally = 0;
	Node_Tally = 0;
	for (inx=Node_Set_Pos[Best_Fit]; inx<Node_List_Count; inx++) {
	    if ((strlen(Scratch)+strlen(Node_List_Pos[inx]->Name)+1) >= Node_List_Size) {
		Node_List_Size += BUF_SIZE;
		Scratch = realloc(Scratch, Node_List_Size);
		if (Scratch == NULL) {
#if DEBUG_SYSTEM
		    fprintf(stderr, "Pick_Best_Nodes: unable to allocate memory\n");
#else
		    syslog(LOG_ERR, "Pick_Best_Nodes: unable to allocate memory\n");
#endif
		    free(Node_Set_Pos);
		    free(Node_Set_Nodes);
		    free(Node_List_CPUs);
		    *Error_Code =  ENOMEM;
		    return (char *)NULL;
		    } /* if */
		} /* if */
	    if (strlen(Scratch) > 0) strcat(Scratch, ",");
	    strcat(Scratch, Node_List_Pos[inx]->Name);
	    CPU_Tally += Node_List_CPUs[inx];
	    Node_Tally++;
	    if ((CPU_Tally  >= Req_CPU_Count) && (Node_Tally >= Req_Node_Count)) break;
	} /* for */
	free(Node_Set_Pos);
	free(Node_Set_Nodes);
	free(Node_Set_CPUs);
	Scratch = realloc(Scratch, strlen(Scratch)+1);
	*Error_Code = 0;
	return Scratch;
    } /* if */

    if (Req_Continguous != 0) {		/* Can not provide contiguous node list */
	free(Node_Set_Pos);
	free(Node_Set_Nodes);
	free(Node_Set_CPUs);
	*Error_Code = EBUSY;
	return (char *)NULL;
    } /* if */

    /* We need to select the "best" sets of contiguous node lists */
    /* This is the smallest number of sets and the smallest number of CPUs */
    Node_List_Size = BUF_SIZE;
    Scratch = (char *)malloc(Node_List_Size);
    if (Scratch == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Pick_Best_Nodes: unable to allocate memory\n");
#else
	syslog(LOG_ERR, "Pick_Best_Nodes: unable to allocate memory\n");
#endif
	*Error_Code =  ENOMEM;
	free(Node_Set_Pos);
	free(Node_Set_Nodes);
	free(Node_List_CPUs);
	return (char *)NULL;
    } /* if */

    Scratch[0] = (char)NULL;
    CPU_Tally = 0;
    Node_Tally = 0;
    Best_Fit = Biggest_Fit;
    while ((CPU_Tally < Req_CPU_Count) || (Node_Tally < Req_Node_Count)) {
	for (inx=Node_Set_Pos[Best_Fit]; inx<Node_List_Count; inx++) {
	    if ((strlen(Scratch)+strlen(Node_List_Pos[inx]->Name)+1) >= Node_List_Size) {
		Scratch = realloc(Scratch, Node_List_Size+BUF_SIZE);
		if (Scratch == NULL) {
#if DEBUG_SYSTEM
		    fprintf(stderr, "Pick_Best_Nodes: unable to allocate memory\n");
#else
		    syslog(LOG_ERR, "Pick_Best_Nodes: unable to allocate memory\n");
#endif
		    free(Node_Set_Pos);
		    free(Node_Set_Nodes);
		    free(Node_List_CPUs);
		    *Error_Code =  ENOMEM;
		    return (char *)NULL;
		} /* if */
	    } /* if */
	    if (strlen(Scratch) > 0) strcat(Scratch, ",");
	    strcat(Scratch, Node_List_Pos[inx]->Name);
	    CPU_Tally += Node_List_CPUs[inx];
	    Node_Tally++;
	    if ((CPU_Tally  >= Req_CPU_Count) && (Node_Tally >= Req_Node_Count)) break;
	    if (((inx+1) < Node_List_Count) && 
	        (Node_List_Index[inx+1] != (Node_List_Index[inx]+1))) break;
	} /* for */
	if ((CPU_Tally  >= Req_CPU_Count) && (Node_Tally >= Req_Node_Count)) break; /* All done */
	Node_Set_CPUs[Best_Fit]  = 0;	/* Don't reuse this set */
	Node_Set_Nodes[Best_Fit] = 0;
	/* Now pick the next best fitting set */
	Best_Fit = -1;
	Biggest_Fit = -1;
	for (inx=0; inx<=Node_Set_Size; inx++) {
	    if (Node_Set_Nodes[inx] == 0) continue;
	    if ((Biggest_Fit == -1) || (Biggest_Fit_CPUs < Node_Set_CPUs[inx])) {
		Biggest_Fit = inx;
		Biggest_Fit_CPUs = Node_Set_CPUs[inx];
	    } /* if */
	    if (Node_Set_CPUs[inx]  < (Req_CPU_Count-CPU_Tally))  continue;
	    if (Node_Set_Nodes[inx] < (Req_Node_Count-Node_Tally)) continue;
	    if ((Best_Fit == -1) || (Best_Fit_CPUs > Node_Set_CPUs[inx])) {
		Best_Fit = inx;
		Best_Fit_CPUs = Node_Set_CPUs[inx];
	    } /* if */
	} /* for */
	if (Biggest_Fit == -1) {
#if DEBUG_SYSTEM
 	   fprintf(stderr, "Pick_Best_Nodes: Internal error\n");
	   fprintf(stderr, "Current node list: %s\n", Scratch);
#else
 	   syslog(LOG_ERR, "Pick_Best_Nodes: Internal error\n");
#endif
 	   free(Node_Set_Pos);
 	   free(Node_Set_Nodes);
 	   free(Node_Set_CPUs);
	   free(Scratch);
	   return (char *)NULL;
	} /* if */
	if (Best_Fit == -1) Best_Fit=Biggest_Fit;
    } /* while */

    free(Node_Set_Pos);
    free(Node_Set_Nodes);
    free(Node_Set_CPUs);
    Scratch = realloc(Scratch, strlen(Scratch)+1);
    *Error_Code = 0;
    return Scratch;
} /* Pick_Best_Nodes */


/*
 * Will_Job_Run - Determine if the given job specification can be initiated now
 * Input: Job_Spec - Specifications for the job
 * Output: Returns node list, NULL if can not be initiated
 *
 * NOTE: The value returned MUST be freed to avoid memory leak
 */
char *Will_Job_Run(char *Specification, int *Error_Code) {
    char My_Name[MAX_NAME_LEN];
    char My_OS[MAX_OS_LEN];
    int My_CPUs, My_Contiguous, My_RealMemory, My_VirtualMemory, My_MaxTime;
    int My_CpuCount, My_NodeCount;
    long My_TmpDisk;
    float My_Speed;
    unsigned Partition;
    int Set_CPUs, Set_Speed, Set_RealMemory, Set_VirtualMemory, Set_TmpDisk;
    int Set_MaxTime, Set_CpuCount, Set_NodeCount;
    char *Scratch, *Node_List_Ptr, *str_ptr1, *str_ptr2, *Fail_Mode;
    int Node_Tally, CPU_Tally;
    int inx, Node_List_Size;
    struct Node_Record  *Node_Record_Point;
    ListIterator Node_Record_Iterator;		/* For iterating through Node_Record_List */
    struct Node_Record **Node_List_Pos;
    int *Node_List_Index;
    int *Node_List_CPUs;

    *Error_Code =  Parse_Job_Spec(Specification, My_Name, My_OS, 
	&My_CPUs, &Set_CPUs, &My_Speed, &Set_Speed, &My_Contiguous,
	&My_RealMemory, &Set_RealMemory, &My_VirtualMemory, &Set_VirtualMemory, 
	&My_TmpDisk, &Set_TmpDisk, &My_MaxTime, &Set_MaxTime,  
	&My_CpuCount, &Set_CpuCount, &My_NodeCount, &Set_NodeCount);
    if (*Error_Code != 0) return (char *)NULL;
    if (strlen(My_Name) == 0) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Will_Job_Run: User name missing from job specification\n");
#else
	syslog(LOG_ERR, "Will_Job_Run: User name missing from job specification\n");
#endif
	*Error_Code = EINVAL;
	return (char *)NULL;
    } /* if */

    *Error_Code = Find_Valid_Parts(Specification, &Partition);
    if (*Error_Code != 0) return (char *)NULL;
    if (Partition == 0) { 
#if DEBUG_SYSTEM
	fprintf(stderr, "Will_Job_Run: No valid partition for job specification\n");
#else
	syslog(LOG_ERR, "Will_Job_Run: No valid partition for job specification\n");
#endif
	*Error_Code = EACCES;
	return (char *)NULL;
    } /* if */

    /* Insure that every node in (optional) list is valid and satisfies constraints*/
    Node_List_Ptr = (char *)strstr(Specification, "NodeList=");
    if (Node_List_Ptr != NULL) {
	Node_List_Ptr += 9;   /* Skip over "NodeList=" */
	Scratch = malloc(strlen(Node_List_Ptr)+1);
	if (Scratch == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Will_Job_Run: unable to allocate memory\n");
#else
	    syslog(LOG_ERR, "Will_Job_Run: unable to allocate memory\n");
#endif
	    *Error_Code =  ENOMEM;
	    return (char *)NULL;
	} /* if */
	strcpy(Scratch, Node_List_Ptr);
	strtok_r(Scratch, SEPCHARS, &str_ptr2);	/* make any white-space into end of string */
	Node_List_Size = strlen(Scratch);
	str_ptr1 = (char *)strtok_r(Scratch, ",", &str_ptr2);
	CPU_Tally = 0;
	Node_Tally = 0;
	while (str_ptr1 != NULL) {
	    Node_Record_Point = Find_Node_Record(str_ptr1);
	    if (Node_Record_Point == (struct Node_Record  *)NULL) {
#if DEBUG_SYSTEM
		fprintf(stderr, "Will_Job_Run: unable to find node named %s\n", str_ptr1);
#else
		syslog(LOG_ERR, "Will_Job_Run: unable to find node named %s\n", str_ptr1);
#endif
		*Error_Code =  ENOENT;
		free(Scratch);
		return (char *)NULL;
	    } /* if */

	    /* Validate the node's use */
	    Fail_Mode = (char *)NULL;
	    if ((strlen(My_OS) != 0) && (OS_Comp(My_OS, Node_Record_Point->OS) < 0)) 
		Fail_Mode = "OS";
	    if ((Partition & Node_Record_Point->Partition) == 0) 
		Fail_Mode = "Partition";
	    if ((Set_CPUs != 0) && (My_CPUs > Node_Record_Point->CPUs)) 
		Fail_Mode = "MinCpus";
	    if ((Set_Speed != 0) && (My_Speed > Node_Record_Point->Speed)) 
		Fail_Mode = "Speed";
	    if ((Set_RealMemory != 0) && (My_RealMemory > Node_Record_Point->RealMemory)) 
		Fail_Mode = "RealMemory";
	    if ((Set_VirtualMemory != 0) && (My_VirtualMemory > Node_Record_Point->VirtualMemory)) 
		Fail_Mode = "VirtualMemory";
	    if ((Set_TmpDisk != 0) && (My_TmpDisk > Node_Record_Point->TmpDisk)) 
		Fail_Mode = "TmpDisk";
	    if (Node_Record_Point->NodeState != STATE_IDLE) 
		Fail_Mode = "NodeState";

	    if (Fail_Mode != (char *)NULL) {
#if DEBUG_SYSTEM
		fprintf(stderr, "Will_Job_Run: node %s does not meet job %s specification\n", 
			str_ptr1, Fail_Mode);
#else
		syslog(LOG_WARNING, "Will_Job_Run: node %s does not meet job %s specification\n", 
			str_ptr1, Fail_Mode);
#endif
	    	*Error_Code =  EINVAL;
		free(Scratch);
		return (char *)NULL;
	    } /* if */
	    if (Node_Record_Point->NodeState == STATE_BUSY) {
#if DEBUG_SYSTEM
		fprintf(stderr, "Will_Job_Run: requested node %s busy\n", str_ptr1);
#else
		syslog(LOG_WARNING, "Will_Job_Run: requested node %s busy\n", str_ptr1);
#endif
		*Error_Code =  EBUSY;
		free(Scratch);
		return (char *)NULL;
	    } /* if */
	    if (Node_Record_Point->NodeState != STATE_IDLE) {
#if DEBUG_SYSTEM
		fprintf(stderr, "Will_Job_Run: node %s not available for use\n", str_ptr1);
#else
		syslog(LOG_WARNING, "Will_Job_Run: node %s not available for use\n", str_ptr1);
#endif
		*Error_Code =  EINVAL;
		free(Scratch);
		return (char *)NULL;
	    } /* if */
	    CPU_Tally += Node_Record_Point->CPUs;
	    Node_Tally++;
	    str_ptr1 = (char *)strtok_r(NULL, ",", &str_ptr2);
	} /* while */
	free(Scratch);
	if (((Set_CpuCount != 0) && (My_CpuCount > CPU_Tally)) ||
	     ((Set_NodeCount != 0) && (My_NodeCount > Node_Tally))) {
	    *Error_Code = EINVAL;
	    return (char *)NULL;
	} /* if */
	Scratch = malloc(Node_List_Size+1);
	strncpy(Scratch, Node_List_Ptr, Node_List_Size);
	Scratch[Node_List_Size] = (char)NULL;
	return Scratch;

    } else if ((Set_NodeCount != 0) || (Set_CpuCount != 0)) {	/* We need to select the nodes */
	if (Set_CpuCount  == 0) My_CpuCount=0;
	if (Set_NodeCount == 0) My_NodeCount=0;
	/* Check for trivial case, no nodes or CPUs requested */
	if ((My_CpuCount  == 0 ) && (My_NodeCount == 0)) {
	    *Error_Code =  0;
	    return (char *)NULL;
	} /* if */

	Node_List_Pos = (struct Node_Record **)malloc(Node_Count * sizeof(struct Node_Record *));
	if (Node_List_Pos == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Will_Job_Run: unable to allocate memory\n");
#else
	    syslog(LOG_ERR, "Will_Job_Run: unable to allocate memory\n");
#endif
	    *Error_Code =  ENOMEM;
	    return (char *)NULL;
	} /* if */

	Node_List_Index = (int *)malloc(Node_Count * sizeof(int *));
	if (Node_List_Index == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Will_Job_Run: unable to allocate memory\n");
#else
	    syslog(LOG_ERR, "Will_Job_Run: unable to allocate memory\n");
#endif
	    *Error_Code =  ENOMEM;
	    free(Node_List_Pos);
	    return (char *)NULL;
	} /* if */


	Node_List_CPUs = (int *)malloc(Node_Count * sizeof(int *));
	if (Node_List_CPUs == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Will_Job_Run: unable to allocate memory\n");
#else
	    syslog(LOG_ERR, "Will_Job_Run: unable to allocate memory\n");
#endif
	    *Error_Code =  ENOMEM;
	    free(Node_List_Pos);
	    free(Node_List_Index);
	    return (char *)NULL;
	} /* if */

	Node_Record_Iterator = list_iterator_create(Node_Record_List);
	if (Node_Record_Iterator == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Will_Job_Run: list_iterator_create unable to allocate memory\n");
#else
	    syslog(LOG_ERR, "Will_Job_Run: list_iterator_create unable to allocate memory\n");
#endif
	    free(Node_List_Pos);
	    free(Node_List_Index);
	    free(Node_List_CPUs);
	    *Error_Code = ENOMEM;
	    return (char *)NULL;
	} /* if */

	/* Identify the acceptable nodes */
	inx = 0;
	Node_Tally = 0;
	CPU_Tally = 0;
	while (Node_Record_Point = (struct Node_Record *)list_next(Node_Record_Iterator)) {
	    if (((strlen(My_OS) != 0) && (OS_Comp(My_OS, Node_Record_Point->OS) < 0)) || 
	        ((Partition & Node_Record_Point->Partition) == 0) ||
	        ((Set_CPUs != 0) && (My_CPUs > Node_Record_Point->CPUs)) ||
	        ((Set_Speed != 0) && (My_Speed > Node_Record_Point->Speed)) ||
	        ((Set_RealMemory != 0) && (My_RealMemory > Node_Record_Point->RealMemory)) ||
	        ((Set_VirtualMemory != 0) && (My_VirtualMemory > Node_Record_Point->VirtualMemory)) ||
	        ((Set_TmpDisk != 0) && (My_TmpDisk > Node_Record_Point->TmpDisk)) ||
	        (Node_Record_Point->NodeState != STATE_IDLE)) {
		inx++;
	    } else {	/* Node is usable */
		Node_List_Pos[Node_Tally]   = Node_Record_Point;
		Node_List_Index[Node_Tally] = inx++;
		Node_List_CPUs[Node_Tally]  = Node_Record_Point->CPUs;
		CPU_Tally += Node_Record_Point->CPUs;
		Node_Tally++;
	    } /* else */
	} /* while */
	list_iterator_destroy(Node_Record_Iterator);

	/* Is there enough nodes to satisfy request (quick test) */
	if ((CPU_Tally  < My_CpuCount ) || (Node_Tally < My_NodeCount)) {
	    free(Node_List_Pos);
	    free(Node_List_Index);
	    free(Node_List_CPUs);
	    *Error_Code =  EBUSY;
	    return (char *)NULL;
	} /* if */

	/* Now pick "best" set of acceptable nodes */
	Node_List_Ptr = Pick_Best_Nodes(Node_List_Pos, Node_List_Index, 
	    Node_List_CPUs, Node_Tally, My_CpuCount, My_NodeCount, 
	    My_Contiguous, Error_Code);
	free(Node_List_Pos);
	free(Node_List_Index);
	free(Node_List_CPUs);
	return Node_List_Ptr;

    } else {
#if DEBUG_SYSTEM
	fprintf(stderr, "Will_Job_Run: No NodeList, CpuCount, or NodeCount specified\n");
#else
	syslog(LOG_WARNING, "Will_Job_Run: No NodeList, CpuCount, or NodeCount specified\n");
#endif
	*Error_Code =  EINVAL;
	return (char *)NULL;
    } /* else */

} /* Will_Job_Run */
