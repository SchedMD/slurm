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

#include "config.h"
#include "list.h"
#include "slurm.h"

#define BUF_SIZE 1024
#define DEBUG_MODULE 0
#define DEBUG_SYSTEM 1
#define SEPCHARS " \n\t"

List   Part_Record_List = NULL;		/* List of Part_Records */
char *Job_Type_String[]   = {"NONE", "INTERACTIVE", "BATCH", "ALL", "END"};

int	Delete_Part_Record(char *name);
struct Part_Record  *Find_Part_Record(char *name);
int 	Parse_Part_Spec(char *Specification, char *My_Name,  
	int *My_Number, int *Set_Number, unsigned *My_Batch, int *Set_Batch,
	unsigned *My_Interactive, int *Set_Interactive, unsigned *My_Available, int *Set_Available, 
	int *My_MaxTime, int *Set_MaxTime, int *My_MaxCpus, int *Set_MaxCpus, 
	char **My_AllowUsers, char **My_DenyUsers);

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
main(int argc, char * argv[]) {
    int Error_Code;
    char Out_Line[BUF_SIZE];
    unsigned Partition, part_target;

    if (argc < 5) {
	printf("Usage: %s <in_file> <text_file> <raw_file> <user_list_file>\n", argv[0]);
	exit(0);
    } /* if */

    Error_Code = Read_Part_Spec_Conf(argv[1]);
    if (Error_Code != 0) {
	printf("Error %d from Read_Part_Spec_Conf", Error_Code);
	exit(1);
    } /* if */

    /* Update existing record */
    Error_Code = Update_Part_Spec_Conf("Name=pbatch DenyUsers=student1");
    if (Error_Code != 0) printf("Error %d from Update_Part_Spec_Conf\n", Error_Code);
    /* Create a new record */
    Error_Code = Update_Part_Spec_Conf("Name=test Number=10 DenyUsers=non_tester State=DOWN");
    if (Error_Code != 0) printf("Error %d from Update_Part_Spec_Conf\n", Error_Code);

    Error_Code = Write_Part_Spec_Conf(argv[2]);
    if (Error_Code != 0) printf("Error %d from Write_Part_Spec_Conf", Error_Code);

    Error_Code = Find_Valid_Parts("User=student1 CpuCount=4 MaxTime=20 JobType=BATCH", &Partition);
    if (Error_Code != 0) printf("Error %d from Find_Valid_Parts\n", Error_Code);
    part_target = (1 << 4);
    if (Partition != part_target) printf("Incorrect partition from Find_Valid_Parts, %x instead of %x\n",
	 (int)Partition, part_target);

    Error_Code = Find_Valid_Parts("User=jette CpuCount=8 MaxTime=20 JobType=BATCH", &Partition);
    if (Error_Code != 0) printf("Error %d from Find_Valid_Parts\n", Error_Code);
    part_target = (1 << 0) + (1 << 3);
    if (Partition != part_target) printf("Incorrect partition from Find_Valid_Parts, %x instead of %x\n",
	 (int)Partition, part_target);

    Error_Code = Show_Part_Record("test", Out_Line, BUF_SIZE);
    if (Error_Code != 0) printf("Error %d from Show_Part_Record", Error_Code);
    if (Error_Code == 0) printf("Show_Part_Record: %s\n", Out_Line);

    Error_Code =  Dump_Part_Records (argv[3], argv[4]);
    if (Error_Code != 0) printf("Error %d from Dump_Part_Records", Error_Code);

    exit(0);
} /* main */

int Tally_Node_CPUs(char *node_list) {
    fprintf(stderr, "Tally_Node_CPUs: only available when built with Mach_Stat_Mgr.o\n");
    return 1;
}
#endif

/* 
 * Delete_Part_Record - Find a record for partition with specified name and delete it
 * Input: name - name of the partition
 * Output: returns 0 on no error, otherwise errno
 */
int Delete_Part_Record(char *name) {
    int Error_Code;
    ListIterator Part_Record_Iterator;		/* For iterating through Part_Record_List */
    struct Part_Record *Part_Record_Point;	/* Pointer to Part_Record */

    Part_Record_Iterator = list_iterator_create(Part_Record_List);
    if (Part_Record_Iterator == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Delete_Part_Record: list_iterator_create unable to allocate memory\n");
#else
	syslog(LOG_ERR, "Delete_Part_Record: list_iterator_create unable to allocate memory\n");
#endif
	return ENOMEM;
    }

    Error_Code = ENOENT;	/* default until found */
    while (Part_Record_Point = (struct Part_Record *)list_next(Part_Record_Iterator)) {
	if (strcmp(Part_Record_Point->Name, name) == 0) {
	    (void) list_remove(Part_Record_Iterator);
	    free(Part_Record_Point);
	    Error_Code = 0;
	    break;
	} /* if */
    } /* while */

    list_iterator_destroy(Part_Record_Iterator);
    return Error_Code;
} /* Delete_Part_Record */


/* 
 * Dump_Part_Records - Raw dump of PART_STRUCT_VERSION value and all Part_Record structures 
 * Input: File_Name - Name of the file to be created and have Part_Record written to 
 *        File_Name_UserList - Name of the file to be created and have AllowUsers 
 *                             and DenyUsers written to 
 */
int Dump_Part_Records (char *File_Name, char *File_Name_UserList) {
    FILE *Part_Spec_File;	/* Pointer to output data file */
    FILE *User_Spec_File;	/* Pointer to output data file */
    int Error_Code;		/* Error returns from system functions */
    ListIterator Part_Record_Iterator;		/* For iterating through Part_Record_List */
    struct Part_Record *Part_Record_Point;	/* Pointer to Part_Record */
    struct Part_Record Tmp_Part_Record;		/* Temporary storage to manipulate user list pointers */
    int i;

    /* Initialization */
    Error_Code = 0;
    Part_Spec_File = fopen(File_Name, "w");
    if (Part_Spec_File == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Dump_Part_Records: error %d opening file %s\n", errno, File_Name);
#else
	syslog(LOG_ERR, "Dump_Part_Records: error %d opening file %s\n", errno, File_Name);
#endif
	return errno;
    } /* if */

    User_Spec_File = fopen(File_Name_UserList, "w");
    if (User_Spec_File == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Dump_Part_Records: error %d opening file %s\n", errno, File_Name_UserList);
#else
	syslog(LOG_ERR, "Dump_Part_Records: error %d opening file %s\n", errno, File_Name_UserList);
#endif
	return errno;
    } /* if */

    i = PART_STRUCT_VERSION;
    if (fwrite((void *)&i, sizeof(i), 1, Part_Spec_File) < 1) {
	Error_Code = ferror(Part_Spec_File);
#if DEBUG_SYSTEM
	fprintf(stderr, "Dump_Part_Records: error %d writing to file %s\n", Error_Code, File_Name);
#else
	syslog(LOG_ERR, "Dump_Part_Records: error %d writing to file %s\n", Error_Code, File_Name);
#endif
    } /* if */
    if (fwrite((void *)&i, sizeof(i), 1, User_Spec_File) < 1) {
	Error_Code = ferror(User_Spec_File);
#if DEBUG_SYSTEM
	fprintf(stderr, "Dump_Part_Records: error %d writing to file %s\n", Error_Code, File_Name_UserList);
#else
	syslog(LOG_ERR, "Dump_Part_Records: error %d writing to file %s\n", Error_Code, File_Name_UserList);
#endif
    } /* if */

    Part_Record_Iterator = list_iterator_create(Part_Record_List);
    if (Part_Record_Iterator == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Dump_Part_Records: list_iterator_create unable to allocate memory\n");
#else
	syslog(LOG_ERR, "Dump_Part_Records: list_iterator_create unable to allocate memory\n");
#endif
	return ENOMEM;
    } /* if */

    /* Process the data file */
    while (Part_Record_Point = (struct Part_Record *)list_next(Part_Record_Iterator)) {
	memcpy(&Tmp_Part_Record, Part_Record_Point, sizeof(Tmp_Part_Record));
	if (Tmp_Part_Record.AllowUsers) {
	    /* Change AllowUsers to file pointer */
	    i = ftell(User_Spec_File);
	    if (fwrite((void *)Tmp_Part_Record.AllowUsers, 
			(size_t)(strlen(Tmp_Part_Record.AllowUsers)+1), 
			(size_t)1, User_Spec_File) < 1) {
		if (Error_Code == 0) Error_Code = ferror(User_Spec_File);
#if DEBUG_SYSTEM
		fprintf(stderr, "Dump_Part_Records error %d writing to file %s\n", 
			Error_Code, File_Name_UserList);
#else
		syslog(LOG_ERR, "Dump_Part_Records error %d writing to file %s\n", 
			Error_Code, File_Name_UserList);
#endif
		Tmp_Part_Record.AllowUsers = (char *)NULL;
	    } else
		Tmp_Part_Record.AllowUsers = (char *)i;
	} /* if */
	if (Tmp_Part_Record.DenyUsers) {
	    /* Change DenyUsers to file pointer */
	    i = ftell(User_Spec_File);
	    if (fwrite((void *)Tmp_Part_Record.DenyUsers, 
			(size_t)(strlen(Tmp_Part_Record.DenyUsers)+1), 
			(size_t)1, User_Spec_File) < 1) {
		if (Error_Code == 0) Error_Code = ferror(User_Spec_File);
#if DEBUG_SYSTEM
		fprintf(stderr, "Dump_Part_Records error %d writing to file %s\n", 
			Error_Code, File_Name_UserList);
#else
		syslog(LOG_ERR, "Dump_Part_Records error %d writing to file %s\n", 
			Error_Code, File_Name_UserList);
#endif
		Tmp_Part_Record.DenyUsers = (char *)NULL;
	    } else
		Tmp_Part_Record.DenyUsers = (char *)i;
	} /* if */
	if (fwrite((void *)&Tmp_Part_Record, sizeof (struct Part_Record), 1, Part_Spec_File) < 1) {
	    if (Error_Code == 0) Error_Code = ferror(Part_Spec_File);
#if DEBUG_SYSTEM
	    fprintf(stderr, "Dump_Part_Records error %d writing to file %s\n", Error_Code, File_Name);
#else
	    syslog(LOG_ERR, "Dump_Part_Records error %d writing to file %s\n", Error_Code, File_Name);
#endif
	} /* if */
    } /* while */

    /* Termination */
    if (fclose(Part_Spec_File) != 0) {
	if (Error_Code == 0) Error_Code = errno;
#if DEBUG_SYSTEM
	fprintf(stderr, "Dump_Part_Records: error %d closing file %s\n", errno, File_Name);
#else
	syslog(LOG_NOTICE, "Dump_Part_Records: error %d closing file %s\n", errno, File_Name);
#endif
    } /* if */
    if (fclose(User_Spec_File) != 0) {
	if (Error_Code == 0) Error_Code = errno;
#if DEBUG_SYSTEM
	fprintf(stderr, "Dump_Part_Records: error %d closing file %s\n", errno, File_Name_UserList);
#else
	syslog(LOG_NOTICE, "Dump_Part_Records: error %d closing file %s\n", errno, File_Name_UserList);
#endif
    } /* if */
    list_iterator_destroy(Part_Record_Iterator);
    return Error_Code;
} /* Dump_Part_Records */


/* 
 * Find_Part_Record - Find a record for partition with specified name, return pointer or NULL if not found
 */
struct Part_Record *Find_Part_Record(char *name) {
    ListIterator Part_Record_Iterator;		/* For iterating through Part_Record_List */
    struct Part_Record *Part_Record_Point;	/* Pointer to Part_Record */

    Part_Record_Iterator = list_iterator_create(Part_Record_List);
    if (Part_Record_Iterator == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Find_Part_Record:list_iterator_create unable to allocate memory\n");
#else
	syslog(LOG_ERR, "Find_Part_Record:list_iterator_create unable to allocate memory\n");
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
 * Find_Valid_Parts - Determine which partitions can be used to initiate a job 
 *	with the given specification
 * Input: Specification - Standard configuration file input line
 *        Partition - Pointer to partition bit-map
 * Output: Partition - is filled in
 *         Returns 0 if satisfactory, errno otherwise
 */
int Find_Valid_Parts (char *Specification, unsigned *Parition) { 
    int Error_Code;
    char *Scratch;
    char *str_ptr1, *str_ptr2;
    char My_User[30];
    char My_Partition[MAX_PART_LEN];
    int My_MaxTime; 	/* Default is -1, unlimited */
    int My_CPUs;	/* Default is one */
    int My_JobType;	/* INTERACTIVE=1, BATCH=2 */
    ListIterator Part_Record_Iterator;		/* For iterating through Part_Record_List */
    struct Part_Record *Part_Record_Point;	/* Pointer to Part_Record */

    *Parition = 0;
    if (Specification[0] == '#') return 0;
    Scratch = malloc(strlen(Specification)+1);
    if (Scratch == NULL) {
#if DEBUG_SYSTEM
    	fprintf(stderr, "Find_Valid_Parts: unable to allocate memory\n");
#else
    	syslog(LOG_ERR, "Find_Valid_Parts: unable to allocate memory\n");
#endif
	return ENOMEM;
    } /* if */

    /* Parse the specification */
    strcpy(My_Partition, "");
    str_ptr1 = (char *)strstr(Specification, "Partition=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+10);
	str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	if ((strlen(str_ptr2)+1) >= MAX_PART_LEN) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Find_Valid_Parts: Partition name too long\n");
#else
	    syslog(LOG_ERR, "Find_Valid_Parts: Partition name too long\n");
#endif
	    free(Scratch);
	    return EINVAL;
	} /* if */
	strcpy(My_Partition, str_ptr2);
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "User=");
    if (str_ptr1 == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Find_Valid_Parts: No User specified\n");
#else
	syslog(LOG_ERR, "Find_Valid_Parts: No User specified\n");
#endif
	free(Scratch);
	return EINVAL;
    } /* if */
    strcpy(Scratch, str_ptr1+5);
    str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
    if ((strlen(str_ptr2)+1) >= sizeof(My_User)) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Find_Valid_Parts: User name too long\n");
#else
	syslog(LOG_ERR, "Find_Valid_Parts: User name too long\n");
#endif
	free(Scratch);
	return EINVAL;
    } /* if */
    str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
    strcpy(My_User, str_ptr2);

    str_ptr1 = (char *)strstr(Specification, "MaxTime=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+8);
	str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	My_MaxTime = (int) strtol(str_ptr2, (char **)NULL, 10);
    } else {
	My_MaxTime = -1;
    } /* else */

    str_ptr1 = (char *)strstr(Specification, "CpuCount=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+5);
	str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	My_CPUs = (int) strtol(str_ptr2, (char **)NULL, 10);
    } else {
	My_CPUs = 1;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "NodeList=");
    if (str_ptr1 != NULL) {
	My_CPUs = Tally_Node_CPUs(str_ptr1+9);
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "JobType=");
    if (str_ptr1 == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Find_Valid_Parts: No JobType specified\n");
#else
	syslog(LOG_ERR, "Find_Valid_Parts: No JobType specified\n");
#endif
	free(Scratch);
	return EINVAL;
    } /* if */
    strcpy(Scratch, str_ptr1+8);
    str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
    if (strcmp(str_ptr2, "INTERACTIVE") == 0) {
	My_JobType = 1;
    } else if (strcmp(str_ptr2, "BATCH") == 0) {
	My_JobType = 2;
    } else {
#if DEBUG_SYSTEM
	fprintf(stderr, "Find_Valid_Parts: Invalid JobType specified\n");
#else
	syslog(LOG_ERR, "Find_Valid_Parts: Invalid JobType specified\n");
#endif
	free(Scratch);
	return EINVAL;
    } /* else */

   /* Scan the partition list for matches */
    Part_Record_Iterator = list_iterator_create(Part_Record_List);
    if (Part_Record_Iterator == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Find_Part_Record: list_iterator_create unable to allocate memory\n");
#else
	syslog(LOG_ERR, "Find_Part_Record: list_iterator_create unable to allocate memory\n");
#endif
	free(Scratch);
	return ENOMEM;
    } /* if */

    while (Part_Record_Point = (struct Part_Record *)list_next(Part_Record_Iterator)) {
	if (Part_Record_Point->Available == 0) continue;
	if ((strlen(My_Partition) != 0) && 
	    (strcmp(My_Partition, Part_Record_Point->Name) != 0)) continue;
	if (Part_Record_Point->MaxTime != -1) {
	    if (My_MaxTime == -1) continue;
	    if (My_MaxTime > Part_Record_Point->MaxTime) continue;
	} /* if */ 
	if ((My_JobType == 1) && (Part_Record_Point->RunInteractive != 1)) continue;
	if ((My_JobType == 2) && (Part_Record_Point->RunBatch != 1)) continue;
	if (Part_Record_Point->MaxCpus != -1) {
	    if (My_CPUs > Part_Record_Point->MaxCpus) continue;
	} /* if */ 
	if (Part_Record_Point->AllowUsers != (char *)NULL) {
	    strcpy(Scratch, Part_Record_Point->AllowUsers);
	    strtok(Scratch, SEPCHARS);	/* make any white-space into end of string */
	    str_ptr1 = (char *)strtok_r(Scratch, ",", &str_ptr2);
	    while (str_ptr1 != NULL) {
		if (strcmp(str_ptr1, My_User) == 0) break;
		str_ptr1 = (char *)strtok_r(NULL, ",", &str_ptr2);
	    } /* while */
	    if (str_ptr1 == NULL) continue;  /* Not in allow list */
 	} else if (Part_Record_Point->DenyUsers != (char *)NULL) {
	    strcpy(Scratch, Part_Record_Point->DenyUsers);
	    strtok(Scratch, SEPCHARS);	/* make any white-space into end of string */
	    str_ptr1 = (char *)strtok_r(Scratch, ",", &str_ptr2);
	    while (str_ptr1 != NULL) {
		if (strcmp(str_ptr1, My_User) == 0) break;
		str_ptr1 = (char *)strtok_r(NULL, ",", &str_ptr2);
	    } /* while */
	    if (str_ptr1 != NULL) continue;  /* In deny list */
	} /* if */
	*Parition += (1 << Part_Record_Point->Number);
    } /* while */

    list_iterator_destroy(Part_Record_Iterator);
    free(Scratch);
    return 0;
} /* Find_Valid_Parts */


/* 
 * Parse_Part_Spec - Parse the partition input specification, return values and set flags
 * Output: 0 if no error, error code otherwise
 */
int Parse_Part_Spec(char *Specification, char *My_Name,  
	int *My_Number, int *Set_Number, unsigned *My_Batch, int *Set_Batch,
	unsigned *My_Interactive, int *Set_Interactive, unsigned *My_Available, int *Set_Available, 
	int *My_MaxTime, int *Set_MaxTime, int *My_MaxCpus, int *Set_MaxCpus, 
	char **My_AllowUsers, char **My_DenyUsers) {
    char *Scratch;
    char *str_ptr1, *str_ptr2, *str_ptr3;
    int Error_Code, user_len;

    Error_Code         = 0;
    My_Name[0]         = (char)NULL;
    *My_Number         = 0;
    *Set_Number        = 0;
    *My_Batch          = 0;
    *Set_Batch         = 0;
    *My_Interactive    = 0;
    *Set_Interactive   = 0;
    *My_Available      = 0;
    *Set_Available     = 0;
    *My_MaxTime        = 0;
    *Set_MaxTime       = 0;
    *My_MaxCpus        = 0;
    *Set_MaxCpus       = 0;
    *My_AllowUsers     = (char *)NULL;
    *My_DenyUsers      = (char *)NULL;

    if (Specification[0] == '#') return 0;
    Scratch = malloc(strlen(Specification)+1);
    if (Scratch == NULL) {
#if DEBUG_SYSTEM
    	fprintf(stderr, "Parse_Part_Spec: unable to allocate memory\n");
#else
    	syslog(LOG_ERR, "Parse_Part_Spec: unable to allocate memory\n");
#endif
	return ENOMEM;
    } /* if */
    str_ptr1 = (char *)strstr(Specification, "Name=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+5);
	str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	if (strlen(str_ptr2) < MAX_PART_LEN) strcpy(My_Name, str_ptr2);
	else {
#if DEBUG_SYSTEM
    	    fprintf(stderr, "Parse_Part_Spec: Partition name too long\n");
#else
    	    syslog(LOG_ERR, "Parse_Part_Spec: Partition name too long\n");
#endif
	    free(Scratch);
	    return EINVAL;
	} /* else */
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "Number=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+7);
	str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	*My_Number = (int) strtol(str_ptr2, (char **)NULL, 10);
	*Set_Number = 1;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "JobType=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+8);
	str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	if (strcmp(Scratch, "BATCH") == 0) {
	    *My_Batch          = 1;
	    *Set_Batch         = 1;
	    *My_Interactive    = 0;
	    *Set_Interactive   = 1;
	} else if (strcmp(Scratch, "INTERACTIVE") == 0) {
	    *My_Batch          = 0;
	    *Set_Batch         = 1;
	    *My_Interactive    = 1;
	    *Set_Interactive   = 1;
	} else if (strcmp(Scratch, "ALL") == 0) {
	    *My_Batch          = 1;
	    *Set_Batch         = 1;
	    *My_Interactive    = 1;
	    *Set_Interactive   = 1;
	} /* if */
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "MaxTime=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+8);
	str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	if (strncmp(str_ptr2, "UNLIMITED", 9) == 0)
	    *My_MaxTime = -1;
	else
	    *My_MaxTime = (int) strtol(str_ptr2, (char **)NULL, 10);
	*Set_MaxTime = 1;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "MaxCpus=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+8);
	str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	if (strncmp(str_ptr2, "UNLIMITED", 9) == 0)
	    *My_MaxCpus = -1;
	else
	    *My_MaxCpus = (int) strtol(str_ptr2, (char **)NULL, 10);
	*Set_MaxCpus = 1;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "State=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+6);
	str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	if (strcmp(Scratch, "UP") == 0) {
	    *My_Available      = 1;
	    *Set_Available     = 1;
	} else if (strcmp(Scratch, "DOWN") == 0) {
	    *My_Available      = 0;
	    *Set_Available     = 1;
	} /* if */
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "AllowUsers=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+11);
	str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	user_len = strlen(str_ptr2);
	str_ptr3 = malloc(user_len+1);
	if (str_ptr3 == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Parse_Part_Spec malloc failure\n");
#else
	    syslog(LOG_ALERT, "Parse_Part_Spec malloc failure\n");
#endif
	    free(Scratch);
	    return errno;
	} /* if */
	strcpy(str_ptr3, str_ptr2);
	*My_AllowUsers = str_ptr3;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "DenyUsers=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+10);
	str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	user_len = strlen(str_ptr2);
	str_ptr3 = malloc(user_len+1);
	if (str_ptr3 == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Parse_Part_Spec malloc failure\n");
#else
	    syslog(LOG_ALERT, "Parse_Part_Spec malloc failure\n");
#endif
	    free(Scratch);
	    return errno;
	} /* if */
	strcpy(str_ptr3, str_ptr2);
	*My_DenyUsers = str_ptr3;
    } /* if */

    free(Scratch);
    return Error_Code;
} /* Parse_Part_Spec */


/*
 * Read_Part_Spec_Conf - Load the partition specification information from the specified file 
 * Input: File_Name - Name of the file containing partition specification
 * Output: return - 0 if no error, otherwise an error code
 */
int Read_Part_Spec_Conf (char *File_Name) {
    FILE *Part_Spec_File;	/* Pointer to input data file */
    int Error_Code;		/* Error returns from system functions */
    int Line_Num;		/* Line number in input file */
    char In_Line[BUF_SIZE];	/* Input line */
    struct Part_Record *Part_Record_Point;	/* Pointer to Part_Record */
    char My_Name[MAX_NAME_LEN];
    int My_Number;
    unsigned My_Batch;
    unsigned My_Interactive;
    unsigned My_Available;
    int My_MaxTime;
    int My_MaxCpus;
    char *My_AllowUsers;
    char *My_DenyUsers;

    struct Part_Record  Default_Record;	/* Default values for node record */
    struct Part_Record  Part_Record_Read;	/* Part record being read */

    int Set_Number, Set_Batch, Set_Interactive, Set_Available, Set_MaxTime, Set_MaxCpus;

    /* Initialization */
    Error_Code = 0;
    Part_Spec_File = fopen(File_Name, "r");
    if (Part_Spec_File == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Read_Part_Spec_Conf error %d opening file %s\n", errno, File_Name);
#else
	syslog(LOG_ALERT, "Read_Part_Spec_Conf error %d opening file %s\n", errno, File_Name);
#endif
	return errno;
    } /* if */
    strcpy(Default_Record.Name, "DEFAULT");
    Default_Record.Number = 0;
    Default_Record.RunBatch = 1;
    Default_Record.RunInteractive = 1;
    Default_Record.Available = 1;
    Default_Record.MaxTime = -1;
    Default_Record.MaxCpus = -1;
    Default_Record.AllowUsers = NULL;
    Default_Record.DenyUsers = NULL;
    Part_Record_List = list_create(NULL);

    /* Process the data file */
    Line_Num = 0;
    while (fgets(In_Line, BUF_SIZE, Part_Spec_File) != NULL) {
	Line_Num++;
	if (strlen(In_Line) >= (BUF_SIZE-1)) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Read_Part_Spec_Conf line %d, of input file %s too long\n", 
		Line_Num, File_Name);
#else
	    syslog(LOG_ALERT, "Read_Part_Spec_Conf line %d, of input file %s too long\n", 
		Line_Num, File_Name);
#endif
	    Error_Code = E2BIG;
	    break;
	} /* if */
	if (In_Line[0] == '#') continue;
	Error_Code = Parse_Part_Spec(In_Line, My_Name, 
	    &My_Number, &Set_Number, &My_Batch, &Set_Batch,
	    &My_Interactive, &Set_Interactive, &My_Available, &Set_Available, 
	    &My_MaxTime, &Set_MaxTime, &My_MaxCpus, &Set_MaxCpus, 
	    &My_AllowUsers, &My_DenyUsers);
	if (Error_Code != 0) break;
	if (strlen(My_Name) == 0) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Read_Part_Spec_Conf line %d, of input file %s contains no Name\n", 
		Line_Num, File_Name);
#else
	    syslog(LOG_ALERT, "Read_Part_Spec_Conf line %d, of input file %s contains no Name\n", 
		Line_Num, File_Name);
#endif
	    Error_Code = EINVAL;
	    break;
	} /* if */
	if (strcmp("DEFAULT", My_Name) == 0) {
	    if (Set_Number != 0)       Default_Record.Number=My_Number;
	    if (Set_Batch != 0)        Default_Record.RunBatch=My_Batch;
	    if (Set_Interactive != 0)  Default_Record.RunInteractive=My_Interactive;
	    if (Set_Available != 0)    Default_Record.Available=My_Available;
	    if (Set_MaxTime != 0)      Default_Record.MaxTime=My_MaxTime;
	    if (Set_MaxCpus != 0)      Default_Record.MaxCpus=My_MaxCpus;
	    if (Default_Record.AllowUsers != NULL) free(Default_Record.AllowUsers);
	    Default_Record.AllowUsers = My_AllowUsers;
	    if (Default_Record.DenyUsers != NULL) free(Default_Record.DenyUsers);
	    Default_Record.DenyUsers = My_DenyUsers;
	} else {
	    Part_Record_Point = Find_Part_Record(Part_Record_Read.Name);
	    if (Part_Record_Point == NULL) {
		Part_Record_Point = (struct Part_Record *)malloc(sizeof(struct Part_Record));
		if (Part_Record_Point == NULL) {
#if DEBUG_SYSTEM
		    fprintf(stderr, "Read_Part_Spec_Conf malloc failure\n");
#else
		    syslog(LOG_ALERT, "Read_Part_Spec_Conf malloc failure\n");
#endif
		    Error_Code =  errno;
		    break;
		} /* if */
		memset(Part_Record_Point, 0, (size_t)sizeof(struct Part_Record));
		if (list_append(Part_Record_List, (void *)Part_Record_Point) == NULL) {
#if DEBUG_SYSTEM
		    fprintf(stderr, "Read_Part_Spec_Conf list_append can not allocate memory\n");
#else
		    syslog(LOG_ALERT, "Read_Part_Spec_Conf list_append can not allocate memory\n");
#endif
		    Error_Code =  errno;
		    break;
		} /* if */
		strcpy(Part_Record_Point->Name, My_Name);
		Part_Record_Point->Number         = Default_Record.Number;
		Part_Record_Point->RunBatch       = Default_Record.RunBatch;
		Part_Record_Point->RunInteractive = Default_Record.RunInteractive;
		Part_Record_Point->Available      = Default_Record.Available;
		Part_Record_Point->MaxTime        = Default_Record.MaxTime;
		Part_Record_Point->MaxCpus        = Default_Record.MaxCpus;
		/* AllowUsers and My_DenyUsers all handled below */
	    } else {
#if DEBUG_SYSTEM
		fprintf(stderr, "Read_Part_Spec_Conf duplicate data for %s, using latest information\n", 
		    Part_Record_Read.Name);
#else
		syslog(LOG_NOTICE, "Read_Part_Spec_Conf duplicate data for %s, using latest information\n", 
		    Part_Record_Read.Name);
#endif
	    } /* else */
	    if (Set_Number != 0)       Part_Record_Point->Number=My_Number;
	    if (Set_Batch != 0)        Part_Record_Point->RunBatch=My_Batch;
	    if (Set_Interactive != 0)  Part_Record_Point->RunInteractive=My_Interactive;
	    if (Set_Available != 0)    Part_Record_Point->Available=My_Available;
	    if (Set_MaxTime != 0)      Part_Record_Point->MaxTime=My_MaxTime;
	    if (Set_MaxCpus != 0)      Part_Record_Point->MaxCpus=My_MaxCpus;
	    if (My_AllowUsers != NULL) {
		Part_Record_Point->AllowUsers = My_AllowUsers;
	    } else if (Default_Record.AllowUsers == NULL) {
		Part_Record_Point->AllowUsers = Default_Record.AllowUsers;
	    } else {
		int list_size;
		char *user_list;
		list_size = strlen(Default_Record.AllowUsers) + 1;
		user_list = malloc(list_size);
		if (user_list == NULL) {
#if DEBUG_SYSTEM
		    fprintf(stderr, "Read_Part_Spec_Conf malloc failure\n");
#else
		    syslog(LOG_ALERT, "Read_Part_Spec_Conf malloc failure\n");
#endif
		    Error_Code =  errno;
		    break;
		} /* if */
		strcpy(user_list, Default_Record.AllowUsers);
		Part_Record_Point->AllowUsers = user_list;
	    } /* else */
	    if (My_DenyUsers != NULL) {
		Part_Record_Point->DenyUsers = My_DenyUsers;
	    } else if (Default_Record.DenyUsers == NULL) {
		Part_Record_Point->DenyUsers = Default_Record.AllowUsers;
	    } else {
		int list_size;
		char *user_list;
		list_size = strlen(Default_Record.DenyUsers) + 1;
		user_list = malloc(list_size);
		if (user_list == NULL) {
#if DEBUG_SYSTEM
		    fprintf(stderr, "Read_Part_Spec_Conf malloc failure\n");
#else
		    syslog(LOG_ALERT, "Read_Part_Spec_Conf malloc failure\n");
#endif
		    Error_Code =  errno;
		    break;
		} /* if */
		strcpy(user_list, Default_Record.DenyUsers);
		Part_Record_Point->DenyUsers = user_list;
	    } /* else */
	} /* else */
    } /* while */

    /* Termination */
    if (fclose(Part_Spec_File) != 0) {
	if (Error_Code == 0) Error_Code = errno;
#if DEBUG_SYSTEM
	fprintf(stderr, "Read_Part_Spec_Conf error %d closing file %s\n", errno, File_Name);
#else
	syslog(LOG_NOTICE, "Read_Part_Spec_Conf error %d closing file %s\n", errno, File_Name);
#endif
    } /* if */
    if (Default_Record.AllowUsers != NULL) free(Default_Record.AllowUsers);
    if (Default_Record.DenyUsers != NULL) free(Default_Record.DenyUsers);
    return Error_Code;
} /* Read_Part_Spec_Conf */


/*
 * Show_Part_Record - Dump the record for the specified partition
 * Input: Part_Name - Name of the partition for which data is requested
 *        Node_Record - Location into which the information is written
 *        Buf_Size - Size of Node_Record in bytes
 * Output: Part_Record is filled
 *         return - 0 if no error, otherwise errno
 */
    int Show_Part_Record (char *Part_Name, char *Part_Record, int Buf_Size) {
    struct Part_Record *Part_Record_Point;
    struct tm *Part_Time;
    char *Out_Type, Out_Time[20], Out_CPUs[20], Out_State[20], Out_Line[BUF_SIZE];

    Part_Record_Point = Find_Part_Record(Part_Name);
    if (Part_Record_Point == NULL) return ENOENT;

    Out_Type = Job_Type_String[Part_Record_Point->RunInteractive +
	                      (2*Part_Record_Point->RunBatch)];

    if (Part_Record_Point->MaxTime == -1) 
	strcpy(Out_Time, "UNLIMITED");
    else
	sprintf(Out_Time, "%-d", Part_Record_Point->MaxTime);

    if (Part_Record_Point->MaxCpus == -1) 
	strcpy(Out_CPUs, "UNLIMITED");
    else
	sprintf(Out_CPUs, "%-d", Part_Record_Point->MaxCpus);

    if (Part_Record_Point->Available) 
	strcpy(Out_State, "UP");
    else
	strcpy(Out_State, "DOWN");

    if (sprintf(Out_Line, 
	  "Name=%s Number=%d JobType=%s MaxTime=%s MaxCpus=%s State=%s",
	  Part_Record_Point->Name, Part_Record_Point->Number, Out_Type, Out_Time, 
	  Out_CPUs, Out_State) < 1) {
	return EINVAL;
    } /* if */
    if (strlen(Out_Line) >= Buf_Size) return E2BIG;
    strcpy(Part_Record, Out_Line);

    if (Part_Record_Point->AllowUsers) {
	if (strlen(Part_Record) + strlen(Part_Record_Point->AllowUsers) + 12 >= Buf_Size) return E2BIG;
	strcat(Part_Record, " AllowUsers=");
	strcat(Part_Record, Part_Record_Point->AllowUsers);
    } else if (Part_Record_Point->DenyUsers) {
	if (strlen(Part_Record) + strlen(Part_Record_Point->DenyUsers) + 11 >= Buf_Size) return E2BIG;
	strcat(Part_Record, " DenyUsers=");
	strcat(Part_Record, Part_Record_Point->DenyUsers);
    } /* if */

    return 0;
} /* Show_Part_Record */


/*
 * Update_Part_Spec_Conf - Update the configuration for the given partition, create record as needed 
 *	NOTE: To delete a record, specify Number=-1 in the configuration
 * Input: Specification - Standard configuration file input line
 * Output: return - 0 if no error, otherwise errno
 */
int Update_Part_Spec_Conf (char *Specification) {
    int Error_Code;				/* Error returns from system functions */
    struct Part_Record *Part_Record_Point;	/* Pointer to Part_Record */
    char My_Name[MAX_NAME_LEN];
    int My_Number;
    unsigned My_Batch;
    unsigned My_Interactive;
    unsigned My_Available;
    int My_MaxTime;
    int My_MaxCpus;
    char *My_AllowUsers;
    char *My_DenyUsers;

    int Set_Number, Set_Batch, Set_Interactive, Set_Available, Set_MaxTime, Set_MaxCpus;

    Error_Code = Parse_Part_Spec(Specification, My_Name, 
	&My_Number, &Set_Number, &My_Batch, &Set_Batch,
	&My_Interactive, &Set_Interactive, &My_Available, &Set_Available, 
	&My_MaxTime, &Set_MaxTime, &My_MaxCpus, &Set_MaxCpus, 
	&My_AllowUsers, &My_DenyUsers);
    if (Error_Code != 0) return EINVAL;

    if (strlen(My_Name) == 0) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Update_Part_Spec_Conf invalid input: %s\n", Specification);
#else
	syslog(LOG_ERR, "Update_Part_Spec_Conf invalid input: %s\n", Specification);
#endif
	return EINVAL;
    } /* if */

    Part_Record_Point = Find_Part_Record(My_Name);
    if (Part_Record_Point == NULL) {		/* Create new record as needed */
	Part_Record_Point = (struct Part_Record *)malloc(sizeof(struct Part_Record));
	if (Part_Record_Point == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Update_Part_Spec_Conf malloc failure\n");
#else
	    syslog(LOG_ERR, "Update_Part_Spec_Conf malloc failure\n");
#endif
	    return errno;
	} /* if */
	memset(Part_Record_Point, 0, (size_t)sizeof(struct Part_Record));
	if (list_append(Part_Record_List, (void *)Part_Record_Point) == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Update_Part_Spec_Conf list_append can not allocate memory\n");
#else
	    syslog(LOG_ERR, "Update_Part_Spec_Conf list_append can not allocate memory\n");
#endif
	    return errno;
	} /* if */

	/* Set defaults */
	strcpy(Part_Record_Point->Name, My_Name);
	Part_Record_Point->Number = 0;
	Part_Record_Point->RunBatch = 1;
	Part_Record_Point->RunInteractive = 1;
	Part_Record_Point->Available = 1;
	Part_Record_Point->MaxTime = -1;
	Part_Record_Point->MaxCpus = -1;
	Part_Record_Point->AllowUsers = NULL;
	Part_Record_Point->DenyUsers = NULL;
    } /* if */

    
    if ((Set_Number != 0) && (My_Number == -1)) {	/* Delete record */
	return Delete_Part_Record(My_Name);
	return 0;
    } /* if */

    if (Set_Number != 0)       Part_Record_Point->Number=My_Number;
    if (Set_Batch != 0)        Part_Record_Point->RunBatch=My_Batch;
    if (Set_Interactive != 0)  Part_Record_Point->RunInteractive=My_Interactive;
    if (Set_Available != 0)    Part_Record_Point->Available=My_Available;
    if (Set_MaxTime != 0)      Part_Record_Point->MaxTime=My_MaxTime;
    if (Set_MaxCpus != 0)      Part_Record_Point->MaxCpus=My_MaxCpus;
    if (My_AllowUsers != NULL) {
	if (Part_Record_Point->AllowUsers != NULL) free(Part_Record_Point->AllowUsers);
	Part_Record_Point->AllowUsers = My_AllowUsers;
    } /* if */
    if (My_DenyUsers != NULL) {
	if (Part_Record_Point->DenyUsers != NULL) free(Part_Record_Point->DenyUsers);
	Part_Record_Point->DenyUsers = My_DenyUsers;
    } /* if */

    return 0;
} /* Update_Part_Spec_Conf */


/*
 * Write_Part_Spec_Conf - Dump the partiton specification information into the specified file 
 * Input: File_Name - Name of the file into which the partiton specification is to be written
 * Output: return - 0 if no error, otherwise an error code
 */
int Write_Part_Spec_Conf (char *File_Name) {
    FILE *Part_Spec_File;	/* Pointer to output data file */
    int Error_Code;		/* Error returns from system functions */
    char *Out_Type, Out_Time[20], Out_CPUs[20], Out_State[20];
    int i;			/* Counter */
    time_t now;			/* Current time */
    ListIterator Part_Record_Iterator;		/* For iterating through Part_Record_List */
    struct Part_Record *Part_Record_Point;	/* Pointer to Part_Record */

    /* Initialization */
    Error_Code = 0;
    Part_Spec_File = fopen(File_Name, "w");
    if (Part_Spec_File == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Write_Part_Spec_Conf error %d opening file %s\n", errno, File_Name);
#else
	syslog(LOG_ERR, "Write_Part_Spec_Conf error %d opening file %s\n", errno, File_Name);
#endif
	return errno;
    } /* if */

    Part_Record_Iterator = list_iterator_create(Part_Record_List);
    if (Part_Record_Iterator == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Write_Part_Spec_Conf: list_iterator_create unable to allocate memory\n");
#else
	syslog(LOG_ERR, "Write_Part_Spec_Conf: list_iterator_create unable to allocate memory\n");
#endif
	return ENOMEM;
    } /* if */

    (void) time(&now);
    if (fprintf(Part_Spec_File, "#\n# Written by SLURM: %s#\n", ctime(&now)) <= 0) {
	Error_Code = errno;
#if DEBUG_SYSTEM
	fprintf(stderr, "Write_Part_Spec_Conf error %d printing to file %s\n", errno, File_Name);
#else
	syslog(LOG_ERR, "Write_Part_Spec_Conf error %d printing to file %s\n", errno, File_Name);
#endif
    } /* if */

    /* Process the data file */
    while (Part_Record_Point = (struct Part_Record *)list_next(Part_Record_Iterator)) {
	Out_Type = Job_Type_String[Part_Record_Point->RunInteractive +
	                      (2*Part_Record_Point->RunBatch)];

	if (Part_Record_Point->MaxTime == -1) 
	    strcpy(Out_Time, "UNLIMITED");
	else
	    sprintf(Out_Time, "%-d", Part_Record_Point->MaxTime);

	if (Part_Record_Point->MaxCpus == -1) 
	    strcpy(Out_CPUs, "UNLIMITED");
	else
	    sprintf(Out_CPUs, "%-d", Part_Record_Point->MaxCpus);

	if (Part_Record_Point->Available) 
	    strcpy(Out_State, "UP");
	else
	    strcpy(Out_State, "DOWN");

	if (fprintf(Part_Spec_File, 
	  "Name=%s Number=%d JobType=%s MaxTime=%s MaxCpus=%s State=%s",
	  Part_Record_Point->Name, Part_Record_Point->Number, Out_Type, Out_Time, 
	  Out_CPUs, Out_State) < 1) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Write_Part_Spec_Conf error %d printing to file %s\n", errno, File_Name);
#else
	    syslog(LOG_ERR, "Write_Part_Spec_Conf error %d printing to file %s\n", errno, File_Name);
#endif
	    Error_Code = errno;
	    break;
	} /* if */

	if (Part_Record_Point->AllowUsers) {
	    if (fprintf(Part_Spec_File," AllowUsers=%s\n", Part_Record_Point->AllowUsers) < 1)  {
#if DEBUG_SYSTEM
		fprintf(stderr, "Write_Part_Spec_Conf error %d printing to file %s\n", errno, File_Name);
#else
		syslog(LOG_ERR, "Write_Part_Spec_Conf error %d printing to file %s\n", errno, File_Name);
#endif
	    Error_Code = errno;
	    break;
	    } /* if */
	} else if (Part_Record_Point->DenyUsers) {
	    if (fprintf(Part_Spec_File," DenyUsers=%s\n", Part_Record_Point->DenyUsers) < 1)  {
#if DEBUG_SYSTEM
		fprintf(stderr, "Write_Part_Spec_Conf error %d printing to file %s\n", errno, File_Name);
#else
		syslog(LOG_ERR, "Write_Part_Spec_Conf error %d printing to file %s\n", errno, File_Name);
#endif
	    Error_Code = errno;
	    break;
	    } /* if */
	} else {
	    if (fprintf(Part_Spec_File," \n") < 1)  {
#if DEBUG_SYSTEM
		fprintf(stderr, "Write_Part_Spec_Conf error %d printing to file %s\n", errno, File_Name);
#else
		syslog(LOG_ERR, "Write_Part_Spec_Conf error %d printing to file %s\n", errno, File_Name);
#endif
	    Error_Code = errno;
	    break;
	    } /* if */
	} /* else */
    } /* while */

    /* Termination */
    if (fclose(Part_Spec_File) != 0) {
	if (Error_Code == 0) Error_Code = errno;
#if DEBUG_SYSTEM
	fprintf(stderr, "Write_Part_Spec_Conf error %d closing file %s\n", errno, File_Name);
#else
	syslog(LOG_NOTICE, "Write_Part_Spec_Conf error %d closing file %s\n", errno, File_Name);
#endif
    } /* if */
    list_iterator_destroy(Part_Record_Iterator);
    return Error_Code;
} /* Write_Part_Spec_Conf */
