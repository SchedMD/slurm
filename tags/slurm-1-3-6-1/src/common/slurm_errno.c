/*****************************************************************************\
 *  slurm_errno.c - error codes and functions for slurm
 ******************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jim Garlick <garlick@llnl.gov>, et. al.
 *  LLNL-CODE-402394.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission 
 *  to link the code of portions of this program with the OpenSSL library under 
 *  certain conditions as described in each individual source file, and 
 *  distribute linked combinations including the two. You must obey the GNU 
 *  General Public License in all respects for all of the code used other than 
 *  OpenSSL. If you modify file(s) with this exception, you may extend this 
 *  exception to your version of the file(s), but you are not obligated to do 
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in 
 *  the program, then also delete it here.
 *
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

/*  This implementation relies on "overloading" the libc errno by 
 *  partitioning its domain into system (<1000) and SLURM (>=1000) values.
 *  SLURM API functions should call slurm_seterrno() to set errno to a value.
 *  API users should call slurm_strerror() to convert all errno values to
 *  their description strings.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
 
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <slurm/slurm_errno.h>

#include "src/common/slurm_jobcomp.h"
#include "src/common/switch.h"

/* Type for error string table entries */
typedef struct {
	int xe_number;
	char *xe_message;
} slurm_errtab_t;

/* Add new error values to slurm/slurm_errno.h, and their descriptions to this table */
static slurm_errtab_t slurm_errtab[] = {
	{0, "No error"},
	{-1, "Unspecified error"},
	{EINPROGRESS, "Operation now in progress"},

	/*General Message error codes */
	{ SLURM_UNEXPECTED_MSG_ERROR, 
	  "Unexpected message received" 			},
	{ SLURM_COMMUNICATIONS_CONNECTION_ERROR,
	  "Communication connection failure"   			},
	{ SLURM_COMMUNICATIONS_SEND_ERROR, 
	  "Message send failure"				},
	{ SLURM_COMMUNICATIONS_RECEIVE_ERROR, 
	  "Message receive failure"				},
	{ SLURM_COMMUNICATIONS_SHUTDOWN_ERROR,
	  "Communication shutdown failure"			},
	{ SLURM_PROTOCOL_VERSION_ERROR,
	  "Protocol version has changed, re-link your code"	},
        { SLURM_PROTOCOL_IO_STREAM_VERSION_ERROR,
          "I/O stream version number error"                     },
        { SLURM_PROTOCOL_AUTHENTICATION_ERROR,
          "Protocol authentication error"                       },
        { SLURM_PROTOCOL_INSANE_MSG_LENGTH,
          "Insane message length"                               },
	{ SLURM_MPI_PLUGIN_NAME_INVALID,
	  "Invalid MPI plugin name"                             },
	{ SLURM_MPI_PLUGIN_PRELAUNCH_SETUP_FAILED,
	  "MPI plugin's pre-launch setup failed"                },
	{ SLURM_PLUGIN_NAME_INVALID,
	  "Plugin initialization failed"			},

	/* communication failures to/from slurmctld */
	{ SLURMCTLD_COMMUNICATIONS_CONNECTION_ERROR,
	  "Unable to contact slurm controller (connect failure)" },
	{ SLURMCTLD_COMMUNICATIONS_SEND_ERROR,
	  "Unable to contact slurm controller (send failure)"    },
	{ SLURMCTLD_COMMUNICATIONS_RECEIVE_ERROR,
	  "Unable to contact slurm controller (receive failure)" },
	{ SLURMCTLD_COMMUNICATIONS_SHUTDOWN_ERROR,
	  "Unable to contact slurm controller (shutdown failure)"},

	/* _info.c/communcation layer RESPONSE_SLURM_RC message codes */

	{ SLURM_NO_CHANGE_IN_DATA,	/* Not really an error */
	  "Data has not changed since time specified"		},

	/* slurmctld error codes */

	{ ESLURM_INVALID_PARTITION_NAME,
	  "Invalid partition name specified"			},
	{ ESLURM_DEFAULT_PARTITION_NOT_SET,
	  "No partition specified or system default partition"	},
	{ ESLURM_ACCESS_DENIED, 
	  "Access/permission denied"				},
	{ ESLURM_JOB_MISSING_REQUIRED_PARTITION_GROUP,
	  "User's group not permitted to use this partition"	},
	{ ESLURM_REQUESTED_NODES_NOT_IN_PARTITION,
	  "Requested nodes not in this partition"		},
	{ ESLURM_TOO_MANY_REQUESTED_CPUS,
	  "More processors requested than permitted"		},
	{ ESLURM_TOO_MANY_REQUESTED_NODES,
	  "Node count specification invalid"			},
	{ ESLURM_ERROR_ON_DESC_TO_RECORD_COPY,
	  "Unable to create job record, try again"		},
	{ ESLURM_JOB_MISSING_SIZE_SPECIFICATION,
	  "Job size specification needs to be provided"		},
	{ ESLURM_JOB_SCRIPT_MISSING, 
	  "Job script not specified"				},
	{ ESLURM_USER_ID_MISSING, 
	  "Invalid user id"					},
	{ ESLURM_DUPLICATE_JOB_ID, 
	  "Duplicate job id"					},
	{ ESLURM_PATHNAME_TOO_LONG,
	  "Pathname of a file, directory or other parameter too long" },
	{ ESLURM_NOT_TOP_PRIORITY,
	  "Immediate execution impossible, insufficient priority" },
	{ ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE,
	  "Requested node configuration is not available"	},
	{ ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE,
	  "Requested partition configuration not available now" },
	{ ESLURM_NODES_BUSY, 
	  "Requested nodes are busy"				},
	{ ESLURM_INVALID_JOB_ID, 
	  "Invalid job id specified"				},
	{ ESLURM_INVALID_NODE_NAME, 
	  "Invalid node name specified"				},
	{ ESLURM_WRITING_TO_FILE,
	  "I/O error writing script/environment to file"	},
	{ ESLURM_TRANSITION_STATE_NO_UPDATE,
	  "Job can not be altered now, try again later"		},
	{ ESLURM_ALREADY_DONE, 
	  "Job/step already completing or completed"		},
	{ ESLURM_INTERCONNECT_FAILURE, 
	  "Error configuring interconnect"			},
	{ ESLURM_BAD_DIST, 
	  "Task distribution specification invalid"		},
	{ ESLURM_JOB_PENDING, 
	  "Job is pending execution"				},
	{ ESLURM_BAD_TASK_COUNT, 
	  "Task count specification invalid"			},
	{ ESLURM_INVALID_JOB_CREDENTIAL, 
	  "Error generating job credential"			},
	{ ESLURM_IN_STANDBY_MODE,
	  "Slurm backup controller in standby mode"		},
	{ ESLURM_INVALID_NODE_STATE, 
	  "Invalid node state specified"			},
	{ ESLURM_INVALID_FEATURE, 
	  "Invalid feature specification"			},
	{ ESLURM_INVALID_AUTHTYPE_CHANGE,
	  "AuthType change requires restart of all SLURM daemons and "
	  "commands to take effect"},
	{ ESLURM_INVALID_CHECKPOINT_TYPE_CHANGE,
	  "CheckpointType change requires restart of all SLURM daemons "
	  "to take effect"					},
	{ ESLURM_INVALID_CRYPTO_TYPE_CHANGE,
	  "CryptoType change requires restart of all SLURM daemons "
	  "to take effect"					},
	{ ESLURM_INVALID_SCHEDTYPE_CHANGE,
	  "SchedulerType change requires restart of the slurmctld daemon "
	  "to take effect"					},
	{ ESLURM_INVALID_SELECTTYPE_CHANGE,
	  "SelectType change requires restart of the slurmctld daemon "
	  "to take effect"					},
	{ ESLURM_INVALID_SWITCHTYPE_CHANGE,
	  "SwitchType change requires restart of all SLURM daemons and "
	  "jobs to take effect"					},
	{ ESLURM_FRAGMENTATION,
	  "Immediate execution impossible, "
	  "resources too fragmented for allocation"		},
	{ ESLURM_NOT_SUPPORTED,
	  "Requested operation not supported on this system"	},
	{ ESLURM_DISABLED,
	  "Requested operation is presently disabled"		},
	{ ESLURM_DEPENDENCY,
	  "Job dependency problem"				},
 	{ ESLURM_BATCH_ONLY,
	  "Only batch jobs are accepted or processed"		},
	{ ESLURM_TASKDIST_ARBITRARY_UNSUPPORTED,
	  "Current SwitchType does not permit arbitrary task distribution"},
	{ ESLURM_TASKDIST_REQUIRES_OVERCOMMIT,
	  "Requested more tasks than available processors"	},
	{ ESLURM_JOB_HELD,
	  "Job is in held state, pending scheduler release"	},
	{ ESLURM_INVALID_BANK_ACCOUNT,
	  "Invalid bank account specified"			},
	{ ESLURM_INVALID_TASK_MEMORY,
	  "Memory required by task is not available"		},
	{ ESLURM_INVALID_ACCOUNT,
	  "Job has invalid account"				},
	{ ESLURM_INVALID_LICENSES,
	  "Job has invalid license specification"		},
	{ ESLURM_NEED_RESTART,
	  "The node configuration changes that were made require restart "
	  "of the slurmctld daemon to take effect"},
	{ ESLURM_ACCOUNTING_POLICY,
	  "Job violates accounting policy (the user's size and/or time limits)"},
	{ ESLURM_INVALID_TIME_LIMIT,
	  "Requested time limit exceeds partition limit"	},

	/* slurmd error codes */

	{ ESLRUMD_PIPE_ERROR_ON_TASK_SPAWN, 
	  "Pipe error on task spawn"				},
	{ ESLURMD_KILL_TASK_FAILED, 
	  "Kill task failed"					},
	{ ESLURMD_UID_NOT_FOUND,
	  "User not found on host"                              },
	{ ESLURMD_GID_NOT_FOUND,
	  "Group ID not found on host"                          },
	{ ESLURMD_INVALID_JOB_CREDENTIAL, 
	  "Invalid job credential"				},
	{ ESLURMD_CREDENTIAL_REVOKED, 
	  "Job credential revoked"                              },
	{ ESLURMD_CREDENTIAL_EXPIRED, 
	  "Job credential expired"                              },
	{ ESLURMD_CREDENTIAL_REPLAYED, 
	  "Job credential replayed"                             },
	{ ESLURMD_CREATE_BATCH_DIR_ERROR,
	  "Slurmd could not create a batch directory"		},
	{ ESLURMD_MODIFY_BATCH_DIR_ERROR,
	  "Slurmd could not chown or chmod a batch directory"	},
	{ ESLURMD_CREATE_BATCH_SCRIPT_ERROR,
	  "Slurmd could not create a batch script"		},
	{ ESLURMD_MODIFY_BATCH_SCRIPT_ERROR,
	  "Slurmd could not chown or chmod a batch script"	},
	{ ESLURMD_SETUP_ENVIRONMENT_ERROR,
	  "Slurmd could not set up environment for batch job"	},
	{ ESLURMD_SHARED_MEMORY_ERROR,
	  "Slurmd shared memory error"				},
	{ ESLURMD_SET_UID_OR_GID_ERROR,
	  "Slurmd could not set UID or GID"			},
	{ ESLURMD_SET_SID_ERROR,
	  "Slurmd could not set session ID"			},
	{ ESLURMD_CANNOT_SPAWN_IO_THREAD,
	  "Slurmd could not spawn I/O thread"			},
	{ ESLURMD_FORK_FAILED,
	  "Slurmd could not fork job"				},
	{ ESLURMD_EXECVE_FAILED,
	  "Slurmd could not execve job"				},
	{ ESLURMD_IO_ERROR,
	  "Slurmd could not connect IO"			        },
	{ ESLURMD_PROLOG_FAILED,
	  "Job prolog failed"			        	},
	{ ESLURMD_EPILOG_FAILED,
	  "Job epilog failed"			        	},
	{ ESLURMD_SESSION_KILLED,
	  "Session manager killed"		        	},
	{ ESLURMD_TOOMANYSTEPS,
	  "Too many job steps on node"		        	},
	{ ESLURMD_STEP_EXISTS,
	  "Job step already in shared memory"	        	},
	{ ESLURMD_JOB_NOTRUNNING,
	  "Job step not running"	        	        },
 	{ ESLURMD_STEP_SUSPENDED,
	  "Job step is suspended"                               },
 	{ ESLURMD_STEP_NOTSUSPENDED,
	  "Job step is not currently suspended"                 },

	/* slurmd errors in user batch job */
	{ ESCRIPT_CHDIR_FAILED,
	  "unable to change directory to work directory"	},
	{ ESCRIPT_OPEN_OUTPUT_FAILED,
	  "cound not open output file"			        },
	{ ESCRIPT_NON_ZERO_RETURN,
	  "Script terminated with non-zero exit code"		},


	/* socket specific SLURM communications error */

	{ SLURM_PROTOCOL_SOCKET_IMPL_ZERO_RECV_LENGTH,
	  "Received zero length message"			},
	{ SLURM_PROTOCOL_SOCKET_IMPL_NEGATIVE_RECV_LENGTH,
	  "Received message length < 0"				},
	{ SLURM_PROTOCOL_SOCKET_IMPL_NOT_ALL_DATA_SENT,
	  "Failed to send entire message"			},
	{ ESLURM_PROTOCOL_INCOMPLETE_PACKET,
	  "Header lengths are longer than data received"	},
	{ SLURM_PROTOCOL_SOCKET_IMPL_TIMEOUT,
	  "Socket timed out on send/recv operation"		},
	{ SLURM_PROTOCOL_SOCKET_ZERO_BYTES_SENT,
	  "Zero Bytes were transmitted or received"		},

	/* slurm_auth errors */

	{ ESLURM_AUTH_CRED_INVALID,
	  "Invalid authentication credential"			},
	{ ESLURM_AUTH_FOPEN_ERROR,
	  "Failed to open authentication public key"		},
	{ ESLURM_AUTH_NET_ERROR,
	  "Failed to connect to authentication agent"		}
};

/* 
 * Linear search through table of errno values and strings,
 * returns NULL on error, string on success.
 */
static char *_lookup_slurm_api_errtab(int errnum)
{
	char *res = NULL;
	int i;

	for (i = 0; i < sizeof(slurm_errtab) / sizeof(slurm_errtab_t); i++) {
		if (slurm_errtab[i].xe_number == errnum) {
			res = slurm_errtab[i].xe_message;
			break;
		}
	}

	if ((res == NULL) && 
	    (errnum >= ESLURM_JOBCOMP_MIN) &&
	    (errnum <= ESLURM_JOBCOMP_MAX))
		res = g_slurm_jobcomp_strerror(errnum);

#if 0	
	/* If needed, re-locate slurmctld/sched_plugin.[ch] into common */
	if ((res == NULL) && 
	    (errnum >= ESLURM_SCHED_MIN) &&
	    (errnum <= ESLURM_SCHED_MAX))
		res = sched_strerror(errnum);
#endif

	if ((res == NULL) &&
	    (errnum >= ESLURM_SWITCH_MIN) &&
	    (errnum <= ESLURM_SWITCH_MAX))
		res = switch_strerror(errnum);

	return res;
}

/*
 * Return string associated with error (SLURM or system).
 * Always returns a valid string (because strerror always does).
 */
char *slurm_strerror(int errnum)
{
	char *res = _lookup_slurm_api_errtab(errnum);
	return (res ? res : strerror(errnum));
}

/*
 * Get errno 
 */
int slurm_get_errno()
{
	return errno;
}

/*
 * Set errno to the specified value.
 */
void slurm_seterrno(int errnum)
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
void slurm_perror(char *msg)
{
	fprintf(stderr, "%s: %s\n", msg, slurm_strerror(errno));
}
