/*
 * Read_Config.c - Read the overall SLURM configuration file
 * This module also has the basic functions for manipulation of data structures
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

void	Dump_Hash();
int 	Hash_Index(char *name);
int 	Load_Integer(int *destination, char *keyword, char *In_Line);
int 	Load_String(char **destination, char *keyword, char *In_Line);
int 	Parse_Node_Spec(char *In_Line);
int 	Parse_Part_Spec(char *In_Line);
void 	Rehash();
void	Report_Leftover(char *In_Line, int Line_Num);

struct {
    struct Config_Record Config;
    struct Node_Record Node;
} Default_Record;
struct Part_Record Default_Part;

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
    printf("\n");

    for (i=0; i<Node_Record_Count; i++) {
	if (strlen((Node_Record_Table_Ptr+i)->Name) == 0) continue;
	printf("NodeName=%s ",      (Node_Record_Table_Ptr+i)->Name);
	printf("NodeState=%s ",     Node_State_String[(Node_Record_Table_Ptr+i)->NodeState]);
	printf("LastResponse=%ld ", (long)(Node_Record_Table_Ptr+i)->LastResponse);

	printf("CPUs=%d ",          (Node_Record_Table_Ptr+i)->Config_Ptr->CPUs);
	printf("RealMemory=%d ",    (Node_Record_Table_Ptr+i)->Config_Ptr->RealMemory);
	printf("TmpDisk=%d ",       (Node_Record_Table_Ptr+i)->Config_Ptr->TmpDisk);
	printf("Feature=%s",        (Node_Record_Table_Ptr+i)->Config_Ptr->Feature);
	printf("\n");
    } /* for */
    printf("\n");

    printf("Default_Part_Name=%s\n", Default_Part_Name);
    printf("PartitionName=%s ",     Default_Part_Loc->Name);
    printf("MaxTime=%d ",           Default_Part_Loc->MaxTime);
    printf("MaxNodes=%d ",          Default_Part_Loc->MaxNodes);
    printf("RootKey=%d ",           Default_Part_Loc->RootKey);
    printf("StateUp=%d ",           Default_Part_Loc->StateUp);
    printf("Shared=%d ",            Default_Part_Loc->Shared);
    printf("Nodes=%s ",             Default_Part_Loc->Nodes);
    printf("AllowGroups=%s\n",      Default_Part_Loc->AllowGroups);
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
	fprintf(stderr, "Create_Config_Record: unable to allocate memory\n");
#else
	syslog(LOG_ERR, "Create_Config_Record: unable to allocate memory\n")
#endif
	*Error_Code = ENOMEM;
	return (struct Config_Record *)NULL;
    } /* if */

    if (list_append(Config_List, Config_Point) == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Create_Config_Record: unable to allocate memory\n");
#else
	syslog(LOG_ERR, "Create_Config_Record: unable to allocate memory\n")
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
	    fprintf(stderr, "Create_Config_Record: unable to allocate memory\n");
#else
	    syslog(LOG_ERR, "Create_Config_Record: unable to allocate memory\n")
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
 * Input: Location to store error value in
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


/* 
 * Create_Part_Record - Create a partition record
 * Input: Location to store error value in
 * Output: Error_Code is set to zero if no error, errno otherwise
 *         Returns a pointer to the record or NULL if error
 * NOTE The record's values are initialized to those of Default_Part
 */
struct Part_Record *Create_Part_Record(int *Error_Code) {
    struct Part_Record *Part_Record_Point;

    *Error_Code = 0;

    Part_Record_Point = (struct Part_Record *)malloc(sizeof(struct Part_Record));
    if (Part_Record_Point == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Create_Part_Record: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Create_Part_Record: unable to allocate memory\n");
#endif
	*Error_Code = ENOMEM;
	return NULL;
    } /* if */

    strcpy(Part_Record_Point->Name, "DEFAULT");
    Part_Record_Point->MaxTime  = Default_Part.MaxTime;
    Part_Record_Point->MaxNodes = Default_Part.MaxNodes;
    Part_Record_Point->RootKey  = Default_Part.RootKey;
    Part_Record_Point->Shared   = Default_Part.Shared;
    Part_Record_Point->StateUp  = Default_Part.StateUp;

    if (Default_Part.AllowGroups == (char *)NULL)
	Part_Record_Point->AllowGroups = Default_Part.AllowGroups;
    else {
	Part_Record_Point->AllowGroups = (char *)malloc(strlen(Default_Part.AllowGroups)+1);
	if (Part_Record_Point->AllowGroups == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Create_Part_Record: unable to allocate memory\n");
#else
	    syslog(LOG_ERR, "Create_Part_Record: unable to allocate memory\n");
#endif
	    *Error_Code = ENOMEM;
	    return NULL;
	} /* if */
	strcpy(Part_Record_Point->AllowGroups, Default_Part.AllowGroups);
    } /* else */

    if (Default_Part.Nodes == (char *)NULL)
	Part_Record_Point->Nodes = Default_Part.Nodes;
    else {
	Part_Record_Point->Nodes = (char *)malloc(strlen(Default_Part.Nodes)+1);
	if (Part_Record_Point->Nodes == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Create_Part_Record: unable to allocate memory\n");
#else
	    syslog(LOG_ERR, "Create_Part_Record: unable to allocate memory\n");
#endif
	    *Error_Code = ENOMEM;
	    return NULL;
	} /* if */
	strcpy(Part_Record_Point->Nodes, Default_Part.Nodes);
    } /* else */
    return Part_Record_Point;
} /* Create_Part_Record */


/* Print the Hash_Table contents, used for debugging or analysis of hash technique */
void Dump_Hash() {
    int i;

    if (Hash_Table ==  NULL) return;
    for (i=0; i<Node_Count; i++) {
	if (Hash_Table[i] == NULL) continue;
	printf("Hash:%d:%s\n", i, (Node_Record_Table_Ptr+Hash_Table[i])->Name);
    } /* for */
} /* Dump_Hash */


/* 
 * Find_Node_Record - Find a record for node with specified name,
 * Stripped down version for module debug
 * Input: name - name of the desired node 
 * Output: return pointer to node record or NULL if not found
 */
struct Node_Record *Find_Node_Record(char *name) {
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

    /* Revert to sequential search */
    for (i=0; i<Node_Record_Count; i++) {
	if (strcmp(name, (Node_Record_Table_Ptr+i)->Name) != 0) continue;
	return (Node_Record_Table_Ptr+i);
    } /* for */

    return (struct Node_Record *)NULL;
} /* Find_Node_Record */


/* 
 * Find_Part_Record - Find a record for node with specified name,
 * Stripped down version for module debug
 * Input: name - name of the desired node 
 * Output: return pointer to node record or NULL if not found
 */
struct Part_Record *Find_Part_Record(char *name) {
    ListIterator Part_Record_Iterator;		/* For iterating through Part_Record_List */
    struct Part_Record *Part_Record_Point;	/* Pointer to Part_Record */

    Part_Record_Iterator = list_iterator_create(Part_List);
    if (Part_Record_Iterator == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Find_Part_Record: list_iterator_create unable to allocate memory\n");
#else
	syslog(LOG_ERR, "Find_Part_Record: list_iterator_create unable to allocate memory\n");
#endif
	return NULL;
    }

    while (Part_Record_Point = (struct Part_Record *)list_next(Part_Record_Iterator)) {
	if (strcmp(Part_Record_Point->Name, name) == 0) break;
    } /* while */

    list_iterator_destroy(Part_Record_Iterator);
    return Part_Record_Point;
} /* Find_Part_Record */


/* 
 * Hash_Index - Return a hash table index for the given node name 
 * This code is optimized for names containing a base-ten suffix (e.g. "lx04")
 * Input: The node's name
 * Output: Return code is the hash table index
 */
int Hash_Index(char *name) {
    int i, inx, tmp;

    if (Node_Record_Count == 0) return 0;		/* Degenerate case */
    inx = 0;

#if ( HASH_BASE == 10 )
    for (i=0; ;i++) { 
	tmp = (int) name[i];
	if (tmp == 0) break;			/* end if string */
	if ((tmp >= (int)'0') && (tmp <= (int)'9')) 
	    inx = (inx * HASH_BASE) + (tmp - (int)'0');
    } /* for */
#elif ( HASH_BASE == 8 )
    for (i=0; ;i++) { 
	tmp = (int) name[i];
	if (tmp == 0) break;			/* end if string */
	if ((tmp >= (int)'0') && (tmp <= (int)'7')) 
	    inx = (inx * HASH_BASE) + (tmp - (int)'0');
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
 * Init_SLURM_Conf - Initialize the SLURM configuration values. 
 * This should be called before first calling Read_SLURM_Conf.
 * Output: return value - 0 if no error, otherwise an error code
 */
int Init_SLURM_Conf() {

    BackupController = NULL;
    ControlMachine = NULL;

    strcpy(Default_Record.Node.Name, "DEFAULT");
    Default_Record.Node.NodeState    = STATE_UNKNOWN;
    Default_Record.Node.LastResponse = (time_t)0;
    Default_Record.Config.CPUs       = 1;
    Default_Record.Config.RealMemory = 1;
    Default_Record.Config.TmpDisk    = 1;
    Default_Record.Config.Feature    = (char *)NULL;

    Node_Record_Table_Ptr = (struct Node_Record *)NULL;
    Node_Record_Count = 0;
    Hash_Table = (int *)NULL;
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

    Default_Part.AllowGroups = (char *)NULL;
    Default_Part.MaxTime     = -1;
    Default_Part.MaxNodes    = -1;
    Default_Part.Nodes       = (char *)NULL;
    Default_Part.RootKey     = 0;
    Default_Part.Shared      = 0;
    Default_Part.StateUp     = 1;

    Part_List = list_create(NULL);
    if (Part_List == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Init_SLURM_Conf: list_create can not allocate memory\n");
#else
	syslog(LOG_ALARM, "Init_SLURM_Conf: list_create can not allocate memory\n");
#endif
	return ENOMEM;
    } /* if */
    strcpy(Default_Part_Name, "");
    Default_Part_Loc = (struct Part_Record *)NULL;

    return 0;
} /* Init_SLURM_Conf */


/*
 * Load_Integer - Parse a string for a keyword, value pair  
 * Input: Search In_Line for the string in keyword, if found then load value into *destination
 * Output: *destination is set to value, No change if value not found, 
 *		Set to 1 if keyword found without value
 *         In_Line - The keyword and value are overwritten by spaces
 *         return value - 0 if no error, otherwise an error code
 */
int Load_Integer(int *destination, char *keyword, char *In_Line) {
    char Scratch[BUF_SIZE];	/* Scratch area for parsing the input line */
    char *str_ptr1, *str_ptr2, *str_ptr3;
    int i, str_len1, str_len2;

    str_ptr1 = (char *)strstr(In_Line, keyword);
    if (str_ptr1 != NULL) {
	str_len1 = strlen(keyword);
	strcpy(Scratch, str_ptr1+str_len1);
	if (isspace((int)Scratch[0])) { /* Keyword with no value set */
	    *destination = 1;
	    str_len2 = 0;
	} else {
	    str_ptr2 = (char *)strtok_r(Scratch, SEPCHARS, &str_ptr3);
	    str_len2 = strlen(str_ptr2);
	    if (strcmp(str_ptr2, "UNLIMITED") == 0)
		*destination = -1;
	    else if ((str_ptr2[0] >= '0') && (str_ptr2[0] <= '9')) 
		*destination = (int) strtol(Scratch, (char **)NULL, 10);
	    else {
#if DEBUG_SYSTEM
		fprintf(stderr, "Load_Integer: bad value for keyword %s\n", keyword);
#else
		syslog(LOG_ALERT, "Load_Integer: bad value for keyword %s\n", keyword);
#endif
		return EINVAL;	
	    } /* else */
	} /* else */
	for (i=0; i<(str_len1+str_len2); i++) {
	    str_ptr1[i] = ' ';
	} /* for */
	
    } /* if */
    return 0;
} /* Load_Integer */


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
	if (isspace((int)Scratch[0])) { /* No value set */
#if DEBUG_SYSTEM
	    fprintf(stderr, "Load_String: keyword %s lacks value\n", keyword);
#else
	    syslog(LOG_ALERT, "Load_String: keyword %s lacks value\n", keyword);
#endif
	    return EINVAL;
	} /* if */
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
} /* Load_String */


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
    char *NodeName, *State, *Feature, *Format, This_Node_Name[BUF_SIZE];
    int Start_Inx, End_Inx, Count_Inx;
    int Error_Code, i;
    int State_Val, CPUs_Val, RealMemory_Val, TmpDisk_Val;
    struct Node_Record *Node_Record_Point;
    struct Config_Record *Config_Point;
    char *str_ptr1, *str_ptr2;

    NodeName = State = Feature = (char *)NULL;
    CPUs_Val = RealMemory_Val = State_Val = NO_VAL;
    TmpDisk_Val = (long)NO_VAL;
    if (Error_Code=Load_String(&NodeName, "NodeName=", In_Line))      return Error_Code;
    if (NodeName == NULL) return 0;	/* No Node info */

    Error_Code += Load_Integer(&CPUs_Val, "CPUs=", In_Line);
    Error_Code += Load_Integer(&RealMemory_Val, "RealMemory=", In_Line);
    Error_Code += Load_Integer(&TmpDisk_Val, "TmpDisk=", In_Line);
    if (Error_Code != 0) {
	free(NodeName);
	return Error_Code;
    } /* if */

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

    Error_Code = Load_String (&Feature, "Feature=", In_Line);
    if (Error_Code != 0) {
	free(NodeName);
	return Error_Code;
    } /* if */

    Error_Code = Parse_Node_Name(NodeName, &Format, &Start_Inx, &End_Inx, &Count_Inx);
    if (Error_Code != 0) {
	free(NodeName);
	free(Feature);
	return Error_Code;
    } /* if */
    if (Count_Inx == 0) { 	/* Execute below loop once */
	Start_Inx = 0;
	End_Inx = 0;
    } /* if */

    for (i=Start_Inx; i<=End_Inx; i++) {
	if (Count_Inx == 0) {	/* Deal with comma separated node names here */
	    if (i == Start_Inx)
		str_ptr2 = strtok_r(Format, ",", &str_ptr1);
	    else
		str_ptr2 = strtok_r(NULL, ",", &str_ptr1);
	    if (str_ptr2 == NULL) break;
	    End_Inx++;
	    strcpy(This_Node_Name, str_ptr2);
	} else
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
    int Line_Num;		/* Line number in input file */
    char *AllowGroups, *Nodes, *PartitionName, *State;
    int MaxTime_Val, MaxNodes_Val, RootKey_Val, Default_Val, Shared_Val, StateUp_Val;
    int Error_Code, i;
    struct Part_Record *Part_Record_Point;

    AllowGroups = Nodes = PartitionName = State = (char *)NULL;
    MaxTime_Val = MaxNodes_Val = RootKey_Val = Default_Val = Shared_Val = NO_VAL;

    if (Error_Code=Load_String(&PartitionName, "PartitionName=", In_Line))      return Error_Code;
    if (PartitionName == NULL) return 0;	/* No partition info */
	if (strlen(PartitionName) >= MAX_NAME_LEN) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Parse_Part_Spec: Partition name %s too long\n", PartitionName);
#else
	syslog(LOG_ERR, "Parse_Part_Spec: Partition name %s too long\n", PartitionName);
#endif
	free(PartitionName);
	return EINVAL;
    } /* if */

    Error_Code += Load_Integer(&MaxTime_Val,  "MaxTime=", In_Line);
    Error_Code += Load_Integer(&MaxNodes_Val, "MaxNodes=", In_Line);
    Error_Code += Load_Integer(&Default_Val,  "Default=NO", In_Line);
    if (Default_Val == 1) Default_Val=0;
    Error_Code += Load_Integer(&Default_Val,  "Default=YES", In_Line);
    Error_Code += Load_Integer(&Shared_Val,   "Shared=NO", In_Line);
    if (Shared_Val == 1) Shared_Val=0;
    Error_Code += Load_Integer(&Shared_Val,   "Shared=YES", In_Line);
    Error_Code += Load_Integer(&StateUp_Val,  "State=DOWN", In_Line);
    if (StateUp_Val == 1) StateUp_Val=0;
    Error_Code += Load_Integer(&StateUp_Val,  "State=UP", In_Line);
    Error_Code += Load_Integer(&RootKey_Val,  "RootKey=NO", In_Line);
    if (RootKey_Val == 1) RootKey_Val=0;
    Error_Code += Load_Integer(&RootKey_Val,  "RootKey=YES", In_Line);
    if (Error_Code != 0) {
	free(PartitionName);
	return Error_Code;
    } /* if */

    Error_Code = Load_String (&Nodes, "Nodes=", In_Line);
    if (Error_Code != 0) {
	free(PartitionName);
	return Error_Code;
    } /* if */

    Error_Code = Load_String (&AllowGroups, "AllowGroups=", In_Line);
    if (Error_Code != 0) {
	free(PartitionName);
	if (Nodes != NULL) free(Nodes);
	return Error_Code;
    } /* if */

    if (strcmp(PartitionName, "DEFAULT") == 0) {
	if (MaxTime_Val  != NO_VAL)    Default_Part.MaxTime      = MaxTime_Val;
	if (MaxNodes_Val != NO_VAL)    Default_Part.MaxNodes     = MaxNodes_Val;
	if (RootKey_Val  != NO_VAL)    Default_Part.RootKey      = RootKey_Val;
	if (StateUp_Val  != NO_VAL)    Default_Part.StateUp      = StateUp_Val;
	if (Shared_Val   != NO_VAL)    Default_Part.Shared       = Shared_Val;
	if (AllowGroups != (char *)NULL) {
	    if (Default_Part.AllowGroups != (char *)NULL) free(Default_Part.AllowGroups);
	    Default_Part.AllowGroups = AllowGroups;
	} /* if */
	if (Nodes != (char *)NULL) {
	    if (Default_Part.Nodes != (char *)NULL) free(Default_Part.Nodes);
	    Default_Part.Nodes = Nodes;
	} /* if */
	return 0;
    } /* if */

    Part_Record_Point = Find_Part_Record(PartitionName);
    if (Part_Record_Point == NULL) {
	Part_Record_Point = Create_Part_Record(&Error_Code);
	if (Error_Code != 0) {
	    free(PartitionName);
	    if (Nodes != NULL) free(Nodes);
	    if (AllowGroups != NULL) free(AllowGroups);
	    return Error_Code;
	} /* if */
	strcpy(Part_Record_Point->Name, PartitionName);
    } else {
#if DEBUG_SYSTEM
	fprintf(stderr, "Parse_Part_Spec: duplicate data for partition %s, using latest information\n", 
		    PartitionName);
#else
	syslog(LOG_NOTICE, "Parse_Node_Spec: duplicate data for partition %s, using latest information\n", 
		    PartitionName);
#endif
    } /* else */
    if (Default_Val  == 1) {
	if (strlen(Default_Part_Name) > 0) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Parse_Part_Spec: changing default partition from %s to %s\n", 
		    Default_Part_Name, PartitionName);
#else
	syslog(LOG_NOTICE, "Parse_Node_Spec: changing default partition from %s to %s\n", 
		    Default_Part_Name, PartitionName);
#endif
	} /* if */
	strcpy(Default_Part_Name, PartitionName);
	Default_Part_Loc = Part_Record_Point;
    } /* if */
    if (MaxTime_Val  != NO_VAL)    Part_Record_Point->MaxTime      = MaxTime_Val;
    if (MaxNodes_Val != NO_VAL)    Part_Record_Point->MaxNodes     = MaxNodes_Val;
    if (RootKey_Val  != NO_VAL)    Part_Record_Point->RootKey      = RootKey_Val;
    if (StateUp_Val  != NO_VAL)    Part_Record_Point->StateUp      = StateUp_Val;
    if (Shared_Val   != NO_VAL)    Part_Record_Point->Shared       = Shared_Val;
    if (AllowGroups != (char *)NULL) {
	if (Part_Record_Point->AllowGroups != (char *)NULL) free(Part_Record_Point->AllowGroups);
	Part_Record_Point->AllowGroups = AllowGroups;
    } /* if */
    if (Nodes != (char *)NULL) {
	if (Part_Record_Point->Nodes != (char *)NULL) free(Part_Record_Point->Nodes);
	Part_Record_Point->Nodes = Nodes;
    } /* if */
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
 * Rehash - Build a hash table of the Node_Record entries. This is a large hash table 
 * to permit the immediate finding of a record based only upon its name without regards 
 * to the number. There should be no need for a search. The algorithm is optimized for 
 * node names with a base-ten sequence number suffix. If you have a large cluster and 
 * use a different naming convention, this function and/or the Hash_Index function 
 * should be re-written.
 */
void Rehash() {
    struct Node_Record *Node_Record_Point;	/* Pointer to Node_Record */
    int i;

    if (Hash_Table ==  (int *)NULL)
	Hash_Table = malloc(sizeof(int *) * Node_Record_Count);
    else
	Hash_Table = realloc(Hash_Table, (sizeof(int *) * Node_Record_Count));

    if (Hash_Table == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Rehash: list_append can not allocate memory\n");
#else
	syslog(LOG_ERR, "Rehash: list_append can not allocate memory\n");
#endif
	return;
    } /* if */
    bzero(Hash_Table, (sizeof(int *) * Node_Count));

    for (i=0; i<Node_Record_Count; i++) {
	inx = Hash_Index((Node_Record_Table_Ptr+i)->Name);
	Hash_Table[inx] = i;
    } /* for */

} /* Rehash */


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
