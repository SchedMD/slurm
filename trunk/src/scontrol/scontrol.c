/*
 * scontrol - Administration tool for SLURM. 
 * Provides interface to read, write, update, and configurations.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "slurmlib.h"

#define	BUF_SIZE 1024
#define	MAX_INPUT_FIELDS 128

char *Command_Name;
int Exit_Flag;		/* Program to terminate if =1 */
int Quiet_Flag;		/* Quiet=1, verbose=-1, normal=0 */
int Input_Words;	/* Number of words of input permitted */

void Dump_Command(int argc, char *argv[]);
int Get_Command(int *argc, char *argv[]);
void Print_Node(char *node_name);
void Print_Node_List(char *node_list);
void Print_Part(char *partition_name);
int Process_Command(int argc, char *argv[]);
void Usage();

main(int argc, char *argv[]) {
    int Error_Code, i, Input_Field_Count;
    char **Input_Fields;

    Command_Name = argv[0];
    Exit_Flag = 0;
    Input_Field_Count = 0;
    Quiet_Flag = 0;
    if (argc > MAX_INPUT_FIELDS)	/* Bogus input, but let's continue anyway */
	Input_Words = argc;
    else
	Input_Words = 128;
    Input_Fields = (char **)malloc(sizeof(char *) * Input_Words);
    for (i=1; i<argc; i++) {
	if (strcmp(argv[i], "-q") == 0) {
	    Quiet_Flag =  1;
	} else if (strcmp(argv[i], "quiet") == 0) {
	    Quiet_Flag = 1;
	} else if (strcmp(argv[i], "-v") == 0) {
	    Quiet_Flag = -1;
	} else if (strcmp(argv[i], "verbose") == 0) {
	    Quiet_Flag = -1;
	} else {
	    Input_Fields[Input_Field_Count++] = argv[i];
	} /* else */
    } /* for */

    if (Input_Field_Count)
	Exit_Flag = 1;
    else
	Error_Code = Get_Command(&Input_Field_Count, Input_Fields);

    while (1) {
#if DEBUG_MODULE
	Dump_Command(Input_Field_Count, Input_Fields);
#endif
	Error_Code = Process_Command(Input_Field_Count, Input_Fields);
	if (Error_Code != 0) break;
	if (Exit_Flag == 1) break;
	Error_Code = Get_Command(&Input_Field_Count, Input_Fields);
	if (Error_Code != 0) break;
    } /* while */

    exit(Error_Code);
} /* main */


/*
 * Dump_Command - Dump the user's command
 * Input: argc - count of arguments
 *        argv - the arguments
 */
void Dump_Command(int argc, char *argv[]) {
    int i;

    for (i=0; i<argc; i++) {
	printf("Arg %d:%s:\n", i, argv[i]);
    } /* for */
} /* Dump_Command */


/*
 * Get_Command - Get a command from the user
 * Input: argc - location to store count of arguments
 *        argv - location to store the argument list
 * Output: returns error code, 0 if no problems
 */
int Get_Command(int *argc, char **argv) {
    static char *In_Line;
    static int In_Line_Size = 0;
    int In_Line_Pos = 0;
    int Temp_Char, i;

    if (In_Line_Size == 0) {
	In_Line_Size += BUF_SIZE;
	In_Line = (char *)malloc(In_Line_Size);
	if (In_Line == NULL) {
	    fprintf(stderr, "%s: Error %d allocating memory\n", Command_Name, errno);
	    In_Line_Size = 0;
	    return ENOMEM;
	} /* if */
    } /* if */
	
    printf("scontrol: ");
    *argc = 0;
    In_Line_Pos = 0;

    while (1) {
	Temp_Char = getc(stdin);
	if (Temp_Char == EOF) break;
	if (Temp_Char == (int)'\n') break;
	if ((In_Line_Pos+2) >= In_Line_Size) {
	    In_Line_Size += BUF_SIZE;
	    In_Line = (char *)realloc(In_Line, In_Line_Size);
	    if (In_Line == NULL) {
		fprintf(stderr, "%s: Error %d allocating memory\n", Command_Name, errno);
		In_Line_Size = 0;
		return ENOMEM;
	    } /* if */
	} /* if */
	In_Line[In_Line_Pos++] = (char)Temp_Char;
    } /* while */
    In_Line[In_Line_Pos] = (char)NULL;

    for (i=0; i<In_Line_Pos; i++) {
	if (isspace((int)In_Line[i])) continue;
	if (((*argc)+1) > MAX_INPUT_FIELDS) {	/* Really bogus input line */
	    fprintf(stderr, "%s: Over %d fields in line: %s\n", Command_Name, Input_Words ,In_Line);
	    return E2BIG;
	} /* if */
	argv[(*argc)++] = &In_Line[i];
	for (i++ ; i<In_Line_Pos; i++) {
	    if (!isspace((int)In_Line[i])) continue;
	    In_Line[i] = (char)NULL;
	    break;
	} /* for */
    } /* for */
    return 0;
} /* Get_Command */


/*
 * Print_Node - Print the specified node's information
 * Input: node_name - NULL to print all node information
 */
void Print_Node(char *node_name) {
    int Error_Code, size, i;
    char Partition[MAX_NAME_LEN], Node_State[MAX_NAME_LEN], Features[FEATURE_SIZE];
    char Req_Name[MAX_NAME_LEN];	/* Name of the partition */
    char Next_Name[MAX_NAME_LEN];	/* Name of the next partition */
    int CPUs, RealMemory, TmpDisk, Weight;
    char *Dump;
    int Dump_Size;
    time_t Update_Time;
    unsigned *NodeBitMap;	/* Bitmap of nodes in partition */
    int BitMapSize;		/* Bytes in NodeBitMap */

    if (node_name) 
	strncpy(Req_Name, node_name, MAX_NAME_LEN);
    else
	strcpy(Req_Name, "");	/* Start at beginning of node list */

    for (i=1; ;i++) {
	Error_Code = Load_Node_Config(Req_Name, Next_Name, &CPUs, &RealMemory, &TmpDisk, &Weight, 
	    Features, Partition, Node_State);
	if (Error_Code != 0)  {
	    printf("Load_Node_Config error %d on %s\n", Error_Code, Req_Name);
	    break;
	} /* if */
	if (Error_Code != 0)  {
	    if (Quiet_Flag != 1) {
		if (Error_Code == ENOENT) 
		    printf("No node %s found\n", Req_Name);
		else
		    printf("Error %d finding information for node %s\n", Error_Code, Req_Name);
	    } /* if */
	    break;
	} /* if */
	printf("NodeName=%s CPUs=%d RealMemory=%d TmpDisk=%d ", 
		Req_Name, CPUs, RealMemory, TmpDisk);
	printf("State=%s Weight=%d Features=%s Partition=%s\n", 
	  	Node_State, Weight, Features, Partition);

	if (node_name || (strlen(Next_Name) == 0)) break;
	strcpy(Req_Name, Next_Name);
    } /* while */
} /* Print_Node */


/*
 * Print_Node_List - Print information about the supplied node list (or regular expression)
 * Input: node_list - Print information about the supplied node list (or regular expression)
 */
void Print_Node_List(char *node_list) {
    static time_t Last_Update_Time = (time_t)NULL;
    int Start_Inx, End_Inx, Count_Inx, Error_Code, i;
    char *str_ptr1, *str_ptr2, *Format, *My_Node_List, This_Node_Name[BUF_SIZE];;

    Error_Code = Load_Node(&Last_Update_Time);
    if (Error_Code) {
	if (Quiet_Flag != 1) printf("Load_Node error %d\n", Error_Code);
	return;
    } /* if */
    if (Quiet_Flag == -1) printf("Last_Update_Time=%ld\n", (long)Last_Update_Time);

    My_Node_List = malloc(strlen(node_list)+1);
    if (My_Node_List == NULL) {
	if (Quiet_Flag != 1) fprintf(stderr, "Unable to allocate memory\n");
	abort();
    } /* if */

    strcpy(My_Node_List, node_list);
    str_ptr2 = (char *)strtok_r(My_Node_List, ",", &str_ptr1);
    while (str_ptr2) {	/* Break apart by comma separators */
	Error_Code = Parse_Node_Name(str_ptr2, &Format, &Start_Inx, &End_Inx, &Count_Inx);
	if (Error_Code) {
	    if (Quiet_Flag != 1) fprintf(stderr, "Invalid node name specification: %s\n", str_ptr2);
	    break;
	} /* if */ 
	if (strlen(Format) >= sizeof(This_Node_Name)) {
	    if (Quiet_Flag != 1) fprintf(stderr, "Invalid node name specification: %s\n", Format);
	    free(Format);
	    break;
	} /* if */
	for (i=Start_Inx; i<=End_Inx; i++) {
	    if (Count_Inx == 0) 
		strncpy(This_Node_Name, Format, sizeof(This_Node_Name));
	    else
		sprintf(This_Node_Name, Format, i);
	    Print_Node(This_Node_Name);
	} /* for */
	free(Format);
	str_ptr2 = (char *)strtok_r(NULL, ",", &str_ptr1);
    } /* while */

    free(My_Node_List);
/*  Free_Node_Info();		Keep data for reuse, cleaned on exit */
    return;
} /* Print_Node_List */


/*
 * Print_Part - Print the specified partition's information
 * Input: partition_name - NULL to print all partition information
 */
void Print_Part(char *partition_name) {
    static time_t Last_Update_Time  = (time_t)NULL;	/* Time desired for data */
    char Req_Name[MAX_NAME_LEN];	/* Name of the partition */
    char Next_Name[MAX_NAME_LEN];	/* Name of the next partition */
    int MaxTime;			/* -1 if unlimited */
    int MaxNodes;			/* -1 if unlimited */
    int TotalNodes;			/* Total number of nodes in the partition */
    int TotalCPUs;			/* Total number of CPUs in the partition */
    char Nodes[FEATURE_SIZE];		/* Names of nodes in partition */
    char AllowGroups[FEATURE_SIZE];	/* NULL indicates ALL */
    int Key;    	 		/* 1 if SLURM distributed key is required for use of partition */
    int StateUp;			/* 1 if state is UP */
    int Shared;				/* 1 if partition can be shared */
    int Default;			/* 1 if default partition */
    int Error_Code;

    Error_Code = Load_Part(&Last_Update_Time);
    if (Error_Code) {
	if (Quiet_Flag != 1) printf("Load_Part error %d\n", Error_Code);
	return;
    } /* if */
    if (Quiet_Flag == -1) printf("Last_Update_Time=%ld\n", (long)Last_Update_Time);

    if (partition_name) 
	strncpy(Req_Name, partition_name, MAX_NAME_LEN);
    else
	strcpy(Req_Name, "");	/* Start at beginning of partition list */

    while (Error_Code == 0) {
	Error_Code = Load_Part_Name(Req_Name, Next_Name, &MaxTime, &MaxNodes, 
	    &TotalNodes, &TotalCPUs, &Key, &StateUp, &Shared, &Default, 
	    Nodes, AllowGroups);
	if (Error_Code != 0)  {
	    if (Quiet_Flag != 1) {
		if (Error_Code == ENOENT) 
		    printf("No partition %s found\n", Req_Name);
		else
		    printf("Error %d finding information for partition %s\n", Error_Code, Req_Name);
	    } /* if */
	    break;
	} /* if */

	printf("PartitionName=%s Nodes=%s  MaxTime=%d  MaxNodes=%d Default=%d ", 
	    Req_Name, Nodes, MaxTime, MaxNodes, Default);
	printf("TotalNodes=%d TotalCPUs=%d Key=%d StateUp=%d Shared=%d AllowGroups=%s\n", 
	    TotalNodes, TotalCPUs, Key, StateUp, Shared, AllowGroups);

	if (partition_name || (strlen(Next_Name) == 0)) break;
	strcpy(Req_Name, Next_Name);
    } /* while */
/*  Free_Part_Info(); 	Keep data for reuse, cleaned on exit */
} /* Print_Part */


/*
 * Process_Command - Process the user's command
 * Input: argc - count of arguments
 *        argv - the arguments
 * Ourput: Return code is 0 or errno (ONLY for errors fatal to scontrol)
 */
int Process_Command(int argc, char *argv[]) {

    if ((strcmp(argv[0], "exit") == 0) || 
        (strcmp(argv[0], "quit") == 0)) {
	if (argc > 1) fprintf(stderr, "Too many arguments for keyword:%s\n", argv[0]);
	Exit_Flag = 1;

    } else if (strcmp(argv[0], "help") == 0) {
	if (argc > 1) fprintf(stderr, "Too many arguments for keyword:%s\n", argv[0]);
	Usage();

    } else if (strcmp(argv[0], "quiet") == 0) {
	if (argc > 1)  fprintf(stderr, "Too many arguments for keyword:%s\n", argv[0]);
	Quiet_Flag = 1;

    } else if (strcmp(argv[0], "reconfigure") == 0) {
	if (argc > 2) fprintf(stderr, "Too many arguments for keyword:%s\n", argv[0]);
	printf("%s keyword not yet implemented\n", argv[0]);

    } else if (strcmp(argv[0], "show") == 0) {
	if (argc > 3) {
	    if (Quiet_Flag != 1) fprintf(stderr, "Too many arguments for keyword:%s\n", argv[0]);
	} else if (argc < 2) {
	    if (Quiet_Flag != 1) fprintf(stderr, "Too few arguments for keyword:%s\n", argv[0]);
	} else if (strncmp(argv[1],"jobs", 3) == 0) {
	    if (Quiet_Flag != 1) printf("keyword:%s entity:%s command not yet implemented\n", argv[0], argv[1]);
	} else if (strncmp(argv[1],"nodes", 3) == 0) {
	    if (argc > 2) 
		Print_Node_List(argv[2]);
	    else
		Print_Node(NULL);
	} else if (strncmp(argv[1],"partitions", 3) == 0) {
	    if (argc > 2) 
		Print_Part(argv[2]);
	    else
		Print_Part(NULL);
	} else if (Quiet_Flag != 1) {
	    fprintf(stderr, "Invalid entity:%s for keyword:%s \n", argv[1], argv[0]);
	} /* if */

    } else if (strcmp(argv[0], "update") == 0) {
	if (argc < 3) {
	    fprintf(stderr, "Too few arguments for %s keyword\n", argv[0]);
	    return 0;
	} /* if */
	if ((strcmp(argv[1],"job") != 0)  && 
	    (strcmp(argv[1],"node") != 0) && (strcmp(argv[1],"partition") != 0)) {
	    fprintf(stderr, "Invalid entity %s for %s keyword\n", argv[1], argv[0]);
	    return 0;
	} /* if */
	printf("%s keyword not yet implemented\n", argv[0]);

    } else if (strcmp(argv[0], "upload") == 0) {
	if (argc > 2) {
	    fprintf(stderr, "Too many arguments for %s keyword\n", argv[0]);
	    return 0;
	} /* if */
	printf("%s keyword not yet implemented\n", argv[0]);

    } else if (strcmp(argv[0], "verbose") == 0) {
	if (argc > 1) {
	    fprintf(stderr, "Too many arguments for %s keyword\n", argv[0]);
	} /* if */
	Quiet_Flag = -1;

    } else if (strcmp(argv[0], "version") == 0) {
	if (argc > 1) {
	    fprintf(stderr, "Too many arguments for %s keyword\n", argv[0]);
	} /* if */
	printf("%s version 0.1\n", Command_Name);

    } else
	fprintf(stderr, "Invalid keyword: %s\n", argv[0]);

    return 0;
} /* Process_Command */


/* Usage - Show the valid scontrol commands */
void Usage() {
    printf("%s [-q | -v] [<keyword>]\n", Command_Name);
    printf("    -q is equivalent to the keyword \"quiet\" described below.\n");
    printf("    -v is equivalent to the keyword \"verbose\" described below.\n");
    printf("    <keyword> may be omitted from the execute line and %s will execute in interactive\n");
    printf("     mode to process multiple keywords (i.e. commands). Valid <entity> values are: job,\n");
    printf("     node, and partition. Valid <keyword> values are:\n\n");
    printf("     exit                         Terminate this command.\n");
    printf("     help                         Print this description of use.\n");
    printf("     quiet                        Print no messages other than error messages.\n");
    printf("     quit                         Terminate this command.\n");
    printf("     reconfigure [<NodeName>]     Re-read configuration files, default is all nodes.\n");
    printf("     show <entity> [<ID>]         Display state of identified entity, default is all records.\n");
    printf("     update <entity> <options>    Update state of identified entity.\n");
    printf("     upload [<NodeName>]          Upload node configuration, default is from all nodes.\n");
    printf("     verbose                      Enable detailed logging.\n");
    printf("     version                      Display tool version number.\n");
} /* Usage */
