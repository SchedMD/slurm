/* 
 * controller.c - Main control machine daemon for SLURM
 * See slurm.h for documentation on external functions and data structures
 *
 * NOTE: DEBUG_MODULE of read_config requires that it be loaded with 
 *       bits_bytes, partition_mgr, read_config, and node_mgr
 *
 * Author: Moe Jette, jette@llnl.gov
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

#include "slurm.h"

#define BUF_SIZE 1024

int Msg_From_Root(void);

#if DEBUG_MODULE
/* main is used here for testing purposes only */
main(int argc, char * argv[]) {
    int Error_Code, Line_Num, i;
    FILE *Command_File;
    char In_Line[BUF_SIZE], Node_Name[MAX_NAME_LEN];
    int CPUs, RealMemory, TmpDisk;
    char *NodeName, *PartName, *TimeStamp;
    time_t Last_Update;
    clock_t Start_Time;
    char *Dump;
    int Dump_Size;

    if (argc < 3) {
	printf("Usage: %s <slurm_conf_file> <command_file>\n", argv[0]);
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

    /* Mark everything up and idle for testing */
    for (i=0; i<Node_Record_Count; i++) {
	BitMapSet(Idle_NodeBitMap, i);
	BitMapSet(Up_NodeBitMap, i);
    } /* for */

    Error_Code = gethostname(Node_Name, MAX_NAME_LEN);
    if (Error_Code != 0) {
	fprintf(stderr, "controller: Error %d from gethostname\n", Error_Code);
    } /* if */
    if (strcmp(Node_Name, ControlMachine) != 0) {
	printf("controller: This machine (%s) is not the primary control machine (%s)\n", 
		Node_Name, ControlMachine);
	exit(1);
    } /* if */

    Command_File = fopen(argv[2], "r");
    if (Command_File == NULL) {
	fprintf(stderr, "controller: error %d opening command file %s\n", 
		errno, argv[2]);
	exit(1);
    } /* if */

    Line_Num = 0;
    while (fgets(In_Line, BUF_SIZE, Command_File)) {
	Line_Num++;
	if (strlen(In_Line) >= (BUF_SIZE-1)) {
	    fprintf(stderr, "controller: line %d, of input file %s too long\n", 
		Line_Num, argv[2]);
	    exit(1);
	} /* if */

	/* Allocate:  Allocate resources for a job */
	if (strncmp("Allocate",   In_Line,  8) == 0) {	
	    printf("\n\nAllocate resources for a job\n");
	    Start_Time = clock();
	    NodeName = NULL;
	    Error_Code = Select_Nodes(&In_Line[8], &NodeName);   /* Skip over "Allocate" */
	    if (Error_Code)
		printf("Error %d allocating resources for %s", Error_Code, &In_Line[8]);
	    else 
		printf("Allocated nodes %s to job %s", NodeName, &In_Line[8]);
	    if (NodeName) free(NodeName);
	    printf("Time = %ld usec\n", (long)(clock() - Start_Time));

	/* DumpNode:  Dump node state information to a buffer */
	} else if (strncmp("DumpNode",    In_Line,  8) == 0) {	
	    printf("\n\nDumping node information\n");
	    Start_Time = clock();
	    TimeStamp = NULL;
	    Error_Code = Load_String(&TimeStamp, "LastUpdate=", In_Line);
	    if (TimeStamp) {
		Last_Update = strtol(TimeStamp, (char **)NULL, 10);
		free(TimeStamp);
	    } else 
		Last_Update = (time_t) 0;
	    Error_Code = Dump_Node(&Dump, &Dump_Size, &Last_Update);
	    printf("Pass %d bytes of data to the user now\n", Dump_Size);
	    if (Dump) free(Dump);
	    printf("Time = %ld usec\n", (long)(clock() - Start_Time));

	/* DumpPart:  Dump partition state information to a buffer */
	} else if (strncmp("DumpPart",    In_Line,  8) == 0) {	
	    printf("\n\nDumping partition information\n");
	    Start_Time = clock();
	    TimeStamp = NULL;
	    Error_Code = Load_String(&TimeStamp, "LastUpdate=", In_Line);
	    if (TimeStamp) {
		Last_Update = strtol(TimeStamp, (char **)NULL, 10);
		free(TimeStamp);
	    } else 
		Last_Update = (time_t) 0;
	    Error_Code = Dump_Part(&Dump, &Dump_Size, &Last_Update);
	    printf("Pass %d bytes of data to the user now\n", Dump_Size);
	    if (Dump) free(Dump);
	    printf("Time = %ld usec\n", (long)(clock() - Start_Time));

	/* JobSubmit:  Submit job to execute */
	} else if (strncmp("JobSubmit",   In_Line,  9) == 0) {	
	    printf("\n\nJob submitted for execution\n");
	    continue;
/* TBD */

	/* JobWillRun:   Will job run if submitted */
	} else if (strncmp("JobWillRun",  In_Line, 10) == 0) {	
	    printf("\n\nTesting if job could run\n");
	    continue;
/* TBD */

	/* NodeConfig: Process node configuration state on check-in */
	} else if ((strncmp("NodeConfig",  In_Line, 10) == 0) && Msg_From_Root()) {
	    printf("\n\nNode check-in: %s\n", In_Line);
	    Start_Time = clock();
	    NodeName = NULL;
	    Error_Code  = Load_String (&NodeName,   "NodeName=",   In_Line);
	    if (NodeName == NULL) {
		if (Error_Code == 0) Error_Code =EINVAL;
		printf("\n\nNodeConfig: ERROR No NodeName specified\n");
	    } else
		printf("\n\nNodeConfig for NodeName %s\n", NodeName);
	    if (Error_Code == 0) Error_Code = Load_Integer(&CPUs,       "CPUs=",       In_Line);
	    if (Error_Code == 0) Error_Code = Load_Integer(&RealMemory, "RealMemory=", In_Line);
	    if (Error_Code == 0) Error_Code = Load_Integer(&TmpDisk,    "TmpDisk=",    In_Line);
	    if (Error_Code == 0) Error_Code = Validate_Node_Specs(NodeName,CPUs,RealMemory,TmpDisk);
	    if (Error_Code) printf("Error %d on NodeConfig with NodeName %s\n", 
			Error_Code, NodeName);
	    printf("Time = %ld usec\n", (long)(clock() - Start_Time));
	    if (NodeName) free(NodeName);

	/* Reconfigure:  Re-read configuration files */
	} else if ((strncmp("Reconfigure", In_Line, 11) == 0) && Msg_From_Root()) {
	    printf("\n\nReconfigure per operator command\n");
	    Start_Time = clock();
	    Error_Code = Init_SLURM_Conf();
	    if (Error_Code != 0) exit(Error_Code);
	    Error_Code = Read_SLURM_Conf(argv[1]);
	    if (Error_Code) printf("Error %d from Read_SLURM_Conf\n", Error_Code);
	    printf("Time = %ld usec\n", (long)(clock() - Start_Time));

	/* Shutdown:  Shutdown controller */
	} else if ((strncmp("Shutdown",    In_Line,  8) == 0) && Msg_From_Root()) {	
	    printf("\n\nShutdown per operator command\n");
	    exit(0);

	/* Update:   Modify the configuration of a job, node, or partition */
	} else if (strncmp("Update",  In_Line, 6) == 0)  {	
	    Start_Time = clock();
	    NodeName = PartName = NULL;
	    Error_Code = Load_String(&NodeName, "NodeName=", In_Line);
	    if ((Error_Code == 0) && (NodeName != NULL) && Msg_From_Root()) {
		printf("\n\nUpdate of Node %s per operator command\n", NodeName);
		Error_Code = Update_Node(NodeName, &In_Line[6]);  /* Skip over "Update" */
		if (Error_Code) printf("Error %d from Update_Node on %s\n", Error_Code, NodeName);
		printf("Time = %ld usec\n", (long)(clock() - Start_Time));
		free(NodeName);
		continue;
	    } /* if */
	    Error_Code = Load_String(&PartName, "PartitionName=", In_Line);
	    if ((Error_Code == 0) && (PartName != NULL) && Msg_From_Root()) {
		printf("\n\nUpdate of Partition %s per operator command\n", PartName);
		Error_Code = Update_Part(PartName, &In_Line[6]);  /* Skip over "Update" */
		if (Error_Code) printf("Error %d from Update_Part on %s\n", Error_Code, PartName);
		printf("Time = %ld usec\n", (long)(clock() - Start_Time));
		free(PartName);
		continue;
	    } /* if */

	} else {
	    printf("\n\nInvalid input: %s\n", In_Line);
	} /* if */
    } /* while */

    exit(0);
} /* main */
#endif


/* 
 * Msg_From_Root - Determine if a message is from user root
 * Output: Returns 1 if the message received is from user root, otherwise 0
 * NOTE: Must be modified once communications infrastructure established
 */
int Msg_From_Root(void) {
    return 1;
} /* Msg_From_Root */

