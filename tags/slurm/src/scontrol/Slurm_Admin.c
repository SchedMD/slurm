/*
 * Slurm_Admin - Administration tool for SLURM. 
 * Provides interface to read, write, update, and configurations.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define BUF_SIZE 1024
#define DEBUG_MODULE 1
#define MAX_INPUT_FIELDS 50

char *Command_Name;
int Exit_Flag;		/* Program to terminate if =1 */
int Quiet_Flag;		/* Quiet=1, verbose=-1, normal=0 */

void Dump_Command(int argc, char *argv[]);
int Get_Command(int *argc, char *argv[]);
int Process_Command(int argc, char *argv[]);
void Usage();

main(int argc, char *argv[]) {
    int i, j;
    int Error_Code;
    int Input_Field_Count;
    char *Input_Fields[MAX_INPUT_FIELDS];

    Command_Name = argv[0];
    Exit_Flag = 0;
    Input_Field_Count = 0;
    Quiet_Flag = 0;

    if ((argc-1) > MAX_INPUT_FIELDS) {
	fprintf(stderr, "%s error: No more than %d input fields permitted\n", Command_Name, MAX_INPUT_FIELDS);
	exit(E2BIG);
    } /* if */

    if (argc > 1) {
	if (strcmp(argv[1], "-q") == 0) {
	    Quiet_Flag =  1;
	    i = 2;
	} else if (strcmp(argv[1], "-v") == 0) {
	    Quiet_Flag = -1;
	    i = 2;
	} else {
	    i = 1;
	} /* else */

	Input_Field_Count = argc - i;
	if (Input_Field_Count > 0) Exit_Flag=1;
	for (j=i; j<argc; j++) {
	    Input_Fields[j-i] = argv[j];
	} /* for */
    } /* if */
    if (Input_Field_Count == 0) Error_Code=Get_Command(&Input_Field_Count, Input_Fields);

    while (1) {
Dump_Command(Input_Field_Count, Input_Fields);
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
int Get_Command(int *argc, char *argv[]) {
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
	    return errno;
	} /* if */
    } /* if */
	
    printf("slurm_admin: ");
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
		return errno;
	    } /* if */
	} /* if */
	In_Line[In_Line_Pos++] = (char)Temp_Char;
    } /* while */
    In_Line[In_Line_Pos] = (char)NULL;

    for (i=0; i<In_Line_Pos; i++) {
	if (isspace((int)In_Line[i])) continue;
	if (((*argc)+1) > MAX_INPUT_FIELDS) {
	    fprintf(stderr, "%s: Over %d fields in line: %s\n", Command_Name, MAX_INPUT_FIELDS ,In_Line);
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
 * Process_Command - Process the user's command
 * Input: argc - count of arguments
 *        argv - the arguments
 * Ourput: Return code is 0 or errno (ONLY for errors fatal to Slurm_Admin)
 */
int Process_Command(int argc, char *argv[]) {

    if ((strcmp(argv[0], "exit") == 0) || 
        (strcmp(argv[0], "quit") == 0)) {
	if (argc > 1) {
	    fprintf(stderr, "Too many arguments for %s keyword\n", argv[0]);
	} /* if */
	Exit_Flag = 1;
	return 0;
    } /* if exit */

   if (strcmp(argv[0], "help") == 0) {
	if (argc > 1) {
	    fprintf(stderr, "Too many arguments for %s keyword\n", argv[0]);
	} /* if */
	Usage();
	return 0;
    } /* if */

    if (strcmp(argv[0], "quiet") == 0) {
	if (argc > 1) {
	    fprintf(stderr, "Too many arguments for %s keyword\n", argv[0]);
	} /* if */
	Quiet_Flag = 1;
	return 0;
    } /* if */

/*  if (strcmp(argv[0], "quit") == 0)  See "exit" above */

    if (strcmp(argv[0], "reconfigure") == 0) {
	if (argc > 2) {
	    fprintf(stderr, "Too many arguments for %s keyword\n", argv[0]);
	    return 0;
	} /* if */
	printf("%s keyword not yet implemented\n", argv[0]);
	return 0;
    } /* if */

    if (strcmp(argv[0], "restart") == 0) {
	if (argc > 2) {
	    fprintf(stderr, "Too many arguments for %s keyword\n", argv[0]);
	    return 0;
	} /* if */
	printf("%s keyword not yet implemented\n", argv[0]);
	return 0;
    } /* if */

    if (strcmp(argv[0], "show") == 0) {
	if (argc > 3) {
	    fprintf(stderr, "Too many arguments for %s keyword\n", argv[0]);
	    return 0;
	} /* if */
	if (argc < 2) {
	    fprintf(stderr, "Too few arguments for %s keyword\n", argv[0]);
	    return 0;
	} /* if */ 
	if ((strcmp(argv[1],"job") != 0)  && 
	    (strcmp(argv[1],"node") != 0) && (strcmp(argv[1],"partition") != 0)) {
	    fprintf(stderr, "Invalid entity %s for %s keyword\n", argv[1], argv[0]);
	    return 0;
	} /* if */
	printf("%s keyword not yet implemented\n", argv[0]);
	return 0;
    } /* if */

    if (strcmp(argv[0], "start") == 0) {
	if (argc > 2) {
	    fprintf(stderr, "Too many arguments for %s keyword\n", argv[0]);
	    return 0;
	} /* if */
	printf("%s keyword not yet implemented\n", argv[0]);
	return 0;
    } /* if */

    if (strcmp(argv[0], "stop") == 0) {
	if (argc > 2) {
	    fprintf(stderr, "Too many arguments for %s keyword\n", argv[0]);
	    return 0;
	} /* if */
	printf("%s keyword not yet implemented\n", argv[0]);
	return 0;
    } /* if */

    if (strcmp(argv[0], "update") == 0) {
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
	return 0;
    } /* if */

    if (strcmp(argv[0], "upload") == 0) {
	if (argc > 2) {
	    fprintf(stderr, "Too many arguments for %s keyword\n", argv[0]);
	    return 0;
	} /* if */
	printf("%s keyword not yet implemented\n", argv[0]);
	return 0;
    } /* if */

    if (strcmp(argv[0], "verbose") == 0) {
	if (argc > 1) {
	    fprintf(stderr, "Too many arguments for %s keyword\n", argv[0]);
	} /* if */
	Quiet_Flag = -1;
	return 0;
    } /* if */

    if (strcmp(argv[0], "version") == 0) {
	if (argc > 1) {
	    fprintf(stderr, "Too many arguments for %s keyword\n", argv[0]);
	} /* if */
	printf("%s version 0.1\n", Command_Name);
	return 0;
    } /* if */

    if (strcmp(argv[0], "write") == 0) {
	if (argc > 3) {
	    fprintf(stderr, "Too many arguments for %s keyword\n", argv[0]);
	    return 0;
	} /* if */
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
	return 0;
    } /* if */

    fprintf(stderr, "Invalid keyword: %s\n", argv[0]);
    return 0;

} /* Process_Command */


/* Usage - Show the valid slurm_admin commands */
void Usage() {
    printf("%s [-q | -v] [<keyword>]\n", Command_Name);
    printf("    -q is equivalent to the keyword \"quiet\" described below.\n");
    printf("    -v is equivalent to the keyword \"verbose\" described below.\n");
    printf("    <keyword> may be omitted from the execute line and %s will execute in interactive\n");
    printf("     mode to process multiple keywords (i.e. commands). Valid <keyword> values are:\n\n");
    printf("     exit                         Terminate this command.\n");
    printf("     help                         Print this description of use.\n");
    printf("     quiet                        Print no messages other than error messages.\n");
    printf("     quit                         Terminate this command.\n");
    printf("     reconfigure [<NodeName>]     Re-read configuration files, default is all nodes.\n");
    printf("     restart [<NodeName>]         Stop and restart daemons, default is all nodes\n");
    printf("     show <entity> [<ID>]         Display state of identified entity, default is all records.\n");
    printf("     start [<NodeName>]           Start daemons as needed, default is all nodes\n");
    printf("     stop [<NodeName>]            Stop daemons, default is all nodes\n");
    printf("     update <entity> <options>    Update state of identified entity.\n");
    printf("     upload [<NodeName>]          Upload node configuration, default is from all nodes.\n");
    printf("     verbose                      Enable detailed logging.\n");
    printf("     version                      Display tool version number.\n");
    printf("     write <entity> <filename>    Write entity configuration to specified file.\n");
} /* Usage */
