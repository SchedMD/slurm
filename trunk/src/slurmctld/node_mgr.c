/* 
 * node_mgr.c - Manage the node records of SLURM
 * See slurm.h for documentation on external functions and data structures
 *
 * Author: Moe Jette, jette@llnl.gov
 */

#define DEBUG_SYSTEM 1
#define PROTOTYPE_API 1

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif 

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

#include "list.h"
#include "slurm.h"

#define BUF_SIZE 	1024
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

#if PROTOTYPE_API
char *Node_API_Buffer = NULL;
int  Node_API_Buffer_Size = 0;

int Dump_Node(char **Buffer_Ptr, int *Buffer_Size, time_t *Update_Time);
int Load_Node(char *Buffer, int Buffer_Size);
int Load_Node_Config(int Index, int *CPUs, 
	int *RealMemory, int *TmpDisk, int *Weight, char **Features,
	char **Nodes, unsigned **NodeBitMap, int *BitMapSize);
int Load_Nodes_Idle(unsigned **NodeBitMap, int *BitMap_Size);
int Load_Node_Name(char *Req_Name, char *Next_Name, int *State, int *CPUs, 
	int *RealMemory, int *TmpDisk, int *Weight, char **Features);
int Load_Nodes_Up(unsigned **NodeBitMap, int *BitMap_Size);
#endif

#if DEBUG_MODULE
/* main is used here for testing purposes only */
main(int argc, char * argv[]) {
    int Error_Code, size;
    char *Out_Line;
    unsigned *Map1, *Map2, *Map3;
    unsigned U_Map[2];
    struct Config_Record *Config_Ptr;
    struct Node_Record *Node_Ptr;
    char *Format;
    int Start_Inx, End_Inx, Count_Inx;
    char Req_Name[MAX_NAME_LEN];	/* Name of the partition */
    char Next_Name[MAX_NAME_LEN];	/* Name of the next partition */
    int State, CPUs, RealMemory, TmpDisk, Weight;
    char *Features, *Nodes;
    char *Dump;
    int Dump_Size;
    time_t Update_Time;
    unsigned *NodeBitMap;	/* Bitmap of nodes in partition */
    int BitMapSize;		/* Bytes in NodeBitMap */

    /* Bitmap tests */
    Node_Record_Count = 97;
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
    if (BitMapValue(Map3, 23) != 1) printf("ERROR: BitMap Error 1\n");
    if (BitMapValue(Map3, 71) != 1) printf("ERROR: BitMap Error 2\n");
    if (BitMapValue(Map3, 93) != 0) printf("ERROR: BitMap Error 3\n");
    BitMapAND(Map3, Map2);
    if (BitMapValue(Map3, 23) != 0) printf("ERROR: BitMap Error 4\n");
    if (BitMapValue(Map3, 71) != 1) printf("ERROR: BitMap Error 5\n");
    if (BitMapValue(Map3, 93) != 0) printf("ERROR: BitMap Error 6\n");
    Out_Line = BitMapPrint(Map3);
    printf("BitMapPrint #3 shows %s\n", Out_Line);
    free(Out_Line);
    Node_Record_Count = 0;

    /* Now check out configuration and node structure functions */
    Error_Code = Init_Node_Conf();
    if (Error_Code) printf("ERROR: Init_Node_Conf error %d\n", Error_Code);
    Default_Config_Record.CPUs       = 12;
    Default_Config_Record.RealMemory = 345;
    Default_Config_Record.TmpDisk    = 67;
    Default_Config_Record.Weight     = 89;
    Default_Node_Record.LastResponse = (time_t)678;

    Config_Ptr = Create_Config_Record(&Error_Code);
    if (Error_Code) printf("ERROR: Create_Config_Record error %d\n", Error_Code);
    if (Config_Ptr->CPUs != 12)        printf("ERROR: Config default CPUs not set\n");
    if (Config_Ptr->RealMemory != 345) printf("ERROR: Config default RealMemory not set\n");
    if (Config_Ptr->TmpDisk != 67)     printf("ERROR: Config default TmpDisk not set\n");
    if (Config_Ptr->Weight != 89)      printf("ERROR: Config default Weight not set\n");
    Config_Ptr->Feature = "for_lx01";
    Config_Ptr->Nodes = "lx01";
    Config_Ptr->NodeBitMap = Map1;
    Node_Ptr   = Create_Node_Record(&Error_Code);
    if (Error_Code) printf("ERROR: Create_Node_Record error %d\n", Error_Code);
    strcpy(Node_Ptr->Name, "lx01");
    Node_Ptr->Config_Ptr = Config_Ptr;
    Node_Ptr   = Create_Node_Record(&Error_Code);
    if (Error_Code) printf("ERROR: Create_Node_Record error %d\n", Error_Code);
    strcpy(Node_Ptr->Name, "lx02");
    Node_Ptr->Config_Ptr = NULL;
    Node_Ptr   = Create_Node_Record(&Error_Code);
    if (Error_Code) printf("ERROR: Create_Node_Record error %d\n", Error_Code);
    Config_Ptr = Create_Config_Record(&Error_Code);
    Config_Ptr->CPUs = 543;
    Config_Ptr->Nodes = "lx[03-20]";
    Config_Ptr->Feature = "for_lx03";
    Config_Ptr->NodeBitMap = Map3;
    strcpy(Node_Ptr->Name, "lx03");
    if (Node_Ptr->LastResponse != (time_t)678) printf("ERROR: Node default LastResponse not set\n");
    Node_Ptr->Config_Ptr = Config_Ptr;
    Update_Time = (time_t)0;
    U_Map[0] = 0xdead;
    U_Map[1] = 0xbeef;
    Up_NodeBitMap = &U_Map[0];
    Idle_NodeBitMap = &U_Map[1];
    Error_Code = Dump_Node(&Dump, &Dump_Size, &Update_Time);
    if (Error_Code) printf("ERROR: Dump_Node error %d\n", Error_Code);

    Rehash();
    Dump_Hash();
    Node_Ptr   = Find_Node_Record("lx02");
    if (Node_Ptr == 0) 
	printf("ERROR: Find_Node_Record failure 1\n");
    else if (strcmp(Node_Ptr->Name, "lx02") != 0)
	printf("ERROR: Find_Node_Record failure 2\n");
    else if (Node_Ptr->LastResponse != (time_t)678) 
	printf("ERROR: Node default LastResponse not set\n");
    printf("NOTE: We expect Delete_Node_Record to report not finding a record for lx04\n");
    Error_Code = Delete_Node_Record("lx04");
    if (Error_Code != ENOENT) printf("ERROR: Delete_Node_Record failure 1\n");
    Error_Code = Delete_Node_Record("lx02");
    if (Error_Code != 0) printf("ERROR: Delete_Node_Record failure 2\n");
    printf("NOTE: We expect Find_Node_Record to report not finding a record for lx02\n");
    Node_Ptr   = Find_Node_Record("lx02");
    if (Node_Ptr != 0) printf("ERROR: Find_Node_Record failure 3\n");

    /* Check node name parsing */
    Out_Line = "linux[003-234]";
    Error_Code = Parse_Node_Name(Out_Line, &Format, &Start_Inx, &End_Inx, &Count_Inx);
    if (Error_Code != 0) 
	printf("ERROR: Parse_Node_Name error %d\n", Error_Code);
    else {
	if ((Start_Inx != 3) || (End_Inx != 234)) printf("ERROR: Parse_Node_Name failure\n");
	printf("Parse_Node_Name of \"%s\" produces format \"%s\", %d to %d, %d records\n", 
	    Out_Line, Format, Start_Inx, End_Inx, Count_Inx);
	free(Format);
    } /* else */

#if PROTOTYPE_API
#if DEBUG_MODULE > 1
    printf("int:%d time_t:%d:\n", sizeof(int), sizeof(time_t));
    for (Start_Inx=0; Start_Inx<Dump_Size; Start_Inx++) {
	End_Inx = (int)Dump[Start_Inx];
	printf("%2.2x ",(unsigned)End_Inx);
	if (Start_Inx%20 == 19) printf("\n");
    }
#endif

    Error_Code = Load_Node(Dump, Dump_Size);
    if (Error_Code) printf("Load_Node error %d\n", Error_Code);

    Error_Code =  Load_Nodes_Up(&NodeBitMap, &BitMapSize);
    if (Error_Code) printf("Load_Nodes_Up error %d\n", Error_Code);
    if (BitMapSize > 0) printf("Load_Nodes_Up  BitMap[0]=0x%x, BitMapSize=%d\n", 
			NodeBitMap[0], BitMapSize);

    Error_Code =  Load_Nodes_Idle(&NodeBitMap, &BitMapSize);
    if (Error_Code) printf("Load_Nodes_Idle error %d\n", Error_Code);
    if (BitMapSize > 0) printf("Load_Nodes_Idle  BitMap[0]=0x%x, BitMapSize=%d\n", 
			NodeBitMap[0], BitMapSize);

    for (Start_Inx=0; ; Start_Inx++) {
	Error_Code = Load_Node_Config(Start_Inx, &CPUs, &RealMemory, &TmpDisk, &Weight, 
	    &Features, &Nodes, &NodeBitMap, &BitMapSize);
	if (Error_Code == ENOENT) break;
	if (Error_Code != 0)  {
	    printf("Load_Node_Config error %d\n", Error_Code);
	    break;
	} /* if */

	printf("Found config CPUs=%d, RealMemory=%d, TmpDisk=%d, ", 
	    CPUs, RealMemory, TmpDisk);
	printf("Weight=%d, Features=%s, Nodes=%s\n", Weight, Features, Nodes);
	if (BitMapSize > 0) 
	    printf("  BitMap[0]=0x%x, BitMapSize=%d\n", NodeBitMap[0], BitMapSize);
    } /* for */

    strcpy(Req_Name, "");	/* Start at beginning of partition list */
    while (1) {
	Error_Code = Load_Node_Name(Req_Name, Next_Name, &State, 
		&CPUs, &RealMemory, &TmpDisk, &Weight, &Features);
	if (Error_Code != 0)  {
	    printf("Load_Node_Name error %d\n", Error_Code);
	    break;
	} /* if */

	printf("Found node Name=%s, State=%d, CPUs=%d, RealMemory=%d, TmpDisk=%d, ", 
	    Req_Name, State, CPUs, RealMemory, TmpDisk);
	printf("Weight=%d, Features=%s\n", Weight, Features);

	if (strlen(Next_Name) == 0) break;
	strcpy(Req_Name, Next_Name);
    } /* while */
#endif
    free(Dump);

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

    size = (Node_Record_Count + (sizeof(unsigned)*8) - 1) / 8;	/* Bytes */
    Output = malloc(size);
    if (Output == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "BitMapCopy: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "BitMapCopy: unable to allocate memory\n");
#endif
	return NULL;
    } /* if */

    size /= sizeof(unsigned);			/* Count of unsigned's */
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
    Output = (char *)malloc(nibbles+3);
    if (Output == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "BitMapPrint: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "BitMapPrint: unable to allocate memory\n");
#endif
	return NULL;
    } /* if */

    strcpy(Output, "0x");
    k = 0;
    for (i=0; i<size; i++) {				/* Each unsigned */
	for (j=((sizeof(unsigned)*8)-4); j>=0; j-=4) {	/* Each nibble */
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

    Last_Node_Update = time(NULL);
    Config_Point = (struct Config_Record *)malloc(sizeof(struct Config_Record));
    if (Config_Point == (struct Config_Record *)NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Create_Config_Record: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Create_Config_Record: unable to allocate memory\n");
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
    if (Default_Config_Record.Feature) {
	Config_Point->Feature = (char *)malloc(strlen(Default_Config_Record.Feature)+1);
	if (Config_Point->Feature == (char *)NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Create_Config_Record: unable to allocate memory\n");
#else
	    syslog(LOG_ALERT, "Create_Config_Record: unable to allocate memory\n");
#endif
	    free(Config_Point);
	    *Error_Code = ENOMEM;
	    return (struct Config_Record *)NULL;
	} /* if */
	strcpy(Config_Point->Feature, Default_Config_Record.Feature);
    } else
	Config_Point->Feature = (char *)NULL;

    if (list_append(Config_List, Config_Point) == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Create_Config_Record: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Create_Config_Record: unable to allocate memory\n");
#endif
	if (Config_Point->Feature) free(Config_Point->Feature);
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
    Last_Node_Update = time(NULL);

    /* Round up the buffer size to reduce overhead of realloc */
    Old_Buffer_Size = (Node_Record_Count) * sizeof(struct Node_Record);
    Old_Buffer_Size = ((int)((Old_Buffer_Size / BUF_SIZE) + 1)) * BUF_SIZE;
    New_Buffer_Size = (Node_Record_Count+1) * sizeof(struct Node_Record);
    New_Buffer_Size = ((int)((New_Buffer_Size / BUF_SIZE) + 1)) * BUF_SIZE;
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

    Last_Node_Update = time(NULL);
    Node_Record_Point = Find_Node_Record(name);
    if (Node_Record_Point == (struct Node_Record *)NULL) {
#if DEBUG_MODULE
	fprintf(stderr, "Delete_Node_Record: Attempt to delete non-existent node %s\n", name);
#else
	syslog(LOG_ALERT, "Delete_Node_Record: Attempt to delete non-existent node %s\n", name);
#endif
	return ENOENT;
    } /* if */

    strcpy(Node_Record_Point->Name, "");
    Node_Record_Point->NodeState = STATE_DOWN;
    return 0;
} /* Delete_Node_Record */


/* Print the Hash_Table contents, used for debugging or analysis of hash technique */
void Dump_Hash() {
    int i;

    if (Hash_Table ==  NULL) return;
    for (i=0; i<Node_Record_Count; i++) {
	if (strlen((Node_Record_Table_Ptr+Hash_Table[i])->Name) == 0) continue;
	printf("Hash:%d:%s\n", i, (Node_Record_Table_Ptr+Hash_Table[i])->Name);
    } /* for */
} /* Dump_Hash */


/* 
 * Dump_Node - Dump all configuration and node information to a buffer
 * Input: Buffer_Ptr - Location into which a pointer to the data is to be stored.
 *                     The data buffer is actually allocated by Dump_Node and the 
 *                     calling function must free the storage.
 *         Buffer_Size - Location into which the size of the created buffer is in bytes
 *         Update_Time - Dump new data only if partition records updated since time 
 *                       specified, otherwise return empty buffer
 * Output: Buffer_Ptr - The pointer is set to the allocated buffer.
 *         Buffer_Size - Set to size of the buffer in bytes
 *         Update_Time - set to time partition records last updated
 *         Returns 0 if no error, errno otherwise
 * NOTE: In this prototype, the buffer at *Buffer_Ptr must be freed by the caller
 * NOTE: This is a prototype for a function to ship data partition to an API.
 */
int Dump_Node(char **Buffer_Ptr, int *Buffer_Size, time_t *Update_Time) {
    ListIterator Config_Record_Iterator;	/* For iterating through Config_List */
    struct Config_Record *Config_Record_Point;	/* Pointer to Config_Record */
    char *Buffer, *Buffer_Loc;
    int Buffer_Allocated, i, inx, Record_Size;
    struct Config_Specs {
	struct Config_Record *Config_Record_Point;
    };
    struct Config_Specs *Config_Spec_List = NULL;
    int Config_Spec_List_Cnt = 0;

    Buffer_Ptr[0] = NULL;
    *Buffer_Size = 0;
    if (*Update_Time == Last_Node_Update) return 0;

    Config_Record_Iterator = list_iterator_create(Config_List);
    if (Config_Record_Iterator == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Dump_Node: list_iterator_create unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Dump_Node: list_iterator_create unable to allocate memory\n");
#endif
	return ENOMEM;
    } /* if */

    Buffer_Allocated = BUF_SIZE + (Node_Record_Count*2);
    Buffer = malloc(Buffer_Allocated);
    if (Buffer == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Dump_Node: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Dump_Node: unable to allocate memory\n");
#endif
	list_iterator_destroy(Config_Record_Iterator);
	return ENOMEM;
    } /* if */

    /* Write haeader, version and time */
    Buffer_Loc = Buffer;
    i = CONFIG_STRUCT_VERSION;
    memcpy(Buffer_Loc, &i, sizeof(i)); 
    Buffer_Loc += sizeof(i);
    memcpy(Buffer_Loc, &Last_Node_Update, sizeof(Last_Node_Update));
    Buffer_Loc += sizeof(Last_Part_Update);

    /* Write up and idle node bitmaps */
    if ((Node_Record_Count > 0) && Up_NodeBitMap){
	i = (Node_Record_Count + (sizeof(unsigned)*8) - 1) / (sizeof(unsigned)*8);
	i *= sizeof(unsigned);
	memcpy(Buffer_Loc, &i, sizeof(i)); 
	Buffer_Loc += sizeof(i);
	memcpy(Buffer_Loc, Up_NodeBitMap, i); 
	Buffer_Loc += i;
    } else {
	i = 0;
	memcpy(Buffer_Loc, &i, sizeof(i)); 
	Buffer_Loc += sizeof(i);
    } /* else */
    if ((Node_Record_Count > 0) && Idle_NodeBitMap){
	i = (Node_Record_Count + (sizeof(unsigned)*8) - 1) / (sizeof(unsigned)*8);
	i *= sizeof(unsigned);
	memcpy(Buffer_Loc, &i, sizeof(i)); 
	Buffer_Loc += sizeof(i);
	memcpy(Buffer_Loc, Idle_NodeBitMap, i); 
	Buffer_Loc += i;
    } else {
	i = 0;
	memcpy(Buffer_Loc, &i, sizeof(i)); 
	Buffer_Loc += sizeof(i);
    } /* else */

    /* Write configuration records */
    while (Config_Record_Point = (struct Config_Record *)list_next(Config_Record_Iterator)) {
	Record_Size = (7 * sizeof(int)) + ((Node_Record_Count + (sizeof(unsigned)*8) - 1) / 8);
	if (Config_Record_Point->Feature) Record_Size+=strlen(Config_Record_Point->Feature)+1;
	if (Config_Record_Point->Nodes) Record_Size+=strlen(Config_Record_Point->Nodes)+1;

	if ((Buffer_Loc-Buffer+Record_Size) >= Buffer_Allocated) { /* Need larger buffer */
	    Buffer_Allocated += (Record_Size + BUF_SIZE);
	    Buffer = realloc(Buffer, Buffer_Allocated);
	    if (Buffer == NULL) {
#if DEBUG_SYSTEM
		fprintf(stderr, "Dump_Node: unable to allocate memory\n");
#else
		syslog(LOG_ALERT, "Dump_Node: unable to allocate memory\n");
#endif
		list_iterator_destroy(Config_Record_Iterator);
		return ENOMEM;
	    } /* if */
	} /* if */

	if (Config_Spec_List_Cnt == 0) 
	    Config_Spec_List = malloc(sizeof(struct Config_Specs));
	else
	    Config_Spec_List = realloc(Config_Spec_List, 
		(Config_Spec_List_Cnt+1)*sizeof(struct Config_Specs));
	if (Config_Spec_List == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Dump_Node: unable to allocate memory\n");
#else
	    syslog(LOG_ALERT, "Dump_Node: unable to allocate memory\n");
#endif
	    list_iterator_destroy(Config_Record_Iterator);
	    return ENOMEM;
	} /* if */
	Config_Spec_List[Config_Spec_List_Cnt++].Config_Record_Point = Config_Record_Point;

	memcpy(Buffer_Loc, &Config_Record_Point->CPUs, sizeof(Config_Record_Point->CPUs)); 
	Buffer_Loc += sizeof(Config_Record_Point->CPUs);

	memcpy(Buffer_Loc, &Config_Record_Point->RealMemory, 
		sizeof(Config_Record_Point->RealMemory)); 
	Buffer_Loc += sizeof(Config_Record_Point->RealMemory);

	memcpy(Buffer_Loc, &Config_Record_Point->TmpDisk, sizeof(Config_Record_Point->TmpDisk)); 
	Buffer_Loc += sizeof(Config_Record_Point->TmpDisk);

	memcpy(Buffer_Loc, &Config_Record_Point->Weight, sizeof(Config_Record_Point->Weight)); 
	Buffer_Loc += sizeof(Config_Record_Point->Weight);

	if (Config_Record_Point->Feature) {
	    i = strlen(Config_Record_Point->Feature) + 1;
	    memcpy(Buffer_Loc, &i, sizeof(i)); 
	    Buffer_Loc += sizeof(i);
	    memcpy(Buffer_Loc, Config_Record_Point->Feature, i); 
	    Buffer_Loc += i;
	} else {
	    i = 0;
	    memcpy(Buffer_Loc, &i, sizeof(i)); 
	    Buffer_Loc += sizeof(i);
	} /* else */

	if (Config_Record_Point->Nodes) {
	    i = strlen(Config_Record_Point->Nodes) + 1;
	    memcpy(Buffer_Loc, &i, sizeof(i)); 
	    Buffer_Loc += sizeof(i);
	    memcpy(Buffer_Loc, Config_Record_Point->Nodes, i); 
	    Buffer_Loc += i;
	} else {
	    i = 0;
	    memcpy(Buffer_Loc, &i, sizeof(i)); 
	    Buffer_Loc += sizeof(i);
	} /* else */

	if ((Node_Record_Count > 0) && (Config_Record_Point->NodeBitMap)){
	    i = (Node_Record_Count + (sizeof(unsigned)*8) - 1) / (sizeof(unsigned)*8);
	    i *= sizeof(unsigned);
	    memcpy(Buffer_Loc, &i, sizeof(i)); 
	    Buffer_Loc += sizeof(i);
	    memcpy(Buffer_Loc, Config_Record_Point->NodeBitMap, i); 
	    Buffer_Loc += i;
	} else {
	    i = 0;
	    memcpy(Buffer_Loc, &i, sizeof(i)); 
	    Buffer_Loc += sizeof(i);
	} /* else */

    } /* while */
    list_iterator_destroy(Config_Record_Iterator);

    /* Mark end of configuration data , looks like CPUs = -1 */
    i = -1;
    memcpy(Buffer_Loc, &i, sizeof(i)); 
    Buffer_Loc += sizeof(i);


    /* Write node records */
    for (inx=0; inx<Node_Record_Count; inx++) {
	if (strlen((Node_Record_Table_Ptr+inx)->Name) == 0) continue;
	Record_Size = MAX_NAME_LEN + 2 * sizeof(int);
	if ((Buffer_Loc-Buffer+Record_Size) >= Buffer_Allocated) { /* Need larger buffer */
	    Buffer_Allocated += (Record_Size + BUF_SIZE);
	    Buffer = realloc(Buffer, Buffer_Allocated);
	    if (Buffer == NULL) {
#if DEBUG_SYSTEM
		fprintf(stderr, "Dump_Node: unable to allocate memory\n");
#else
		syslog(LOG_ALERT, "Dump_Node: unable to allocate memory\n");
#endif
		return ENOMEM;
	    } /* if */
	} /* if */

	memcpy(Buffer_Loc, (Node_Record_Table_Ptr+inx)->Name, 
		sizeof((Node_Record_Table_Ptr+inx)->Name)); 
	Buffer_Loc += sizeof((Node_Record_Table_Ptr+inx)->Name);

	i = (int)(Node_Record_Table_Ptr+inx)->NodeState;
	memcpy(Buffer_Loc, &i, sizeof(i)); 
	Buffer_Loc += sizeof(i);

	for (i=0; i<Config_Spec_List_Cnt; i++) {
	    if (Config_Spec_List[i].Config_Record_Point ==
		(Node_Record_Table_Ptr+inx)->Config_Ptr) break;
	} /* for (i */
	if (i < Config_Spec_List_Cnt) 
	    i++;
	else
	    i = 0;
	memcpy(Buffer_Loc, &i, sizeof(i)); 
	Buffer_Loc += sizeof(i);

    } /* for (inx */

    Buffer = realloc(Buffer, (int)(Buffer_Loc - Buffer));
    if (Buffer == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Dump_Node: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Dump_Node: unable to allocate memory\n");
#endif
	return ENOMEM;
    } /* if */

    Buffer_Ptr[0] = Buffer;
    *Buffer_Size = (int)(Buffer_Loc - Buffer);
    *Update_Time = Last_Node_Update;
    return 0;
} /* Dump_Node */


/* 
 * Find_Node_Record - Find a record for node with specified name,
 * Input: name - name of the desired node 
 * Output: return pointer to node record or NULL if not found
 */
struct Node_Record *Find_Node_Record(char *name) {
    struct Node_Record *Node_Record_Point;	/* Pointer to Node_Record */
    int i;

    /* Try to find in hash table first */
    if (Hash_Table) {
	i = Hash_Index(name);
        if (strcmp((Node_Record_Table_Ptr+Hash_Table[i])->Name, name) == 0) 
		return (Node_Record_Table_Ptr+Hash_Table[i]);
#if DEBUG_SYSTEM
	fprintf(stderr, "Find_Node_Record: Hash table lookup failure for %s\n", name);
#else
	syslog(LOG_DEBUG, "Find_Node_Record: Hash table lookup failure for %s\n", name);
#endif
    } /* if */

#if DEBUG_SYSTEM
    if (Hash_Table) {
	printf("Sequential search for %s\n", name);
	Dump_Hash();
    } /* if */
#endif

    /* Revert to sequential search */
    for (i=0; i<Node_Record_Count; i++) {
	if (strcmp(name, (Node_Record_Table_Ptr+i)->Name) != 0) continue;
	return (Node_Record_Table_Ptr+i);
    } /* for */

#if DEBUG_SYSTEM
    fprintf(stderr, "Find_Node_Record: Lookup failure for %s\n", name);
#else
    syslog(LOG_ERR, "Find_Node_Record: Lookup failure for %s\n", name);
#endif
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

    Last_Node_Update = time(NULL);
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
	syslog(LOG_WARNING, "Init_Node_Conf: executed more than once\n");
#endif
    } /* if */

    Config_List = list_create(NULL);
    if (Config_List == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Init_Node_Conf: list_create can not allocate memory\n");
#else
	syslog(LOG_ALERT, "Init_Node_Conf: list_create can not allocate memory\n");
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
	syslog(LOG_ERR, "Parse_Node_Name: unable to allocate memory\n");
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
	} else if (NodeName[i] == '[') {		/* '[' preceeding number range */
	    if (NodeName[++i] == (char)NULL) break;
	    if (Base != 0) {
#if DEBUG_SYSTEM
		fprintf(stderr, "Parse_Node_Name: Invalid '[' in node name %s\n", NodeName);
#else
		syslog(LOG_ALERT, "Parse_Node_Name: Invalid '[' in node name %s\n", NodeName);
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
		if (NodeName[i] == '-') {		/* '-' between numbers */
		    i++;
		    break;
		} /* if */
#if DEBUG_SYSTEM
		fprintf(stderr, "Parse_Node_Name: Invalid '%c' in node name %s\n", 
			NodeName[i], NodeName);
#else
		syslog(LOG_ALERT, "Parse_Node_Name: Invalid '%c' in node name %s\n", 
			NodeName[i], NodeName);
#endif
		free(Format[0]);
		return EINVAL;
	    } /* while */
	    while (1) {
		if ((NodeName[i] >= '0') && (NodeName[i] <= '9')) {
		    *End_Inx = ((*End_Inx) * Base) + (int)(NodeName[i++] - '0');
		    continue;
		} /* if */
		if (NodeName[i] == ']') {		/* ']' terminating number range */ 
		    i++;
		    break;
		} /* if */
#if DEBUG_SYSTEM
		fprintf(stderr, "Parse_Node_Name: Invalid '%c' in node name %s\n", 
			NodeName[i], NodeName);
#else
		syslog(LOG_ALERT, "Parse_Node_Name: Invalid '%c' in node name %s\n", 
			NodeName[i], NodeName);
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

    Hash_Table = (int *)realloc(Hash_Table, (sizeof(int) * Node_Record_Count));

    if (Hash_Table == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Rehash: list_append can not allocate memory\n");
#else
	syslog(LOG_ERR, "Rehash: list_append can not allocate memory\n");
#endif
	return;
    } /* if */
    memset(Hash_Table, 0, (sizeof(int) * Node_Record_Count));

    for (i=0; i<Node_Record_Count; i++) {
	if (strlen((Node_Record_Table_Ptr+i)->Name) == 0) continue;
	inx = Hash_Index((Node_Record_Table_Ptr+i)->Name);
	Hash_Table[inx] = i;
    } /* for */

} /* Rehash */


#if PROTOTYPE_API
/*
 * Load_Node - Load the supplied node information buffer for use by info gathering APIs
 * Input: Buffer - Pointer to node information buffer
 *        Buffer_Size - size of Buffer
 * Output: Returns 0 if no error, EINVAL if the buffer is invalid
 */
int Load_Node(char *Buffer, int Buffer_Size) {
    int Version;

    if (Buffer_Size < (4*sizeof(int))) return EINVAL;	/* Too small to be legitimate */

    memcpy(&Version, Buffer, sizeof(Version));
    if (Version != CONFIG_STRUCT_VERSION) return EINVAL;	/* Incompatable versions */

    Node_API_Buffer = Buffer;
    Node_API_Buffer_Size = Buffer_Size;
    return 0;
} /* Load_Node */


/* 
 * Load_Node_Config - Load the state information about configuration at specified inxed
 * Input: Index - zero origin index of the configuration requested
 *        CPUs, etc. - Pointers into which the information is to be stored
 * Output: CPUs, etc. - The node's state information
 *         BitMap_Size - Size of BitMap in bytes
 *         Returns 0 on success, ENOENT if not found, or EINVAL if buffer is bad
 */
int Load_Node_Config(int Index, int *CPUs, 
	int *RealMemory, int *TmpDisk, int *Weight, char **Features, 
	char **Nodes, unsigned **NodeBitMap, int *BitMap_Size) {
    int i, Config_Num, Version;
    time_t Update_Time;
    char *Buffer_Loc;
    struct Config_Record Read_Config_List;
    int Read_Config_List_Cnt = 0;
    struct Node_Record My_Node_Entry;
    int My_BitMap_Size;

    /* Load buffer's header */
    Buffer_Loc = Node_API_Buffer;
    memcpy(&Version, Buffer_Loc, sizeof(Version));
    Buffer_Loc += sizeof(Version);
    memcpy(&Update_Time, Buffer_Loc, sizeof(Update_Time));
    Buffer_Loc += sizeof(Update_Time);

    /* Read up and idle node bitmaps */
    memcpy(&i, Buffer_Loc, sizeof(i)); 
    Buffer_Loc += (sizeof(i) + i);
    memcpy(&i, Buffer_Loc, sizeof(i)); 
    Buffer_Loc += (sizeof(i) + i);

    /* Load the configuration records */
    while ((Buffer_Loc+(sizeof(int)*7)) <= 
	   (Node_API_Buffer+Node_API_Buffer_Size)) {	

	/* Load all info for next configuration */
	memcpy(&Read_Config_List.CPUs, Buffer_Loc, 
		sizeof(Read_Config_List.CPUs)); 
	Buffer_Loc += sizeof(Read_Config_List.CPUs);
	if (Read_Config_List.CPUs == -1) break; /* End of config recs */

	memcpy(&Read_Config_List.RealMemory, Buffer_Loc, 
		sizeof(Read_Config_List.RealMemory)); 
	Buffer_Loc += sizeof(Read_Config_List.RealMemory);

	memcpy(&Read_Config_List.TmpDisk, Buffer_Loc, 
		sizeof(Read_Config_List.TmpDisk)); 
	Buffer_Loc += sizeof(Read_Config_List.TmpDisk);

	memcpy(&Read_Config_List.Weight, Buffer_Loc, 
		sizeof(Read_Config_List.Weight)); 
	Buffer_Loc += sizeof(Read_Config_List.Weight);

	memcpy(&i, Buffer_Loc, sizeof(i)); 
	Buffer_Loc += sizeof(i);
	if ((Buffer_Loc+i) > (Node_API_Buffer+Node_API_Buffer_Size)) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Load_Node_Config: malformed buffer\n");
#else
	    syslog(LOG_ERR, "Load_Node_Config: malformed buffer\n");
#endif
	    return EINVAL;
	} /* if */
	if (i)
	    Read_Config_List.Feature = Buffer_Loc;
	else
	    Read_Config_List.Feature = NULL;
	Buffer_Loc += i;

	memcpy(&i, Buffer_Loc, sizeof(i)); 
	Buffer_Loc += sizeof(i);
	if ((Buffer_Loc+i) > (Node_API_Buffer+Node_API_Buffer_Size)) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Load_Node_Config: malformed buffer\n");
#else
	    syslog(LOG_ERR, "Load_Node_Config: malformed buffer\n");
#endif
	    return EINVAL;
	} /* if */
	if (i)
	    Read_Config_List.Nodes = Buffer_Loc;
	else
	    Read_Config_List.Nodes = NULL;
	Buffer_Loc += i;

	memcpy(&My_BitMap_Size, Buffer_Loc, sizeof(My_BitMap_Size)); 
	Buffer_Loc += sizeof(My_BitMap_Size);
	if ((Buffer_Loc+My_BitMap_Size) > (Node_API_Buffer+Node_API_Buffer_Size)) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Load_Node_Config: malformed buffer\n");
#else
	    syslog(LOG_ERR, "Load_Node_Config: malformed buffer\n");
#endif
	    return EINVAL;
	} /* if */
	if (My_BitMap_Size)
	    Read_Config_List.NodeBitMap = (unsigned *)Buffer_Loc;
	else
	    Read_Config_List.NodeBitMap = NULL;
	Buffer_Loc += My_BitMap_Size;

	if (Read_Config_List_Cnt++ != Index) continue;

	*CPUs 		= Read_Config_List.CPUs;
	*RealMemory	= Read_Config_List.RealMemory;
	*TmpDisk	= Read_Config_List.TmpDisk;
	*Weight		= Read_Config_List.Weight;
	Features[0]	= Read_Config_List.Feature;
	Nodes[0]	= Read_Config_List.Nodes;
	NodeBitMap[0]	= Read_Config_List.NodeBitMap;
	*BitMap_Size	= My_BitMap_Size;
	return 0;
    } /* while */

    *CPUs 		= 0;
    *RealMemory		= 0;
    *TmpDisk		= 0;
    *Weight		= 0;
    Features[0]		= NULL;
    NodeBitMap[0]	= NULL;
    *BitMap_Size	= 0;
    return ENOENT;
} /* Load_Node_Config */


/* 
 * Load_Nodes_Idle - Load the bitmap of idle nodes
 * Input: NodeBitMap - Location to put bitmap pointer
 *        BitMap_Size - Location into which the byte size of NodeBitMap is to be stored
 * Output: NodeBitMap - Pointer to bitmap
 *         BitMap_Size - Byte size of NodeBitMap
 *         Returns 0 on success or EINVAL if buffer is bad
 */
int Load_Nodes_Idle(unsigned **NodeBitMap, int *BitMap_Size) {
    int i, Config_Num, Version;
    time_t Update_Time;
    char *Buffer_Loc;

    /* Load buffer's header */
    Buffer_Loc = Node_API_Buffer;
    memcpy(&Version, Buffer_Loc, sizeof(Version));
    Buffer_Loc += sizeof(Version);
    memcpy(&Update_Time, Buffer_Loc, sizeof(Update_Time));
    Buffer_Loc += sizeof(Update_Time);

    /* Read up and idle node bitmaps */
    memcpy(&i, Buffer_Loc, sizeof(i)); 
    Buffer_Loc += sizeof(i) + i;
    memcpy(&i, Buffer_Loc, sizeof(i)); 
    Buffer_Loc += sizeof(i);

    if ((Buffer_Loc+i) > (Node_API_Buffer+Node_API_Buffer_Size)) return EINVAL;	
    NodeBitMap[0] = (unsigned *)Buffer_Loc;
    *BitMap_Size = i;
    return 0;
} /* Load_Nodes_Idle */


/* 
 * Load_Node_Name - Load the state information about the named node
 * Input: Req_Name - Name of the node for which information is requested
 *		     if "", then get info for the first node in list
 *        Next_Name - Location into which the name of the next node is 
 *                   stored, "" if no more
 *        State, etc. - Pointers into which the information is to be stored
 * Output: Req_Name - The node's name is stored here
 *         Next_Name - The name of the next node in the list is stored here
 *         State, etc. - The node's state information
 *         Returns 0 on success, ENOENT if not found, or EINVAL if buffer is bad
 */
int Load_Node_Name(char *Req_Name, char *Next_Name, int *State, int *CPUs, 
	int *RealMemory, int *TmpDisk, int *Weight, char **Features) {
    int i, Config_Num, Version;
    time_t Update_Time;
    char *Buffer_Loc;
    struct Config_Record *Read_Config_List = NULL;
    int Read_Config_List_Cnt = 0;
    struct Node_Record My_Node_Entry;

    /* Load buffer's header */
    Buffer_Loc = Node_API_Buffer;
    memcpy(&Version, Buffer_Loc, sizeof(Version));
    Buffer_Loc += sizeof(Version);
    memcpy(&Update_Time, Buffer_Loc, sizeof(Update_Time));
    Buffer_Loc += sizeof(Update_Time);

    /* Read up and idle node bitmaps */
    memcpy(&i, Buffer_Loc, sizeof(i)); 
    Buffer_Loc += sizeof(i) + i;
    memcpy(&i, Buffer_Loc, sizeof(i)); 
    Buffer_Loc += sizeof(i) + i;

    /* Load the configuration records */
    while ((Buffer_Loc+(sizeof(int)*7)) <= 
	   (Node_API_Buffer+Node_API_Buffer_Size)) {	
	if (Read_Config_List_Cnt)
	    Read_Config_List = realloc(Read_Config_List, 
		sizeof(struct Config_Record) * (Read_Config_List_Cnt+1));
	else
	    Read_Config_List = malloc(sizeof(struct Config_Record));
	if (Read_Config_List == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Load_Node_Name: unable to allocate memory\n");
#else
	    syslog(LOG_ALERT, "Load_Node_Name: unable to allocate memory\n");
#endif
	    return ENOMEM;
	} /* if */

	/* Load all info for next configuration */
	memcpy(&Read_Config_List[Read_Config_List_Cnt].CPUs, Buffer_Loc, 
		sizeof(Read_Config_List[Read_Config_List_Cnt].CPUs)); 
	Buffer_Loc += sizeof(Read_Config_List[Read_Config_List_Cnt].CPUs);
	if (Read_Config_List[Read_Config_List_Cnt].CPUs == -1) break; /* End of config recs */

	memcpy(&Read_Config_List[Read_Config_List_Cnt].RealMemory, Buffer_Loc, 
		sizeof(Read_Config_List[Read_Config_List_Cnt].RealMemory)); 
	Buffer_Loc += sizeof(Read_Config_List[Read_Config_List_Cnt].RealMemory);

	memcpy(&Read_Config_List[Read_Config_List_Cnt].TmpDisk, Buffer_Loc, 
		sizeof(Read_Config_List[Read_Config_List_Cnt].TmpDisk)); 
	Buffer_Loc += sizeof(Read_Config_List[Read_Config_List_Cnt].TmpDisk);

	memcpy(&Read_Config_List[Read_Config_List_Cnt].Weight, Buffer_Loc, 
		sizeof(Read_Config_List[Read_Config_List_Cnt].Weight)); 
	Buffer_Loc += sizeof(Read_Config_List[Read_Config_List_Cnt].Weight);

	memcpy(&i, Buffer_Loc, sizeof(i)); 
	Buffer_Loc += sizeof(i);
	if ((Buffer_Loc+i) > (Node_API_Buffer+Node_API_Buffer_Size)) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Load_Node_Name: malformed buffer\n");
#else
	    syslog(LOG_ERR, "Load_Node_Name: malformed buffer\n");
#endif
	    free(Read_Config_List);
	    return EINVAL;
	} /* if */
	if (i)
	    Read_Config_List[Read_Config_List_Cnt].Feature = Buffer_Loc;
	else
	    Read_Config_List[Read_Config_List_Cnt].Feature = NULL;
	Buffer_Loc += i;

	memcpy(&i, Buffer_Loc, sizeof(i)); 
	Buffer_Loc += sizeof(i);
	if ((Buffer_Loc+i) > (Node_API_Buffer+Node_API_Buffer_Size)) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Load_Node_Name: malformed buffer\n");
#else
	    syslog(LOG_ERR, "Load_Node_Name: malformed buffer\n");
#endif
	    free(Read_Config_List);
	    return EINVAL;
	} /* if */
	/* List of nodes in the configuration is here */
	Buffer_Loc += i;

	memcpy(&i, Buffer_Loc, sizeof(i)); 
	Buffer_Loc += sizeof(i);
	if ((Buffer_Loc+i) > (Node_API_Buffer+Node_API_Buffer_Size)) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Load_Node_Name: malformed buffer\n");
#else
	    syslog(LOG_ERR, "Load_Node_Name: malformed buffer\n");
#endif
	    free(Read_Config_List);
	    return EINVAL;
	} /* if */
	/* Bitmap of nodes in the configuration is here */
	Buffer_Loc += i;

#if 0
	printf("CPUs=%d, ", Read_Config_List[Read_Config_List_Cnt].CPUs);
	printf("RealMemory=%d, ", Read_Config_List[Read_Config_List_Cnt].RealMemory);
	printf("TmpDisk=%d, ", Read_Config_List[Read_Config_List_Cnt].TmpDisk);
	printf("Weight=%d, ", Read_Config_List[Read_Config_List_Cnt].Weight);
	printf("Feature=%s\n", Read_Config_List[Read_Config_List_Cnt].Feature);
#endif
	Read_Config_List_Cnt++;
    } /* while */

    /* Load and scan the node records */
    while ((Buffer_Loc+sizeof(My_Node_Entry.Name)+(sizeof(int)*2)) <= 
	   (Node_API_Buffer+Node_API_Buffer_Size)) {	
	memcpy(&My_Node_Entry.Name, Buffer_Loc, sizeof(My_Node_Entry.Name)); 
	Buffer_Loc += sizeof(My_Node_Entry.Name);
	if (strlen(Req_Name) == 0) strcpy(Req_Name, My_Node_Entry.Name);

	memcpy(&My_Node_Entry.NodeState, Buffer_Loc, sizeof(My_Node_Entry.NodeState)); 
	Buffer_Loc += sizeof(My_Node_Entry.NodeState);

	memcpy(&Config_Num, Buffer_Loc, sizeof(Config_Num)); 
	Buffer_Loc += sizeof(Config_Num);

#if 0
	printf("Name=%s, ", My_Node_Entry.Name);
	printf("NodeState=%d, ", My_Node_Entry.NodeState);
	printf("Config_Num=%d\n", Config_Num);
#endif

	if (strcmp(My_Node_Entry.Name, Req_Name) != 0) continue;
	*State = My_Node_Entry.NodeState;
	if (Config_Num == 0) {
	    *CPUs 	= 0;
	    *RealMemory	= 0;
	    *TmpDisk	= 0;
	    *Weight	= 0;
	    Features[0]	= NULL;
	} else {
	    Config_Num--;
	    *CPUs 	= Read_Config_List[Config_Num].CPUs;
	    *RealMemory	= Read_Config_List[Config_Num].RealMemory;
	    *TmpDisk	= Read_Config_List[Config_Num].TmpDisk;
	    *Weight	= Read_Config_List[Config_Num].Weight;
	    Features[0]	= Read_Config_List[Config_Num].Feature;
	} /* else */
	if ((Buffer_Loc+sizeof(My_Node_Entry.Name)) <=
	    (Node_API_Buffer+Node_API_Buffer_Size)) 
	    memcpy(Next_Name, Buffer_Loc, sizeof(My_Node_Entry.Name));
	else
	    strcpy(Next_Name, "");

	if (Read_Config_List) free(Read_Config_List);
	return 0;
    } /* while */
    free(Read_Config_List);
#if DEBUG_SYSTEM
    fprintf(stderr, "Load_Node_Name: Could not locate node %s\n", Req_Name);
#else
    syslog(LOG_ERR, "Load_Node_Name: Could not locate node %s\n", Req_Name);
#endif
    return ENOENT;
} /* Load_Node_Name */


/* 
 * Load_Nodes_Up - Load the bitmap of up nodes
 * Input: NodeBitMap - Location to put bitmap pointer
 *        BitMap_Size - Location into which the byte size of NodeBitMap is to be stored
 * Output: NodeBitMap - Pointer to bitmap
 *         BitMap_Size - Byte size of NodeBitMap
 *         Returns 0 on success or EINVAL if buffer is bad
 */
int Load_Nodes_Up(unsigned **NodeBitMap, int *BitMap_Size) {
    int i, Config_Num, Version;
    time_t Update_Time;
    char *Buffer_Loc;

    /* Load buffer's header */
    Buffer_Loc = Node_API_Buffer;
    memcpy(&Version, Buffer_Loc, sizeof(Version));
    Buffer_Loc += sizeof(Version);
    memcpy(&Update_Time, Buffer_Loc, sizeof(Update_Time));
    Buffer_Loc += sizeof(Update_Time);

    /* Read up and idle node bitmaps */
    memcpy(&i, Buffer_Loc, sizeof(i)); 
    Buffer_Loc += sizeof(i);

    if ((Buffer_Loc+i) > (Node_API_Buffer+Node_API_Buffer_Size)) return EINVAL;	
    NodeBitMap[0] = (unsigned *)Buffer_Loc;
    *BitMap_Size = i;
    return 0;
} /* Load_Nodes_Up */
#endif
