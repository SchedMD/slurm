/*
 * Read_Config.c - Read the overall SLURM configuration file
 * See slurm.h for documentation on external functions and data structures
 *
 * Author: Moe Jette, jette@llnl.gov
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "slurm.h"

#define BUF_SIZE 1024
#define DEBUG_MODULE 1
#define DEBUG_SYSTEM 1
#define SEPCHARS " \n\t"
#define NO_VAL (-99)

int 	Load_String(char **destination, char *keyword, char *In_Line);
int 	Parse_Node_Spec(char *In_Line);
int 	Parse_Part_Spec(char *In_Line);
void	Report_Leftover(char *In_Line, int Line_Num);

#if DEBUG_MODULE
struct Node_Record *Find_Node_Record(char *name);
#endif

struct {
    struct Config_Record Config;
    struct Node_Record Node;
} Default_Record;

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
main(int argc, char * argv[]) {
    int Error_Code, Start_Inx, End_Inx, Count_Inx;
    char Out_Line[BUF_SIZE];
    char *Format, *NodeName;
    int i;

    if (argc > 2) {
	printf("Usage: %s <in_file>\n", argv[0]);
	exit(0);
    } /* if */

    Error_Code = Init_SLURM_Conf();
    if (Error_Code != 0) exit(Error_Code);

    if (argc == 2) 
	Error_Code = Read_SLURM_Conf(argv[1]);
    else
	Error_Code = Read_SLURM_Conf(SLURM_CONF);

    if (Error_Code != 0) {
	printf("Error %d from Read_SLURM_Conf\n", Error_Code);
	exit(1);
    } /* if */

    printf("ControlMachine=%s\n", ControlMachine);
    printf("BackupController=%s\n", BackupController);

    for (i=0; i<Node_Record_Count; i++) {
	if (((Node_Record_Table_Ptr+i)->Name[0]) == (char)NULL) continue;
	printf("NodeName=%s ",      (Node_Record_Table_Ptr+i)->Name);
	printf("NodeState=%s ",     Node_State_String[(Node_Record_Table_Ptr+i)->NodeState]);
	printf("LastResponse=%ld ", (long)(Node_Record_Table_Ptr+i)->LastResponse);

	printf("CPUs=%d ",          (Node_Record_Table_Ptr+i)->Config_Ptr->CPUs);
	printf("RealMemory=%d ",    (Node_Record_Table_Ptr+i)->Config_Ptr->RealMemory);
	printf("TmpDisk=%d ",       (Node_Record_Table_Ptr+i)->Config_Ptr->TmpDisk);
	printf("Feature=%s",        (Node_Record_Table_Ptr+i)->Config_Ptr->Feature);
	printf("\n");
    } /* for */

    exit(0);
} /* main */
#endif

/*
 * Create_Config_Record - Create a Config_Record entry, append it to the Config_List, 
 *	and set is values to the defaults.
 * Input: Pointer to an error code
 * Output: Pointer to the Config_Record
 *         Error_Code is zero if no error, errno otherwise
 */
struct Config_Record *Create_Config_Record(int *Error_Code) {
    struct Config_Record *Config_Point;

    Config_Point = (struct Config_Record *)malloc(sizeof(struct Config_Record));
    if (Config_Point == (struct Config_Record *)NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Config_Record: unable to allocate memory\n");
#else
	syslog(LOG_ERR, "Parse_Node_Spec: unable to allocate memory\n")
#endif
	*Error_Code = ENOMEM;
	return (struct Config_Record *)NULL;
    } /* if */

    if (list_append(Config_List, Config_Point) == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Config_Record: unable to allocate memory\n");
#else
	syslog(LOG_ERR, "Config_Record: unable to allocate memory\n")
#endif

	free(Config_Point);
	*Error_Code = ENOMEM;
	return (struct Config_Record *)NULL;
    } /* if */

    /* Set default values */
    Config_Point->CPUs = Default_Record.Config.CPUs;
    Config_Point->RealMemory = Default_Record.Config.RealMemory;
    Config_Point->TmpDisk = Default_Record.Config.TmpDisk;
    if (Default_Record.Config.Feature == (char *)NULL)
	Config_Point->Feature = Default_Record.Config.Feature;
    else {
	Config_Point->Feature = (char *)malloc(strlen(Default_Record.Config.Feature)+1);
	if (Config_Point->Feature == (char *)NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Config_Record: unable to allocate memory\n");
#else
	    syslog(LOG_ERR, "Config_Record: unable to allocate memory\n")
#endif
	    free(Config_Point);
	    *Error_Code = ENOMEM;
	    return (struct Config_Record *)NULL;
	} /* if */
	strcpy(Config_Point->Feature, Default_Record.Config.Feature);
    } /* else */

    return Config_Point;
} /* Create_Config_Record */

/* 
 * Create_Node_Record - Create a node record
 * Input: None
 * Output: Error_Code is set to zero if no error, errno otherwise
 *         Returns a pointer to the record or NULL if error
 * NOTE The record's values are initialized to those of Default_Record
 */
struct Node_Record *Create_Node_Record(int *Error_Code) {
    struct Node_Record *Node_Record_Point;
    int Old_Buffer_Size, New_Buffer_Size;

    *Error_Code = 0;

    /* Round up the buffer size to reduce overhead of realloc */
    Old_Buffer_Size = (Node_Record_Count) * sizeof(struct Node_Record);
    Old_Buffer_Size = ((int)((Old_Buffer_Size / 1024) + 1)) * 1024;
    New_Buffer_Size = (Node_Record_Count+1) * sizeof(struct Node_Record);
    New_Buffer_Size = ((int)((New_Buffer_Size / 1024) + 1)) * 1024;
    if (Node_Record_Count == 0)
	Node_Record_Table_Ptr = (struct Node_Record *)malloc(New_Buffer_Size);
    else if (Old_Buffer_Size != New_Buffer_Size)
	Node_Record_Table_Ptr = (struct Node_Record *)realloc(Node_Record_Table_Ptr, New_Buffer_Size);

    if (Node_Record_Table_Ptr == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Create_Node_Record: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Create_Node_Record: unable to allocate memory\n");
#endif
	*Error_Code = ENOMEM;
	return;
    } /* if */

    Node_Record_Point = Node_Record_Table_Ptr + (Node_Record_Count++);
    strcpy(Node_Record_Point->Name,    Default_Record.Node.Name);
    Node_Record_Point->NodeState     = Default_Record.Node.NodeState;
    Node_Record_Point->LastResponse  = (time_t) NULL;
    return Node_Record_Point;
} /* Create_Node_Record */


#if DEBUG_MODULE
/* 
 * Find_Node_Record - Find a record for node with specified name,
 * Stripped down version for module debug
 * Input: name - name of the desired node 
 * Output: return pointer to node record or NULL if not found
 */
struct Node_Record *Find_Node_Record(char *name) {
    int i;

    /* Revert to sequential search */
    for (i=0; i<Node_Record_Count; i++) {
	if (strcmp(name, (Node_Record_Table_Ptr+i)->Name) != 0) continue;
	return (Node_Record_Table_Ptr+i);
    } /* for */

    return (struct Node_Record *)NULL;
} /* Find_Node_Record */
#endif


/* 
 * Init_SLURM_Conf - Initialize the SLURM configuration values. 
 * This should be called before first calling Read_SLURM_Conf.
 * Output: return value - 0 if no error, otherwise an error code
 */
int Init_SLURM_Conf() {

    BackupController = NULL;
    ControlMachine = NULL;

    strcpy(Default_Record.Node.Name, "DEFAULT");
    Default_Record.Node.NodeState = STATE_UNKNOWN;
    Default_Record.Node.LastResponse = (time_t)0;
    Default_Record.Config.CPUs = 1;
    Default_Record.Config.RealMemory = 1;
    Default_Record.Config.TmpDisk = 1L;
    Default_Record.Config.Feature = (char *)NULL;

    Node_Record_Table_Ptr = (struct Node_Record *)NULL;
    Node_Record_Count = 0;
    Last_Node_Update = time(NULL);

    Config_List = list_create(NULL);
    if (Config_List == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Init_SLURM_Conf: list_create can not allocate memory\n");
#else
	syslog(LOG_ALARM, "Init_SLURM_Conf: list_create can not allocate memory\n");
#endif
	return ENOMEM;
    } /* if */

    return 0;
} /* Init_SLURM_Conf */


/*
 * Load_String - Parse a string for a keyword, value pair  
 * Input: Search In_Line for the string in keyword, if found then load value into *destination
 * Output: *destination is set to pointer to value, if keyword is found
 *           this memory location must later be freed
 *	     if *destination had previous value, that memory location is automatically freed
 *         In_Line - The keyword and value are overwritten by spaces
 *         return value - 0 if no error, otherwise an error code
 */
int Load_String(char **destination, char *keyword, char *In_Line) {
    char Scratch[BUF_SIZE];	/* Scratch area for parsing the input line */
    char *str_ptr1, *str_ptr2, *str_ptr3;
    int i, str_len1, str_len2;

    str_ptr1 = (char *)strstr(In_Line, keyword);
    if (str_ptr1 != NULL) {
	str_len1 = strlen(keyword);
	strcpy(Scratch, str_ptr1+str_len1);
	str_ptr2 = (char *)strtok_r(Scratch, SEPCHARS, &str_ptr3);
	str_len2 = strlen(str_ptr2);
	if (destination[0] != NULL) free(destination[0]);
	destination[0] = (char *)malloc(str_len2+1);
	if (destination[0] == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Load_String: unable to allocate memory\n");
#else
	    syslog(LOG_ALERT, "Load_String: unable to allocate memory\n");
#endif
	    return ENOMEM;
	} /* if */
	strcpy(destination[0], str_ptr2);
	for (i=0; i<(str_len1+str_len2); i++) {
	    str_ptr1[i] = ' ';
	} /* for */
    } /* if */
    return 0;
} /* Load_String  */


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
int Parse_Node_Name(char *NodeName, char **Format, int *Start_Inx, int *End_Inx, int *Count_Inx) {
    int Base, Format_Pos, Precision, i;
    char Type[1];

    i = strlen(NodeName);
    Format[0] = (char *)malloc(i+1);
    if (Format[0] == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Parse_Node_Name: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Parse_Node_Name: unable to allocate memory\n")
#endif
	return ENOMEM;
    } /* if */

    *Start_Inx = 0;
    *End_Inx   = 0;
    *Count_Inx = 0;
    Format_Pos = 0;
    Base = 0;
    Format[0][Format_Pos] = (char)NULL;
    i = 0;
    while (1) {
	if (NodeName[i] == (char)NULL) break;
	if (NodeName[i] == '\\') {
	    if (NodeName[++i] == (char)NULL) break;
	    Format[0][Format_Pos++] = NodeName[i++];
	} else if (NodeName[i] == '[') {
	    if (NodeName[++i] == (char)NULL) break;
	    if (Base != 0) {
#if DEBUG_SYSTEM
		fprintf(stderr, "Parse_Node_Name: Invalid '[' in node name %s\n", NodeName);
#else
		syslog(LOG_ALERT, "Parse_Node_Name: Invalid '[' in node name %s\n", NodeName)
#endif
		free(Format[0]);
		return EINVAL;
	    } /* if */
	    if (NodeName[i] == 'o') {
		Type[0] = NodeName[i++];
		Base = 8;
	    } else {
		Type[0] = 'd';
		Base = 10;
	    } /* else */
	    Precision = 0;
	    while (1) {
		if ((NodeName[i] >= '0') && (NodeName[i] <= '9')) {
		    *Start_Inx = ((*Start_Inx) * Base) + (int)(NodeName[i++] - '0');
		    Precision++;
		    continue;
		} /* if */
		if (NodeName[i] == '-') {
		    i++;
		    break;
		} /* if */
#if DEBUG_SYSTEM
		fprintf(stderr, "Parse_Node_Name: Invalid '%c' in node name %s\n", 
			NodeName[i], NodeName);
#else
		syslog(LOG_ALERT, "Parse_Node_Name: Invalid '%c' in node name %s\n", 
			NodeName[i], NodeName)
#endif
		free(Format[0]);
		return EINVAL;
	    } /* while */
	    while (1) {
		if ((NodeName[i] >= '0') && (NodeName[i] <= '9')) {
		    *End_Inx = ((*End_Inx) * Base) + (int)(NodeName[i++] - '0');
		    continue;
		} /* if */
		if (NodeName[i] == ']') {
		    i++;
		    break;
		} /* if */
#if DEBUG_SYSTEM
		fprintf(stderr, "Parse_Node_Name: Invalid '%c' in node name %s\n", 
			NodeName[i], NodeName);
#else
		syslog(LOG_ALERT, "Parse_Node_Name: Invalid '%c' in node name %s\n", 
			NodeName[i], NodeName)
#endif
		free(Format[0]);
		return EINVAL;
	    } /* while */
	    *Count_Inx = (*End_Inx - *Start_Inx) + 1;
	    Format[0][Format_Pos++] = '%';
	    Format[0][Format_Pos++] = '.';
	    if (Precision > 9) Format[0][Format_Pos++] = '0' + (Precision/10);
	    Format[0][Format_Pos++] = '0' + (Precision%10);
	    Format[0][Format_Pos++] = Type[0];
	} else {
	    Format[0][Format_Pos++] = NodeName[i++];
	} /* else */
    } /* while */
    Format[0][Format_Pos] = (char)NULL;
    return 0;
} /* Parse_Node_Name */


/* 
 * Parse_Node_Spec - Parse the node specification, build table and set values
 * Input:  In_Line line from the configuration file
 * Output: In_Line parsed keywords and values replaced by blanks
 *         return value 0 if no error, error code otherwise
 */
int Parse_Node_Spec (char *In_Line) {
    char *NodeName, *CPUs, *RealMemory, *TmpDisk, *State, *Feature, *Format, This_Node_Name[BUF_SIZE];
    int Start_Inx, End_Inx, Count_Inx;
    int Error_Code, i;
    int State_Val, CPUs_Val, RealMemory_Val;
    long TmpDisk_Val;
    struct Node_Record *Node_Record_Point;
    struct Config_Record *Config_Point;

    NodeName = CPUs = RealMemory = TmpDisk = State = Feature = (char *)NULL;
    CPUs_Val = RealMemory_Val = State_Val = NO_VAL;
    TmpDisk_Val = (long)NO_VAL;
    if (Error_Code=Load_String(&NodeName, "NodeName=", In_Line))      return Error_Code;
    if (NodeName == NULL) return 0;	/* No Node info */

    if (Error_Code=Load_String (&State, "State=", In_Line))           return Error_Code;
    if (State != NULL) {
	for (i=0; i<=STATE_END; i++) {
	    if (strcmp(Node_State_String[i], "END") == 0) break;
	    if (strcmp(Node_State_String[i], State) == 0) {
		State_Val = i;
		break;
	    } /* if */
	} /* for */
	if (State_Val == NO_VAL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Parse_Node_Spec: Invalid State %s for NodeName %s\n", State, NodeName);
#else
	    syslog(LOG_ERR, "Parse_Node_Spec: Invalid State %s for NodeName %s\n", State, NodeName);
#endif
	    free(NodeName);
	    free(State);
	    return EINVAL;
	} /* if */
	free(State);
    } /* if */

    Error_Code = Load_String(&CPUs, "CPUs=", In_Line);
    if (Error_Code != 0) {
	free(NodeName);
	return Error_Code;
    } /* if */
    if (CPUs != NULL) {
	CPUs_Val = (int) strtol(CPUs, (char **)NULL, 10);
	free(CPUs);
    } /* if */

    Error_Code = Load_String(&RealMemory, "RealMemory=", In_Line);
    if (Error_Code != 0) {
	free(NodeName);
	return Error_Code;
    } /* if */
    if (RealMemory != NULL) {
	RealMemory_Val = (int) strtol(RealMemory, (char **)NULL, 10);
	free(RealMemory);
    } /* if */

    Error_Code = Load_String(&TmpDisk, "TmpDisk=", In_Line);
    if (Error_Code != 0) {
	free(NodeName);
	return Error_Code;
    } /* if */
    if (TmpDisk != NULL) {
	TmpDisk_Val = strtol(TmpDisk, (char **)NULL, 10);
	free(TmpDisk);
    } /* if */

    Error_Code = Load_String (&Feature, "Feature=", In_Line);
    if (Error_Code != 0) {
	free(NodeName);
	return Error_Code;
    } /* if */

    Error_Code = Parse_Node_Name(NodeName, &Format, &Start_Inx, &End_Inx, &Count_Inx);
    if (Error_Code != 0) {
	free(NodeName);
	return Error_Code;
    } /* if */
    if (Count_Inx == 0) { 	/* Execute below loop once */
	Start_Inx = 0;
	End_Inx = 0;
    } /* if */

    for (i=Start_Inx; i<=End_Inx; i++) {
	if (Count_Inx == 0)
	    sprintf(This_Node_Name, Format);
	else
	    sprintf(This_Node_Name, Format, i);
	if (strlen(This_Node_Name) >= MAX_NAME_LEN) {
#if DEBUG_SYSTEM
    	    fprintf(stderr, "Parse_Node_Spec: Node name %s too long\n", This_Node_Name);
#else
    	    syslog(LOG_ERR, "Parse_Node_Spec: Node name %s too long\n", This_Node_Name);
#endif
	    free(NodeName);
	    if (Feature != NULL) free(NodeName);
	    Error_Code = EINVAL;
	    break;
	} /* if */
	if (strcmp(This_Node_Name, "DEFAULT") == 0) {
	    if (CPUs_Val != NO_VAL)          Default_Record.Config.CPUs = CPUs_Val;
	    if (RealMemory_Val != NO_VAL)    Default_Record.Config.RealMemory = RealMemory_Val;
	    if (TmpDisk_Val != NO_VAL)       Default_Record.Config.TmpDisk = TmpDisk_Val;
	    if (State_Val != NO_VAL)         Default_Record.Node.NodeState = State_Val;
	    if (Feature != NULL) {
		if (Default_Record.Config.Feature != (char *)NULL) free(Default_Record.Config.Feature);
		Default_Record.Config.Feature = Feature;
	    } /* if */
	} else {
	    if (i == Start_Inx) {
		Config_Point = Create_Config_Record(&Error_Code);
		if (Error_Code != 0) {
		    free(NodeName);
		    free(Format);
		    return Error_Code;
		} /* if */
		if (CPUs_Val != NO_VAL)       Config_Point->CPUs = CPUs_Val;
		if (RealMemory_Val != NO_VAL) Config_Point->RealMemory = RealMemory_Val;
		if (TmpDisk_Val != NO_VAL)    Config_Point->TmpDisk = TmpDisk_Val;
		if (Feature != NULL) {
		    if (Config_Point->Feature != NULL) free(Config_Point->Feature);
		    Config_Point->Feature = (char *)malloc(strlen(Feature)+1);
		    if (Config_Point->Feature == (char *)NULL) {
#if DEBUG_SYSTEM
			fprintf(stderr, "Parse_Node_Spec: unable to allocate memory\n");
#else
			syslog(LOG_ERR, "Parse_Node_Spec: unable to allocate memory\n")
#endif
			free(NodeName);
			free(Format);
			free(Config_Point);
			return ENOMEM;
		    } /* if */
		    strcpy(Config_Point->Feature, Feature);
		} /* if */
	    } /* if */

	    Node_Record_Point = Find_Node_Record(This_Node_Name);
	    if (Node_Record_Point == NULL) {
		Node_Record_Point = Create_Node_Record(&Error_Code);
		if (Error_Code != 0) break;
		strcpy(Node_Record_Point->Name, This_Node_Name);
	    } else {
#if DEBUG_SYSTEM
		fprintf(stderr, "Parse_Node_Spec: duplicate data for node %s, using latest information\n", 
		    This_Node_Name);
#else
		syslog(LOG_NOTICE, "Parse_Node_Spec: duplicate data for node %s, using latest information\n", 
		    This_Node_Name);
#endif
	    } /* else */
	    if (State_Val != NO_VAL)         Node_Record_Point->NodeState=State_Val;
	    Node_Record_Point->Config_Ptr = Config_Point;
	} /* else */
    } /* for (i */
    free(Format);

    /* Free allocated storage */
    free(NodeName);
    if (Feature != NULL) free(Feature);
    return Error_Code;
} /* Parse_Node_Spec */


/*
 * Parse_Part_Spec - Parse the partition specification, build table and set values
 * Output: 0 if no error, error code otherwise
 */
int Parse_Part_Spec (char *In_Line) {
    return 0;
} /* Parse_Part_Spec */


/*
 * Read_SLURM_Conf - Load the SLURM configuration from the specified file. 
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
    char *str_ptr1, *str_ptr2, *str_ptr3;
    int i, j, Error_Code;

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

	/* Everything after a non-escaped "#" is a comment */
	/* Replace comment flag "#" with an end of string (NULL) */
	for (i=0; i<BUF_SIZE; i++) {
	    if (In_Line[i] == (char)NULL) break;
	    if (In_Line[i] != '#') continue;
	    if ((i>0) && (In_Line[i-1]=='\\')) {	/* escaped "#" */
		for (j=i; j<BUF_SIZE; j++) {
		    In_Line[j-1] = In_Line[j];
		} /* for */
		continue;
	    } /* if */
	    In_Line[i] = (char)NULL;
	    break;
	} /* for */

	/* Parse what is left */
	/* Overall SLURM configuration parameters */
	if (Error_Code=Load_String(&ControlMachine, "ControlMachine=", In_Line))     return Error_Code;
	if (Error_Code=Load_String(&BackupController, "BackupController=", In_Line)) return Error_Code;

	/* Node configuration parameters */
	if (Error_Code=Parse_Node_Spec(In_Line)) return Error_Code;

	/* Partition configuration parameters */
	if (Error_Code=Parse_Part_Spec(In_Line)) return Error_Code;

	/* Report any leftover strings on input line */
	Report_Leftover(In_Line, Line_Num);
    } /* while */

    /* If values not set in configuration file, set defaults */
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
	BackupController[0] = (char)NULL;
#if DEBUG_SYSTEM
    fprintf(stderr, "Read_SLURM_Conf: BackupController value not specified.\n");
#else
    syslog(LOG_WARN, "Read_SLURM_Conf: BackupController value not specified.\n")
#endif
    } /* if */
    if (ControlMachine == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Read_SLURM_Conf: ControlMachine value not specified.\n");
#else
	syslog(LOG_ALERT, "Read_SLURM_Conf: ControlMachine value not specified.\n")
#endif
	return EINVAL;
    } /* if */

    return 0;
} /* Read_SLURM_Conf */


/* 
 * Report_Leftover - Report any un-parsed (non-whitespace) characters on the
 * configuration input line.
 * Input: In_Line - What is left of the configuration input line.
 *        Line_Num - Line number of the configuration file.
 * Output: NONE
 */
void Report_Leftover(char *In_Line, int Line_Num) {
    int Bad_Index, i;

    Bad_Index = -1;
    for (i=0; i<strlen(In_Line); i++) {
	if (In_Line[i] == '\n') In_Line[i]=' ';
	if (isspace((int)In_Line[i])) continue;
	if (Bad_Index == -1) Bad_Index=i;
    } /* if */

    if (Bad_Index == -1) return;
#if DEBUG_SYSTEM
    fprintf(stderr, "Report_Leftover: Ignored input on line %d of configuration: %s\n", 
	Line_Num, &In_Line[Bad_Index]);
#else
    syslog(LOG_ERR, "Report_Leftover: Ignored input on line %d of configuration: %s\n", 
	Line_Num, &In_Line[Bad_Index]);
#endif
    return;
} /* Report_Leftover */
