/* 
 * Read_Config.c - Read the overall SLURM configuration file
 * See slurm.h for documentation on external functions and data structures
 *
 * Author: Moe Jette, jette@llnl.gov
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "slurm.h"

#define BUF_SIZE 1024
#define DEBUG_MODULE 0
#define DEBUG_SYSTEM 1
#define SEPCHARS " \n\t"

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
main(int argc, char * argv[]) {
    int Error_Code;
    char Out_Line[BUF_SIZE];

    if (argc < 2) {
	printf("Usage: %s <in_file>\n", argv[0]);
	exit(0);
    } /* if */

    Init_SLURM_Conf();
    Error_Code = Read_SLURM_Conf(argv[1]);
    if (Error_Code != 0) {
	printf("Error %d from Read_SLURM_Conf", Error_Code);
	exit(1);
    } /* if */

    printf("Administrators=%s\n", Administrators);
    printf("ControlMachine=%s\n", ControlMachine);
    printf("BackupController=%s\n", BackupController);
    printf("NodeSpecConf=%s\n", NodeSpecConf);
    printf("PartitionConf=%s\n", PartitionConf);
    printf("ControlDaemon=%s\n", ControlDaemon);
    printf("ServerDaemon=%s\n", ServerDaemon);

    exit(0);
} /* main */
#endif


/* 
 * Init_SLURM_Conf - Initialize the SLURM configuration values. This should be called 
 * before ever calling Read_SLURM_Conf.
 */
void Init_SLURM_Conf() {
    Administrators = NULL;
    ControlMachine = NULL;
    BackupController = NULL;
    NodeSpecConf = NULL;
    PartitionConf = NULL;
    ControlDaemon = NULL;
    ServerDaemon = NULL;
} /* Init_SLURM_Conf */


/*
 * Read_SLURM_Conf - Load the overall SLURM configuration from the specified file 
 * Call Init_SLURM_Conf before ever calling Read_SLURM_Conf.  
 * Read_SLURM_Conf can be called more than once if so desired.
 * Input: File_Name - Name of the file containing overall SLURM configuration information
 * Output: return - 0 if no error, otherwise an error code
 */
int Read_SLURM_Conf (char *File_Name) {
    FILE *SLURM_Spec_File;	/* Pointer to input data file */
    int Line_Num;		/* Line number in input file */
    char In_Line[BUF_SIZE];	/* Input line */
    char Scratch[BUF_SIZE];	/* Scratch area for parsing the input line */

    int Set_Admin, Set_ControlMach, Set_Backup, Set_NodeSpec, Set_PartSpec;
    int Set_ControlDaemon, Set_ServerDaemon;
    char *str_ptr1, *str_ptr2;
    int str_len;

    /* Initialization */
    SLURM_Spec_File = fopen(File_Name, "r");
    if (SLURM_Spec_File == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Read_SLURM_Conf error %d opening file %s\n", errno, File_Name);
#else
	syslog(LOG_ALERT, "Read_SLURM_Conf error %d opening file %s\n", errno, File_Name);
#endif
	return errno;
    } /* if */

    /* Process the data file */
    Line_Num = 0;
    while (fgets(In_Line, BUF_SIZE, SLURM_Spec_File) != NULL) {
	Line_Num++;
	if (strlen(In_Line) >= (BUF_SIZE-1)) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Read_SLURM_Conf line %d, of input file %s too long\n", 
		Line_Num, File_Name);
#else
	    syslog(LOG_ALERT, "Read_SLURM_Conf line %d, of input file %s too long\n", 
		Line_Num, File_Name);
#endif
	    return E2BIG;
	    break;
	} /* if */
	if (In_Line[0] == '#') continue;

	str_ptr1 = (char *)strstr(In_Line, "Administrators=");
	if (str_ptr1 != NULL) {
	    strcpy(Scratch, str_ptr1+15);
	    str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	    str_len = strlen(str_ptr2);
	    if (Administrators != NULL) free(Administrators);
	    Administrators = (char *)malloc(str_len+1);
	    if (Administrators == NULL) {
#if DEBUG_SYSTEM
		fprintf(stderr, "Read_SLURM_Conf: unable to allocate memory\n");
#else
		syslog(LOG_ALERT, "Read_SLURM_Conf: unable to allocate memory\n")
#endif
		return ENOMEM;
	    } /* if */
	    strcpy(Administrators, str_ptr2);
	} /* if */

	str_ptr1 = (char *)strstr(In_Line, "ControlMachine=");
	if (str_ptr1 != NULL) {
	    strcpy(Scratch, str_ptr1+15);
	    str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	    str_len = strlen(str_ptr2);
	    if (ControlMachine != NULL) free(ControlMachine);
	    ControlMachine = (char *)malloc(str_len+1);
	    if (ControlMachine == NULL) {
#if DEBUG_SYSTEM
		fprintf(stderr, "Read_SLURM_Conf: unable to allocate memory\n");
#else
		syslog(LOG_ALERT, "Read_SLURM_Conf: unable to allocate memory\n")
#endif
		return ENOMEM;
	    } /* if */
	    strcpy(ControlMachine, str_ptr2);
	} /* if */

	str_ptr1 = (char *)strstr(In_Line, "BackupController=");
	if (str_ptr1 != NULL) {
	    strcpy(Scratch, str_ptr1+17);
	    str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	    str_len = strlen(str_ptr2);
	    if (BackupController != NULL) free(ControlMachine);
	    BackupController = (char *)malloc(str_len+1);
	    if (BackupController == NULL) {
#if DEBUG_SYSTEM
		fprintf(stderr, "Read_SLURM_Conf: unable to allocate memory\n");
#else
		syslog(LOG_ALERT, "Read_SLURM_Conf: unable to allocate memory\n")
#endif
		return ENOMEM;
	    } /* if */
	    strcpy(BackupController, str_ptr2);
	} /* if */

	str_ptr1 = (char *)strstr(In_Line, "NodeSpecConf=");
	if (str_ptr1 != NULL) {
	    strcpy(Scratch, str_ptr1+13);
	    str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	    str_len = strlen(str_ptr2);
	    if (NodeSpecConf != NULL) free(NodeSpecConf);
	    NodeSpecConf = (char *)malloc(str_len+1);
	    if (NodeSpecConf == NULL) {
#if DEBUG_SYSTEM
		fprintf(stderr, "Read_SLURM_Conf: unable to allocate memory\n");
#else
		syslog(LOG_ALERT, "Read_SLURM_Conf: unable to allocate memory\n")
#endif
		return ENOMEM;
	    } /* if */
	    strcpy(NodeSpecConf, str_ptr2);
	} /* if */

	str_ptr1 = (char *)strstr(In_Line, "PartitionConf=");
	if (str_ptr1 != NULL) {
	    strcpy(Scratch, str_ptr1+14);
	    str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	    str_len = strlen(str_ptr2);
	    if (PartitionConf != NULL) free(PartitionConf);
	    PartitionConf = (char *)malloc(str_len+1);
	    if (PartitionConf == NULL) {
#if DEBUG_SYSTEM
		fprintf(stderr, "Read_SLURM_Conf: unable to allocate memory\n");
#else
		syslog(LOG_ALERT, "Read_SLURM_Conf: unable to allocate memory\n")
#endif
		return ENOMEM;
	    } /* if */
	    strcpy(PartitionConf, str_ptr2);
	} /* if */

	str_ptr1 = (char *)strstr(In_Line, "ControlDaemon=");
	if (str_ptr1 != NULL) {
	    strcpy(Scratch, str_ptr1+14);
	    str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	    str_len = strlen(str_ptr2);
	    if (ControlDaemon != NULL) free(ControlDaemon);
	    ControlDaemon = (char *)malloc(str_len+1);
	    if (ControlDaemon == NULL) {
#if DEBUG_SYSTEM
		fprintf(stderr, "Read_SLURM_Conf: unable to allocate memory\n");
#else
		syslog(LOG_ALERT, "Read_SLURM_Conf: unable to allocate memory\n")
#endif
		return ENOMEM;
	    } /* if */
	    strcpy(ControlDaemon, str_ptr2);
	} /* if */

	str_ptr1 = (char *)strstr(In_Line, "ServerDaemon=");
	if (str_ptr1 != NULL) {
	    strcpy(Scratch, str_ptr1+13);
	    str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	    str_len = strlen(str_ptr2);
	    if (ServerDaemon != NULL) free(ServerDaemon);
	    ServerDaemon = (char *)malloc(str_len+1);
	    if (ServerDaemon == NULL) {
#if DEBUG_SYSTEM
		fprintf(stderr, "Read_SLURM_Conf: unable to allocate memory\n");
#else
		syslog(LOG_ALERT, "Read_SLURM_Conf: unable to allocate memory\n")
#endif
		return ENOMEM;
	    } /* if */
	    strcpy(ServerDaemon, str_ptr2);
	} /* if */
    } /* while */

    /* Termination */
    if (fclose(SLURM_Spec_File) != 0) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Read_SLURM_Conf error %d closing file %s\n", errno, File_Name);
#else
	syslog(LOG_NOTICE, "Read_SLURM_Conf error %d closing file %s\n", errno, File_Name);
#endif
	return errno;
    } /* if */

    /* If values not set in configuration file, set defaults */
    if (Administrators == NULL) {
	Administrators = (char *)malloc(5);
	if (Administrators == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Read_SLURM_Conf: unable to allocate memory\n");
#else
	    syslog(LOG_ALERT, "Read_SLURM_Conf: unable to allocate memory\n")
#endif
	    return ENOMEM;
	} /* if */
	strcpy(Administrators, "root");
    } /* if */

    if (ControlMachine == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Read_SLURM_Conf: ControlMachine value not specified.\n");
#else
	syslog(LOG_ALERT, "Read_SLURM_Conf: ControlMachine value not specified.\n")
#endif
	return EINVAL;
    } /* if */

    if (BackupController == NULL) {
	BackupController = (char *)malloc(1);
	if (BackupController == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Read_SLURM_Conf: unable to allocate memory\n");
#else
	    syslog(LOG_ALERT, "Read_SLURM_Conf: unable to allocate memory\n")
#endif
	    return ENOMEM;
	} /* if */
	strcpy(BackupController, "");
    } /* if */

    if (NodeSpecConf == NULL)  {
	NodeSpecConf = (char *)malloc(strlen(DEFAULT_NODE_SPEC_CONF)+1);
	if (NodeSpecConf == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Read_SLURM_Conf: unable to allocate memory\n");
#else
	    syslog(LOG_ALERT, "Read_SLURM_Conf: unable to allocate memory\n")
#endif
	    return ENOMEM;
	} /* if */
	strcpy(NodeSpecConf, DEFAULT_NODE_SPEC_CONF);
    } /* if */

    if (PartitionConf == NULL) {
	PartitionConf = (char *)malloc(strlen(DEFAULT_PARTITION_CONF)+1);
	if (PartitionConf == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Read_SLURM_Conf: unable to allocate memory\n");
#else
	    syslog(LOG_ALERT, "Read_SLURM_Conf: unable to allocate memory\n")
#endif
	    return ENOMEM;
	} /* if */
	strcpy(PartitionConf, DEFAULT_PARTITION_CONF);
    } /* if */

    if (ControlDaemon == NULL) {
	ControlDaemon = (char *)malloc(strlen(DEFAULT_CONTROL_DAEMON)+1);
	if (ControlDaemon == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Read_SLURM_Conf: unable to allocate memory\n");
#else
	    syslog(LOG_ALERT, "Read_SLURM_Conf: unable to allocate memory\n")
#endif
	    return ENOMEM;
	} /* if */
	strcpy(ControlDaemon, DEFAULT_CONTROL_DAEMON);
    } /* if */


    if (ServerDaemon == NULL) {
	ServerDaemon = (char *)malloc(strlen(DEFAULT_SERVER_DAEMON)+1);
	if (ServerDaemon == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Read_SLURM_Conf: unable to allocate memory\n");
#else
	    syslog(LOG_ALERT, "Read_SLURM_Conf: unable to allocate memory\n")
#endif
	    return ENOMEM;
	} /* if */
	strcpy(ServerDaemon, DEFAULT_SERVER_DAEMON);
    } /* if */



    return 0;
} /* Read_SLURM_Conf */
