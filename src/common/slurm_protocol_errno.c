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
	{ 0, 					"" },
	{ -1, 					"" },
	{ SLURM_UNEXPECTED_MSG_ERROR, 		"Unexpected message recieved" },
	{ SLURM_COMMUNICATIONS_CONNECTION_ERROR,"Communication connection failure" },
	{ SLURM_COMMUNICATIONS_SEND_ERROR,	"Message send failure" },
	{ SLURM_COMMUNICATIONS_RECEIVE_ERROR,	"Message receive failure" },
	{ SLURM_COMMUNICATIONS_SHUTDOWN_ERROR,	"Communication shutdown failure" },
	{ SLURM_PROTOCOL_VERSION_ERROR,		"Protocol version has changed, re-link your code" },
	{ SLURM_NO_CHANGE_IN_DATA, 		"Data has not changed since time specified" },

	/* job_mgr.c/job_create */
	{ ESLURM_INVALID_PARTITION_NAME,	"Invalid partition name specified" },
	{ ESLURM_DEFAULT_PARTITION_NOT_SET, 	"System default partition not set" },
	{ ESLURM_JOB_MISSING_PARTITION_KEY, 	"Key must be specified to use this partition" },
	{ ESLURM_JOB_MISSING_REQUIRED_PARTITION_GROUP,	"User's group not permitted to use this partition" },
	{ ESLURM_REQUESTED_NODES_NOT_IN_PARTITION, "Requested nodes not in this partition" },
	{ ESLURM_TOO_MANY_REQUESTED_CPUS, 	"More processors requested than permitted" },
	{ ESLURM_TOO_MANY_REQUESTED_NODES, 	"More nodes requested than permitted" },
	{ ESLURM_ERROR_ON_DESC_TO_RECORD_COPY,	"Unable to create job record, try again" },
	{ ESLURM_JOB_MISSING_SIZE_SPECIFICATION,"Job size specification needs to be provided" },
	{ ESLURM_JOB_SCRIPT_MISSING,	 	"Job script not specified" },
	{ ESLURM_USER_ID_MISSING , 		"User id missing" },
	{ ESLURM_JOB_NAME_TOO_LONG,		"Job name too long" },
	{ ESLURM_DUPLICATE_JOB_ID , 		"Duplicate job id" },
	{ ESLURM_NOT_TOP_PRIORITY,		"Immediate execution impossible, higher priority jobs pending" },
	{ ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE, "Requested node configuration is not available" },
	{ ESLURM_NODES_BUSY,			"Requested nodes are busy" },
	{ ESLURM_INVALID_JOB_ID,		"Invalid job id specified" },
	{ ESLURM_INVALID_NODE_NAME,		"Invalid node name specified" },
	{ ESLURM_TRANSITION_STATE_NO_UPDATE,	"Job can not be altered now, try again later" },
	{ ESLURM_ALREADY_DONE,			"Job/step already completed" },

	{ ESLURM_ACCESS_DENIED,			"Access denied" }
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
 * Get errno 
 */
int
slurm_get_errno()
{
	return errno ;
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
