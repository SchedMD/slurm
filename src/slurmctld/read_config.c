/*
 * read_config.c - Read the overall SLURM configuration file
 * See slurm.h for documentation on external functions and data structures
 *
 * NOTE: DEBUG_MODULE mode test with execution line
 *	read_config ../../etc/SLURM.conf1
 *	read_config ../../etc/SLURM.conf1 1000
 *
 * Author: Moe Jette, jette@llnl.gov
 */

#define DEBUG_SYSTEM 1

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include "slurm.h"
#include "list.h"

#define BUF_SIZE 1024
#define NO_VAL (-99)

int 	Parse_Node_Spec(char *In_Line);
int 	Parse_Part_Spec(char *In_Line);

char *BackupController = NULL;
char *ControlMachine  = NULL;

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
main(int argc, char * argv[]) {
    int Error_Code, Start_Inx, End_Inx, Count_Inx;
    char Out_Line[BUF_SIZE];
    char *Format, *NodeName, *BitMap;
    char *Partitions[] = {"login", "debug", "batch", "class", "END"};
    struct Part_Record *Part_Ptr;
    int cycles, i, found;

    if (argc > 3) {
	printf("Usage: %s <in_file> <cnt>\n", argv[0]);
	exit(0);
    } /* if */

    Error_Code = Init_SLURM_Conf();
    if (Error_Code != 0) exit(Error_Code);

    if (argc >= 2) 
	Error_Code = Read_SLURM_Conf(argv[1]);
    else
	Error_Code = Read_SLURM_Conf(SLURM_CONF);

    if (Error_Code) {
	printf("Error %d from Read_SLURM_Conf\n", Error_Code);
	exit(1);
    } /* if */

    printf("ControlMachine=%s\n", ControlMachine);
    printf("BackupController=%s\n", BackupController);
    printf("\n");

    for (i=0; i<Node_Record_Count; i++) {
	if (strlen((Node_Record_Table_Ptr+i)->Name) == 0) continue;
	printf("NodeName=%s ",      (Node_Record_Table_Ptr+i)->Name);
	printf("NodeState=%s ",     Node_State_String[Node_Record_Table_Ptr[i].NodeState]);
	printf("LastResponse=%ld ", (long)Node_Record_Table_Ptr[i].LastResponse);

	printf("CPUs=%d ",          Node_Record_Table_Ptr[i].CPUs);
	printf("RealMemory=%d ",    Node_Record_Table_Ptr[i].RealMemory);
	printf("TmpDisk=%d ",       Node_Record_Table_Ptr[i].TmpDisk);
	printf("Weight=%d ",        Node_Record_Table_Ptr[i].Config_Ptr->Weight);
	printf("Feature=%s\n",      Node_Record_Table_Ptr[i].Config_Ptr->Feature);
    } /* for */
    BitMap = BitMapPrint(Up_NodeBitMap);
    printf("\nUp_NodeBitMap  =%s\n", BitMap);
    free(BitMap);
    BitMap = BitMapPrint(Idle_NodeBitMap);
    printf("Idle_NodeBitMap=%s\n\n", BitMap);
    free(BitMap);

    printf("Default_Part_Name=%s\n", Default_Part_Name);
    found = 0;
    for (i=0; ;i++) {
	if (strcmp(Partitions[i], "END") == 0) {
	    if (found) break;
	    Part_Ptr = Default_Part_Loc;
	} else {
	    Part_Ptr = list_find_first(Part_List, &List_Find_Part, Partitions[i]);
	    if (Part_Ptr == Default_Part_Loc) found = 1;
	} /* else */
	if (Part_Ptr == NULL) continue;
	printf("PartitionName=%s ",     Part_Ptr->Name);
	printf("MaxTime=%d ",           Part_Ptr->MaxTime);
	printf("MaxNodes=%d ",          Part_Ptr->MaxNodes);
	printf("Key=%d ",               Part_Ptr->Key);
	printf("StateUp=%d ",           Part_Ptr->StateUp);
	printf("Shared=%d ",            Part_Ptr->Shared);
	printf("Nodes=%s ",             Part_Ptr->Nodes);
	printf("TotalNodes=%d ",        Part_Ptr->TotalNodes);
	printf("TotalCPUs=%d ",         Part_Ptr->TotalCPUs);
	printf("AllowGroups=%s  ",      Part_Ptr->AllowGroups);
	BitMap = BitMapPrint(Part_Ptr->NodeBitMap);
	printf("NodeBitMap=%s\n",	BitMap);
	if (BitMap) free(BitMap);
    } /* for */
    if (argc < 3) exit(0);

    cycles = atoi(argv[2]);
    printf("Let's reinitialize the database %d times. Run /bin/ps to get memory size.\n", cycles);
    sleep(5);
    for (i=0; i<cycles; i++) {
	Error_Code = Init_SLURM_Conf();
	if (Error_Code) {
	    printf("Error %d from Init_SLURM_Conf\n", Error_Code);
	    exit(Error_Code);
	} /* if */

	Error_Code = Read_SLURM_Conf(argv[1]);
	if (Error_Code) {
	    printf("Error %d from Read_SLURM_Conf\n", Error_Code);
	    exit(Error_Code);
	} /* if */
    } /* for */
    printf("All done. Run /bin/ps again look for increase in memory size (leakage).\n");
    sleep(10);

    exit(0);
} /* main */
#endif


/*
 * Build_BitMaps - Build node bitmaps to define which nodes are in which 
 *    1) Partition  2) Configuration record  3) UP state  4) IDLE state
 *    Also sets values of TotalNodes and TotalCPUs for every partition.
 * Output: Returns 0 if no error, errno otherwise
 */
int Build_BitMaps() {
    int i, j, size, Error_Code;
    ListIterator Config_Record_Iterator;	/* For iterating through Config_Record */
    ListIterator Part_Record_Iterator;		/* For iterating through Part_Record_List */
    struct Config_Record *Config_Record_Point;	/* Pointer to Config_Record */
    struct Part_Record *Part_Record_Point;	/* Pointer to Part_Record */
    struct Node_Record *Node_Record_Point;	/* Pointer to Node_Record */
    unsigned *AllPart_NodeBitMap;
    char *Format, This_Node_Name[BUF_SIZE];
    int Start_Inx, End_Inx, Count_Inx;
    char *My_Node_List, *str_ptr1, *str_ptr2;

    Error_Code = 0;
    Last_Node_Update = time(NULL);
    Last_Part_Update = time(NULL);
    size = (Node_Record_Count + (sizeof(unsigned)*8) - 1) / 
		(sizeof(unsigned)*8); 	/* Unsigned int records in bitmap */
    size *= 8;				/* Bytes in bitmap */

    /* Initialize the Idle and Up bitmaps */
    if (Idle_NodeBitMap) free(Idle_NodeBitMap);
    if (Up_NodeBitMap)   free(Up_NodeBitMap);
    Idle_NodeBitMap = (unsigned *)malloc(size);
    Up_NodeBitMap   = (unsigned *)malloc(size);
    if ((Idle_NodeBitMap == NULL) || (Up_NodeBitMap == NULL)) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Build_BitMaps: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Build_BitMaps: unable to allocate memory\n");
#endif
	if (Idle_NodeBitMap) free(Idle_NodeBitMap);
	if (Up_NodeBitMap)   free(Up_NodeBitMap);
	return ENOMEM;
    } /* if */
    memset(Idle_NodeBitMap, 0, size);
    memset(Up_NodeBitMap, 0, size);

    /* Initialize the configuration bitmaps */
    Config_Record_Iterator = list_iterator_create(Config_List);
    if (Config_Record_Iterator == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Build_BitMaps: list_iterator_create unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Build_BitMaps: list_iterator_create unable to allocate memory\n");
#endif
	return ENOMEM;
    } /* if */
    while (Config_Record_Point = (struct Config_Record *)list_next(Config_Record_Iterator)) {
	if (Config_Record_Point->NodeBitMap) free(Config_Record_Point->NodeBitMap);
	Config_Record_Point->NodeBitMap = (unsigned *)malloc(size);
	if (Config_Record_Point->NodeBitMap == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Build_BitMaps: unable to allocate memory\n");
#else
	    syslog(LOG_ALERT, "Build_BitMaps: unable to allocate memory\n");
#endif
	    list_iterator_destroy(Config_Record_Iterator);
	    return ENOMEM;
	} /* if */
	memset(Config_Record_Point->NodeBitMap, 0, size);
    } /* while */
    list_iterator_destroy(Config_Record_Iterator);

    /* Scan all nodes and identify which are UP and IDLE and their configuration */
    for (i=0; i<Node_Record_Count; i++) {
	if (strlen((Node_Record_Table_Ptr+i)->Name) == 0) continue;	/* Defunct */
	if ((Node_Record_Table_Ptr+i)->NodeState == STATE_IDLE) BitMapSet(Idle_NodeBitMap, i);
	if ((Node_Record_Table_Ptr+i)->NodeState != STATE_DOWN) BitMapSet(Up_NodeBitMap, i);
	if ((Node_Record_Table_Ptr+i)->Config_Ptr) 
		BitMapSet(Node_Record_Table_Ptr[i].Config_Ptr->NodeBitMap, i);
    } /* for */

    /* Scan partition table and identify nodes in each */
    AllPart_NodeBitMap = (unsigned *)malloc(size);
    if (AllPart_NodeBitMap == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Build_BitMaps: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Build_BitMaps: unable to allocate memory\n");
#endif
	return ENOMEM;
    } /* if */
    memset(AllPart_NodeBitMap, 0, size);
    Part_Record_Iterator = list_iterator_create(Part_List);
    if (Part_Record_Iterator == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Build_BitMaps: list_iterator_create unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Build_BitMaps: list_iterator_create unable to allocate memory\n");
#endif
	free(AllPart_NodeBitMap);
	return ENOMEM;
    } /* if */
    while (Part_Record_Point = (struct Part_Record *)list_next(Part_Record_Iterator)) {
	if (Part_Record_Point->NodeBitMap) free(Part_Record_Point->NodeBitMap);
	Part_Record_Point->NodeBitMap = (unsigned *)malloc(size);
	if (Part_Record_Point->NodeBitMap == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Build_BitMaps: unable to allocate memory\n");
#else
	    syslog(LOG_ALERT, "Build_BitMaps: unable to allocate memory\n");
#endif
	    Error_Code = ENOMEM;
	    break;
	} /* if */
	memset(Part_Record_Point->NodeBitMap, 0, size);

	/* Check for each node in the partition */
	if ((Part_Record_Point->Nodes == NULL) || 
	    (strlen(Part_Record_Point->Nodes) == 0)) continue;
	My_Node_List = (char *)malloc(strlen(Part_Record_Point->Nodes)+1);
	if (My_Node_List == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Build_BitMaps: unable to allocate memory\n");
#else
	    syslog(LOG_ALERT, "Build_BitMaps: unable to allocate memory\n");
#endif
	    Error_Code = ENOMEM;
	    break;
	} /* if */
	strcpy(My_Node_List, Part_Record_Point->Nodes);
	str_ptr2 = (char *)strtok_r(My_Node_List, ",", &str_ptr1);
	while (str_ptr2) {	/* Break apart by comma separators */
	    Error_Code = Parse_Node_Name(str_ptr2, &Format, &Start_Inx, &End_Inx, &Count_Inx);
	    if (Error_Code) continue;
	    if (strlen(Format) >= sizeof(This_Node_Name)) {
#if DEBUG_SYSTEM
		fprintf(stderr, "Build_BitMaps: Node name specification too long: %s\n", Format);
#else
		syslog(LOG_ERR, "Build_BitMaps: Node name specification too long: %s\n", Format);
#endif
		free(Format);
		continue;
	    } /* if */
	    for (i=Start_Inx; i<=End_Inx; i++) {
		if (Count_Inx == 0) 
		    strncpy(This_Node_Name, Format, sizeof(This_Node_Name));
		else
		    sprintf(This_Node_Name, Format, i);
		Node_Record_Point = Find_Node_Record(This_Node_Name);
		if (Node_Record_Point == NULL) {
#if DEBUG_SYSTEM
		    fprintf(stderr, "Build_BitMaps: Invalid node specified %s\n", This_Node_Name);
#else
		    syslog(LOG_ERR, "Build_BitMaps: Invalid node specified %s\n", This_Node_Name);
#endif
		    continue;
		} /* if */
		j = Node_Record_Point - Node_Record_Table_Ptr;
		if (BitMapValue(AllPart_NodeBitMap, j) == 1) {
#if DEBUG_SYSTEM
		    fprintf(stderr, "Build_BitMaps: Node %s defined in more than one partition\n", 
			This_Node_Name);
		    fprintf(stderr, "Build_BitMaps: Only the first partition's specification is honored\n");
#else
		    syslog(LOG_ERR, "Build_BitMaps: Node %s defined in more than one partition\n", 
			This_Node_Name);
		    syslog(LOG_ERR, "Build_BitMaps: Only the first partition's specification is honored\n");
#endif
		} else {
		    BitMapSet(Part_Record_Point->NodeBitMap, j);
		    BitMapSet(AllPart_NodeBitMap, j);
		    Part_Record_Point->TotalNodes++;
		    Part_Record_Point->TotalCPUs += Node_Record_Point->CPUs;
		    Node_Record_Point->Partition_Ptr = Part_Record_Point;
		} /* else */
	    } /* for */
	    free(Format);
	    str_ptr2 = (char *)strtok_r(NULL, ",", &str_ptr1);
	} /* while (str_ptr2 */
	free(My_Node_List);
    } /* while (Part_Record_Point */
    list_iterator_destroy(Part_Record_Iterator);
    free(AllPart_NodeBitMap);
    return Error_Code;
} /* Build_BitMaps */


/* 
 * Init_SLURM_Conf - Initialize or re-initialize the SLURM configuration  
 *	values. This should be called before calling Read_SLURM_Conf.  
 * Output: return value - 0 if no error, otherwise an error code
 */
int Init_SLURM_Conf() {
    int Error_Code;

    if (ControlMachine)   {
	free(ControlMachine);
	ControlMachine = NULL;
    }
    if (BackupController) {
 	free(BackupController);
	BackupController = NULL;
    }

    Error_Code = Init_Node_Conf();
    if (Error_Code) return Error_Code;

    Error_Code = Init_Part_Conf();
    if (Error_Code) return Error_Code;

    return 0;
} /* Init_SLURM_Conf */


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
    int State_Val, CPUs_Val, RealMemory_Val, TmpDisk_Val, Weight_Val;
    struct Node_Record *Node_Record_Point;
    struct Config_Record *Config_Point;
    char *str_ptr1, *str_ptr2;

    NodeName = State = Feature = (char *)NULL;
    CPUs_Val = RealMemory_Val = State_Val = NO_VAL;
    TmpDisk_Val = Weight_Val = NO_VAL;
    if (Error_Code=Load_String(&NodeName, "NodeName=", In_Line))      return Error_Code;
    if (NodeName == NULL) return 0;	/* No Node info */

    if (Error_Code == 0) Error_Code = Load_Integer(&CPUs_Val, "CPUs=", In_Line);
    if (Error_Code == 0) Error_Code = Load_Integer(&RealMemory_Val, "RealMemory=", In_Line);
    if (Error_Code == 0) Error_Code = Load_Integer(&TmpDisk_Val, "TmpDisk=", In_Line);
    if (Error_Code == 0) Error_Code = Load_Integer(&Weight_Val, "Weight=", In_Line);
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
	if (Feature) free(Feature);
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
	    strncpy(This_Node_Name, str_ptr2, sizeof(This_Node_Name));
	} else
	    sprintf(This_Node_Name, Format, i);
	if (strlen(This_Node_Name) >= MAX_NAME_LEN) {
#if DEBUG_SYSTEM
    	    fprintf(stderr, "Parse_Node_Spec: Node name %s too long\n", This_Node_Name);
#else
    	    syslog(LOG_ERR, "Parse_Node_Spec: Node name %s too long\n", This_Node_Name);
#endif
	    if (i == Start_Inx) free(NodeName);
	    if (Feature) free(Feature);		/* Can't use feature */
	    Error_Code = EINVAL;
	    break;
	} /* if */
	if (strcmp(NodeName, "DEFAULT") == 0) {
	    if (i == Start_Inx)              free(NodeName);
	    if (CPUs_Val != NO_VAL)          Default_Config_Record.CPUs = CPUs_Val;
	    if (RealMemory_Val != NO_VAL)    Default_Config_Record.RealMemory = RealMemory_Val;
	    if (TmpDisk_Val != NO_VAL)       Default_Config_Record.TmpDisk = TmpDisk_Val;
	    if (Weight_Val != NO_VAL)        Default_Config_Record.Weight = Weight_Val;
	    if (State_Val != NO_VAL)         Default_Node_Record.NodeState = State_Val;
	    if (Feature) {
		if (Default_Config_Record.Feature) free(Default_Config_Record.Feature);
		Default_Config_Record.Feature = Feature;
	    } /* if */
	} else {
	    if (i == Start_Inx) {
		Config_Point = Create_Config_Record(&Error_Code);
		if (Error_Code != 0) {
		    if (Feature) free(Feature);	/* Can't use feature */
		    break;
		} /* if */
		Config_Point->Nodes = NodeName;
		if (CPUs_Val != NO_VAL)       Config_Point->CPUs = CPUs_Val;
		if (RealMemory_Val != NO_VAL) Config_Point->RealMemory = RealMemory_Val;
		if (TmpDisk_Val != NO_VAL)    Config_Point->TmpDisk = TmpDisk_Val;
		if (Weight_Val != NO_VAL)     Config_Point->Weight = Weight_Val;
		if (Feature) {
		    if (Config_Point->Feature) free(Config_Point->Feature);
		    Config_Point->Feature = Feature;
		} /* if */
	    } /* if */

	    Node_Record_Point = Find_Node_Record(This_Node_Name);
	    if (Node_Record_Point == NULL) {
		Node_Record_Point = Create_Node_Record(&Error_Code, Config_Point, This_Node_Name);
		if (Error_Code != 0) break;
		if (State_Val != NO_VAL)         Node_Record_Point->NodeState=State_Val;
	    } else {
#if DEBUG_SYSTEM
		fprintf(stderr, "Parse_Node_Spec: Reconfiguration for node %s ignored.\n", 
		    This_Node_Name);
#else
		syslog(LOG_ERR, "Parse_Node_Spec: Reconfiguration for node %s ignored.\n", 
		    This_Node_Name);
#endif
	    } /* else */
	} /* else */
    } /* for (i */

    /* Free allocated storage */
    if (Format)  free(Format);
    return Error_Code;
} /* Parse_Node_Spec */


/*
 * Parse_Part_Spec - Parse the partition specification, build table and set values
 * Output: 0 if no error, error code otherwise
 */
int Parse_Part_Spec (char *In_Line) {
    int Line_Num;		/* Line number in input file */
    char *AllowGroups, *Nodes, *PartitionName;
    int MaxTime_Val, MaxNodes_Val, Key_Val, Default_Val, StateUp_Val, Shared_Val;
    int Error_Code, i;
    struct Part_Record *Part_Record_Point;

    AllowGroups = Nodes = PartitionName = (char *)NULL;
    MaxTime_Val = MaxNodes_Val = Key_Val = Default_Val = StateUp_Val = Shared_Val = NO_VAL;

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

    if (Error_Code == 0)  Error_Code = Load_Integer(&MaxTime_Val,  "MaxTime=", In_Line);
    if (Error_Code == 0)  Error_Code = Load_Integer(&MaxNodes_Val, "MaxNodes=", In_Line);
    if (Error_Code == 0)  Error_Code = Load_Integer(&Default_Val,  "Default=NO", In_Line);
    if (Default_Val == 1) Default_Val=0;
    if (Error_Code == 0)  Error_Code = Load_Integer(&Default_Val,  "Default=YES", In_Line);
    if (Error_Code == 0)  Error_Code = Load_Integer(&Shared_Val,   "Shared=NO", In_Line);
    if (StateUp_Val == 1) Shared_Val=0;
    if (Error_Code == 0)  Error_Code = Load_Integer(&Shared_Val,   "Shared=FORCE", In_Line);
    if (StateUp_Val == 1) Shared_Val=2;
    if (Error_Code == 0)  Error_Code = Load_Integer(&Shared_Val,   "Shared=YES", In_Line);
    if (Error_Code == 0)  Error_Code = Load_Integer(&StateUp_Val,  "State=DOWN", In_Line);
    if (StateUp_Val == 1) StateUp_Val=0;
    if (Error_Code == 0)  Error_Code = Load_Integer(&StateUp_Val,  "State=UP", In_Line);
    if (Error_Code == 0)  Error_Code = Load_Integer(&Key_Val,      "Key=NO", In_Line);
    if (Key_Val == 1)     Key_Val=0;
    if (Error_Code == 0)  Error_Code = Load_Integer(&Key_Val,      "Key=YES", In_Line);
    if (Error_Code != 0) {
	free(PartitionName);
	return Error_Code;
    } /* if */

    Error_Code = Load_String (&Nodes, "Nodes=", In_Line);
    if (Error_Code) {
	free(PartitionName);
	return Error_Code;
    } /* if */

    Error_Code = Load_String (&AllowGroups, "AllowGroups=", In_Line);
    if (Error_Code) {
	free(PartitionName);
	if (Nodes) free(Nodes);
	return Error_Code;
    } /* if */

    if (strcmp(PartitionName, "DEFAULT") == 0) {
	free(PartitionName);
	if (MaxTime_Val  != NO_VAL)    Default_Part.MaxTime      = MaxTime_Val;
	if (MaxNodes_Val != NO_VAL)    Default_Part.MaxNodes     = MaxNodes_Val;
	if (Key_Val != NO_VAL)         Default_Part.Key          = Key_Val;
	if (StateUp_Val  != NO_VAL)    Default_Part.StateUp      = StateUp_Val;
	if (Shared_Val  != NO_VAL)     Default_Part.Shared       = Shared_Val;
	if (AllowGroups) {
	    if (Default_Part.AllowGroups) free(Default_Part.AllowGroups);
	    Default_Part.AllowGroups = AllowGroups;
	} /* if */
	if (Nodes) {
	    if (Default_Part.Nodes) free(Default_Part.Nodes);
	    Default_Part.Nodes = Nodes;
	} /* if */
	return 0;
    } /* if */

    Part_Record_Point = list_find_first(Part_List, &List_Find_Part, PartitionName);
    if (Part_Record_Point == NULL) {
	Part_Record_Point = Create_Part_Record(&Error_Code);
	if (Error_Code) {
	    free(PartitionName);
	    if (Nodes) free(Nodes);
	    if (AllowGroups) free(AllowGroups);
	    return Error_Code;
	} /* if */
	strcpy(Part_Record_Point->Name, PartitionName);
    } else {
#if DEBUG_SYSTEM
	fprintf(stderr, "Parse_Part_Spec: duplicate entry for partition %s\n", PartitionName);
#else
	syslog(LOG_NOTICE, "Parse_Node_Spec: duplicate entry for partition %s\n", PartitionName);
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
    if (Key_Val  != NO_VAL)        Part_Record_Point->Key          = Key_Val;
    if (StateUp_Val  != NO_VAL)    Part_Record_Point->StateUp      = StateUp_Val;
    if (Shared_Val  != NO_VAL)     Part_Record_Point->Shared       = Shared_Val;
    if (AllowGroups) {
	if (Part_Record_Point->AllowGroups) free(Part_Record_Point->AllowGroups);
	Part_Record_Point->AllowGroups = AllowGroups;
    } /* if */
    if (Nodes) {
	if (Part_Record_Point->Nodes) free(Part_Record_Point->Nodes);
	Part_Record_Point->Nodes = Nodes;
    } /* if */
    free(PartitionName);
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

#if DEBUG_SYSTEM
    fprintf(stderr, "Read_SLURM_Conf: Loading configuration from %s\n", File_Name);
#else
    syslog(LOG_NOTICE, "Read_SLURM_Conf: Loading configuration from %s\n", File_Name);
#endif

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
	    fclose(SLURM_Spec_File);
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
	if (Error_Code=Load_String(&ControlMachine, "ControlMachine=", In_Line)) {
	    fclose(SLURM_Spec_File);
	    return Error_Code;
	} /* if */
	if (Error_Code=Load_String(&BackupController, "BackupController=", In_Line)) {
	    fclose(SLURM_Spec_File);
	    return Error_Code;
	} /* if */

	/* Node configuration parameters */
	if (Error_Code=Parse_Node_Spec(In_Line)) {
	    fclose(SLURM_Spec_File);
	    return Error_Code;
	} /* if */

	/* Partition configuration parameters */
	if (Error_Code=Parse_Part_Spec(In_Line)) {
	    fclose(SLURM_Spec_File);
	    return Error_Code;
	} /* if */

	/* Report any leftover strings on input line */
	Report_Leftover(In_Line, Line_Num);
    } /* while */
    fclose(SLURM_Spec_File);

    /* If values not set in configuration file, set defaults */
    if (BackupController == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Read_SLURM_Conf: BackupController value not specified.\n");
#else
	syslog(LOG_WARNING, "Read_SLURM_Conf: BackupController value not specified.\n");
#endif
    } /* if */

    if (ControlMachine == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Read_SLURM_Conf: ControlMachine value not specified.\n");
#else
	syslog(LOG_ALERT, "Read_SLURM_Conf: ControlMachine value not specified.\n");
#endif
	return EINVAL;
    } /* if */

    if (Default_Part_Loc == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Read_SLURM_Conf: Default partition not set.\n");
#else
	syslog(LOG_ALERT, "Read_SLURM_Conf: Default partition not set.\n");
#endif
	return EINVAL;
    } /* if */
    Rehash();
    if (Error_Code=Build_BitMaps()) return Error_Code;
    list_sort(Config_List, &List_Compare_Config);

#if DEBUG_SYSTEM
    fprintf(stderr, "Read_SLURM_Conf: Finished loading configuration\n");
#else
    syslog(LOG_NOTICE, "Read_SLURM_Conf: Finished loading configuration\n");
#endif

    return 0;
} /* Read_SLURM_Conf */
