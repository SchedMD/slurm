/*****************************************************************************\
 *  slurm_errno.c - error codes and functions for slurm
 ******************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jim Garlick <garlick@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

/*  This implementation relies on "overloading" the libc errno by
 *  partitioning its domain into system (<1000) and Slurm (>=1000) values.
 *  Slurm API functions should call slurm_seterrno() to set errno to a value.
 *  API users should call slurm_strerror() to convert all errno values to
 *  their description strings.
 */

#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slurm/slurm_errno.h"

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

	/* General Message error codes */
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
	  "Incompatible versions of client and server code"	},
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
	{ SLURM_UNKNOWN_FORWARD_ADDR,
	  "Can't find an address, check slurm.conf"		},

	/* communication failures to/from slurmctld */
	{ SLURMCTLD_COMMUNICATIONS_CONNECTION_ERROR,
	  "Unable to contact slurm controller (connect failure)" },
	{ SLURMCTLD_COMMUNICATIONS_SEND_ERROR,
	  "Unable to contact slurm controller (send failure)"    },
	{ SLURMCTLD_COMMUNICATIONS_RECEIVE_ERROR,
	  "Unable to contact slurm controller (receive failure)" },
	{ SLURMCTLD_COMMUNICATIONS_SHUTDOWN_ERROR,
	  "Unable to contact slurm controller (shutdown failure)"},

	/* _info.c/communication layer RESPONSE_SLURM_RC message codes */

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
	{ ESLURM_INVALID_NODE_COUNT,
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
	{ ESLURM_INVALID_CORE_CNT,
	  "Core count for reservation node list is not consistent!" },
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
	  "AuthType change requires restart of all Slurm daemons and "
	  "commands to take effect"},
	{ ESLURM_INVALID_CHECKPOINT_TYPE_CHANGE,
	  "CheckpointType change requires restart of all Slurm daemons "
	  "to take effect"					},
	{ ESLURM_INVALID_CRED_TYPE_CHANGE,
	  "CredType change requires restart of all Slurm daemons "
	  "to take effect"					},
	{ ESLURM_INVALID_SCHEDTYPE_CHANGE,
	  "SchedulerType change requires restart of the slurmctld daemon "
	  "to take effect"					},
	{ ESLURM_INVALID_SELECTTYPE_CHANGE,
	  "SelectType change requires restart of the slurmctld daemon "
	  "to take effect"					},
	{ ESLURM_INVALID_SWITCHTYPE_CHANGE,
	  "SwitchType change requires restart of all Slurm daemons and "
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
	{ ESLURM_INVALID_TASK_MEMORY,
	  "Memory required by task is not available"		},
	{ ESLURM_INVALID_ACCOUNT,
	  "Invalid account or account/partition combination specified"},
	{ ESLURM_INVALID_PARENT_ACCOUNT,
	  "Invalid parent account specified"			},
	{ ESLURM_SAME_PARENT_ACCOUNT,
	  "Account already child of parent account specified"   },
	{ ESLURM_INVALID_QOS,
	  "Invalid qos specification"				},
	{ ESLURM_INVALID_WCKEY,
	  "Invalid wckey specification"				},
	{ ESLURM_INVALID_LICENSES,
	  "Invalid license specification"			},
	{ ESLURM_NEED_RESTART,
	  "The node configuration changes that were made require restart "
	  "of the slurmctld daemon to take effect"},
	{ ESLURM_ACCOUNTING_POLICY,
	  "Job violates accounting/QOS policy (job submit limit, user's "
	  "size and/or time limits)"},
	{ ESLURM_INVALID_TIME_LIMIT,
	  "Requested time limit is invalid (missing or exceeds some limit)"},
	{ ESLURM_RESERVATION_ACCESS,
	  "Access denied to requested reservation"		},
	{ ESLURM_RESERVATION_INVALID,
	  "Requested reservation is invalid"			},
	{ ESLURM_INVALID_TIME_VALUE,
	  "Invalid time specified"				},
	{ ESLURM_RESERVATION_BUSY,
	  "Requested reservation is in use"			},
	{ ESLURM_RESERVATION_NOT_USABLE,
	  "Requested reservation not usable now"		},
	{ ESLURM_RESERVATION_OVERLAP,
	  "Requested reservation overlaps with another reservation"	},
	{ ESLURM_PORTS_BUSY,
	  "Required ports are in use"				},
	{ ESLURM_PORTS_INVALID,
	  "Requires more ports than can be reserved"		},
	{ ESLURM_PROLOG_RUNNING,
	  "SlurmctldProlog is still running"			},
	{ ESLURM_NO_STEPS,
	  "Job steps can not be run on this cluster"		},
	{ ESLURM_INVALID_BLOCK_STATE,
	  "Invalid block state specified"			},
	{ ESLURM_INVALID_BLOCK_LAYOUT,
	  "Functionality not available with current block layout mode"},
	{ ESLURM_INVALID_BLOCK_NAME,
	  "Invalid block name specified"			},
	{ ESLURM_QOS_PREEMPTION_LOOP,
	  "QOS Preemption loop detected"                	},
	{ ESLURM_NODE_NOT_AVAIL,
	  "Required node not available (down, drained or reserved)"},
	{ ESLURM_INVALID_CPU_COUNT,
	  "CPU count specification invalid"             	},
	{ ESLURM_PARTITION_NOT_AVAIL,
	  "Required partition not available (inactive or drain)"},
	{ ESLURM_CIRCULAR_DEPENDENCY,
	  "Circular job dependency"				},
	{ ESLURM_INVALID_GRES,
	  "Invalid generic resource (gres) specification"	},
	{ ESLURM_JOB_NOT_PENDING,
	  "Job is no longer pending execution"			},
	{ ESLURM_QOS_THRES,
	  "Requested account has breached requested QOS usage threshold"},
	{ ESLURM_PARTITION_IN_USE,
	  "Partition is in use"					},
	{ ESLURM_STEP_LIMIT,
	  "Step limit reached for this job"			},
	{ ESLURM_JOB_SUSPENDED,
	  "Job is current suspended, requested operation disabled"	},
	{ ESLURM_CAN_NOT_START_IMMEDIATELY,
	  "Job can not start immediately"			},
	{ ESLURM_INTERCONNECT_BUSY,
	  "Switch resources currently not available"		},
	{ ESLURM_RESERVATION_EMPTY,
	  "Reservation request lacks users or accounts"		},
	{ ESLURM_INVALID_ARRAY,
	  "Invalid job array specification"			},
	{ ESLURM_RESERVATION_NAME_DUP,
	  "Duplicate reservation name"				},
	{ ESLURM_JOB_STARTED,
	  "Job has already started"				},
	{ ESLURM_JOB_FINISHED,
	  "Job has already finished"				},
	{ ESLURM_JOB_NOT_RUNNING,
	  "Job is not running"},
	{ ESLURM_JOB_NOT_PENDING_NOR_RUNNING,
	  "Job is not pending nor running"			},
	{ ESLURM_JOB_NOT_SUSPENDED,
	  "Job is not suspended"				},
	{ ESLURM_JOB_NOT_FINISHED,
	  "Job is not finished"					},
	{ ESLURM_TRIGGER_DUP,
	  "Duplicate event trigger"				},
	{ ESLURM_INTERNAL,
	  "Slurm internal error, contact system administrator"	},
	{ ESLURM_INVALID_BURST_BUFFER_CHANGE,
	  "BurstBufferType change requires restart of slurmctld daemon "
	  "to take effect"},
	{ ESLURM_BURST_BUFFER_PERMISSION,
	  "Burst Buffer permission denied"			},
	{ ESLURM_BURST_BUFFER_LIMIT,
	  "Burst Buffer resource limit exceeded"		},
	{ ESLURM_INVALID_BURST_BUFFER_REQUEST,
	  "Burst Buffer request invalid"			},
	{ ESLURM_PRIO_RESET_FAIL,
	  "Changes to job priority are not persistent, change nice instead" },
	{ ESLURM_POWER_NOT_AVAIL,
	  "Required power not available now"			},
	{ ESLURM_POWER_RESERVED,
	  "Required power at least partially reserved"		},
	{ ESLURM_INVALID_POWERCAP,
	  "Required powercap is not valid, check min/max values"},
	{ ESLURM_INVALID_MCS_LABEL,
	  "Invalid mcs_label specified"				},
	{ ESLURM_BURST_BUFFER_WAIT,
	  "Waiting for burst buffer"				},
	{ ESLURM_PARTITION_DOWN,
	  "Partition in DOWN state"				},
	{ ESLURM_DUPLICATE_GRES,
	  "Duplicate generic resource (gres) specification"	},
	{ ESLURM_JOB_SETTING_DB_INX,
	  "Job update not available right now, the DB index is being set, try again in a bit" },
	{ ESLURM_RSV_ALREADY_STARTED,
	  "Reservation already started"				},
	{ ESLURM_SUBMISSIONS_DISABLED,
	  "System submissions disabled"				},
	{ ESLURM_NOT_PACK_JOB,
	  "Job not heterogeneous job"				},
	{ ESLURM_NOT_PACK_JOB_LEADER,
	  "Job not heterogeneous job leader"			},
	{ ESLURM_NOT_PACK_WHOLE,
	  "Operation not permitted on individual component of heterogeneous job" },
	{ ESLURM_CORE_RESERVATION_UPDATE,
	  "Core-based reservation can not be updated"		},
	{ ESLURM_DUPLICATE_STEP_ID,
	  "Duplicate job step id"				},
	{ ESLURM_X11_NOT_AVAIL,
	  "X11 forwarding not available"			},
	{ ESLURM_GROUP_ID_MISSING,
	  "Invalid group id"					},
	{ ESLURM_BATCH_CONSTRAINT,
	  "Job --batch option is invalid or not a subset of --constraints" },
	{ ESLURM_INVALID_TRES,
	  "Invalid Trackable RESource (TRES) specification"	},
	{ ESLURM_INVALID_TRES_BILLING_WEIGHTS,
	  "Invalid TRESBillingWeights specification"            },
	{ ESLURM_INVALID_JOB_DEFAULTS,
	  "Invalid JobDefaults specification"			},
	{ ESLURM_RESERVATION_MAINT,
	  "Job can not start due to maintenance reservation."	},
	{ ESLURM_INVALID_GRES_TYPE,
	  "Invalid GRES specification (with and without type identification)" },
	{ ESLURM_REBOOT_IN_PROGRESS,
	  "Reboot already in progress" },
	{ ESLURM_MULTI_KNL_CONSTRAINT,
	  "Multiple KNL NUMA and/or MCDRAM constraints require use of a heterogeneous job" },
	{ ESLURM_UNSUPPORTED_GRES,
	  "Requested GRES option unsupported by configured SelectType plugin" },
	{ ESLURM_INVALID_NICE,
	  "Invalid --nice value"				},
	{ ESLURM_INVALID_TIME_MIN_LIMIT,
	  "Invalid time-min specification (exceeds job's time or other limits)"},

	/* slurmd error codes */
	{ ESLURMD_PIPE_ERROR_ON_TASK_SPAWN,
	  "Pipe error on task spawn"				},
	{ ESLURMD_KILL_TASK_FAILED,
	  "Kill task failed"					},
	{ ESLURMD_UID_NOT_FOUND,
	  "User not found on host"                              },
	{ ESLURMD_GID_NOT_FOUND,
	  "Group ID not found on host"                          },
	{ ESLURMD_INVALID_ACCT_FREQ,
	  "Invalid accounting frequency requested"		},
	{ ESLURMD_INVALID_JOB_CREDENTIAL,
	  "Invalid job credential"				},
	{ ESLURMD_CREDENTIAL_REVOKED,
	  "Job credential revoked"                              },
	{ ESLURMD_CREDENTIAL_EXPIRED,
	  "Job credential expired"                              },
	{ ESLURMD_CREDENTIAL_REPLAYED,
	  "Job credential replayed"                             },
	{ ESLURMD_CREATE_BATCH_DIR_ERROR,
	  "Slurmd could not create a batch directory or file"	},
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
	  "Job step already exists"		        	},
	{ ESLURMD_JOB_NOTRUNNING,
	  "Job step not running"	        	        },
 	{ ESLURMD_STEP_SUSPENDED,
	  "Job step is suspended"                               },
 	{ ESLURMD_STEP_NOTSUSPENDED,
	  "Job step is not currently suspended"                 },
	{ ESLURMD_INVALID_SOCKET_NAME_LEN,
	  "Unix socket name exceeded maximum length"		},

	/* slurmd errors in user batch job */
	{ ESCRIPT_CHDIR_FAILED,
	  "unable to change directory to work directory"	},
	{ ESCRIPT_OPEN_OUTPUT_FAILED,
	  "could not open output file"			        },
	{ ESCRIPT_NON_ZERO_RETURN,
	  "Script terminated with non-zero exit code"		},


	/* socket specific Slurm communications error */

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
	  "Failed to connect to authentication agent"		},
	{ ESLURM_AUTH_BADARG,
	  "Bad argument to plugin function"			},
	{ ESLURM_AUTH_MEMORY,
	  "Memory management error"				},
	{ ESLURM_AUTH_INVALID,
	  "Authentication credential invalid"			},
	{ ESLURM_AUTH_UNPACK,
	  "Cannot unpack credential"				},

	/* accounting errors */
	{ ESLURM_DB_CONNECTION,
	  "Unable to connect to database"			},
	{ ESLURM_JOBS_RUNNING_ON_ASSOC,
	  "Job(s) active, cancel job(s) before remove"		},
	{ ESLURM_CLUSTER_DELETED,
	  "Cluster deleted, commit/rollback immediately"        },
	{ ESLURM_ONE_CHANGE,
	  "Can only change one at a time"                       },
	{ ESLURM_BAD_NAME,
	  "Unacceptable name given. (No '.' in name allowed)"   },
	{ ESLURM_OVER_ALLOCATE,
	  "You can not allocate more than 100% of a resource"	},
	{ ESLURM_RESULT_TOO_LARGE,
	  "Query result exceeds size limit"			},
	{ ESLURM_DB_QUERY_TOO_WIDE,
	  "Too wide of a date range in query"			},

	/* Federation Errors */
	{ ESLURM_FED_CLUSTER_MAX_CNT,
	  "Too many clusters in federation"			},
	{ ESLURM_FED_CLUSTER_MULTIPLE_ASSIGNMENT,
	  "Clusters can only be assigned to one federation" 	},
	{ ESLURM_INVALID_CLUSTER_FEATURE,
	  "Invalid cluster feature specification"		},
	{ ESLURM_JOB_NOT_FEDERATED,
	  "Not a valid federated job"				},
	{ ESLURM_INVALID_CLUSTER_NAME,
	  "Invalid cluster name"				},
	{ ESLURM_FED_JOB_LOCK,
	  "Job locked by another sibling"			},
	{ ESLURM_FED_NO_VALID_CLUSTERS,
	  "No eligible clusters for federated job"		},

	/* plugin and custom errors */
	{ ESLURM_MISSING_TIME_LIMIT,
	  "Time limit specification required, but not provided"	},
	{ ESLURM_INVALID_KNL,
	  "Invalid KNL configuration (MCDRAM or NUMA option)"	}
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

	return res;
}

/*
 * Return string associated with error (Slurm or system).
 * Always returns a valid string (because strerror always does).
 */
char *slurm_strerror(int errnum)
{
	char *res = _lookup_slurm_api_errtab(errnum);
	if (res)
		return res;
	else if (errnum > 0)
		return strerror(errnum);
	else
		return "Unknown negative error number";
}

/*
 * Get errno
 */
int slurm_get_errno(void)
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
void slurm_perror(const char *msg)
{
	fprintf(stderr, "%s: %s\n", msg, slurm_strerror(errno));
}
