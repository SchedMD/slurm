/* 
 * partition_info.c - Get the partition information of SLURM
 * See slurm.h for documentation on external functions and data structures
 *
 * Author: Moe Jette, jette@llnl.gov
 */

#define PROTOTYPE_API 1

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "slurm.h"
#include "slurmlib.h"

char *Part_API_Buffer = NULL;
int  Part_API_Buffer_Size = 0;

int Dump_Part(char **Buffer_Ptr, int *Buffer_Size, time_t *Update_Time);

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
main(int argc, char * argv[]) {
    int Error_Code;
    time_t Update_Time;
    char *Dump;
    int Dump_Size;
    char Req_Name[MAX_NAME_LEN];	/* Name of the partition */
    char Next_Name[MAX_NAME_LEN];	/* Name of the next partition */
    int MaxTime;		/* -1 if unlimited */
    int MaxNodes;		/* -1 if unlimited */
    int TotalNodes;		/* Total number of nodes in the partition */
    int TotalCPUs;		/* Total number of CPUs in the partition */
    char *Nodes;		/* Names of nodes in partition */
    char *AllowGroups;		/* NULL indicates ALL */
    int Key;    	 	/* 1 if SLURM distributed key is required for use of partition */
    int StateUp;		/* 1 if state is UP */
    int Shared;			/* 1 if partition can be shared */
    unsigned *NodeBitMap;	/* Bitmap of nodes in partition */
    int BitMapSize;		/* Bytes in NodeBitMap */

    Error_Code = Load_Part(Dump, Dump_Size);
    if (Error_Code) printf("Load_Part error %d\n", Error_Code);
    strcpy(Req_Name, "");	/* Start at beginning of partition list */
    while (Error_Code == 0) {
	Error_Code = Load_Part_Name(Req_Name, Next_Name, &MaxTime, &MaxNodes, 
	    &TotalNodes, &TotalCPUs, &Key, &StateUp, &Shared,
	    &Nodes, &AllowGroups, &NodeBitMap, &BitMapSize);
	if (Error_Code != 0)  {
	    printf("Load_Part_Name error %d finding %s\n", Error_Code, Req_Name);
	    break;
	} /* if */

	printf("Found partition Name=%s, TotalNodes=%d, Nodes=%s, MaxTime=%d, MaxNodes=%d\n", 
	    Req_Name, TotalNodes, Nodes, MaxTime, MaxNodes);
	printf("  TotalNodes=%d, TotalCPUs=%d, Key=%d StateUp=%d, Shared=%d, AllowGroups=%s\n", 
	    TotalNodes, TotalCPUs, Key, StateUp, Shared, AllowGroups);
	if (BitMapSize > 0) 
	    printf("  BitMap[0]=0x%x, BitMapSize=%d\n", NodeBitMap[0], BitMapSize);
	if (strlen(Next_Name) == 0) break;
	strcpy(Req_Name, Next_Name);
    } /* while */
    free(Dump);

    exit(0);
} /* main */
#endif

/*
 * Load_Part - Load the supplied partition information buffer for use by info gathering APIs
 * Input: Buffer - Pointer to partition information buffer
 *        Buffer_Size - size of Buffer
 * Output: Returns 0 if no error, EINVAL if the buffer is invalid
 */
int Load_Part(char *Buffer, int Buffer_Size) {
    int Buffer_Offset, Error_Code, Version;

    Buffer_Offset = 0;
    Error_Code = Read_Value(Buffer, &Buffer_Offset, Buffer_Size, "PartVersion", &Version);
    if (Error_Code) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Load_Part: Partition buffer lacks valid header\n");
#else
	syslog(LOG_ERR, "Load_Part: Partition buffer lacks valid header\n");
#endif
	return EINVAL;
    } /* if */
    if (Version != PART_STRUCT_VERSION) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Load_Part: expect version %d, read %d\n", PART_STRUCT_VERSION, Version);
#else
	syslog(LOG_ERR, "Load_Part: expect version %d, read %d\n", PART_STRUCT_VERSION, Version);
#endif
	return EINVAL;
    } /* if */

    Part_API_Buffer = Buffer;
    Part_API_Buffer_Size = Buffer_Size;
    return 0;
} /* Load_Part */


/* 
 * Load_Part_Name - Load the state information about the named partition
 * Input: Req_Name - Name of the partition for which information is requested
 *		     if "", then get info for the first partition in list
 *        Next_Name - Location into which the name of the next partition is 
 *                   stored, "" if no more
 *        MaxTime, etc. - Pointers into which the information is to be stored
 * Output: Req_Name - The partition's name is stored here
 *         Next_Name - The name of the next partition in the list is stored here
 *         MaxTime, etc. - The partition's state information
 *         BitMap_Size - Size of BitMap in bytes
 *         Returns 0 on success, ENOENT if not found, or EINVAL if buffer is bad
 * NOTE:  Req_Name and Next_Name must have length MAX_NAME_LEN
 */
int Load_Part_Name(char *Req_Name, char *Next_Name, int *MaxTime, int *MaxNodes, 
	int *TotalNodes, int *TotalCPUs, int *Key, int *StateUp, int *Shared,
	char **Nodes, char **AllowGroups, unsigned **NodeBitMap, int *BitMap_Size) {
    int i, Error_Code, Version, Buffer_Offset;
    time_t Update_Time;
    struct Part_Record My_Part;
    int My_BitMap_Size;
    char Next_Name_Value[MAX_NAME_LEN];

    /* Load buffer's header (data structure version and time) */
    Buffer_Offset = 0;
    if (Error_Code = Read_Value(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"PartVersion", &Version)) return Error_Code;
    if (Error_Code = Read_Value(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size,
		"UpdateTime", &Update_Time)) return Error_Code;
    if (Error_Code = Read_Value(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size,
		"BitMapSize", &My_BitMap_Size)) return Error_Code;

    while (1) {	
	/* Load all info for next partition */
	Error_Code = Read_Value(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"PartName", &My_Part.Name);
	if (Error_Code == EFAULT) break; /* End of buffer */
	if (Error_Code) return Error_Code;
	if (strlen(Req_Name) == 0)  strcpy(Req_Name,My_Part.Name);

	if (Error_Code = Read_Value(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"MaxTime", &My_Part.MaxTime)) return Error_Code;

	if (Error_Code = Read_Value(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"MaxNodes", &My_Part.MaxNodes)) return Error_Code;

	if (Error_Code = Read_Value(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"TotalNodes", &My_Part.TotalNodes)) return Error_Code;

	if (Error_Code = Read_Value(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"TotalCPUs", &My_Part.TotalCPUs)) return Error_Code;

	if (Error_Code = Read_Value(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"Key", &i)) return Error_Code;
	My_Part.Key = i;

	if (Error_Code = Read_Value(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"StateUp", &i)) return Error_Code;
	My_Part.StateUp = i;

	if (Error_Code = Read_Value(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"Shared", &i)) return Error_Code;
	My_Part.Shared = i;

	if (Error_Code = Read_Array(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"NodeList", (void **)&My_Part.Nodes, (int *)NULL)) return Error_Code;

	if (Error_Code = Read_Array(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"AllowGroups", (void **)&My_Part.AllowGroups, (int *)NULL)) return Error_Code;

	if (Error_Code = Read_Array(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"NodeBitMap", (void **)&My_Part.NodeBitMap, (int *)NULL)) return Error_Code;

	if (Error_Code = Read_Tag(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"EndPart")) return Error_Code;

	/* Check if this is requested partition */ 
	if (strcmp(Req_Name, My_Part.Name) != 0) continue;

	/*Load values to be returned */
	*MaxTime 	= My_Part.MaxTime;
	*MaxNodes 	= My_Part.MaxNodes;
	*TotalNodes	= My_Part.TotalNodes;
	*TotalCPUs	= My_Part.TotalCPUs;
	*Key    	= (int)My_Part.Key;
	*StateUp 	= (int)My_Part.StateUp;
	*Shared 	= (int)My_Part.Shared;
	Nodes[0]	= My_Part.Nodes;
	AllowGroups[0]	= My_Part.AllowGroups;
	NodeBitMap[0]	= My_Part.NodeBitMap;
	if (My_Part.NodeBitMap == NULL)
	    *BitMap_Size = 0;
	else
	    *BitMap_Size = My_BitMap_Size;

	Error_Code = Read_Value(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"PartName", &Next_Name_Value);
	if (Error_Code)		/* No more records or bad tag */
	    strcpy(Next_Name, "");
	else
	    strcpy(Next_Name, Next_Name_Value);
	return 0;
    } /* while */
    return ENOENT;
} /* Load_Part_Name */
