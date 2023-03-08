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

/*
 * Each table entry needs the macro directly (to be translated into the raw
 * value) alongside the text string representation. Rather than duplicate them
 * - and risk them getting out of sync on a bad copy + paste - use this macro
 * to construct each row.
 */
#define ERRTAB_ENTRY(_e) _e, #_e

/* Add new error values to slurm/slurm_errno.h, and their descriptions to this table */
slurm_errtab_t slurm_errtab[] = {
	{ ERRTAB_ENTRY(SLURM_SUCCESS), "No error"},
	{ ERRTAB_ENTRY(SLURM_ERROR), "Unspecified error"},
	{ ERRTAB_ENTRY(EINPROGRESS), "Operation now in progress"},

	/* General Message error codes */
	{ ERRTAB_ENTRY(SLURM_UNEXPECTED_MSG_ERROR),
	  "Unexpected message received" 			},
	{ ERRTAB_ENTRY(SLURM_COMMUNICATIONS_CONNECTION_ERROR),
	  "Communication connection failure"   			},
	{ ERRTAB_ENTRY(SLURM_COMMUNICATIONS_SEND_ERROR),
	  "Message send failure"				},
	{ ERRTAB_ENTRY(SLURM_COMMUNICATIONS_RECEIVE_ERROR),
	  "Message receive failure"				},
	{ ERRTAB_ENTRY(SLURM_COMMUNICATIONS_SHUTDOWN_ERROR),
	  "Communication shutdown failure"			},
	{ ERRTAB_ENTRY(SLURM_PROTOCOL_VERSION_ERROR),
	  "Incompatible versions of client and server code"	},
        { ERRTAB_ENTRY(SLURM_PROTOCOL_IO_STREAM_VERSION_ERROR),
          "I/O stream version number error"                     },
        { ERRTAB_ENTRY(SLURM_PROTOCOL_AUTHENTICATION_ERROR),
          "Protocol authentication error"                       },
        { ERRTAB_ENTRY(SLURM_PROTOCOL_INSANE_MSG_LENGTH),
          "Insane message length"                               },
	{ ERRTAB_ENTRY(SLURM_MPI_PLUGIN_NAME_INVALID),
	  "Invalid MPI plugin name"                             },
	{ ERRTAB_ENTRY(SLURM_MPI_PLUGIN_PRELAUNCH_SETUP_FAILED),
	  "MPI plugin's pre-launch setup failed"                },
	{ ERRTAB_ENTRY(SLURM_PLUGIN_NAME_INVALID),
	  "Plugin initialization failed"			},
	{ ERRTAB_ENTRY(SLURM_UNKNOWN_FORWARD_ADDR),
	  "Can't find an address, check slurm.conf"		},
	{ ERRTAB_ENTRY(SLURM_COMMUNICATIONS_MISSING_SOCKET_ERROR),
	  "Unexpected missing socket error"			},

	/* communication failures to/from slurmctld */
	{ ERRTAB_ENTRY(SLURMCTLD_COMMUNICATIONS_CONNECTION_ERROR),
	  "Unable to contact slurm controller (connect failure)" },
	{ ERRTAB_ENTRY(SLURMCTLD_COMMUNICATIONS_SEND_ERROR),
	  "Unable to contact slurm controller (send failure)"    },
	{ ERRTAB_ENTRY(SLURMCTLD_COMMUNICATIONS_RECEIVE_ERROR),
	  "Unable to contact slurm controller (receive failure)" },
	{ ERRTAB_ENTRY(SLURMCTLD_COMMUNICATIONS_SHUTDOWN_ERROR),
	  "Unable to contact slurm controller (shutdown failure)"},
	{ ERRTAB_ENTRY(SLURMCTLD_COMMUNICATIONS_BACKOFF),
	  "Rate limit exceeded, please retry momentarily"},

	/* _info.c/communication layer RESPONSE_SLURM_RC message codes */

	/* Not really an error */
	{ ERRTAB_ENTRY(SLURM_NO_CHANGE_IN_DATA),
	  "Data has not changed since time specified"		},

	/* slurmctld error codes */

	{ ERRTAB_ENTRY(ESLURM_INVALID_PARTITION_NAME),
	  "Invalid partition name specified"			},
	{ ERRTAB_ENTRY(ESLURM_DEFAULT_PARTITION_NOT_SET),
	  "No partition specified or system default partition"	},
	{ ERRTAB_ENTRY(ESLURM_ACCESS_DENIED),
	  "Access/permission denied"				},
	{ ERRTAB_ENTRY(ESLURM_JOB_MISSING_REQUIRED_PARTITION_GROUP),
	  "User's group not permitted to use this partition"	},
	{ ERRTAB_ENTRY(ESLURM_REQUESTED_NODES_NOT_IN_PARTITION),
	  "Requested nodes not in this partition"		},
	{ ERRTAB_ENTRY(ESLURM_TOO_MANY_REQUESTED_CPUS),
	  "More processors requested than permitted"		},
	{ ERRTAB_ENTRY(ESLURM_INVALID_NODE_COUNT),
	  "Node count specification invalid"			},
	{ ERRTAB_ENTRY(ESLURM_ERROR_ON_DESC_TO_RECORD_COPY),
	  "Unable to create job record, try again"		},
	{ ERRTAB_ENTRY(ESLURM_JOB_MISSING_SIZE_SPECIFICATION),
	  "Job size specification needs to be provided"		},
	{ ERRTAB_ENTRY(ESLURM_JOB_SCRIPT_MISSING),
	  "Job script not specified"				},
	{ ERRTAB_ENTRY(ESLURM_USER_ID_MISSING),
	  "Invalid user id"					},
	{ ERRTAB_ENTRY(ESLURM_DUPLICATE_JOB_ID),
	  "Duplicate job id"					},
	{ ERRTAB_ENTRY(ESLURM_PATHNAME_TOO_LONG),
	  "Pathname of a file, directory or other parameter too long" },
	{ ERRTAB_ENTRY(ESLURM_NOT_TOP_PRIORITY),
	  "Immediate execution impossible, insufficient priority" },
	{ ERRTAB_ENTRY(ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE),
	  "Requested node configuration is not available"	},
	{ ERRTAB_ENTRY(ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE),
	  "Requested partition configuration not available now" },
	{ ERRTAB_ENTRY(ESLURM_NODES_BUSY),
	  "Requested nodes are busy"				},
	{ ERRTAB_ENTRY(ESLURM_INVALID_JOB_ID),
	  "Invalid job id specified"				},
	{ ERRTAB_ENTRY(ESLURM_INVALID_NODE_NAME),
	  "Invalid node name specified"				},
	{ ERRTAB_ENTRY(ESLURM_INVALID_CORE_CNT),
	  "Core count for reservation node list is not consistent!" },
	{ ERRTAB_ENTRY(ESLURM_WRITING_TO_FILE),
	  "I/O error writing script/environment to file"	},
	{ ERRTAB_ENTRY(ESLURM_TRANSITION_STATE_NO_UPDATE),
	  "Job can not be altered now, try again later"		},
	{ ERRTAB_ENTRY(ESLURM_ALREADY_DONE),
	  "Job/step already completing or completed"		},
	{ ERRTAB_ENTRY(ESLURM_INTERCONNECT_FAILURE),
	  "Error configuring interconnect"			},
	{ ERRTAB_ENTRY(ESLURM_BAD_DIST),
	  "Task distribution specification invalid"		},
	{ ERRTAB_ENTRY(ESLURM_JOB_PENDING),
	  "Job is pending execution"				},
	{ ERRTAB_ENTRY(ESLURM_BAD_TASK_COUNT),
	  "Task count specification invalid"			},
	{ ERRTAB_ENTRY(ESLURM_INVALID_JOB_CREDENTIAL),
	  "Error generating job credential"			},
	{ ERRTAB_ENTRY(ESLURM_IN_STANDBY_MODE),
	  "Slurm backup controller in standby mode"		},
	{ ERRTAB_ENTRY(ESLURM_INVALID_NODE_STATE),
	  "Invalid node state specified"			},
	{ ERRTAB_ENTRY(ESLURM_INVALID_FEATURE),
	  "Invalid feature specification"			},
	{ ERRTAB_ENTRY(ESLURM_INVALID_AUTHTYPE_CHANGE),
	  "AuthType change requires restart of all Slurm daemons and "
	  "commands to take effect"},
	{ ERRTAB_ENTRY(ESLURM_ACTIVE_FEATURE_NOT_SUBSET),
	  "Active features not subset of available features"	},
	{ ERRTAB_ENTRY(ESLURM_INVALID_CRED_TYPE_CHANGE),
	  "CredType change requires restart of all Slurm daemons "
	  "to take effect"					},
	{ ERRTAB_ENTRY(ESLURM_INVALID_SCHEDTYPE_CHANGE),
	  "SchedulerType change requires restart of the slurmctld daemon "
	  "to take effect"					},
	{ ERRTAB_ENTRY(ESLURM_INVALID_SELECTTYPE_CHANGE),
	  "SelectType change requires restart of the slurmctld daemon "
	  "to take effect"					},
	{ ERRTAB_ENTRY(ESLURM_INVALID_SWITCHTYPE_CHANGE),
	  "SwitchType change requires restart of all Slurm daemons and "
	  "jobs to take effect"					},
	{ ERRTAB_ENTRY(ESLURM_FRAGMENTATION),
	  "Immediate execution impossible, "
	  "resources too fragmented for allocation"		},
	{ ERRTAB_ENTRY(ESLURM_NOT_SUPPORTED),
	  "Requested operation not supported on this system"	},
	{ ERRTAB_ENTRY(ESLURM_DISABLED),
	  "Requested operation is presently disabled"		},
	{ ERRTAB_ENTRY(ESLURM_DEPENDENCY),
	  "Job dependency problem"				},
	{ ERRTAB_ENTRY(ESLURM_BATCH_ONLY),
	  "Only batch jobs are accepted or processed"		},
	{ ERRTAB_ENTRY(ESLURM_LICENSES_UNAVAILABLE),
	  "Licenses currently unavailable"			},
	{ ERRTAB_ENTRY(ESLURM_JOB_HELD),
	  "Job is in held state, pending scheduler release"	},
	{ ERRTAB_ENTRY(ESLURM_INVALID_TASK_MEMORY),
	  "Memory required by task is not available"		},
	{ ERRTAB_ENTRY(ESLURM_INVALID_ACCOUNT),
	  "Invalid account or account/partition combination specified"},
	{ ERRTAB_ENTRY(ESLURM_INVALID_PARENT_ACCOUNT),
	  "Invalid parent account specified"			},
	{ ERRTAB_ENTRY(ESLURM_SAME_PARENT_ACCOUNT),
	  "Account already child of parent account specified"   },
	{ ERRTAB_ENTRY(ESLURM_INVALID_QOS),
	  "Invalid qos specification"				},
	{ ERRTAB_ENTRY(ESLURM_INVALID_WCKEY),
	  "Invalid wckey specification"				},
	{ ERRTAB_ENTRY(ESLURM_INVALID_LICENSES),
	  "Invalid license specification"			},
	{ ERRTAB_ENTRY(ESLURM_NEED_RESTART),
	  "The node configuration changes that were made require restart "
	  "of the slurmctld daemon to take effect"},
	{ ERRTAB_ENTRY(ESLURM_ACCOUNTING_POLICY),
	  "Job violates accounting/QOS policy (job submit limit, user's "
	  "size and/or time limits)"},
	{ ERRTAB_ENTRY(ESLURM_INVALID_TIME_LIMIT),
	  "Requested time limit is invalid (missing or exceeds some limit)"},
	{ ERRTAB_ENTRY(ESLURM_RESERVATION_ACCESS),
	  "Access denied to requested reservation"		},
	{ ERRTAB_ENTRY(ESLURM_RESERVATION_INVALID),
	  "Requested reservation is invalid"			},
	{ ERRTAB_ENTRY(ESLURM_INVALID_TIME_VALUE),
	  "Invalid time specified"				},
	{ ERRTAB_ENTRY(ESLURM_RESERVATION_BUSY),
	  "Requested reservation is in use"			},
	{ ERRTAB_ENTRY(ESLURM_RESERVATION_NOT_USABLE),
	  "Requested reservation not usable now"		},
	{ ERRTAB_ENTRY(ESLURM_RESERVATION_OVERLAP),
	  "Requested reservation overlaps with another reservation"	},
	{ ERRTAB_ENTRY(ESLURM_PORTS_BUSY),
	  "Required ports are in use"				},
	{ ERRTAB_ENTRY(ESLURM_PORTS_INVALID),
	  "Requires more ports than can be reserved"		},
	{ ERRTAB_ENTRY(ESLURM_PROLOG_RUNNING),
	  "PrologSlurmctld is still running"			},
	{ ERRTAB_ENTRY(ESLURM_NO_STEPS),
	  "Job steps can not be run on this cluster"		},
	{ ERRTAB_ENTRY(ESLURM_QOS_PREEMPTION_LOOP),
	  "QOS Preemption loop detected"                	},
	{ ERRTAB_ENTRY(ESLURM_NODE_NOT_AVAIL),
	  "Required node not available (down, drained or reserved)"},
	{ ERRTAB_ENTRY(ESLURM_INVALID_CPU_COUNT),
	  "CPU count specification invalid"             	},
	{ ERRTAB_ENTRY(ESLURM_PARTITION_NOT_AVAIL),
	  "Required partition not available (inactive or drain)"},
	{ ERRTAB_ENTRY(ESLURM_CIRCULAR_DEPENDENCY),
	  "Circular job dependency"				},
	{ ERRTAB_ENTRY(ESLURM_INVALID_GRES),
	  "Invalid generic resource (gres) specification"	},
	{ ERRTAB_ENTRY(ESLURM_JOB_NOT_PENDING),
	  "Job is no longer pending execution"			},
	{ ERRTAB_ENTRY(ESLURM_QOS_THRES),
	  "Requested account has breached requested QOS usage threshold"},
	{ ERRTAB_ENTRY(ESLURM_PARTITION_IN_USE),
	  "Partition is in use"					},
	{ ERRTAB_ENTRY(ESLURM_STEP_LIMIT),
	  "Step limit reached for this job"			},
	{ ERRTAB_ENTRY(ESLURM_JOB_SUSPENDED),
	  "Job is current suspended, requested operation disabled"	},
	{ ERRTAB_ENTRY(ESLURM_CAN_NOT_START_IMMEDIATELY),
	  "Job can not start immediately"			},
	{ ERRTAB_ENTRY(ESLURM_INTERCONNECT_BUSY),
	  "Switch resources currently not available"		},
	{ ERRTAB_ENTRY(ESLURM_RESERVATION_EMPTY),
	  "Reservation request lacks users, groups or accounts"		},
	{ ERRTAB_ENTRY(ESLURM_INVALID_ARRAY),
	  "Invalid job array specification"			},
	{ ERRTAB_ENTRY(ESLURM_RESERVATION_NAME_DUP),
	  "Duplicate reservation name"				},
	{ ERRTAB_ENTRY(ESLURM_JOB_STARTED),
	  "Job has already started"				},
	{ ERRTAB_ENTRY(ESLURM_JOB_FINISHED),
	  "Job has already finished"				},
	{ ERRTAB_ENTRY(ESLURM_JOB_NOT_RUNNING),
	  "Job is not running"},
	{ ERRTAB_ENTRY(ESLURM_JOB_NOT_PENDING_NOR_RUNNING),
	  "Job is not pending nor running"			},
	{ ERRTAB_ENTRY(ESLURM_JOB_NOT_SUSPENDED),
	  "Job is not suspended"				},
	{ ERRTAB_ENTRY(ESLURM_JOB_NOT_FINISHED),
	  "Job is not finished"					},
	{ ERRTAB_ENTRY(ESLURM_TRIGGER_DUP),
	  "Duplicate event trigger"				},
	{ ERRTAB_ENTRY(ESLURM_INTERNAL),
	  "Slurm internal error, contact system administrator"	},
	{ ERRTAB_ENTRY(ESLURM_INVALID_BURST_BUFFER_CHANGE),
	  "BurstBufferType change requires restart of slurmctld daemon "
	  "to take effect"},
	{ ERRTAB_ENTRY(ESLURM_BURST_BUFFER_PERMISSION),
	  "Burst Buffer permission denied"			},
	{ ERRTAB_ENTRY(ESLURM_BURST_BUFFER_LIMIT),
	  "Burst Buffer resource limit exceeded"		},
	{ ERRTAB_ENTRY(ESLURM_INVALID_BURST_BUFFER_REQUEST),
	  "Burst Buffer request invalid"			},
	{ ERRTAB_ENTRY(ESLURM_PRIO_RESET_FAIL),
	  "Changes to job priority are not persistent, change nice instead" },
	{ ERRTAB_ENTRY(ESLURM_CANNOT_MODIFY_CRON_JOB),
	  "Cannot modify scrontab jobs through scontrol"	},
	{ ERRTAB_ENTRY(ESLURM_INVALID_JOB_CONTAINER_CHANGE),
	  "JobContainerType change requires restart of all Slurm daemons and commands to take effect" },
	{ ERRTAB_ENTRY(ESLURM_CANNOT_CANCEL_CRON_JOB),
	  "Cannot cancel scrontab jobs without --cron flag."	},
	{ ERRTAB_ENTRY(ESLURM_INVALID_MCS_LABEL),
	  "Invalid mcs_label specified"				},
	{ ERRTAB_ENTRY(ESLURM_BURST_BUFFER_WAIT),
	  "Waiting for burst buffer"				},
	{ ERRTAB_ENTRY(ESLURM_PARTITION_DOWN),
	  "Partition in DOWN state"				},
	{ ERRTAB_ENTRY(ESLURM_DUPLICATE_GRES),
	  "Duplicate generic resource (gres) specification"	},
	{ ERRTAB_ENTRY(ESLURM_JOB_SETTING_DB_INX),
	  "Job update not available right now, the DB index is being set, try again in a bit" },
	{ ERRTAB_ENTRY(ESLURM_RSV_ALREADY_STARTED),
	  "Reservation already started"				},
	{ ERRTAB_ENTRY(ESLURM_SUBMISSIONS_DISABLED),
	  "System submissions disabled"				},
	{ ERRTAB_ENTRY(ESLURM_NOT_HET_JOB),
	  "Job not heterogeneous job"				},
	{ ERRTAB_ENTRY(ESLURM_NOT_HET_JOB_LEADER),
	  "Job not heterogeneous job leader"			},
	{ ERRTAB_ENTRY(ESLURM_NOT_WHOLE_HET_JOB),
	  "Operation not permitted on individual component of heterogeneous job" },
	{ ERRTAB_ENTRY(ESLURM_CORE_RESERVATION_UPDATE),
	  "Core-based reservation can not be updated"		},
	{ ERRTAB_ENTRY(ESLURM_DUPLICATE_STEP_ID),
	  "Duplicate job step id"				},
	{ ERRTAB_ENTRY(ESLURM_X11_NOT_AVAIL),
	  "X11 forwarding not available"			},
	{ ERRTAB_ENTRY(ESLURM_GROUP_ID_MISSING),
	  "Invalid group id"					},
	{ ERRTAB_ENTRY(ESLURM_BATCH_CONSTRAINT),
	  "Job --batch option is invalid or not a subset of --constraints" },
	{ ERRTAB_ENTRY(ESLURM_INVALID_TRES),
	  "Invalid Trackable RESource (TRES) specification"	},
	{ ERRTAB_ENTRY(ESLURM_INVALID_TRES_BILLING_WEIGHTS),
	  "Invalid TRESBillingWeights specification"            },
	{ ERRTAB_ENTRY(ESLURM_INVALID_JOB_DEFAULTS),
	  "Invalid JobDefaults specification"			},
	{ ERRTAB_ENTRY(ESLURM_RESERVATION_MAINT),
	  "Job can not start due to maintenance reservation."	},
	{ ERRTAB_ENTRY(ESLURM_INVALID_GRES_TYPE),
	  "Invalid GRES specification (with and without type identification)" },
	{ ERRTAB_ENTRY(ESLURM_REBOOT_IN_PROGRESS),
	  "Reboot already in progress" },
	{ ERRTAB_ENTRY(ESLURM_MULTI_KNL_CONSTRAINT),
	  "Multiple KNL NUMA and/or MCDRAM constraints require use of a heterogeneous job" },
	{ ERRTAB_ENTRY(ESLURM_UNSUPPORTED_GRES),
	  "Requested GRES option unsupported by configured SelectType plugin" },
	{ ERRTAB_ENTRY(ESLURM_INVALID_NICE),
	  "Invalid --nice value"				},
	{ ERRTAB_ENTRY(ESLURM_INVALID_TIME_MIN_LIMIT),
	  "Invalid time-min specification (exceeds job's time or other limits)"},
	{ ERRTAB_ENTRY(ESLURM_DEFER),
	  "Immediate execution impossible. "
	  "Individual job submission scheduling attempts deferred"},
	{ ERRTAB_ENTRY(ESLURM_CONFIGLESS_DISABLED),
	  "ConfigLess mode is disabled in slurm configuration."	},
	{ ERRTAB_ENTRY(ESLURM_ENVIRONMENT_MISSING),
	  "Environment is missing in job"			},
	{ ERRTAB_ENTRY(ESLURM_RESERVATION_NO_SKIP),
	  "Reservation given is not skipable, try deleting instead"},
	{ ERRTAB_ENTRY(ESLURM_RESERVATION_USER_GROUP),
	  "Reservations can't have users and groups specified, only one or the other"},
	{ ERRTAB_ENTRY(ESLURM_PARTITION_ASSOC),
	  "Multiple partition job request not supported when a partition is set in the association" },
	{ ERRTAB_ENTRY(ESLURM_IN_STANDBY_USE_BACKUP),
	  "Controller is in standby mode, try a different controller"},
	{ ERRTAB_ENTRY(ESLURM_BAD_THREAD_PER_CORE),
	  "Cannot request more threads per core than the job allocation" },
	{ ERRTAB_ENTRY(ESLURM_INVALID_PREFER),
	  "Invalid preferred feature specification"		},
	{ ERRTAB_ENTRY(ESLURM_INSUFFICIENT_GRES),
	  "Insufficient GRES available in allocation"		},
	{ ERRTAB_ENTRY(ESLURM_INVALID_CONTAINER_ID),
	  "Invalid container id specified"			},
	{ ERRTAB_ENTRY(ESLURM_EMPTY_JOB_ID),
	  "JobID must not be an empty string" },
	{ ERRTAB_ENTRY(ESLURM_INVALID_JOB_ID_ZERO),
	  "JobID can not be zero" },
	{ ERRTAB_ENTRY(ESLURM_INVALID_JOB_ID_NEGATIVE),
	  "JobID can not be a negative number" },
	{ ERRTAB_ENTRY(ESLURM_INVALID_JOB_ID_TOO_LARGE),
	  "JobID larger than acceptable range" },
	{ ERRTAB_ENTRY(ESLURM_INVALID_JOB_ID_NON_NUMERIC),
	  "JobID includes unexpected non-numeric characters" },
	{ ERRTAB_ENTRY(ESLURM_EMPTY_JOB_ARRAY_ID),
	  "Job Array ID must not be an empty string" },
	{ ERRTAB_ENTRY(ESLURM_INVALID_JOB_ARRAY_ID_NEGATIVE),
	  "Job Array ID can not be a negative number" },
	{ ERRTAB_ENTRY(ESLURM_INVALID_JOB_ARRAY_ID_TOO_LARGE),
	  "Job Array ID larger than acceptable range" },
	{ ERRTAB_ENTRY(ESLURM_INVALID_JOB_ARRAY_ID_NON_NUMERIC),
	  "HetJob component includes unexpected non-numeric characters" },
	{ ERRTAB_ENTRY(ESLURM_EMPTY_HET_JOB_COMP),
	  "HetJob component must not be an empty string" },
	{ ERRTAB_ENTRY(ESLURM_INVALID_HET_JOB_COMP_NEGATIVE),
	  "HetJob component can not be a negative number" },
	{ ERRTAB_ENTRY(ESLURM_INVALID_HET_JOB_COMP_TOO_LARGE),
	  "HetJob component larger than acceptable range" },
	{ ERRTAB_ENTRY(ESLURM_EMPTY_STEP_ID),
	  "StepID must not be an empty string" },
	{ ERRTAB_ENTRY(ESLURM_INVALID_STEP_ID_NEGATIVE),
	  "StepID can not be a negative number" },
	{ ERRTAB_ENTRY(ESLURM_INVALID_STEP_ID_TOO_LARGE),
	  "StepID larger than acceptable range" },
	{ ERRTAB_ENTRY(ESLURM_INVALID_STEP_ID_NON_NUMERIC),
	  "StepID includes unexpected non-numeric characters" },
	{ ERRTAB_ENTRY(ESLURM_EMPTY_HET_STEP),
	  "HetStep component must not be an empty string" },
	{ ERRTAB_ENTRY(ESLURM_INVALID_HET_STEP_ZERO),
	  "HetStep component can not be zero" },
	{ ERRTAB_ENTRY(ESLURM_INVALID_HET_STEP_NEGATIVE),
	  "HetStep component can not be a negative number" },
	{ ERRTAB_ENTRY(ESLURM_INVALID_HET_STEP_TOO_LARGE),
	  "HetStep component larger than acceptable range" },
	{ ERRTAB_ENTRY(ESLURM_INVALID_HET_STEP_NON_NUMERIC),
	  "HetStep component includes unexpected non-numeric characters" },
	{ ERRTAB_ENTRY(ESLURM_INVALID_HET_STEP_JOB),
	  "HetJob can not Het step id" },
	{ ERRTAB_ENTRY(ESLURM_JOB_TIMEOUT_KILLED),
	  "Job killed due hitting max wall clock limit" },
	{ ERRTAB_ENTRY(ESLURM_JOB_NODE_FAIL_KILLED),
	  "Job killed due node failure" },

	/* SPANK errors */
	{ ERRTAB_ENTRY(ESPANK_ERROR),
	  "Generic error"					},
	{ ERRTAB_ENTRY(ESPANK_BAD_ARG),
	  "Bad argument"					},
	{ ERRTAB_ENTRY(ESPANK_NOT_TASK),
	  "Not in task context"					},
	{ ERRTAB_ENTRY(ESPANK_ENV_EXISTS),
	  "Environment variable exists"				},
	{ ERRTAB_ENTRY(ESPANK_ENV_NOEXIST),
	  "No such environment variable"			},
	{ ERRTAB_ENTRY(ESPANK_NOSPACE),
	  "Buffer too small"					},
	{ ERRTAB_ENTRY(ESPANK_NOT_REMOTE),
	  "Valid only in remote context"			},
	{ ERRTAB_ENTRY(ESPANK_NOEXIST),
	  "Id/PID does not exist on this node"			},
	{ ERRTAB_ENTRY(ESPANK_NOT_EXECD),
	  "Lookup by PID requested, but no tasks running"	},
	{ ERRTAB_ENTRY(ESPANK_NOT_AVAIL),
	  "Item not available from this callback"		},
	{ ERRTAB_ENTRY(ESPANK_NOT_LOCAL),
	  "Valid only in local or allocator context"		},

	/* slurmd error codes */
	{ ERRTAB_ENTRY(ESLURMD_KILL_TASK_FAILED),
	  "Kill task failed"					},
	{ ERRTAB_ENTRY(ESLURMD_INVALID_ACCT_FREQ),
	  "Invalid accounting frequency requested"		},
	{ ERRTAB_ENTRY(ESLURMD_INVALID_JOB_CREDENTIAL),
	  "Invalid job credential"				},
	{ ERRTAB_ENTRY(ESLURMD_CREDENTIAL_REVOKED),
	  "Job credential revoked"                              },
	{ ERRTAB_ENTRY(ESLURMD_CREDENTIAL_EXPIRED),
	  "Job credential expired"                              },
	{ ERRTAB_ENTRY(ESLURMD_CREDENTIAL_REPLAYED),
	  "Job credential replayed"                             },
	{ ERRTAB_ENTRY(ESLURMD_CREATE_BATCH_DIR_ERROR),
	  "Slurmd could not create a batch directory or file"	},
	{ ERRTAB_ENTRY(ESLURMD_SETUP_ENVIRONMENT_ERROR),
	  "Slurmd could not set up environment for batch job"	},
	{ ERRTAB_ENTRY(ESLURMD_SET_UID_OR_GID_ERROR),
	  "Slurmd could not set UID or GID"			},
	{ ERRTAB_ENTRY(ESLURMD_EXECVE_FAILED),
	  "Slurmd could not execve job"				},
	{ ERRTAB_ENTRY(ESLURMD_IO_ERROR),
	  "Slurmd could not connect IO"			        },
	{ ERRTAB_ENTRY(ESLURMD_PROLOG_FAILED),
	  "Job prolog failed"			        	},
	{ ERRTAB_ENTRY(ESLURMD_EPILOG_FAILED),
	  "Job epilog failed"			        	},
	{ ERRTAB_ENTRY(ESLURMD_TOOMANYSTEPS),
	  "Too many job steps on node"		        	},
	{ ERRTAB_ENTRY(ESLURMD_STEP_EXISTS),
	  "Job step already exists"		        	},
	{ ERRTAB_ENTRY(ESLURMD_JOB_NOTRUNNING),
	  "Job step not running"	        	        },
	{ ERRTAB_ENTRY(ESLURMD_STEP_SUSPENDED),
	  "Job step is suspended"                               },
	{ ERRTAB_ENTRY(ESLURMD_STEP_NOTSUSPENDED),
	  "Job step is not currently suspended"                 },
	{ ERRTAB_ENTRY(ESLURMD_INVALID_SOCKET_NAME_LEN),
	  "Unix socket name exceeded maximum length"		},
	{ ERRTAB_ENTRY(ESLURMD_CONTAINER_RUNTIME_INVALID),
	  "Container runtime not configured or invalid"		},
	{ ERRTAB_ENTRY(ESLURMD_CPU_BIND_ERROR),
	  "Unable to satisfy cpu bind request"			},
	{ ERRTAB_ENTRY(ESLURMD_CPU_LAYOUT_ERROR),
	  "Unable to layout tasks on given cpus"		},

	/* socket specific Slurm communications error */

	{ ERRTAB_ENTRY(ESLURM_PROTOCOL_INCOMPLETE_PACKET),
	  "Header lengths are longer than data received"	},
	{ ERRTAB_ENTRY(SLURM_PROTOCOL_SOCKET_IMPL_TIMEOUT),
	  "Socket timed out on send/recv operation"		},
	{ ERRTAB_ENTRY(SLURM_PROTOCOL_SOCKET_ZERO_BYTES_SENT),
	  "Zero Bytes were transmitted or received"		},

	/* slurm_auth errors */

	{ ERRTAB_ENTRY(ESLURM_AUTH_CRED_INVALID),
	  "Invalid authentication credential"			},
	{ ERRTAB_ENTRY(ESLURM_AUTH_BADARG),
	  "Bad argument to plugin function"			},
	{ ERRTAB_ENTRY(ESLURM_AUTH_UNPACK),
	  "Cannot unpack credential"				},
	{ ERRTAB_ENTRY(ESLURM_AUTH_SKIP),
	  "Authentication does not apply to request"		},
	{ ERRTAB_ENTRY(ESLURM_AUTH_UNABLE_TO_GENERATE_TOKEN),
	  "Token Generation failed."				},

	/* accounting errors */
	{ ERRTAB_ENTRY(ESLURM_DB_CONNECTION),
	  "Unable to connect to database"			},
	{ ERRTAB_ENTRY(ESLURM_JOBS_RUNNING_ON_ASSOC),
	  "Job(s) active, cancel job(s) before remove"		},
	{ ERRTAB_ENTRY(ESLURM_CLUSTER_DELETED),
	  "Cluster deleted, commit/rollback immediately"        },
	{ ERRTAB_ENTRY(ESLURM_ONE_CHANGE),
	  "Can only change one at a time"                       },
	{ ERRTAB_ENTRY(ESLURM_BAD_NAME),
	  "Unacceptable name given. (No '.' in name allowed)"   },
	{ ERRTAB_ENTRY(ESLURM_OVER_ALLOCATE),
	  "You can not allocate more than 100% of a resource"	},
	{ ERRTAB_ENTRY(ESLURM_RESULT_TOO_LARGE),
	  "Query result exceeds size limit"			},
	{ ERRTAB_ENTRY(ESLURM_DB_QUERY_TOO_WIDE),
	  "Too wide of a date range in query"			},
	{ ERRTAB_ENTRY(ESLURM_DB_CONNECTION_INVALID),
	  "Database connection reference is invalid"		},
	{ ERRTAB_ENTRY(ESLURM_NO_REMOVE_DEFAULT_ACCOUNT),
	  "You can not remove the default account of a user"	},

	/* Federation Errors */
	{ ERRTAB_ENTRY(ESLURM_FED_CLUSTER_MAX_CNT),
	  "Too many clusters in federation"			},
	{ ERRTAB_ENTRY(ESLURM_FED_CLUSTER_MULTIPLE_ASSIGNMENT),
	  "Clusters can only be assigned to one federation" 	},
	{ ERRTAB_ENTRY(ESLURM_INVALID_CLUSTER_FEATURE),
	  "Invalid cluster feature specification"		},
	{ ERRTAB_ENTRY(ESLURM_JOB_NOT_FEDERATED),
	  "Not a valid federated job"				},
	{ ERRTAB_ENTRY(ESLURM_INVALID_CLUSTER_NAME),
	  "Invalid cluster name"				},
	{ ERRTAB_ENTRY(ESLURM_FED_JOB_LOCK),
	  "Job locked by another sibling"			},
	{ ERRTAB_ENTRY(ESLURM_FED_NO_VALID_CLUSTERS),
	  "No eligible clusters for federated job"		},

	/* plugin and custom errors */
	{ ERRTAB_ENTRY(ESLURM_MISSING_TIME_LIMIT),
	  "Time limit specification required, but not provided"	},
	{ ERRTAB_ENTRY(ESLURM_INVALID_KNL),
	  "Invalid KNL configuration (MCDRAM or NUMA option)"	},
	{ ERRTAB_ENTRY(ESLURM_PLUGIN_INVALID),
	  "Plugin has invalid format or unable to be loaded"	},
	{ ERRTAB_ENTRY(ESLURM_PLUGIN_INCOMPLETE),
	  "Plugin missing required symbol or function"		},
	{ ERRTAB_ENTRY(ESLURM_PLUGIN_NOT_LOADED),
	  "Required plugin type not loaded or initalized"	},

	/* REST errors */
	{ ERRTAB_ENTRY(ESLURM_REST_INVALID_QUERY),
	  "Query empty or not RFC7320 compliant"},
	{ ERRTAB_ENTRY(ESLURM_REST_FAIL_PARSING),
	  "Failure during parsing"},
	{ ERRTAB_ENTRY(ESLURM_REST_INVALID_JOBS_DESC),
	  "Jobs description entry not found, empty or not dictionary or list"},
	{ ERRTAB_ENTRY(ESLURM_REST_EMPTY_RESULT),
	  "Nothing found with query"},
	{ ERRTAB_ENTRY(ESLURM_REST_MISSING_UID),
	  "Missing UNIX user in the system"},
	{ ERRTAB_ENTRY(ESLURM_REST_MISSING_GID),
	  "Missing UNIX group in the system"},

	/* data_t errors */
	{ ERRTAB_ENTRY(ESLURM_DATA_PATH_NOT_FOUND),
	  "Unable to resolve path"},
	{ ERRTAB_ENTRY(ESLURM_DATA_PTR_NULL),
	  "Data pointer is NULL"},
	{ ERRTAB_ENTRY(ESLURM_DATA_CONV_FAILED),
	  "Unable to convert Data type"},
	{ ERRTAB_ENTRY(ESLURM_DATA_REGEX_COMPILE),
	  "Unable to compile regex"},
	{ ERRTAB_ENTRY(ESLURM_DATA_UNKNOWN_MIME_TYPE),
	  "MIME type is unknown to any loaded plugins"},
	{ ERRTAB_ENTRY(ESLURM_DATA_TOO_LARGE),
	  "Data too large to handle"},
	{ ERRTAB_ENTRY(ESLURM_DATA_FLAGS_INVALID_TYPE),
	  "Data parser expects flags to be a list"},
	{ ERRTAB_ENTRY(ESLURM_DATA_FLAGS_INVALID),
	  "Data parser unable to parse invalid flag"},
	{ ERRTAB_ENTRY(ESLURM_DATA_EXPECTED_LIST),
	  "Data parser expected a list"},
	{ ERRTAB_ENTRY(ESLURM_DATA_EXPECTED_DICT),
	  "Data parser expected a dictionary or object"},
	{ ERRTAB_ENTRY(ESLURM_DATA_AMBIGUOUS_MODIFY),
	  "Request matched more than one object to modify. Modifications must only apply to a single object. Try adding more properties to make update match a unique object."},
	{ ERRTAB_ENTRY(ESLURM_DATA_AMBIGUOUS_QUERY),
	  "Request matched more than one object to query. Request is limited to query of a single matching object."},
	{ ERRTAB_ENTRY(ESLURM_DATA_PARSE_NOTHING),
	  "Request to parse empty string rejected"},

	/* container  errors */
	{ ERRTAB_ENTRY(ESLURM_CONTAINER_NOT_CONFIGURED),
	  "Container support is not configured"},
};

unsigned int slurm_errtab_size = sizeof(slurm_errtab) / sizeof(slurm_errtab_t);

/*
 * Linear search through table of errno values and strings,
 * returns NULL on error, string on success.
 */
static char *_lookup_slurm_api_errtab(int errnum)
{
	char *res = NULL;
	int i;

	for (i = 0; i < slurm_errtab_size; i++) {
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
