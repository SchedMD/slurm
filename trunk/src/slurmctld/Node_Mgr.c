/* 
 * Node_Mgr.c - Manage the node records of SLURM
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

#define DEBUG_MODULE 	0
#define DEBUG_SYSTEM 	1
#define NODE_BUF_SIZE 	1024
#define SEPCHARS 	" \n\t"

List 	Config_List = NULL;		/* List of Config_Record entries */
int	Node_Record_Count = 0;		/* Count of records in the Node Record Table */
struct Node_Record *Node_Record_Table_Ptr = NULL; /* Location of the node records */
char 	*Node_State_String[] = {"UNKNOWN", "IDLE", "STAGE_IN", "BUSY", "STAGE_OUT", "DOWN", "DRAINED", "DRAINING", "END"};
int	*Hash_Table = NULL;		/* Table of hashed indicies into Node_Record */
struct 	Config_Record Default_Config_Record;
struct 	Node_Record   Default_Node_Record;
time_t 	Last_Node_Update =(time_t)NULL;	/* Time of last update to Node Records */

unsigned *Up_NodeBitMap  = NULL;		/* Bitmap of nodes are UP */
unsigned *Idle_NodeBitMap = NULL;	/* Bitmap of nodes are IDLE */

void	Dump_Hash();
int 	Hash_Index(char *name);
void 	Rehash();

#if DEBUG_MODULE
/* main is used here for testing purposes only */
main(int argc, char * argv[]) {
    int Error_Code, size;
    char *Out_Line;
    unsigned *Map1, *Map2, *Map3;
    struct Config_Record *Config_Ptr;
    struct Node_Record *Node_Ptr;
    char *Format;
    int Start_Inx, End_Inx, Count_Inx;

    /* Bitmap tests */
    Node_Record_Count = 123;
    size = (Node_Record_Count + 7) / 8;
    Map1 = malloc(size);
    memset(Map1, 0, size);
    BitMapSet(Map1, 7);
    BitMapSet(Map1, 23);
    BitMapSet(Map1, 71);
    Out_Line = BitMapPrint(Map1);
    printf("BitMapPrint #1 shows %s\n", Out_Line);
    Map2 = BitMapCopy(Map1);
    Out_Line = BitMapPrint(Map2);
    printf("BitMapPrint #2 shows %s\n", Out_Line);
    Map3 = BitMapCopy(Map1);
    BitMapClear(Map2, 23);
    BitMapOR(Map3, Map2);
    if (BitMapValue(Map3, 23) != 1) printf("BitMap Error 1\n");
    if (BitMapValue(Map3, 71) != 1) printf("BitMap Error 2\n");
    if (BitMapValue(Map3, 93) != 0) printf("BitMap Error 3\n");
    BitMapAND(Map3, Map2);
    if (BitMapValue(Map3, 23) != 0) printf("BitMap Error 4\n");
    if (BitMapValue(Map3, 71) != 1) printf("BitMap Error 5\n");
    if (BitMapValue(Map3, 93) != 0) printf("BitMap Error 6\n");
    Out_Line = BitMapPrint(Map3);
    printf("BitMapPrint #3 shows %s\n", Out_Line);
    free(Out_Line);
    Node_Record_Count = 0;

    /* Now check out configuration and node structure functions */
    Error_Code = Init_Node_Conf();
    if (Error_Code) printf("Init_Node_Conf error %d\n", Error_Code);
    Default_Config_Record.Weight = 345;
    Default_Node_Record.LastResponse = (time_t)678;

    Config_Ptr = Create_Config_Record(&Error_Code);
    if (Error_Code) printf("Create_Config_Record error %d\n", Error_Code);
    if (Config_Ptr->Weight != 345) printf("Config defaults not set\n");
    Node_Ptr   = Create_Node_Record(&Error_Code);
    if (Error_Code) printf("Create_Node_Record error %d\n", Error_Code);
    strcpy(Node_Ptr->Name, "lx01");
    Node_Ptr   = Create_Node_Record(&Error_Code);
    if (Error_Code) printf("Create_Node_Record error %d\n", Error_Code);
    strcpy(Node_Ptr->Name, "lx02");
    Node_Ptr   = Create_Node_Record(&Error_Code);
    if (Error_Code) printf("Create_Node_Record error %d\n", Error_Code);
    strcpy(Node_Ptr->Name, "lx03");
    if (Node_Ptr->LastResponse != (time_t)678) printf("Node defaults not set\n");
    Rehash();
    Dump_Hash();
    Node_Ptr   = Find_Node_Record("lx02");
    if (Node_Ptr == 0) 
	printf("Find_Node_Record failure 1\n");
    else if (strcmp(Node_Ptr->Name, "lx02") != 0)
	printf("Find_Node_Record failure 2\n");
    else if (Node_Ptr->LastResponse != (time_t)678) 
	printf("Node defaults not set\n");
    printf("NOTE: We execte Delete_Node_Record to report not finding a record for lx04\n");
    Error_Code = Delete_Node_Record("lx04");
    if (Error_Code != ENOENT) printf("Delete_Node_Record failure 1\n");
    Error_Code = Delete_Node_Record("lx02");
    if (Error_Code != 0) printf("Delete_Node_Record failure 2\n");
    printf("NOTE: We execte Delete_Node_Record to report not finding a record for lx02\n");
    Node_Ptr   = Find_Node_Record("lx02");
    if (Node_Ptr != 0) printf("Find_Node_Record failure 3\n");

    /* Check node name parsing */
    Out_Line = "linux[003-234]";
    Error_Code = Parse_Node_Name(Out_Line, &Format, &Start_Inx, &End_Inx, &Count_Inx);
    if (Error_Code != 0) 
	printf("Parse_Node_Name error %d\n", Error_Code);
    else {
	printf("Parse_Node_Name of \"%s\" produces format \"%s\", %d to %d, %d records\n", 
	    Out_Line, Format, Start_Inx, End_Inx, Count_Inx);
	free(Format);
    } /* else */

    exit(0);
} /* main */
#endif

/*
 * BitMapAND - AND two bitmaps together
 * Input: BitMap1 and BitMap2 - The bitmaps to AND
 * Output: BitMap1 is set to the value of BitMap1 & BitMap2
 */
void BitMapAND(unsigned *BitMap1, unsigned *BitMap2) {
    int i, size;

    size = (Node_Record_Count + (sizeof(unsigned)*8) - 1) / (sizeof(unsigned)*8);
    for (i=0; i<size; i++) {
	BitMap1[i] &= BitMap2[i];
    } /* for (i */
} /* BitMapAND */


/*
 * BitMapClear - Clear the specified bit in the specified bitmap
 * Input: BitMap - The bit map to manipulate
 *        Position - Postition to clear
 * Output: BitMap - Updated value
 */
void BitMapClear(unsigned *BitMap, int Position) {
    int val, bit, mask;

    val  = Position / (sizeof(unsigned)*8);
    bit  = Position % (sizeof(unsigned)*8);
    mask = ~(0x1 << ((sizeof(unsigned)*8)-1-bit));

    BitMap[val] &= mask;
} /* BitMapClear */


/*
 * BitMapCopy - Create a copy of a bitmap
 * Input: BitMap - The bitmap create a copy of
 * Output: Returns pointer to copy of BitMap or NULL if error (no memory)
 *   The returned value MUST BE FREED by the calling routine
 */
unsigned *BitMapCopy(unsigned *BitMap) {
    int i, size;
    unsigned *Output;

    size = (Node_Record_Count + 7) / 8;
    Output = malloc(size);
    if (Output == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "BitMapCopy: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "BitMapCopy: unable to allocate memory\n")
#endif
	return NULL;
    } /* if */

    size = (Node_Record_Count + (sizeof(unsigned)*8) - 1) / (sizeof(unsigned)*8);
    for (i=0; i<size; i++) {
	Output[i] = BitMap[i];
    } /* for (i */
    return Output;
} /* BitMapCopy */


/*
 * BitMapOR - OR two bitmaps together
 * Input: BitMap1 and BitMap2 - The bitmaps to OR
 * Output: BitMap1 is set to the value of BitMap1 | BitMap2
 */
void BitMapOR(unsigned *BitMap1, unsigned *BitMap2) {
    int i, size;

    size = (Node_Record_Count + (sizeof(unsigned)*8) - 1) / (sizeof(unsigned)*8);
    for (i=0; i<size; i++) {
	BitMap1[i] |= BitMap2[i];
    } /* for (i */
} /* BitMapOR */


/*
 * BitMapPrint - Convert the specified bitmap into a printable hexadecimal string
 * Input: BitMap - The bit map to print
 * Output: Returns a string
 * NOTE: The returned string must be freed by the calling program
 */
char *BitMapPrint(unsigned *BitMap) {
    int i, j, k, size, nibbles;
    char *Output, temp_str[2];

    size = (Node_Record_Count + (sizeof(unsigned)*8) - 1) / (sizeof(unsigned)*8);
    nibbles = (Node_Record_Count + 3) / 4;
    Output = (char *)malloc((sizeof(unsigned)*size*2)+3);
    if (Output == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "BitMapPrint: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "BitMapPrint: unable to allocate memory\n")
#endif
	return NULL;
    } /* if */

    strcpy(Output, "0x");
    k = 0;
    for (i=0; i<size; i++) {
	for (j=((sizeof(unsigned)*8)-4); j>=0; j-=4) {
	    sprintf(temp_str, "%x", ((BitMap[i]>>j)&0xf));
	    strcat(Output, temp_str);
	    k++;
	    if (k == nibbles) return Output;
	} /* for (j */
    } /* for (i */
    return Output;
} /* BitMapPrint */


/*
 * BitMapSet - Set the specified bit in the specified bitmap
 * Input: BitMap - The bit map to manipulate
 *        Position - Postition to set
 * Output: BitMap - Updated value
 */
void BitMapSet(unsigned *BitMap, int Position) {
    int val, bit, mask;

    val  = Position / (sizeof(unsigned)*8);
    bit  = Position % (sizeof(unsigned)*8);
    mask = (0x1 << ((sizeof(unsigned)*8)-1-bit));

    BitMap[val] |= mask;
} /* BitMapSet */


/*
 * BitMapValue - Return the value of specified bit in the specified bitmap
 * Input: BitMap - The bit map to get value from
 *        Position - Postition to get
 * Output: Returns the value 0 or 1
 */
int BitMapValue(unsigned *BitMap, int Position) {
    int val, bit, mask;

    val  = Position / (sizeof(unsigned)*8);
    bit  = Position % (sizeof(unsigned)*8);
    mask = (0x1 << ((sizeof(unsigned)*8)-1-bit));

    mask &= BitMap[val];
    if (mask == 0)
	return 0;
    else
	return 1;
} /* BitMapValue */


/*
 * Create_Config_Record - Create a Config_Record entry, append it to the Config_List, 
 *	and set is values to the defaults.
 * Input: Error_Code - Pointer to an error code
 * Output: Returns pointer to the Config_Record
 *         Error_Code - set to zero if no error, errno otherwise
 * NOTE: The pointer returned is allocated memory that must be freed when no longer needed.
 */
struct Config_Record *Create_Config_Record(int *Error_Code) {
    struct Config_Record *Config_Point;

    Config_Point = (struct Config_Record *)malloc(sizeof(struct Config_Record));
    if (Config_Point == (struct Config_Record *)NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Create_Config_Record: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Create_Config_Record: unable to allocate memory\n")
#endif
	*Error_Code = ENOMEM;
	return (struct Config_Record *)NULL;
    } /* if */

    /* Set default values */
    Config_Point->CPUs = Default_Config_Record.CPUs;
    Config_Point->RealMemory = Default_Config_Record.RealMemory;
    Config_Point->TmpDisk = Default_Config_Record.TmpDisk;
    Config_Point->Weight = Default_Config_Record.Weight;
    Config_Point->NodeBitMap = NULL;
    if (Default_Config_Record.Feature == (char *)NULL)
	Config_Point->Feature = (char *)NULL;
    else {
	Config_Point->Feature = (char *)malloc(strlen(Default_Config_Record.Feature)+1);
	if (Config_Point->Feature == (char *)NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Create_Config_Record: unable to allocate memory\n");
#else
	    syslog(LOG_ALERT, "Create_Config_Record: unable to allocate memory\n")
#endif
	    free(Config_Point);
	    *Error_Code = ENOMEM;
	    return (struct Config_Record *)NULL;
	} /* if */
	strcpy(Config_Point->Feature, Default_Config_Record.Feature);
    } /* else */

    if (list_append(Config_List, Config_Point) == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Create_Config_Record: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Create_Config_Record: unable to allocate memory\n")
#endif
	if (Config_Point->Feature != (char *)NULL) free(Config_Point->Feature);
	free(Config_Point);
	*Error_Code = ENOMEM;
	return (struct Config_Record *)NULL;
    } /* if */

    return Config_Point;
} /* Create_Config_Record */

/* 
 * Create_Node_Record - Create a node record
 * Input: Error_Code - Location to store error value in
 * Output: Error_Code - Set to zero if no error, errno otherwise
 *         Returns a pointer to the record or NULL if error
 * NOTE The record's values are initialized to those of Default_Record
 */
struct Node_Record *Create_Node_Record(int *Error_Code) {
    struct Node_Record *Node_Record_Point;
    int Old_Buffer_Size, New_Buffer_Size;

    *Error_Code = 0;

    /* Round up the buffer size to reduce overhead of realloc */
    Old_Buffer_Size = (Node_Record_Count) * sizeof(struct Node_Record);
    Old_Buffer_Size = ((int)((Old_Buffer_Size / NODE_BUF_SIZE) + 1)) * NODE_BUF_SIZE;
    New_Buffer_Size = (Node_Record_Count+1) * sizeof(struct Node_Record);
    New_Buffer_Size = ((int)((New_Buffer_Size / NODE_BUF_SIZE) + 1)) * NODE_BUF_SIZE;
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
    strcpy(Node_Record_Point->Name,    Default_Node_Record.Name);
    Node_Record_Point->NodeState     = Default_Node_Record.NodeState;
    Node_Record_Point->LastResponse  = Default_Node_Record.LastResponse;
    return Node_Record_Point;
} /* Create_Node_Record */


/* 
 * Delete_Node_Record - Delete record for node with specified name
 *   To avoid invalidating the bitmaps and hash table, we just clear the name 
 *   set its state to STATE_DOWN
 * Input: name - name of the desired node 
 * Output: return 0 on success, errno otherwise
 */
int Delete_Node_Record(char *name) {
    struct Node_Record *Node_Record_Point;	/* Pointer to Node_Record */

    Node_Record_Point = Find_Node_Record(name);
    if (Node_Record_Point == NULL) return ENOENT;

    strcpy(Node_Record_Point->Name, "");
    Node_Record_Point->NodeState = STATE_DOWN;
    return 0;
} /* Delete_Node_Record */


/* Print the Hash_Table contents, used for debugging or analysis of hash technique */
void Dump_Hash() {
    int i;

    if (Hash_Table ==  NULL) return;
    for (i=0; i<Node_Record_Count; i++) {
	if (Hash_Table[i] == (int)NULL) continue;
	printf("Hash:%d:%s\n", i, (Node_Record_Table_Ptr+Hash_Table[i])->Name);
    } /* for */
} /* Dump_Hash */


/* 
 * Find_Node_Record - Find a record for node with specified name,
 * Input: name - name of the desired node 
 * Output: return pointer to node record or NULL if not found
 */
struct Node_Record *Find_Node_Record(char *name) {
    struct Node_Record *Node_Record_Point;	/* Pointer to Node_Record */
    int i;

    /* Try to find in hash table first */
    if (Hash_Table != NULL) {
	i = Hash_Index(name);
        if (strcmp((Node_Record_Table_Ptr+Hash_Table[i])->Name, name) == 0) 
		return (Node_Record_Table_Ptr+Hash_Table[i]);
#if DEBUG_SYSTEM
	fprintf(stderr, "Find_Node_Record: Hash table lookup failure for %s\n", name);
#else
	syslog(LOG_DEBUG, "Find_Node_Record: Hash table lookup failure for %s\n", name)
#endif
    } /* if */

#if DEBUG_SYSTEM
    if (Hash_Table != NULL) {
	printf("Sequential search for %s\n", name);
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

    inx = inx % Node_Record_Count;
    return inx;
} /* Hash_Index */


/* 
 * Init_Node_Conf - Initialize the node configuration values. 
 * This should be called before creating any node or configuration entries.
 * Output: return value - 0 if no error, otherwise an error code
 */
int Init_Node_Conf() {

    strcpy(Default_Node_Record.Name, "DEFAULT");
    Default_Node_Record.NodeState    = STATE_UNKNOWN;
    Default_Node_Record.LastResponse = (time_t)0;
    Default_Config_Record.CPUs       = 1;
    Default_Config_Record.RealMemory = 1;
    Default_Config_Record.TmpDisk    = 1;
    Default_Config_Record.Weight     = 1;
    Default_Config_Record.Feature    = (char *)NULL;

    if (Config_List) {	/* Second time executed, warning */
#if DEBUG_SYSTEM
	fprintf(stderr, "Init_Node_Conf: executed more than once\n");
#else
	syslog(LOG_ERR, "Init_Node_Conf: executed more than once\n");
#endif
    } /* if */

    Config_List = list_create(NULL);
    if (Config_List == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Init_Node_Conf: list_create can not allocate memory\n");
#else
	syslog(LOG_ALARM, "Init_Node_Conf: list_create can not allocate memory\n");
#endif
	return ENOMEM;
    } /* if */
    return 0;
} /* Init_Node_Conf */


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
	syslog(LOG_ERR, "Parse_Node_Name: unable to allocate memory\n")
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
 * Rehash - Build a hash table of the Node_Record entries. This is a large hash table 
 * to permit the immediate finding of a record based only upon its name without regards 
 * to the number. There should be no need for a search. The algorithm is optimized for 
 * node names with a base-ten sequence number suffix. If you have a large cluster and 
 * use a different naming convention, this function and/or the Hash_Index function 
 * should be re-written.
 */
void Rehash() {
    struct Node_Record *Node_Record_Point;	/* Pointer to Node_Record */
    int i, inx;

    if (Hash_Table ==  (int *)NULL)
	Hash_Table = (int *)malloc(sizeof(int) * Node_Record_Count);
    else
	Hash_Table = (int *)realloc(Hash_Table, (sizeof(int) * Node_Record_Count));

    if (Hash_Table == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Rehash: list_append can not allocate memory\n");
#else
	syslog(LOG_ERR, "Rehash: list_append can not allocate memory\n");
#endif
	return;
    } /* if */
    memset(Hash_Table, 0, (sizeof(int *) * Node_Record_Count));

    for (i=0; i<Node_Record_Count; i++) {
	if (strlen((Node_Record_Table_Ptr+i)->Name) == 0) continue;
	inx = Hash_Index((Node_Record_Table_Ptr+i)->Name);
	Hash_Table[inx] = i;
    } /* for */

} /* Rehash */


