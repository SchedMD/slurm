/* 
 * Partition_Mgr.c - Manage the partition information of SLURM
 * See slurm.h for documentation on external functions and data structures
 *
 * Author: Moe Jette, jette@llnl.gov
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "list.h"
#include "slurm.h"

#define BUF_SIZE 1024
#define DEBUG_MODULE 0
#define DEBUG_SYSTEM 1
#define SEPCHARS " \n\t"

struct	Part_Record Default_Part;		/* Default configuration values */
char	Default_Part_Name[MAX_NAME_LEN];	/* Name of default partition */
struct	Part_Record *Default_Part_Loc = NULL;	/* Location of default partition */

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
main(int argc, char * argv[]) {
    int Error_Code;
    struct Part_Record *Part_Ptr;

    Error_Code = Init_Part_Conf();
    if (Error_Code) printf("Init_Part_Conf error %d\n", Error_Code);
    Default_Part.MaxTime     = 223344;
    Default_Part.MaxNodes    = 556677;

    Part_Ptr = Create_Part_Record(&Error_Code);
    if (Error_Code) 
	printf("Create_Part_Record error %d\n", Error_Code);
    else {
	if (Part_Ptr->MaxTime  != 223344) printf("Part default MaxTime not set\n");
	if (Part_Ptr->MaxNodes != 556677) printf("Part default MaxNodes not set\n");
	strcpy(Part_Ptr->Name, "Interactive");
    } /* else */
    Part_Ptr = Create_Part_Record(&Error_Code);
    if (Error_Code) 
	printf("Create_Part_Record error %d\n", Error_Code);
    else 
	strcpy(Part_Ptr->Name, "Batch");
    Part_Ptr = Create_Part_Record(&Error_Code);
    if (Error_Code) 
	printf("Create_Part_Record error %d\n", Error_Code);
    else 
	strcpy(Part_Ptr->Name, "Class");

    Part_Ptr   = Find_Part_Record("Batch");
    if (Part_Ptr == 0) 
	printf("Find_Part_Record failure\n");

    Error_Code = Delete_Part_Record("Batch");
    if (Error_Code != 0)  printf("Delete_Part_Record error1 %d\n", Error_Code);
    printf("NOTE: We execte Delete_Part_Record to report not finding a record for Batch\n");
    Error_Code = Delete_Part_Record("Batch");
    if (Error_Code != ENOENT)  printf("Delete_Part_Record error2 %d\n", Error_Code);
    exit(0);
} /* main */
#endif

/* 
 * Create_Part_Record - Create a partition record
 * Input: Error_Code - Location to store error value in
 * Output: Error_Code - Set to zero if no error, errno otherwise
 *         Returns a pointer to the record or NULL if error
 * NOTE: The record's values are initialized to those of Default_Part
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
	return (struct Part_Record *)NULL;
    } /* if */

    strcpy(Part_Record_Point->Name, "DEFAULT");
    Part_Record_Point->MaxTime     = Default_Part.MaxTime;
    Part_Record_Point->MaxNodes    = Default_Part.MaxNodes;
    Part_Record_Point->Key         = Default_Part.Key;
    Part_Record_Point->StateUp     = Default_Part.StateUp;
    Part_Record_Point->TotalNodes  = 0;
    Part_Record_Point->TotalCPUs   = 0;
    Part_Record_Point->NodeBitMap  = NULL;

    if (Default_Part.AllowGroups == (char *)NULL)
	Part_Record_Point->AllowGroups = (char *)NULL;
    else {
	Part_Record_Point->AllowGroups = (char *)malloc(strlen(Default_Part.AllowGroups)+1);
	if (Part_Record_Point->AllowGroups == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Create_Part_Record: unable to allocate memory\n");
#else
	    syslog(LOG_ALERT, "Create_Part_Record: unable to allocate memory\n");
#endif
	    free(Part_Record_Point);
	    *Error_Code = ENOMEM;
	    return (struct Part_Record *)NULL;
	} /* if */
	strcpy(Part_Record_Point->AllowGroups, Default_Part.AllowGroups);
    } /* else */

    if (Default_Part.Nodes == (char *)NULL)
	Part_Record_Point->Nodes = (char *)NULL;
    else {
	Part_Record_Point->Nodes = (char *)malloc(strlen(Default_Part.Nodes)+1);
	if (Part_Record_Point->Nodes == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Create_Part_Record: unable to allocate memory\n");
#else
	    syslog(LOG_ALERT, "Create_Part_Record: unable to allocate memory\n");
#endif
	    if (Part_Record_Point->AllowGroups != NULL) free(Part_Record_Point->AllowGroups);
	    free(Part_Record_Point);
	    *Error_Code = ENOMEM;
	    return NULL;
	} /* if */
	strcpy(Part_Record_Point->Nodes, Default_Part.Nodes);
    } /* else */

    if (list_append(Part_List, Part_Record_Point) == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Create_Part_Record: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Create_Part_Record: unable to allocate memory\n")
#endif
	if (Part_Record_Point->Nodes       != NULL) free(Part_Record_Point->Nodes);
	if (Part_Record_Point->AllowGroups != NULL) free(Part_Record_Point->AllowGroups);
	free(Part_Record_Point);
	*Error_Code = ENOMEM;
	return (struct Part_Record *)NULL;
    } /* if */

    return Part_Record_Point;
} /* Create_Part_Record */


/* 
 * Delete_Part_Record - Delete record for partition with specified name
 * Input: name - name of the desired node 
 * Output: return 0 on success, errno otherwise
 */
int Delete_Part_Record(char *name) {
    ListIterator Part_Record_Iterator;		/* For iterating through Part_Record_List */
    struct Part_Record *Part_Record_Point;	/* Pointer to Part_Record */

    Part_Record_Iterator = list_iterator_create(Part_List);
    if (Part_Record_Iterator == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Delete_Part_Record: list_iterator_create unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Delete_Part_Record: list_iterator_create unable to allocate memory\n");
#endif
	return ENOMEM;
    } /* if */

    while (Part_Record_Point = (struct Part_Record *)list_next(Part_Record_Iterator)) {
	if (strcmp(Part_Record_Point->Name, name) != 0) continue;
	if (Part_Record_Point->AllowGroups) free(Part_Record_Point->AllowGroups);
	if (Part_Record_Point->Nodes)       free(Part_Record_Point->Nodes);
	if (list_delete(Part_Record_Iterator) != 1) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Delete_Part_Record: list_delete failure on %s\n", name);
#else
	    syslog(LOG_ALERT, "Delete_Part_Record: list_delete failure on %s\n", name);
#endif
	    strcpy(Part_Record_Point->Name, "DEFUNCT");
	} else
	    free(Part_Record_Point);
	list_iterator_destroy(Part_Record_Iterator);
	return 0;
    } /* while */

    list_iterator_destroy(Part_Record_Iterator);
    return ENOENT;
} /* Delete_Part_Record */


/* 
 * Find_Part_Record - Find a record for node with specified name,
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
	syslog(LOG_ALERT, "Find_Part_Record: list_iterator_create unable to allocate memory\n");
#endif
	return NULL;
    }

    while (Part_Record_Point = (struct Part_Record *)list_next(Part_Record_Iterator)) {
	if (strcmp(Part_Record_Point->Name, name) == 0) break;
    } /* while */

    list_iterator_destroy(Part_Record_Iterator);
    return Part_Record_Point;	/* Value is NULL at end of list */
} /* Find_Part_Record */


/* 
 * Init_Part_Conf - Initialize the partition configuration values. 
 * This should be called before creating any partition entries.
 * Output: return value - 0 if no error, otherwise an error code
 */
int Init_Part_Conf() {
    Default_Part.AllowGroups = (char *)NULL;
    Default_Part.MaxTime     = -1;
    Default_Part.MaxNodes    = -1;
    Default_Part.Nodes       = (char *)NULL;
    Default_Part.Key         = 1;
    Default_Part.StateUp     = 1;

    Part_List = list_create(NULL);
    if (Part_List == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Init_Part_Conf: list_create can not allocate memory\n");
#else
	syslog(LOG_ALARM, "Init_Part_Conf: list_create can not allocate memory\n");
#endif
	return ENOMEM;
    } /* if */
    strcpy(Default_Part_Name, "");
    Default_Part_Loc = (struct Part_Record *)NULL;

    return 0;
} /* Init_Part_Conf */




