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

#include "config.h"
#include "list.h"
#include "slurm.h"

#define BUF_SIZE 1024
#define DEBUG_MODULE 1
#define SEPCHARS " \n\t"

extern List   Node_Record_List;		/* List of Node_Records */
extern List   Part_Record_List;		/* List of Part_Records */

#if DEBUG_MODULE
/* main is used here for testing purposes only */
main(int argc, char * argv[]) {
    int Error_Code, Line_Num;
    char In_Line[BUF_SIZE];	/* Input line */
    char *Node_List;		/* List of nodes assigned to the job */
    FILE *Job_Spec_File;	/* Pointer to input data file */

    if (argc < 4) {
	printf("Usage: %s <node_in_file> <part_in_file> <job_specs_file>\n", argv[0]);
	exit(0);
    } /* if */

    Error_Code = Read_Node_Spec_Conf(argv[1]);
    if (Error_Code != 0) {
	printf("Error %d from Read_Node_Spec_Conf\n", Error_Code);
	exit(1);
    } /* if */

    Error_Code = Read_Part_Spec_Conf(argv[2]);
    if (Error_Code != 0) {
	printf("Error %d from Read_Part_Spec_Conf", Error_Code);
	exit(1);
    } /* if */

    Job_Spec_File = fopen(argv[3], "r");
    if (Job_Spec_File == NULL) {
	fprintf(stderr, "Read_Node_Spec_Conf: error %d opening file %s\n", errno, argv[3]);
	exit(1);
    } /* if */

    Line_Num = 0;
    while (fgets(In_Line, BUF_SIZE, Job_Spec_File) != NULL) {
	Line_Num++;
	if (strlen(In_Line) >= (BUF_SIZE-1)) {
	    fprintf(stderr, "Controller: line %d, of input file %s too long\n", 
		Line_Num, argv[3]);
	    exit(1);
	} /* if */

	printf("%s\n", In_Line);
	Node_List = Will_Job_Run(In_Line);
	if (Node_List == (char *)NULL) {
	    printf("  Job can not be scheduled at this time \n\n");
	} else {
	    printf("  Job scheduled on this nodes:\n  %s\n\n", Node_List);
	    free(Node_List);
	} /* else */
    } /* while */

    exit(0);
} /* main */
#endif

/*
 * Will_Job_Run - Determine if the given job specification can be initiated now
 * Input: Job_Spec - Specifications for the job
 * Output: Returns node list, NULL if can not be initiated
 *
 * NOTE: The value returned MUST be freed to avoid memory leak
 */
char *Will_Job_Run(char *Specification) {

} /* Will_Job_Run */
