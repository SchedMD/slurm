#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <src/common/slurm_protocol_errno.h>


/* Type for error string table entries */
typedef struct {
	int xe_number;
	char *xe_message;
} slurm_errtab_t;

/* Add new error values to xerrno.h, and their descriptions to this table */
static slurm_errtab_t slurm_errtab[] = {
	{ 0, 						"" },
	{ -1, 						"" },
	{ SLURM_UNEXPECTED_MSG_ERROR, 			"" },
	{ SLURM_PROTOCOL_VERSION_ERROR,			"" },
	{ SLURM_NO_CHANGE_IN_DATA, 			"" },
	{ ESLURM_INVALID_PARTITION_SPECIFIED, 		"" },
	{ ESLURM_DEFAULT_PATITION_NOT_SET, 		"" },
	{ ESLURM_JOB_MISSING_PARTITION_KEY, 		"" },
	{ ESLURM_JOB_MISSING_REQUIRED_PARTITION_GROUP,	"" },
	{ ESLURM_REQUESTED_NODES_NOT_IN_PARTITION, 	"" },
	{ ESLURM_TOO_MANY_REQUESTED_CPUS, 		"" },
	{ ESLURM_TOO_MANY_REQUESTED_NODES, 		"" },
	{ ESLURM_ERROR_ON_DESC_TO_RECORD_COPY,	 	"" },
	{ ESLURM_JOB_MISSING_SIZE_SPECIFICATION, 	"" },
	{ ESLURM_JOB_SCRIPT_MISSING,	 		"" },
	{ ESLURM_USER_ID_MISSING , 			"" },
	{ ESLURM_JOB_NAME_TOO_LONG,			"" },
	{ ESLURM_DUPLICATE_JOB_ID , 			"" },
	{ ESLURM_INVALID_PROCS_PER_TASK,	 	"" },
	{ ESLURM_NOT_TOP_PRIORITY,			"" },
	{ ESLURM_REQUESTED_NODE_CONFIGURATION_UNAVAILBLE, "" },
	{ ESLURM_NODES_BUSY,				"" },

	/* partition_mgr.c/update_part */
	{ ESLURM_PROTOCOL_INVALID_PARTITION_NAME,	"" },
	/* node_mgr.c/update_node */
	{ ESLURM_INVALID_NODE_NAME,		"" }
};

/* 
 * Linear search through table of errno values and strings, 
 * returns NULL on error, string on success.
 */
static char *
_lookup_slurm_api_errtab(int errnum)
{
	char *res = NULL;
	int i;

	for (i = 0; i < sizeof(slurm_errtab) / sizeof(slurm_errtab_t); i++) {
		if (slurm_errtab[i].xe_number == errnum) {
			res = slurm_errtab[i].xe_message;
			break;
		}
	}
	return res;
}

/*
 * Return string associated with error (SLURM or system).
 * Always returns a valid string (because strerror always does).
 */
char *
slurm_strerror(int errnum)
{
	char *res = _lookup_slurm_api_errtab(errnum);

	return (res ? res : strerror(errnum));
}

/*
 * Set errno to the specified value.
 */
void
slurm_seterrno(int errnum)
{
#ifdef __set_errno
	__set_errno(errnum);
#else
	errno = errnum;
#endif
}

/*
 * Print "message: error description" on stderr for current errno value.
 */
void
slurm_perror(char *msg)
{
	fprintf(stderr, "%s: %s\n", msg, slurm_strerror(errno));
}
