/* 
 * node_scheduler.c - Allocated nodes to jobs 
 * See slurm.h for documentation on external functions and data structures
 *
 * NOTE: DEBUG_MODULE mode test with execution line
 *	node_scheduler ../../etc/SLURM.conf2 ../../etc/SLURM.jobs
 *
 * Author: Moe Jette, jette@llnl.gov
 */

#define DEBUG_SYSTEM  1

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

#include "slurm.h"

#define BUF_SIZE 1024
#define NO_VAL (-99)

int Is_Key_Valid(int Key);
int Match_Group(char *AllowGroups, char *UserGroups);
int Parse_Job_Specs(char *Job_Specs, char **Req_Features, char **Req_Node_List, char **Job_Name,
	char **Req_Group, char **Req_Partition, int *Contiguous, int *Req_CPUs, 
	int *Req_Nodes, int *Min_CPUs, int *Min_RealMemory, int *Min_TmpDisk, int *Key);

#if DEBUG_MODULE
/* main is used here for testing purposes only */
main(int argc, char * argv[]) {
    int Error_Code, Line_Num;
    FILE *Command_File;
    char In_Line[BUF_SIZE], *Node_List;

    if (argc < 3) {
	printf("Usage: %s <slurm_conf_file> <slurm_job_file>\n", argv[0]);
	exit(0);
    } /* if */

    Error_Code = Init_SLURM_Conf();
    if (Error_Code) {
	printf("controller: Error %d from Init_SLURM_Conf\n", Error_Code);
	exit(Error_Code);
    } /* if */

    Error_Code = Read_SLURM_Conf(argv[1]);
    if (Error_Code) {
	printf("controller: Error %d from Read_SLURM_Conf\n", Error_Code);
	exit(Error_Code);
    } /* if */

    Command_File = fopen(argv[2], "r");
    if (Command_File == NULL) {
	fprintf(stderr, "node_scheduler: error %d opening command file %s\n", 
		errno, argv[2]);
	exit(1);
    } /* if */

    Line_Num = 0;
    while (fgets(In_Line, BUF_SIZE, Command_File)) {
	Line_Num++;
	Error_Code = Allocate_Nodes(In_Line, &Node_List);
	if (Error_Code) {
	    if (strncmp(In_Line, "Name=FAIL", 9) != 0) printf("ERROR:");
	    printf("For job: %s", In_Line, Node_List);
	    printf("node_scheduler: Error %d from Allocate_Nodes on line %d\n\n", Error_Code, Line_Num);
	} else {
	    if (strncmp(In_Line, "Name=FAIL", 9) == 0) printf("ERROR: ");
	    printf("For job: %s  Nodes selected %s\n\n", In_Line, Node_List);
	    free(Node_List);
	} /* else */
    } /* while */
} /* main */
#endif


/*
 * Allocate_Nodes - Allocate nodes to a job with the given specifications
 * Input: Job_Specs - Job specifications
 *        Node_List - Pointer to node list returned
 * Output: Node_List - List of allocated nodes
 *         Returns 0 on success, EINVAL if not possible to satisfy request, 
 *		or EAGAIN if resources are presently busy
 * NOTE: The calling program must free the memory pointed to by Node_List
 */
int Allocate_Nodes(char *Job_Specs, char **Node_List) {
    char *Req_Features, *Req_Node_List, *Job_Name, *Req_Group, *Req_Partition, *Out_Line;
    int Contiguous, Req_CPUs, Req_Nodes, Min_CPUs, Min_RealMemory, Min_TmpDisk;
    int Error_Code, CPU_Tally, Node_Tally, Key;
    struct Part_Record *Part_Ptr;
    unsigned *Req_BitMap, *Part_BitMap;

    Req_Features = Req_Node_List = Req_Group = Req_Partition = NULL;
    Req_BitMap = Part_BitMap = NULL;
    Contiguous = Req_CPUs = Req_Nodes = Min_CPUs = Min_RealMemory = Min_TmpDisk = Key = NO_VAL;

    Error_Code = Parse_Job_Specs(Job_Specs, &Req_Features, &Req_Node_List, &Job_Name, &Req_Group, 
		&Req_Partition, &Contiguous, &Req_CPUs, &Req_Nodes, &Min_CPUs, 
		&Min_RealMemory, &Min_TmpDisk, &Key);
    if (Error_Code == ENOMEM) {
	Error_Code = EAGAIN;	/* Don't want to kill the job off */
	goto cleanup;
    } /* if */
    if (Error_Code != 0) {
	Error_Code =  EINVAL;	/* Permanent error, invalid parsing */
#if DEBUG_SYSTEM
	fprintf(stderr, "Allocate_Nodes: Parsing failure on %s\n", Job_Specs);
#else
	syslog(LOG_NOTICE, "Allocate_Nodes: Parsing failure on %s\n", Job_Specs);
#endif
	goto cleanup;
    } /* if */

    /* Find selected partition */
    if (Req_Partition) {
	Part_Ptr   = list_find_first(Part_List, &List_Find_Part, Req_Partition);
	if (Part_Ptr == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Allocate_Nodes: Invalid partition specified: %s\n", Req_Partition);
#else
	    syslog(LOG_NOTICE, "Allocate_Nodes: Invalid partition specified: %s\n", Req_Partition);
#endif
	    Error_Code = EINVAL;
	    goto cleanup;
	} /* if */
    } else {
	if (Default_Part_Loc == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Allocate_Nodes: Default partition not set.\n");
#else
	    syslog(LOG_ERR, "Allocate_Nodes: Default partition not set.\n");
#endif
	    Error_Code = EINVAL;
	    goto cleanup;
	} /* if */
	Part_Ptr = Default_Part_Loc;
    } /* if */

    /* Can this user access this partition */
    if (Part_Ptr->Key && (Is_Key_Valid(Key) == 0)) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Allocate_Nodes: Job lacks key required of partition %s\n", 
		Part_Ptr->Name);
#else
	syslog(LOG_NOTICE, "Allocate_Nodes: Job lacks key required of partition %s\n", 
		Part_Ptr->Name);
#endif
	Error_Code = EINVAL;
	goto cleanup;
    } /* if */
    if (Match_Group(Part_Ptr->AllowGroups, Req_Group) == 0) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Allocate_Nodes: Job lacks group required of partition %s\n", 
		Part_Ptr->Name);
#else
	syslog(LOG_NOTICE, "Allocate_Nodes: Job lacks group required of partition %s\n", 
		Part_Ptr->Name);
#endif
	Error_Code = EINVAL;
	goto cleanup;
    } /* if */

    /* Check if select partition has sufficient resources to satisfy request */
    if ((Req_CPUs != NO_VAL) && (Req_CPUs > Part_Ptr->TotalCPUs)) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Allocate_Nodes: Too many CPUs (%d) requested of partition %s(%d)\n", 
		Req_CPUs, Part_Ptr->Name, Part_Ptr->TotalCPUs);
#else
	syslog(LOG_NOTICE, "Allocate_Nodes: Too many CPUs (%d) requested of partition %s(%d)\n", 
		Req_CPUs, Part_Ptr->Name, Part_Ptr->TotalCPUs);
#endif
	Error_Code = EINVAL;
	goto cleanup;
    } /* if */
    if ((Req_Nodes != NO_VAL) && (Req_Nodes > Part_Ptr->TotalNodes)) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Allocate_Nodes: Too many nodes (%d) requested of partition %s(%d)\n", 
		Req_Nodes, Part_Ptr->Name, Part_Ptr->TotalNodes);
#else
	syslog(LOG_NOTICE, "Allocate_Nodes: Too many nodes (%d) requested of partition %s(%d)\n", 
		Req_Nodes, Part_Ptr->Name, Part_Ptr->TotalNodes);
#endif
	Error_Code = EINVAL;
	goto cleanup;
    } /* if */
    if (Req_Node_List) { /* Insure that selected nodes are in this partition */
	Error_Code = NodeName2BitMap(Req_Node_List, &Req_BitMap);
	if (Error_Code == EINVAL) goto cleanup;
	if (Error_Code != 0) {
	    Error_Code = EAGAIN;  /* No memory */
	    goto cleanup;
	} /* if */
	Part_BitMap = BitMapCopy(Part_Ptr->NodeBitMap);
	if (Part_BitMap == NULL)  {
	    Error_Code = EAGAIN;  /* No memory */
	    goto cleanup;
	} /* if */
	BitMapAND(Part_BitMap, Req_BitMap);
	if (BitMapCount(Part_BitMap) != BitMapCount(Req_BitMap)) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Allocate_Nodes: Requested nodes %s not in partition %s\n", 
		Req_Node_List, Part_Ptr->Name);
#else
	    syslog(LOG_NOTICE, "Allocate_Nodes: Requested nodes %s not in partition %s\n", 
		Req_Node_List, Part_Ptr->Name);
#endif
	    Error_Code = EINVAL;
	    goto cleanup;
	} /* if */
    } else
	Req_BitMap = (unsigned *)NULL;

    /* Pick up nodes from the Weight ordered configuration list */

    /* Pick the nodes providing a best-fit */

    /* Mark the selected nodes as STATE_STAGING */

    Error_Code = BitMap2NodeName(Req_BitMap, Node_List);
    if (Error_Code) printf("BitMap2NodeName error %d\n", Error_Code);

cleanup:
    if (Req_Features)	free(Req_Features);
    if (Req_Node_List)	free(Req_Node_List);
    if (Req_Group)	free(Req_Group);
    if (Req_Partition)	free(Req_Partition);
    if (Req_BitMap)	free(Req_BitMap);
    if (Part_BitMap)	free(Part_BitMap);
    return Error_Code;
} /* Allocate_Nodes */


/* 
 * Is_Key_Valid - Determine if supplied key is valid
 * Input: Key - A SLURM key acquired by user root
 * Output: Returns 1 if key is valid, 0 otherwise
 * NOTE: This is only a placeholder for a future function
 */
int Is_Key_Valid(int Key) {
    if (Key == NO_VAL) return 0;
    return 1;
}  /* Is_Key_Valid */


/*
 * Match_Group - Determine if the user is a member of any groups permitted to use this partition
 * Input: AllowGroups - Comma delimited list of groups permitted to use the partition, 
 *			NULL is for ALL groups
 *        UserGroups - Comma delimited list of groups the user belongs to
 * Output: Returns 1 if user is member, 0 otherwise
 */
int Match_Group(char *AllowGroups, char *UserGroups) {
    char *Tmp_Allow_Group, *str_ptr1, *str_ptr2;
    char *Tmp_User_Group, *str_ptr3, *str_ptr4;

    if (AllowGroups == NULL) return 1;	/* Anybody can use it */
    if (UserGroups  == NULL) return 0;	/* Empty group list */

    Tmp_Allow_Group = malloc(strlen(AllowGroups)+1);
    if (Tmp_Allow_Group == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Match_Group: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Match_Group: unable to allocate memory\n");
#endif
	return 1; /* Assume good for now */
    } /* if */
    strcpy(Tmp_Allow_Group, AllowGroups);

    Tmp_User_Group = malloc(strlen(UserGroups)+1);
    if (Tmp_User_Group == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Match_Group: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Match_Group: unable to allocate memory\n");
#endif
	free(Tmp_Allow_Group);
	return 1; /* Assume good for now */
    } /* if */
    strcpy(Tmp_User_Group, UserGroups);

    str_ptr1 = (char *)strtok_r(Tmp_Allow_Group, ",", &str_ptr2);
    while (str_ptr1) {
	strcpy(Tmp_User_Group, UserGroups);
	str_ptr3 = (char *)strtok_r(Tmp_User_Group, ",", &str_ptr4);
	while (str_ptr3) {
	    if (strcmp(str_ptr1, str_ptr3) == 0) {  /* We have a match */
		free(Tmp_Allow_Group);
		free(Tmp_User_Group);
		return 1;
	    } /* if */
	    str_ptr3 = (char *)strtok_r(NULL, ",", &str_ptr4);
	} /* while (str_ptr3) */
	str_ptr1 = (char *)strtok_r(NULL, ",", &str_ptr2);
    } /* while (str_ptr1)*/
    free(Tmp_Allow_Group);
    free(Tmp_User_Group);
    return 0;  /* No match */
} /* Match_Group */


/* 
 * Parse_Job_Specs - Pick the appropriate fields out of a job request specification
 * Input: Job_Specs - String containing the specification
 *        Req_Features, etc. - Pointers to storage for the specifications
 * Output: Req_Features, etc. - The job's specifications
 *         Returns 0 if no error, errno otherwise
 * NOTE: The calling function must free memory at Req_Features[0], Req_Node_List[0],
	Req_Group[0], and Req_Partition[0]
 */
int Parse_Job_Specs(char *Job_Specs, char **Req_Features, char **Req_Node_List, char **Job_Name,
	char **Req_Group, char **Req_Partition, int *Contiguous, int *Req_CPUs, 
	int *Req_Nodes, int *Min_CPUs, int *Min_RealMemory, int *Min_TmpDisk, int *Key) {
    int Bad_Index, Error_Code, i;
    char *Temp_Specs;

    Req_Features[0] = Req_Node_List[0] = Req_Group[0] = Req_Partition[0] = Job_Name[0] = NULL;
    *Contiguous = *Req_CPUs = *Req_Nodes = *Min_CPUs = *Min_RealMemory = *Min_TmpDisk = NO_VAL;

    Temp_Specs = malloc(strlen(Job_Specs)+1);
    if (Temp_Specs == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Parse_Job_Specs: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Parse_Job_Specs: unable to allocate memory\n");
#endif
	return ENOMEM;
    } /* if */
    strcpy(Temp_Specs, Job_Specs);

    Error_Code = Load_String (Job_Name, "Name=", Temp_Specs);
    if (Error_Code) goto cleanup;

    Error_Code = Load_String (Req_Features, "Features=", Temp_Specs);
    if (Error_Code) goto cleanup;

    Error_Code = Load_String (Req_Node_List, "Node_List=", Temp_Specs);
    if (Error_Code) goto cleanup;

    Error_Code = Load_String (Req_Group, "Groups=", Temp_Specs);
    if (Error_Code) goto cleanup;

    Error_Code = Load_String (Req_Partition, "PartitionName=", Temp_Specs);
    if (Error_Code) goto cleanup;

    Error_Code = Load_Integer (Contiguous, "Contiguous", Temp_Specs);
    if (Error_Code) goto cleanup;

    Error_Code = Load_Integer (Req_CPUs, "Req_CPUs=", Temp_Specs);
    if (Error_Code) goto cleanup;

    Error_Code = Load_Integer (Req_Nodes, "Req_Nodes=", Temp_Specs);
    if (Error_Code) goto cleanup;

    Error_Code = Load_Integer (Min_CPUs, "Min_CPUs=", Temp_Specs);
    if (Error_Code) goto cleanup;

    Error_Code = Load_Integer (Min_RealMemory, "Min_RealMemory=", Temp_Specs);
    if (Error_Code) goto cleanup;

    Error_Code = Load_Integer (Min_TmpDisk, "Min_TmpDisk=", Temp_Specs);
    if (Error_Code) goto cleanup;

    Error_Code = Load_Integer (Key, "Key=", Temp_Specs);
    if (Error_Code) {
	return Error_Code;
    } /* if */

    Bad_Index = -1;
    for (i=0; i<strlen(Temp_Specs); i++) {
	if (Temp_Specs[i] == '\n') Temp_Specs[i]=' ';
	if (isspace((int)Temp_Specs[i])) continue;
	Bad_Index=i;
	break;
    } /* if */

    if (Bad_Index != -1) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Parse_Job_Specs: Bad job specification input: %s\n", &Temp_Specs[Bad_Index]);
#else
	syslog(LOG_ERR, "Parse_Job_Specs: Bad job specification input: %s\n", &Temp_Specs[Bad_Index]);
#endif
	Error_Code = EINVAL;
    } /* if */

    free(Temp_Specs);
    return Error_Code;

cleanup:
    free(Temp_Specs);
    if (Job_Name[0])      free(Job_Name[0]);
    if (Req_Features[0])  free(Req_Features[0]);
    if (Req_Node_List[0]) free(Req_Node_List[0]);
    if (Req_Group[0])     free(Req_Group[0]);
    if (Req_Partition[0]) free(Req_Partition[0]);
} /* Parse_Job_Specs */
