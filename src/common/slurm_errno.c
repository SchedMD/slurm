/*****************************************************************************\
 *  slurm_errno.c - error codes and functions for slurm
 *  This implementation relies on "overloading" the libc errno by 
 *  partitioning its domain into system (<1000) and SLURM (>=1000) values.
 *  SLURM API functions should call slurm_seterrno() to set errno to a value.
 *  API users should call slurm_strerror() to convert all errno values to
 *  their description strings.
 ******************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jim Garlick <garlick@llnl.gov>, et. al.
 *  UCRL-CODE-2002-040.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <src/common/slurm_errno.h>


/* Type for error string table entries */
typedef struct {
	int xe_number;
	char *xe_message;
} slurm_errtab_t;

/* Add new error values to xerrno.h, and their descriptions to this table */
static slurm_errtab_t slurm_errtab[] = {
	{ 0, 					"No error" },
	{ -1, 					"Unspecified error" },
	/*General Message error codes */
	{ SLURM_UNEXPECTED_MSG_ERROR, 		"Unexpected message recieved" },
	{ SLURM_COMMUNICATIONS_CONNECTION_ERROR,"Communication connection failure" },
	{ SLURM_COMMUNICATIONS_SEND_ERROR,	"Message send failure" },
	{ SLURM_COMMUNICATIONS_RECEIVE_ERROR,	"Message receive failure" },
	{ SLURM_COMMUNICATIONS_SHUTDOWN_ERROR,	"Communication shutdown failure" },
	{ SLURM_PROTOCOL_VERSION_ERROR,		"Protocol version has changed, re-link your code" },
	
	/* _info.c/ocommuncation layer RESPONSE_SLURM_RC message codes */
	{ SLURM_NO_CHANGE_IN_DATA, 		"Data has not changed since time specified" },

	/* slurmctld error codes */
	{ ESLURM_INVALID_PARTITION_NAME,	"Invalid partition name specified" },
	{ ESLURM_DEFAULT_PARTITION_NOT_SET, 	"System default partition not set" },
	{ ESLURM_ACCESS_DENIED,			"Access denied" },
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
	{ ESLURM_PATHNAME_TOO_LONG , 		"Pathname of a file or directory too long" },
	{ ESLURM_NOT_TOP_PRIORITY,		"Immediate execution impossible, higher priority jobs pending" },
	{ ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE, "Requested node configuration is not available" },
	{ ESLURM_NODES_BUSY,			"Requested nodes are busy" },
	{ ESLURM_INVALID_JOB_ID,		"Invalid job id specified" },
	{ ESLURM_INVALID_NODE_NAME,		"Invalid node name specified" },
	{ ESLURM_WRITING_TO_FILE,		"I/O error writing script/environment to file" },
	{ ESLURM_TRANSITION_STATE_NO_UPDATE,	"Job can not be altered now, try again later" },
	{ ESLURM_ALREADY_DONE,			"Job/step already completed" },

	/* Quadrics Elan routine error codes */
	{ ENOSLURM,             "Out of slurm" }, /* oh no! */
        { EBADMAGIC_QSWLIBSTATE,"Bad magic in QSW libstate" },
        { EBADMAGIC_QSWJOBINFO, "Bad magic in QSW jobinfo" },
        { EINVAL_PRGCREATE,     "Program identifier in use or number of CPUs invalid"
 },
        { ECHILD_PRGDESTROY,    "Processes belonging to this program are still runnin
g" },
        { EEXIST_PRGDESTROY,    "Program identifier does not exist" },
        { EELAN3INIT,           "Too many processes using Elan or mapping failure" },
        { EELAN3CONTROL,        "Could not open elan3 control device" },
        { EELAN3CREATE,         "Could not create elan capability" },
        { ESRCH_PRGADDCAP,      "Program does not exist (addcap)" },
        { EFAULT_PRGADDCAP,     "Capability has invalid address (addcap)" },
        { EINVAL_SETCAP,        "Invalid context number (setcap)" },
        { EFAULT_SETCAP,        "Capability has invalid address (setcap)" },
        { EGETNODEID,           "Cannot determine local elan address" },
        { EGETNODEID_BYHOST,    "Cannot translate hostname to elan address" },
        { EGETHOST_BYNODEID,    "Cannot translate elan address to hostname" },
        { ESRCH_PRGSIGNAL,      "No such program identifier" },
        { EINVAL_PRGSIGNAL,     "Invalid signal number" },

        /* slurmd error codes */
        { ESLRUMD_PIPE_ERROR_ON_TASK_SPAWN,		"Pipe error on task spawn" },
	{ ESLURMD_KILL_TASK_FAILED,			"Kill task failed" },
	{ ESLURMD_OPENSSL_ERROR,			"Openssl error" },
	{ ESLURMD_NO_AVAILABLE_JOB_STEP_SLOTS_IN_SHMEM,	"No available job step slots in shmem" },
	{ ESLURMD_NO_AVAILABLE_TASK_SLOTS_IN_SHMEM,	"No available task slots in shmem" },
        /* socket specific SLURM communications error */
	{ SLURM_PROTOCOL_SOCKET_IMPL_ZERO_RECV_LENGTH,		"Received zero length message" },
	{ SLURM_PROTOCOL_SOCKET_IMPL_NEGATIVE_RECV_LENGTH,	"Received message length < 0" },
	{ SLURM_PROTOCOL_SOCKET_IMPL_NOT_ALL_DATA_SENT,		"Failed to send entire message" }
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
