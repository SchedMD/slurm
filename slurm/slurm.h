/*****************************************************************************\
 *  slurm.h - Definitions for all of the Slurm RPCs
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010-2017 SchedMD LLC <https://www.schedmd.com>.
 *  Portions Copyright (C) 2012-2013 Los Alamos National Security, LLC.
 *  Portions Copyright 2013 Hewlett Packard Enterprise Development LP
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>, et. al.
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

#ifndef _SLURM_H
#define _SLURM_H

/* Number of dimensions the system has */
#define SYSTEM_DIMENSIONS 1
#define HIGHEST_DIMENSIONS 5

#ifdef __cplusplus
extern "C" {
#endif

#include <slurm/slurm_errno.h>
#include <slurm/slurm_version.h>

#include <inttypes.h>		/* for uint16_t, uint32_t definitions */
#include <netinet/in.h>		/* struct sockaddr_in */
#include <stdbool.h>
#include <stdio.h>		/* for FILE definitions */
#include <sys/types.h>		/* for uid_t definition */
#include <time.h>		/* for time_t definitions */
#include <unistd.h>

/* Define slurm_addr_t below to avoid including extraneous slurm headers */
typedef struct sockaddr_storage slurm_addr_t;

#ifndef __slurmdb_cluster_rec_t_defined
#  define __slurmdb_cluster_rec_t_defined
typedef struct slurmdb_cluster_rec slurmdb_cluster_rec_t;
#endif

#ifndef __slurm_cred_t_defined
#  define __slurm_cred_t_defined
typedef struct slurm_job_credential slurm_cred_t;
#endif

/* Define switch_jobinfo_t below to avoid including extraneous slurm headers */
#ifndef __switch_jobinfo_t_defined
#  define  __switch_jobinfo_t_defined
typedef struct switch_jobinfo switch_jobinfo_t;	/* opaque data type */
#endif

/* Define job_resources_t below
 * to avoid including extraneous slurm headers */
#ifndef __job_resources_t_defined
#  define  __job_resources_t_defined	/* Opaque data for select plugins */
typedef struct job_resources job_resources_t;
#endif

/* Define select_jobinfo_t, select_nodeinfo_t below
 * to avoid including extraneous slurm headers */
#ifndef __select_jobinfo_t_defined
#  define  __select_jobinfo_t_defined	/* Opaque data for select plugins */
typedef struct select_jobinfo select_jobinfo_t;  /* for BlueGene */
typedef struct select_nodeinfo select_nodeinfo_t;  /* for BlueGene */
#endif

/* Define jobacctinfo_t below to avoid including extraneous slurm headers */
#ifndef __jobacctinfo_t_defined
#  define  __jobacctinfo_t_defined
typedef struct jobacctinfo jobacctinfo_t;     /* opaque data type */
#endif

/* Define allocation_msg_thread_t below to avoid including extraneous
 * slurm headers */
#ifndef __allocation_msg_thread_t_defined
#  define  __allocation_msg_thread_t_defined
typedef struct allocation_msg_thread allocation_msg_thread_t;
#endif

#ifndef __sbcast_cred_t_defined
#  define  __sbcast_cred_t_defined
typedef struct sbcast_cred sbcast_cred_t;		/* opaque data type */
#endif

/*****************************************************************************\
 *	DEFINITIONS FOR POSIX VALUES
\*****************************************************************************/
#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 64
#endif

/*****************************************************************************\
 *	DEFINITIONS FOR INPUT VALUES
\*****************************************************************************/

/* INFINITE is used to identify unlimited configurations,  */
/* eg. the maximum count of nodes any job may use in some partition */
#define	INFINITE8  (0xff)
#define	INFINITE16 (0xffff)
#define	INFINITE   (0xffffffff)
#define	INFINITE64 (0xffffffffffffffff)
#define NO_VAL8    (0xfe)
#define NO_VAL16   (0xfffe)
#define NO_VAL     (0xfffffffe)
#define NO_VAL64   (0xfffffffffffffffe)
#define NO_CONSUME_VAL64 (0xfffffffffffffffd)
#define MAX_TASKS_PER_NODE 512
#define MAX_JOB_ID (0x03FFFFFF) /* bits 0-25 */
#define MAX_HET_JOB_COMPONENTS 128
#define MAX_FED_CLUSTERS 63

/*
 * Max normal step id leaving a few for special steps like the batch and extern
 * steps
 */
#define SLURM_MAX_NORMAL_STEP_ID (0xfffffff0)
/* Job step ID of pending step */
#define SLURM_PENDING_STEP (0xfffffffd)
/* Job step ID of external process container */
#define SLURM_EXTERN_CONT  (0xfffffffc)
/* Job step ID of batch scripts */
#define SLURM_BATCH_SCRIPT (0xfffffffb)
/* Job step ID for the interactive step (if used) */
#define SLURM_INTERACTIVE_STEP (0xfffffffa)

/* How many seconds to wait after eio_signal_shutdown() is called before
 * terminating the job and abandoning any I/O remaining to be processed.
 */
#define DEFAULT_EIO_SHUTDOWN_WAIT 60

/*
 * SLURM_ID_HASH
 * Description:
 *  Creates a hash of a Slurm JOBID and STEPID
 *  The JOB STEP ID is in the top 32 bits of the hash with the job id occupying
 *  the lower 32 bits.
 *
 * IN  _jobid -- SLURM's JOB ID (uint32_t)
 * IN  _stepid -- SLURM's JOB STEP ID (uint32_t)
 * RET id_hash -- (uint64_t)
 */
#define SLURM_ID_HASH(_jobid, _stepid) \
	(uint64_t)(((uint64_t)_stepid << 32) + _jobid)
#define SLURM_ID_HASH_JOB_ID(hash_id) (uint32_t)(hash_id & 0x00000000FFFFFFFF)
#define SLURM_ID_HASH_STEP_ID(hash_id) (uint32_t)(hash_id >> 32)

/*
 * Convert a hash ID to its legacy (pre-17.11) equivalent
 * Used for backward compatibility for Cray PMI
 */
#define SLURM_ID_HASH_LEGACY(hash_id) \
	((hash_id >> 32) * 10000000000 + (hash_id & 0x00000000FFFFFFFF))

/* Slurm hash definition to be used for various purposes */
typedef struct {
	unsigned char type;
	unsigned char hash[32];
} slurm_hash_t;

/*
 * Bit definitions when setting flags
 *
 * SLURM_BIT(0)  0x0000000000000001
 * SLURM_BIT(1)  0x0000000000000002
 * SLURM_BIT(2)  0x0000000000000004
 * SLURM_BIT(3)  0x0000000000000008
 * SLURM_BIT(4)  0x0000000000000010
 * SLURM_BIT(5)  0x0000000000000020
 * SLURM_BIT(6)  0x0000000000000040
 * SLURM_BIT(7)  0x0000000000000080
 * SLURM_BIT(8)  0x0000000000000100
 * SLURM_BIT(9)  0x0000000000000200
 * SLURM_BIT(10) 0x0000000000000400
 * SLURM_BIT(11) 0x0000000000000800
 * SLURM_BIT(12) 0x0000000000001000
 * SLURM_BIT(13) 0x0000000000002000
 * SLURM_BIT(14) 0x0000000000004000
 * SLURM_BIT(15) 0x0000000000008000
 * SLURM_BIT(16) 0x0000000000010000
 * SLURM_BIT(17) 0x0000000000020000
 * SLURM_BIT(18) 0x0000000000040000
 * SLURM_BIT(19) 0x0000000000080000
 * SLURM_BIT(20) 0x0000000000100000
 * SLURM_BIT(21) 0x0000000000200000
 * SLURM_BIT(22) 0x0000000000400000
 * SLURM_BIT(23) 0x0000000000800000
 * SLURM_BIT(24) 0x0000000001000000
 * SLURM_BIT(25) 0x0000000002000000
 * SLURM_BIT(26) 0x0000000004000000
 * SLURM_BIT(27) 0x0000000008000000
 * SLURM_BIT(28) 0x0000000010000000
 * SLURM_BIT(29) 0x0000000020000000
 * SLURM_BIT(30) 0x0000000040000000
 * SLURM_BIT(31) 0x0000000080000000
 * SLURM_BIT(32) 0x0000000100000000
 * SLURM_BIT(33) 0x0000000200000000
 * SLURM_BIT(34) 0x0000000400000000
 * SLURM_BIT(35) 0x0000000800000000
 * SLURM_BIT(36) 0x0000001000000000
 * SLURM_BIT(37) 0x0000002000000000
 * SLURM_BIT(38) 0x0000004000000000
 * SLURM_BIT(39) 0x0000008000000000
 * SLURM_BIT(40) 0x0000010000000000
 * SLURM_BIT(41) 0x0000020000000000
 * SLURM_BIT(42) 0x0000040000000000
 * SLURM_BIT(43) 0x0000080000000000
 * SLURM_BIT(44) 0x0000100000000000
 * SLURM_BIT(45) 0x0000200000000000
 * SLURM_BIT(46) 0x0000400000000000
 * SLURM_BIT(47) 0x0000800000000000
 * SLURM_BIT(48) 0x0001000000000000
 * SLURM_BIT(49) 0x0002000000000000
 * SLURM_BIT(50) 0x0004000000000000
 * SLURM_BIT(51) 0x0008000000000000
 * SLURM_BIT(52) 0x0010000000000000
 * SLURM_BIT(53) 0x0020000000000000
 * SLURM_BIT(54) 0x0040000000000000
 * SLURM_BIT(55) 0x0080000000000000
 * SLURM_BIT(56) 0x0100000000000000
 * SLURM_BIT(57) 0x0200000000000000
 * SLURM_BIT(58) 0x0400000000000000
 * SLURM_BIT(59) 0x0800000000000000
 * SLURM_BIT(60) 0x1000000000000000
 * SLURM_BIT(61) 0x2000000000000000
 * SLURM_BIT(62) 0x4000000000000000
 * SLURM_BIT(63) 0x8000000000000000
 */

#define SLURM_BIT(offset) ((uint64_t)1 << offset)

/* last entry must be JOB_END, keep in sync with job_state_string and
 *	job_state_string_compact. values may be ORed with JOB_STATE_FLAGS
 *	below.  */
enum job_states {
	JOB_PENDING,		/* queued waiting for initiation */
	JOB_RUNNING,		/* allocated resources and executing */
	JOB_SUSPENDED,		/* allocated resources, execution suspended */
	JOB_COMPLETE,		/* completed execution successfully */
	JOB_CANCELLED,		/* cancelled by user */
	JOB_FAILED,		/* completed execution unsuccessfully */
	JOB_TIMEOUT,		/* terminated on reaching time limit */
	JOB_NODE_FAIL,		/* terminated on node failure */
	JOB_PREEMPTED,		/* terminated due to preemption */
	JOB_BOOT_FAIL,		/* terminated due to node boot failure */
	JOB_DEADLINE,		/* terminated on deadline */
	JOB_OOM,		/* experienced out of memory error */
	JOB_END			/* not a real state, last entry in table */
};
#define	JOB_STATE_BASE	  0x000000ff	/* Used for job_states above */
#define	JOB_STATE_FLAGS	  0xffffff00	/* Used for state flags below */

/* SLURM_BIT(0-7) are already taken with base job_states above */
#define JOB_LAUNCH_FAILED SLURM_BIT(8)
#define JOB_UPDATE_DB     SLURM_BIT(9)  /* Send job start to database again */
#define JOB_REQUEUE       SLURM_BIT(10) /* Requeue job in completing state */
#define JOB_REQUEUE_HOLD  SLURM_BIT(11) /* Requeue any job in hold */
#define JOB_SPECIAL_EXIT  SLURM_BIT(12) /* Requeue an exit job in hold */
#define	JOB_RESIZING	  SLURM_BIT(13) /* Size of job about to change, flag set
					 * before calling accounting functions
					 * immediately before job changes size
					 */
#define	JOB_CONFIGURING	  SLURM_BIT(14) /* Allocated nodes booting */
#define	JOB_COMPLETING	  SLURM_BIT(15) /* Waiting for epilog completion */
#define JOB_STOPPED       SLURM_BIT(16) /* Job is stopped state (holding
					   resources, but sent SIGSTOP */
#define JOB_RECONFIG_FAIL SLURM_BIT(17) /* Node configuration for job failed,
					   not job state, just job requeue
					   flag */
#define	JOB_POWER_UP_NODE SLURM_BIT(18) /* Allocated powered down nodes,
					 * waiting for reboot */
#define JOB_REVOKED       SLURM_BIT(19) /* Sibling job revoked */
#define JOB_REQUEUE_FED   SLURM_BIT(20) /* Job being requeued by federation */
#define JOB_RESV_DEL_HOLD SLURM_BIT(21) /* Job is hold */
#define JOB_SIGNALING     SLURM_BIT(22) /* Outgoing signal is pending */
#define JOB_STAGE_OUT     SLURM_BIT(23) /* Staging out data (burst buffer) */

#define READY_JOB_FATAL	   -2	/* fatal error */
#define READY_JOB_ERROR    -1	/* ordinary error */

#define READY_NODE_STATE   SLURM_BIT(0) /* job's node's are ready */
#define READY_JOB_STATE    SLURM_BIT(1)	/* job is ready to execute */
#define READY_PROLOG_STATE SLURM_BIT(2)	/* PrologSlurmctld is done */

#define MAIL_JOB_BEGIN      SLURM_BIT(0)  /* Notify when job begins */
#define MAIL_JOB_END        SLURM_BIT(1)  /* Notify when job ends */
#define MAIL_JOB_FAIL       SLURM_BIT(2)  /* Notify if job fails */
#define MAIL_JOB_REQUEUE    SLURM_BIT(3)  /* Notify if job requeued */
#define MAIL_JOB_TIME100    SLURM_BIT(4)  /* Notify on reaching 100% of time
					   * limit */
#define MAIL_JOB_TIME90     SLURM_BIT(5)  /* Notify on reaching 90% of time
					   * limit */
#define MAIL_JOB_TIME80     SLURM_BIT(6)  /* Notify on reaching 80% of time
					   * limit */
#define MAIL_JOB_TIME50     SLURM_BIT(7)  /* Notify on reaching 50% of time
					   * limit */
#define MAIL_JOB_STAGE_OUT  SLURM_BIT(8)  /* Notify on completion of burst
					   * buffer stage out */
#define MAIL_ARRAY_TASKS    SLURM_BIT(9)  /* Send emails for each array task */
#define MAIL_INVALID_DEPEND SLURM_BIT(10) /* Notify on job invalid dependency */

/*
 * job_array_struct_t array_flags definitions. ARRAY_TASK_REQUEUED could be
 * substituted in the future to tot_requeued_tasks member in the struct, which
 * would provide a more accurated array statistic.
 */
#define ARRAY_TASK_REQUEUED 0x0001	/* At least one task was requeued. */

#define NICE_OFFSET 0x80000000		/* offset for job's nice value */

/* Reason for job to be pending rather than executing or reason for job
 * failure. If multiple reasons exists, only one is given for the sake of
 * system efficiency */
enum job_state_reason {
/* Reasons for job to be pending */
	WAIT_NO_REASON = 0,	/* not set or job not pending */
	WAIT_PRIORITY,		/* higher priority jobs exist */
	WAIT_DEPENDENCY,	/* dependent job has not completed */
	WAIT_RESOURCES,		/* required resources not available */
	WAIT_PART_NODE_LIMIT,	/* request exceeds partition node limit */
	WAIT_PART_TIME_LIMIT,	/* request exceeds partition time limit */
	WAIT_PART_DOWN,		/* requested partition is down */
	WAIT_PART_INACTIVE,	/* requested partition is inactive */
	WAIT_HELD,		/* job is held by administrator */
	WAIT_TIME,		/* job waiting for specific begin time */
	WAIT_LICENSES,		/* job is waiting for licenses */
	WAIT_ASSOC_JOB_LIMIT,	/* user/bank job limit reached */
	WAIT_ASSOC_RESOURCE_LIMIT,/* user/bank resource limit reached */
	WAIT_ASSOC_TIME_LIMIT,  /* user/bank time limit reached */
	WAIT_RESERVATION,	/* reservation not available */
	WAIT_NODE_NOT_AVAIL,	/* required node is DOWN or DRAINED */
	WAIT_HELD_USER,		/* job is held by user */
	WAIT_FRONT_END,		/* Front end nodes are DOWN */
	FAIL_DEFER,		/* individual submit time sched deferred */
	FAIL_DOWN_PARTITION,	/* partition for job is DOWN */
	FAIL_DOWN_NODE,		/* some node in the allocation failed */
	FAIL_BAD_CONSTRAINTS,	/* constraints can not be satisfied */
	FAIL_SYSTEM,		/* slurm system failure */
	FAIL_LAUNCH,		/* unable to launch job */
	FAIL_EXIT_CODE,		/* exit code was non-zero */
	FAIL_TIMEOUT,		/* reached end of time limit */
	FAIL_INACTIVE_LIMIT,	/* reached slurm InactiveLimit */
	FAIL_ACCOUNT,		/* invalid account */
	FAIL_QOS,	 	/* invalid QOS */
	WAIT_QOS_THRES,	       	/* required QOS threshold has been breached */
	WAIT_QOS_JOB_LIMIT,	/* QOS job limit reached */
	WAIT_QOS_RESOURCE_LIMIT,/* QOS resource limit reached */
	WAIT_QOS_TIME_LIMIT,	/* QOS time limit reached */
	FAIL_SIGNAL,		/* raised a signal that caused it to exit */
	DEFUNCT_WAIT_34, /* free for reuse */
	WAIT_CLEANING,          /* If a job is requeued and it is
				 * still cleaning up from the last run. */
	WAIT_PROLOG,		/* Prolog is running */
	WAIT_QOS,	 	/* QOS not allowed */
	WAIT_ACCOUNT,	 	/* Account not allowed */
	WAIT_DEP_INVALID,        /* Dependency condition invalid or never
				  * satisfied
				  */
	WAIT_QOS_GRP_CPU,            /* QOS GrpTRES exceeded (CPU) */
	WAIT_QOS_GRP_CPU_MIN,        /* QOS GrpTRESMins exceeded (CPU) */
	WAIT_QOS_GRP_CPU_RUN_MIN,    /* QOS GrpTRESRunMins exceeded (CPU) */
	WAIT_QOS_GRP_JOB,            /* QOS GrpJobs exceeded */
	WAIT_QOS_GRP_MEM,            /* QOS GrpTRES exceeded (Memory) */
	WAIT_QOS_GRP_NODE,           /* QOS GrpTRES exceeded (Node) */
	WAIT_QOS_GRP_SUB_JOB,        /* QOS GrpSubmitJobs exceeded */
	WAIT_QOS_GRP_WALL,           /* QOS GrpWall exceeded */
	WAIT_QOS_MAX_CPU_PER_JOB,    /* QOS MaxTRESPerJob exceeded (CPU) */
	WAIT_QOS_MAX_CPU_MINS_PER_JOB,/* QOS MaxTRESMinsPerJob exceeded (CPU) */
	WAIT_QOS_MAX_NODE_PER_JOB,   /* QOS MaxTRESPerJob exceeded (Node) */
	WAIT_QOS_MAX_WALL_PER_JOB,   /* QOS MaxWallDurationPerJob exceeded */
	WAIT_QOS_MAX_CPU_PER_USER,   /* QOS MaxTRESPerUser exceeded (CPU) */
	WAIT_QOS_MAX_JOB_PER_USER,   /* QOS MaxJobsPerUser exceeded */
	WAIT_QOS_MAX_NODE_PER_USER,  /* QOS MaxTRESPerUser exceeded (Node) */
	WAIT_QOS_MAX_SUB_JOB,        /* QOS MaxSubmitJobsPerUser exceeded */
	WAIT_QOS_MIN_CPU,            /* QOS MinTRESPerJob not reached (CPU) */
	WAIT_ASSOC_GRP_CPU,          /* ASSOC GrpTRES exceeded (CPU) */
	WAIT_ASSOC_GRP_CPU_MIN,      /* ASSOC GrpTRESMins exceeded (CPU) */
	WAIT_ASSOC_GRP_CPU_RUN_MIN,  /* ASSOC GrpTRESRunMins exceeded (CPU) */
	WAIT_ASSOC_GRP_JOB,          /* ASSOC GrpJobs exceeded */
	WAIT_ASSOC_GRP_MEM,          /* ASSOC GrpTRES exceeded (Memory) */
	WAIT_ASSOC_GRP_NODE,         /* ASSOC GrpTRES exceeded (Node) */
	WAIT_ASSOC_GRP_SUB_JOB,      /* ASSOC GrpSubmitJobs exceeded */
	WAIT_ASSOC_GRP_WALL,         /* ASSOC GrpWall exceeded */
	WAIT_ASSOC_MAX_JOBS,         /* ASSOC MaxJobs exceeded */
	WAIT_ASSOC_MAX_CPU_PER_JOB,  /* ASSOC MaxTRESPerJob exceeded (CPU) */
	WAIT_ASSOC_MAX_CPU_MINS_PER_JOB,/* ASSOC MaxTRESMinsPerJob
					 * exceeded (CPU) */
	WAIT_ASSOC_MAX_NODE_PER_JOB, /* ASSOC MaxTRESPerJob exceeded (NODE) */
	WAIT_ASSOC_MAX_WALL_PER_JOB, /* ASSOC MaxWallDurationPerJob
				      * exceeded */
	WAIT_ASSOC_MAX_SUB_JOB,      /* ASSOC MaxSubmitJobsPerUser exceeded */

	WAIT_MAX_REQUEUE,            /* MAX_BATCH_REQUEUE reached */
	WAIT_ARRAY_TASK_LIMIT,       /* job array running task limit */
	WAIT_BURST_BUFFER_RESOURCE,  /* Burst buffer resources */
	WAIT_BURST_BUFFER_STAGING,   /* Burst buffer file stage-in */
	FAIL_BURST_BUFFER_OP,	     /* Burst buffer operation failure */
	WAIT_POWER_NOT_AVAIL,        /* not enough power available */
	WAIT_POWER_RESERVED,         /* job is waiting for available power
				      * because of power reservations */
	WAIT_ASSOC_GRP_UNK,          /* ASSOC GrpTRES exceeded
				      * (Unknown) */
	WAIT_ASSOC_GRP_UNK_MIN,      /* ASSOC GrpTRESMins exceeded
				      * (Unknown) */
	WAIT_ASSOC_GRP_UNK_RUN_MIN,  /* ASSOC GrpTRESRunMins exceeded
				      * (Unknown) */
	WAIT_ASSOC_MAX_UNK_PER_JOB,  /* ASSOC MaxTRESPerJob exceeded
				      * (Unknown) */
	WAIT_ASSOC_MAX_UNK_PER_NODE,  /* ASSOC MaxTRESPerNode exceeded
				       * (Unknown) */
	WAIT_ASSOC_MAX_UNK_MINS_PER_JOB,/* ASSOC MaxTRESMinsPerJob
					 * exceeded (Unknown) */
	WAIT_ASSOC_MAX_CPU_PER_NODE,  /* ASSOC MaxTRESPerNode exceeded (CPU) */
	WAIT_ASSOC_GRP_MEM_MIN,      /* ASSOC GrpTRESMins exceeded
				      * (Memory) */
	WAIT_ASSOC_GRP_MEM_RUN_MIN,  /* ASSOC GrpTRESRunMins exceeded
				      * (Memory) */
	WAIT_ASSOC_MAX_MEM_PER_JOB,  /* ASSOC MaxTRESPerJob exceeded (Memory) */
	WAIT_ASSOC_MAX_MEM_PER_NODE,  /* ASSOC MaxTRESPerNode exceeded (CPU) */
	WAIT_ASSOC_MAX_MEM_MINS_PER_JOB,/* ASSOC MaxTRESMinsPerJob
					 * exceeded (Memory) */
	WAIT_ASSOC_GRP_NODE_MIN,     /* ASSOC GrpTRESMins exceeded (Node) */
	WAIT_ASSOC_GRP_NODE_RUN_MIN, /* ASSOC GrpTRESRunMins exceeded (Node) */
	WAIT_ASSOC_MAX_NODE_MINS_PER_JOB,/* ASSOC MaxTRESMinsPerJob
					  * exceeded (Node) */
	WAIT_ASSOC_GRP_ENERGY,           /* ASSOC GrpTRES exceeded
					  * (Energy) */
	WAIT_ASSOC_GRP_ENERGY_MIN,       /* ASSOC GrpTRESMins exceeded
					  * (Energy) */
	WAIT_ASSOC_GRP_ENERGY_RUN_MIN,   /* ASSOC GrpTRESRunMins exceeded
					  * (Energy) */
	WAIT_ASSOC_MAX_ENERGY_PER_JOB,   /* ASSOC MaxTRESPerJob exceeded
					  * (Energy) */
	WAIT_ASSOC_MAX_ENERGY_PER_NODE,  /* ASSOC MaxTRESPerNode
					  * exceeded (Energy) */
	WAIT_ASSOC_MAX_ENERGY_MINS_PER_JOB,/* ASSOC MaxTRESMinsPerJob
					    * exceeded (Energy) */
	WAIT_ASSOC_GRP_GRES,          /* ASSOC GrpTRES exceeded (GRES) */
	WAIT_ASSOC_GRP_GRES_MIN,      /* ASSOC GrpTRESMins exceeded (GRES) */
	WAIT_ASSOC_GRP_GRES_RUN_MIN,  /* ASSOC GrpTRESRunMins exceeded (GRES) */
	WAIT_ASSOC_MAX_GRES_PER_JOB,  /* ASSOC MaxTRESPerJob exceeded (GRES) */
	WAIT_ASSOC_MAX_GRES_PER_NODE, /* ASSOC MaxTRESPerNode exceeded (GRES) */
	WAIT_ASSOC_MAX_GRES_MINS_PER_JOB,/* ASSOC MaxTRESMinsPerJob
					  * exceeded (GRES) */
	WAIT_ASSOC_GRP_LIC,          /* ASSOC GrpTRES exceeded
				      * (license) */
	WAIT_ASSOC_GRP_LIC_MIN,      /* ASSOC GrpTRESMins exceeded
				      * (license) */
	WAIT_ASSOC_GRP_LIC_RUN_MIN,  /* ASSOC GrpTRESRunMins exceeded
				      * (license) */
	WAIT_ASSOC_MAX_LIC_PER_JOB,  /* ASSOC MaxTRESPerJob exceeded
				      * (license) */
	WAIT_ASSOC_MAX_LIC_MINS_PER_JOB,/* ASSOC MaxTRESMinsPerJob exceeded
					 * (license) */
	WAIT_ASSOC_GRP_BB,          /* ASSOC GrpTRES exceeded
				     * (burst buffer) */
	WAIT_ASSOC_GRP_BB_MIN,      /* ASSOC GrpTRESMins exceeded
				     * (burst buffer) */
	WAIT_ASSOC_GRP_BB_RUN_MIN,  /* ASSOC GrpTRESRunMins exceeded
				     * (burst buffer) */
	WAIT_ASSOC_MAX_BB_PER_JOB,  /* ASSOC MaxTRESPerJob exceeded
				     * (burst buffer) */
	WAIT_ASSOC_MAX_BB_PER_NODE, /* ASSOC MaxTRESPerNode exceeded
				     * (burst buffer) */
	WAIT_ASSOC_MAX_BB_MINS_PER_JOB,/* ASSOC MaxTRESMinsPerJob exceeded
					* (burst buffer) */
	WAIT_QOS_GRP_UNK,           /* QOS GrpTRES exceeded (Unknown) */
	WAIT_QOS_GRP_UNK_MIN,       /* QOS GrpTRESMins exceeded (Unknown) */
	WAIT_QOS_GRP_UNK_RUN_MIN,   /* QOS GrpTRESRunMins exceeded (Unknown) */
	WAIT_QOS_MAX_UNK_PER_JOB,   /* QOS MaxTRESPerJob exceeded (Unknown) */
	WAIT_QOS_MAX_UNK_PER_NODE,  /* QOS MaxTRESPerNode exceeded (Unknown) */
	WAIT_QOS_MAX_UNK_PER_USER,  /* QOS MaxTRESPerUser exceeded (Unknown) */
	WAIT_QOS_MAX_UNK_MINS_PER_JOB,/* QOS MaxTRESMinsPerJob
				       * exceeded (Unknown) */
	WAIT_QOS_MIN_UNK,           /* QOS MinTRESPerJob exceeded (Unknown) */
	WAIT_QOS_MAX_CPU_PER_NODE,  /* QOS MaxTRESPerNode exceeded (CPU) */
	WAIT_QOS_GRP_MEM_MIN,       /* QOS GrpTRESMins exceeded
				     * (Memory) */
	WAIT_QOS_GRP_MEM_RUN_MIN,   /* QOS GrpTRESRunMins exceeded
				     * (Memory) */
	WAIT_QOS_MAX_MEM_MINS_PER_JOB,/* QOS MaxTRESMinsPerJob
				       * exceeded (Memory) */
	WAIT_QOS_MAX_MEM_PER_JOB,   /* QOS MaxTRESPerJob exceeded (CPU) */
	WAIT_QOS_MAX_MEM_PER_NODE,  /* QOS MaxTRESPerNode exceeded (MEM) */
	WAIT_QOS_MAX_MEM_PER_USER,  /* QOS MaxTRESPerUser exceeded (CPU) */
	WAIT_QOS_MIN_MEM,           /* QOS MinTRESPerJob not reached (Memory) */
	WAIT_QOS_GRP_ENERGY,        /* QOS GrpTRES exceeded (Energy) */
	WAIT_QOS_GRP_ENERGY_MIN,    /* QOS GrpTRESMins exceeded (Energy) */
	WAIT_QOS_GRP_ENERGY_RUN_MIN, /* QOS GrpTRESRunMins exceeded (Energy) */
	WAIT_QOS_MAX_ENERGY_PER_JOB, /* QOS MaxTRESPerJob exceeded (Energy) */
	WAIT_QOS_MAX_ENERGY_PER_NODE,/* QOS MaxTRESPerNode exceeded (Energy) */
	WAIT_QOS_MAX_ENERGY_PER_USER,/* QOS MaxTRESPerUser exceeded (Energy) */
	WAIT_QOS_MAX_ENERGY_MINS_PER_JOB,/* QOS MaxTRESMinsPerJob
					  * exceeded (Energy) */
	WAIT_QOS_MIN_ENERGY,        /* QOS MinTRESPerJob not reached (Energy) */
	WAIT_QOS_GRP_NODE_MIN,     /* QOS GrpTRESMins exceeded (Node) */
	WAIT_QOS_GRP_NODE_RUN_MIN, /* QOS GrpTRESRunMins exceeded (Node) */
	WAIT_QOS_MAX_NODE_MINS_PER_JOB,  /* QOS MaxTRESMinsPerJob
					  * exceeded (Node) */
	WAIT_QOS_MIN_NODE,          /* QOS MinTRESPerJob not reached (Node) */
	WAIT_QOS_GRP_GRES,          /* QOS GrpTRES exceeded (GRES) */
	WAIT_QOS_GRP_GRES_MIN,      /* QOS GrpTRESMins exceeded (GRES) */
	WAIT_QOS_GRP_GRES_RUN_MIN,  /* QOS GrpTRESRunMins exceeded (GRES) */
	WAIT_QOS_MAX_GRES_PER_JOB,  /* QOS MaxTRESPerJob exceeded (GRES) */
	WAIT_QOS_MAX_GRES_PER_NODE, /* QOS MaxTRESPerNode exceeded (GRES) */
	WAIT_QOS_MAX_GRES_PER_USER, /* QOS MaxTRESPerUser exceeded
				     * (GRES) */
	WAIT_QOS_MAX_GRES_MINS_PER_JOB,/* QOS MaxTRESMinsPerJob
					* exceeded (GRES) */
	WAIT_QOS_MIN_GRES,          /* QOS MinTRESPerJob not reached (CPU) */
	WAIT_QOS_GRP_LIC,           /* QOS GrpTRES exceeded (license) */
	WAIT_QOS_GRP_LIC_MIN,       /* QOS GrpTRESMins exceeded (license) */
	WAIT_QOS_GRP_LIC_RUN_MIN,   /* QOS GrpTRESRunMins exceeded (license) */
	WAIT_QOS_MAX_LIC_PER_JOB,   /* QOS MaxTRESPerJob exceeded (license) */
	WAIT_QOS_MAX_LIC_PER_USER,  /* QOS MaxTRESPerUser exceeded (license) */
	WAIT_QOS_MAX_LIC_MINS_PER_JOB,/* QOS MaxTRESMinsPerJob exceeded
				       * (license) */
	WAIT_QOS_MIN_LIC,           /* QOS MinTRESPerJob not reached
				     * (license) */
	WAIT_QOS_GRP_BB,            /* QOS GrpTRES exceeded
				     * (burst buffer) */
	WAIT_QOS_GRP_BB_MIN,        /* QOS GrpTRESMins exceeded
				     * (burst buffer) */
	WAIT_QOS_GRP_BB_RUN_MIN,    /* QOS GrpTRESRunMins exceeded
				     * (burst buffer) */
	WAIT_QOS_MAX_BB_PER_JOB,   /* QOS MaxTRESPerJob exceeded
				    * (burst buffer) */
	WAIT_QOS_MAX_BB_PER_NODE,  /* QOS MaxTRESPerNode exceeded
				    * (burst buffer) */
	WAIT_QOS_MAX_BB_PER_USER,  /* QOS MaxTRESPerUser exceeded
				    * (burst buffer) */
	WAIT_QOS_MAX_BB_MINS_PER_JOB,/* QOS MaxTRESMinsPerJob exceeded
				      * (burst buffer) */
	WAIT_QOS_MIN_BB,           /* QOS MinTRESPerJob not reached
				    * (burst buffer) */
	FAIL_DEADLINE,              /* reached deadline */
	/* QOS MaxTRESPerAccount */
	WAIT_QOS_MAX_BB_PER_ACCT,     /* exceeded burst buffer */
	WAIT_QOS_MAX_CPU_PER_ACCT,    /* exceeded CPUs */
	WAIT_QOS_MAX_ENERGY_PER_ACCT, /* exceeded Energy */
	WAIT_QOS_MAX_GRES_PER_ACCT,   /* exceeded GRES */
	WAIT_QOS_MAX_NODE_PER_ACCT,   /* exceeded Nodes */
	WAIT_QOS_MAX_LIC_PER_ACCT,    /* exceeded Licenses */
	WAIT_QOS_MAX_MEM_PER_ACCT,    /* exceeded Memory */
	WAIT_QOS_MAX_UNK_PER_ACCT,    /* exceeded Unknown */
	/********************/
	WAIT_QOS_MAX_JOB_PER_ACCT,    /* QOS MaxJobPerAccount exceeded */
	WAIT_QOS_MAX_SUB_JOB_PER_ACCT,/* QOS MaxJobSubmitSPerAccount exceeded */
	WAIT_PART_CONFIG,	      /* Generic partition configuration reason */
	WAIT_ACCOUNT_POLICY,          /* Generic accounting policy reason */

	WAIT_FED_JOB_LOCK,            /* Can't get fed job lock */
	FAIL_OOM,		      /* Exhausted memory */
	WAIT_PN_MEM_LIMIT,	      /* MaxMemPer[CPU|Node] exceeded */

	/* exceeded Billing TRES limits */
	WAIT_ASSOC_GRP_BILLING,             /* GrpTRES           */
	WAIT_ASSOC_GRP_BILLING_MIN,         /* GrpTRESMins       */
	WAIT_ASSOC_GRP_BILLING_RUN_MIN,     /* GrpTRESRunMins    */
	WAIT_ASSOC_MAX_BILLING_PER_JOB,     /* MaxTRESPerJob     */
	WAIT_ASSOC_MAX_BILLING_PER_NODE,    /* MaxTRESPerNode    */
	WAIT_ASSOC_MAX_BILLING_MINS_PER_JOB,/* MaxTRESMinsPerJob */

	WAIT_QOS_GRP_BILLING,               /* GrpTRES           */
	WAIT_QOS_GRP_BILLING_MIN,           /* GrpTRESMins       */
	WAIT_QOS_GRP_BILLING_RUN_MIN,       /* GrpTRESRunMins    */
	WAIT_QOS_MAX_BILLING_PER_JOB,       /* MaxTRESPerJob     */
	WAIT_QOS_MAX_BILLING_PER_NODE,      /* MaxTRESPerNode    */
	WAIT_QOS_MAX_BILLING_PER_USER,      /* MaxTRESPerUser    */
	WAIT_QOS_MAX_BILLING_MINS_PER_JOB,  /* MaxTRESMinsPerJob */
	WAIT_QOS_MAX_BILLING_PER_ACCT,      /* MaxTRESPerAcct    */
	WAIT_QOS_MIN_BILLING,               /* MinTRESPerJob     */

	WAIT_RESV_DELETED	      /* Reservation was deleted */
};

enum job_acct_types {
	JOB_START,
	JOB_STEP,
	JOB_SUSPEND,
	JOB_TERMINATED
};

/* Partition state flags */
#define PARTITION_SUBMIT	0x01	/* Allow job submission to partition */
#define PARTITION_SCHED 	0x02	/* Allow job startup from partition */

/* Actual partition states based upon state flags */
#define PARTITION_DOWN		(PARTITION_SUBMIT)
#define PARTITION_UP		(PARTITION_SUBMIT | PARTITION_SCHED)
#define PARTITION_DRAIN		(PARTITION_SCHED)
#define PARTITION_INACTIVE	0x00

/* Partition enforce flags for jobs */
#define PARTITION_ENFORCE_NONE 0
#define PARTITION_ENFORCE_ALL  1 /* job limit must be valid for ALL
				  * partitions */
#define PARTITION_ENFORCE_ANY  2 /* job limit must be valid for ANY
				  * partition */

/*
 * Auth plugin (id) used for communication.
 * Update auth_plugin_types in slurm_auth.c if changed.
 */
enum auth_plugin_type {
	AUTH_PLUGIN_NONE	= 100,
	AUTH_PLUGIN_MUNGE	= 101,
	AUTH_PLUGIN_JWT		= 102,
};

/*
 * Hash plugin (id) used for communication.
 */
enum hash_plugin_type {
	HASH_PLUGIN_DEFAULT = 0,
	HASH_PLUGIN_NONE,
	HASH_PLUGIN_K12,
	HASH_PLUGIN_SHA256,
	HASH_PLUGIN_CNT,
};

/* Select plugin (id) in use by cluster */
enum select_plugin_type {
	/* 100 unused (originally for BlueGene) */
	SELECT_PLUGIN_CONS_RES       = 101, /* Cons Res on a normal system */
	SELECT_PLUGIN_LINEAR         = 102, /* Linear on a normal system */
	/* 103 unused (originally used for BGQ) */
	/* 104 unused (originally used for Cray/ALPS with select/linear) */
	/* 105 unused (originally used for Cray/ALPS with select/cons_res) */
	SELECT_PLUGIN_SERIAL         = 106, /* Serial */
	SELECT_PLUGIN_CRAY_LINEAR    = 107, /* Linear on a Native Cray */
	SELECT_PLUGIN_CRAY_CONS_RES  = 108, /* Cons Res on a Native Cray */
	SELECT_PLUGIN_CONS_TRES      = 109, /* Cons TRES on a normal system */
	SELECT_PLUGIN_CRAY_CONS_TRES = 110  /* Cons TRES on a Native Cray */
};

/* switch plugin (id) in use by cluster */
enum switch_plugin_type {
	SWITCH_PLUGIN_NONE         = 100, /* NONE */
	SWITCH_PLUGIN_GENERIC      = 101, /* Generic */
	SWITCH_PLUGIN_CRAY         = 102, /* Cray */
	/* 103 unused (originally used for NRT) */
	SWITCH_PLUGIN_SLINGSHOT    = 104, /* HPE Slingshot */
};

enum select_jobdata_type {
	SELECT_JOBDATA_NETWORK	= 23,	/* data-> char * network info */
};

enum select_nodedata_type {
	SELECT_NODEDATA_SUBCNT = 2,		/* data-> uint16_t */
	SELECT_NODEDATA_PTR = 5,		/* data-> select_nodeinfo_t *nodeinfo */
	SELECT_NODEDATA_MEM_ALLOC = 8,		/* data-> uint32_t */
	SELECT_NODEDATA_TRES_ALLOC_FMT_STR = 9,	/* data-> char *,
						 * free with xfree */
	SELECT_NODEDATA_TRES_ALLOC_WEIGHTED = 10, /* data-> double */
};

enum select_print_mode {
	SELECT_PRINT_HEAD,	/* Print just the header */
	SELECT_PRINT_DATA,	/* Print just the data */
	SELECT_PRINT_MIXED,	/* Print "field=value" */
	SELECT_PRINT_MIXED_SHORT,/* Print less "field=value" */
	SELECT_PRINT_BG_ID,	/* Print just the BG_ID */
	SELECT_PRINT_NODES,	/* Print the nodelist */
	SELECT_PRINT_CONNECTION,/* Print just the CONNECTION type */
	SELECT_PRINT_ROTATE,    /* Print just the ROTATE */
	SELECT_PRINT_GEOMETRY,	/* Print just the GEO */
	SELECT_PRINT_START,	/* Print just the START location */
	SELECT_PRINT_BLRTS_IMAGE,/* Print just the BLRTS IMAGE */
	SELECT_PRINT_LINUX_IMAGE,/* Print just the LINUX IMAGE */
	SELECT_PRINT_MLOADER_IMAGE,/* Print just the MLOADER IMAGE */
	SELECT_PRINT_RAMDISK_IMAGE,/* Print just the RAMDISK IMAGE */
	SELECT_PRINT_REBOOT,	/* Print just the REBOOT */
	SELECT_PRINT_RESV_ID,	/* Print just Cray/BASIL reservation ID */
	SELECT_PRINT_START_LOC	/* Print just the start location */
};

enum select_node_cnt {
	SELECT_GET_NODE_SCALING,      /* Give scaling factor for node count */
	SELECT_GET_NODE_CPU_CNT,      /* Give how many cpus are on a node */
	SELECT_GET_MP_CPU_CNT,        /* Give how many cpus are on a
				       * base partition */
	SELECT_APPLY_NODE_MIN_OFFSET, /* Apply min offset to variable */
	SELECT_APPLY_NODE_MAX_OFFSET, /* Apply max offset to variable */
	SELECT_SET_NODE_CNT,	      /* Set altered node cnt */
	SELECT_SET_MP_CNT             /* Given a node cnt return the
				       * base partition count */
};

enum acct_gather_profile_info {
	ACCT_GATHER_PROFILE_DIR,     /* Give directory profiling is stored */
	ACCT_GATHER_PROFILE_DEFAULT, /* What is being collected for
				      * profiling by default */
	ACCT_GATHER_PROFILE_RUNNING  /* What is actually be collected
				      * wither it be user or
				      * default. (Only works in the slurmstepd)
				      */
};

#define ACCT_GATHER_PROFILE_NOT_SET 0x00000000
#define ACCT_GATHER_PROFILE_NONE    SLURM_BIT(0)
#define ACCT_GATHER_PROFILE_ENERGY  SLURM_BIT(1)
#define ACCT_GATHER_PROFILE_TASK    SLURM_BIT(2)
#define ACCT_GATHER_PROFILE_LUSTRE  SLURM_BIT(3)
#define ACCT_GATHER_PROFILE_NETWORK SLURM_BIT(4)
#define ACCT_GATHER_PROFILE_ALL     0xffffffff

/* jobacct data types */
enum jobacct_data_type {
	JOBACCT_DATA_TOTAL,	/* data-> jobacctinfo_t * */
	JOBACCT_DATA_PIPE,      /* data-> file descriptor */
	JOBACCT_DATA_RUSAGE,	/* data-> rusage set user_cpu_sec,
				 * user_cpu_usec, sys_cpu_sec, sys_cpu_usec */
	JOBACCT_DATA_TOT_VSIZE = 5,	/* data-> uint64_t vsize */
	JOBACCT_DATA_TOT_RSS = 8,	/* data-> uint64_t psize */
};

enum acct_energy_type {
	ENERGY_DATA_JOULES_TASK,
	ENERGY_DATA_STRUCT,
	ENERGY_DATA_RECONFIG,
	ENERGY_DATA_PROFILE,
	ENERGY_DATA_LAST_POLL,
	ENERGY_DATA_SENSOR_CNT,
	ENERGY_DATA_NODE_ENERGY,
	ENERGY_DATA_NODE_ENERGY_UP,
	ENERGY_DATA_STEP_PTR
};

typedef enum {
	UPDATE_SET, /* Set to specified value */
	UPDATE_ADD, /* Append to existing value (+=)*/
	UPDATE_REMOVE, /* Remove from existing vale (-=) */
} update_mode_t;

/*
 * Task distribution states/methods
 *
 * Symbol format is SLURM_DIST_<node>_<socket>_<core>
 *
 * <node>   = Method for distributing tasks to nodes.
 *            This determines the order in which task ids are
 *            distributed to the nodes selected for the job/step.
 * <socket> = Method for distributing allocated lllps across sockets.
 *            This determines the order in which allocated lllps are
 *            distributed across sockets for binding to tasks.
 * <core>   = Method for distributing allocated lllps across cores.
 *            This determines the order in which allocated lllps are
 *            distributed across cores for binding to tasks.
 *
 * Note that the socket and core distributions apply only to task affinity.
 */
typedef enum task_dist_states {
	/* NOTE: start SLURM_DIST_CYCLIC at 1 for HP MPI */
	SLURM_DIST_CYCLIC               = 0x0001,
	SLURM_DIST_BLOCK                = 0x0002,
	SLURM_DIST_ARBITRARY            = 0x0003,
	SLURM_DIST_PLANE                = 0x0004,
	SLURM_DIST_CYCLIC_CYCLIC        = 0x0011,
	SLURM_DIST_CYCLIC_BLOCK         = 0x0021,
	SLURM_DIST_CYCLIC_CFULL         = 0x0031,
	SLURM_DIST_BLOCK_CYCLIC         = 0x0012,
	SLURM_DIST_BLOCK_BLOCK          = 0x0022,
	SLURM_DIST_BLOCK_CFULL          = 0x0032,
	SLURM_DIST_CYCLIC_CYCLIC_CYCLIC = 0x0111,
	SLURM_DIST_CYCLIC_CYCLIC_BLOCK  = 0x0211,
	SLURM_DIST_CYCLIC_CYCLIC_CFULL  = 0x0311,
	SLURM_DIST_CYCLIC_BLOCK_CYCLIC  = 0x0121,
	SLURM_DIST_CYCLIC_BLOCK_BLOCK   = 0x0221,
	SLURM_DIST_CYCLIC_BLOCK_CFULL   = 0x0321,
	SLURM_DIST_CYCLIC_CFULL_CYCLIC  = 0x0131,
	SLURM_DIST_CYCLIC_CFULL_BLOCK   = 0x0231,
	SLURM_DIST_CYCLIC_CFULL_CFULL   = 0x0331,
	SLURM_DIST_BLOCK_CYCLIC_CYCLIC  = 0x0112,
	SLURM_DIST_BLOCK_CYCLIC_BLOCK   = 0x0212,
	SLURM_DIST_BLOCK_CYCLIC_CFULL   = 0x0312,
	SLURM_DIST_BLOCK_BLOCK_CYCLIC   = 0x0122,
	SLURM_DIST_BLOCK_BLOCK_BLOCK    = 0x0222,
	SLURM_DIST_BLOCK_BLOCK_CFULL    = 0x0322,
	SLURM_DIST_BLOCK_CFULL_CYCLIC   = 0x0132,
	SLURM_DIST_BLOCK_CFULL_BLOCK    = 0x0232,
	SLURM_DIST_BLOCK_CFULL_CFULL    = 0x0332,

	SLURM_DIST_NODECYCLIC           = 0x0001,
	SLURM_DIST_NODEBLOCK            = 0x0002,
	SLURM_DIST_SOCKCYCLIC           = 0x0010,
	SLURM_DIST_SOCKBLOCK            = 0x0020,
	SLURM_DIST_SOCKCFULL            = 0x0030,
	SLURM_DIST_CORECYCLIC           = 0x0100,
	SLURM_DIST_COREBLOCK            = 0x0200,
	SLURM_DIST_CORECFULL            = 0x0300,

	/* Unused                       = 0x1000, */
	SLURM_DIST_UNKNOWN              = 0x2000
} task_dist_states_t;

#define SLURM_DIST_STATE_BASE		0x00FFFF
#define SLURM_DIST_STATE_FLAGS		0xFF0000
#define SLURM_DIST_PACK_NODES		0x800000
#define SLURM_DIST_NO_PACK_NODES	0x400000

#define SLURM_DIST_NODEMASK               0xF00F
#define SLURM_DIST_SOCKMASK               0xF0F0
#define SLURM_DIST_COREMASK               0xFF00
#define SLURM_DIST_NODESOCKMASK           0xF0FF

/* Open stdout/err file mode, 0 for system default (JobFileAppend) */
#define OPEN_MODE_APPEND	1
#define OPEN_MODE_TRUNCATE	2

typedef enum cpu_bind_type {	/* cpu binding type from --cpu-bind=... */
	/* verbose can be set with any other flag */
	CPU_BIND_VERBOSE    = 0x0001, /* =v, */
	/* the following auto-binding flags are mutually exclusive */
	CPU_BIND_TO_THREADS = 0x0002, /* =threads */
	CPU_BIND_TO_CORES   = 0x0004, /* =cores */
	CPU_BIND_TO_SOCKETS = 0x0008, /* =sockets */
	CPU_BIND_TO_LDOMS   = 0x0010, /* locality domains */
	/* the following manual binding flags are mutually exclusive */
	/* CPU_BIND_NONE needs to be the lowest value among manual bindings */
	CPU_BIND_NONE	    = 0x0020, /* =no */
	CPU_BIND_RANK  	    = 0x0040, /* =rank */
	CPU_BIND_MAP	    = 0x0080, /* =map_cpu:<list of CPU IDs> */
	CPU_BIND_MASK	    = 0x0100, /* =mask_cpu:<list of CPU masks> */
	CPU_BIND_LDRANK     = 0x0200, /* =locality domain rank */
	CPU_BIND_LDMAP	    = 0x0400, /* =map_ldom:<list of locality domains> */
	CPU_BIND_LDMASK	    = 0x0800, /* =mask_ldom:<list of ldom masks> */

	/* the following is used primarily for the
	   --hint=nomultithread when -mblock:block is requested. */
	CPU_BIND_ONE_THREAD_PER_CORE = 0x2000,/* Only bind to one
					       * thread of a core */

	/* default binding if auto binding doesn't match. */
	CPU_AUTO_BIND_TO_THREADS = 0x04000,
	CPU_AUTO_BIND_TO_CORES   = 0x10000,
	CPU_AUTO_BIND_TO_SOCKETS = 0x20000,

	/* the following is used only as a flag for expressing
	 * the contents of TaskPluginParams */
	SLURMD_OFF_SPEC            = 0x40000,
	CPU_BIND_OFF               = 0x80000	/* Disable binding */
} cpu_bind_type_t;

#define CPU_BIND_T_TO_MASK 0x001e
#define CPU_BIND_T_AUTO_TO_MASK 0x34000
#define CPU_BIND_T_MASK 0x0fe0
#define CPU_BIND_T_TASK_PARAMS_MASK (SLURMD_OFF_SPEC | CPU_BIND_OFF)

/* Flag to indicate that cpu_freq is a range: low,medium,high,high-1
 * instead of an integer value in kilohertz */
#define CPU_FREQ_RANGE_FLAG		0x80000000
#define CPU_FREQ_LOW			0x80000001
#define CPU_FREQ_MEDIUM			0x80000002
#define CPU_FREQ_HIGH			0x80000003
#define CPU_FREQ_HIGHM1			0x80000004
#define CPU_FREQ_CONSERVATIVE		0x88000000
#define CPU_FREQ_ONDEMAND		0x84000000
#define CPU_FREQ_PERFORMANCE		0x82000000
#define CPU_FREQ_POWERSAVE		0x81000000
#define CPU_FREQ_USERSPACE		0x80800000
#define CPU_FREQ_SCHEDUTIL		0x80400000
#define CPU_FREQ_GOV_MASK   		0x8ff00000

typedef enum mem_bind_type {    /* memory binding type from --mem-bind=... */
	/* verbose can be set with any other flag */
	MEM_BIND_VERBOSE= 0x01,	/* =v, */
	/* the following five manual binding flags are mutually exclusive */
	/* MEM_BIND_NONE needs to be the first in this sub-list */
	MEM_BIND_NONE   = 0x02,	/* =no */
	MEM_BIND_RANK   = 0x04,	/* =rank */
	MEM_BIND_MAP    = 0x08,	/* =map_mem:<list of NUMA IDs> */
	MEM_BIND_MASK   = 0x10,	/* =mask_mem:<list of NUMA masks> */
	MEM_BIND_LOCAL  = 0x20,	/* =local */
	/* sort and prefer can be set with any other flags */
	MEM_BIND_SORT	= 0x40,	/* =sort */
	MEM_BIND_PREFER = 0x80	/* =prefer */
} mem_bind_type_t;

#define MEM_BIND_TYPE_MASK 0x3e
#define MEM_BIND_TYPE_FLAGS_MASK 0xc0

typedef enum accel_bind_type {    /* accelerator binding from --accel_bind= */
	ACCEL_BIND_VERBOSE         = 0x01, /* 'v' verbose */
	ACCEL_BIND_CLOSEST_GPU     = 0x02, /* 'g' Use closest GPU to the CPU */
	ACCEL_BIND_CLOSEST_NIC     = 0x08  /* 'n' Use closest NIC to CPU */
} accel_bind_type_t;

/* The last entry in node_states must be STATE_END, keep in sync with
 * node_state_string. values may be ORed with NODE_STATE_FLAGS below.
 * Node states typically alternate between NODE_STATE_IDLE and
 * NODE_STATE_ALLOCATED. The NODE_STATE_COMPLETING flag will be set
 * when jobs are in the process of terminating. */
enum node_states {
	NODE_STATE_UNKNOWN,	/* node's initial state, unknown */
	NODE_STATE_DOWN,	/* node in non-usable state */
	NODE_STATE_IDLE,	/* node idle and available for use */
	NODE_STATE_ALLOCATED,	/* node has been allocated to a job */
	NODE_STATE_ERROR,	/* UNUSED - node is in an error state */
	NODE_STATE_MIXED,	/* node has a mixed state */
	NODE_STATE_FUTURE,	/* node slot reserved for future use */
	NODE_STATE_END		/* last entry in table */
};
#define NODE_STATE_BASE       0x0000000f
#define NODE_STATE_FLAGS      0xfffffff0

/* SLURM_BIT(0-3) taken for base states */
#define NODE_STATE_NET        SLURM_BIT(4) /* If a node is using Cray's
					    * Network Performance
					    * Counters but isn't in a
					    * allocation. */
#define NODE_STATE_RES        SLURM_BIT(5) /* If a node is in a
					    * reservation (used primarily
					    * to note a node isn't idle
					    * for non-reservation jobs) */
#define NODE_STATE_UNDRAIN    SLURM_BIT(6) /* Clear DRAIN flag for a node */
#define NODE_STATE_CLOUD      SLURM_BIT(7) /* node comes from cloud */
#define NODE_RESUME           SLURM_BIT(8) /* Restore a DRAINED, DRAINING, DOWN
					    * or FAILING node to service (e.g.
					    * IDLE or ALLOCATED). Used in
					    * slurm_update_node() request */
#define NODE_STATE_DRAIN      SLURM_BIT(9) /* do not allocated new work */
#define NODE_STATE_COMPLETING SLURM_BIT(10) /* node is completing allocated
					     * job */
#define NODE_STATE_NO_RESPOND SLURM_BIT(11) /* node is not responding */
#define NODE_STATE_POWERED_DOWN SLURM_BIT(12) /* node is powered down */
#define NODE_STATE_FAIL       SLURM_BIT(13) /* node is failing, do not allocate
					     * new work */
#define NODE_STATE_POWERING_UP SLURM_BIT(14) /* node is powering up */
#define NODE_STATE_MAINT      SLURM_BIT(15) /* node in maintenance
					     * reservation */
#define NODE_STATE_REBOOT_REQUESTED SLURM_BIT(16) /* node reboot requested */
#define NODE_STATE_REBOOT_CANCEL SLURM_BIT(17) /* cancel pending reboot */
#define NODE_STATE_POWERING_DOWN SLURM_BIT(18) /* node is powering down */
#define NODE_STATE_DYNAMIC_FUTURE SLURM_BIT(19) /* dynamic future node */
#define NODE_STATE_REBOOT_ISSUED SLURM_BIT(20) /* node reboot passed to agent */
#define NODE_STATE_PLANNED    SLURM_BIT(21) /* node scheduled for a job in the
					     * future */
#define NODE_STATE_INVALID_REG SLURM_BIT(22) /* invalid registration, don't
					      * ping */
#define NODE_STATE_POWER_DOWN SLURM_BIT(23) /* manual node power down */
#define NODE_STATE_POWER_UP SLURM_BIT(24) /* manual node power up */
#define NODE_STATE_POWER_DRAIN SLURM_BIT(25) /* signal power down asap */
#define NODE_STATE_DYNAMIC_NORM SLURM_BIT(26) /* dynamic norm node */

/* used to define the size of the credential.signature size
 * used to define the key size of the io_stream_header_t
 */
#define SLURM_SSL_SIGNATURE_LENGTH 128

/* Used as show_flags for slurm_get_ and slurm_load_ function calls.
 * Values can be ORed */
#define SHOW_ALL	0x0001	/* Show info for "hidden" partitions */
#define SHOW_DETAIL	0x0002	/* Show detailed resource information */
/*  was SHOW_DETAIL2	0x0004     Removed v19.05 */
#define SHOW_MIXED	0x0008	/* Automatically set node MIXED state */
#define SHOW_LOCAL	0x0010	/* Show only local information, even on
				 * federated cluster */
#define SHOW_SIBLING	0x0020	/* Show sibling jobs on a federated cluster */
#define SHOW_FEDERATION	0x0040	/* Show federated state information.
				 * Shows local info if not in federation */
#define SHOW_FUTURE	0x0080	/* Show future nodes */

/* CR_CPU, CR_SOCKET and CR_CORE are mutually exclusive
 * CR_MEMORY may be added to any of the above values or used by itself
 * CR_ONE_TASK_PER_CORE may also be added to any of the above values */
#define CR_CPU		0x0001	/* Resources are shared down to the level of
				 * logical processors which can be socket,
				 * core, or thread depending on the system. */
#define CR_SOCKET	0x0002	/* Resources are shared down to the socket
				 * level. Jobs will not be co-allocated
				 * within a socket. */
#define CR_CORE		0x0004	/* Resources are shared down to the core level.
				 * Jobs will not be co-allocated within a
				 * core. */
#define CR_BOARD	0x0008	/* Resources are shared down to the board
				 * level. Jobs will not be co-allocated
				 * within a board. */
#define CR_MEMORY	0x0010	/* Memory as consumable resources. Memory is
				 * not over-committed when selected as a CR. */
#define CR_OTHER_CONS_RES    0x0020 /* if layering select plugins use
				     * cons_res instead of linear (default)
				     */
/* was CR_NHC_STEP_NO 0x0040, removed v19.05 */
/* was CR_NHC_NO 0x0080, removed v19.05 */

/* By default, schedule only one task per core.
 * Without this option, tasks would be allocated threads. */
#define CR_ONE_TASK_PER_CORE 0x0100

/* Pack tasks tightly onto allocated nodes rather than distributing them evenly
 * across available nodes */
#define CR_PACK_NODES  0x0200

/* was CR_NHC_ABSOLUTELY_NO 0x0400, removed v19.05 */
#define CR_OTHER_CONS_TRES   0x0800 /* if layering select plugins use
				     * cons_tres instead of linear (default)
				     */
/* By default, distribute cores using a block approach inside the nodes */
#define CR_CORE_DEFAULT_DIST_BLOCK 0x1000
#define CR_LLN		0x4000  /* Select nodes by "least loaded." */

#define MEM_PER_CPU  0x8000000000000000
#define SHARED_FORCE 0x8000

#define PRIVATE_DATA_JOBS         SLURM_BIT(0) /* job/step data is private */
#define PRIVATE_DATA_NODES        SLURM_BIT(1) /* node data is private */
#define PRIVATE_DATA_PARTITIONS   SLURM_BIT(2) /* partition data is private */
#define PRIVATE_DATA_USAGE        SLURM_BIT(3) /* accounting usage data is
						* private */
#define PRIVATE_DATA_USERS        SLURM_BIT(4) /* accounting user data is
						* private */
#define PRIVATE_DATA_ACCOUNTS     SLURM_BIT(5) /* accounting account data is
						* private */
#define PRIVATE_DATA_RESERVATIONS SLURM_BIT(6) /* reservation data is private */
/* SLURM_BIT(7) Available 2 versions after 23.02 */
#define PRIVATE_DATA_EVENTS       SLURM_BIT(8) /* events are private */

#define PRIORITY_RESET_NONE	0x0000	/* never clear */
#define PRIORITY_RESET_NOW	0x0001	/* clear now (when slurmctld restarts) */
#define PRIORITY_RESET_DAILY	0x0002	/* clear daily at midnight */
#define PRIORITY_RESET_WEEKLY	0x0003	/* clear weekly at Sunday 00:00 */
#define PRIORITY_RESET_MONTHLY	0x0004	/* clear monthly on first at 00:00 */
#define PRIORITY_RESET_QUARTERLY 0x0005	/* clear quarterly on first at 00:00 */
#define PRIORITY_RESET_YEARLY	0x0006	/* clear yearly on first at 00:00 */

#define PROP_PRIO_OFF		0x0000	/* Do not propagage user nice value */
#define PROP_PRIO_ON		0x0001	/* Propagate user nice value */
#define PROP_PRIO_NICER		0x0002	/* Ensure that user tasks have a nice
					 * value that is higher than slurmd */

#define PRIORITY_FLAGS_ACCRUE_ALWAYS	 SLURM_BIT(0) /* Flag to always accrue
						       * age priority to pending
						       * jobs ignoring
						       * dependencies or holds
						       */
#define PRIORITY_FLAGS_MAX_TRES 	 SLURM_BIT(1) /* Calculate billed_tres
						       * as the MAX of TRES on a
						       * node rather than the
						       * sum or TRES. */
#define PRIORITY_FLAGS_SIZE_RELATIVE	 SLURM_BIT(2) /* Enable job size
						       * measurement relative to
						       * its time limit */
#define PRIORITY_FLAGS_DEPTH_OBLIVIOUS	 SLURM_BIT(3) /* Flag to use depth
						       * oblivious formula for
						       * computing hierarchical
						       * fairshare */
#define PRIORITY_FLAGS_CALCULATE_RUNNING SLURM_BIT(4) /* Calculate priorities
						       * for running jobs, not
						       * only the pending jobs.
						       */
#define PRIORITY_FLAGS_FAIR_TREE	 SLURM_BIT(5) /* Prioritize by level in
						       * account hierarchy. */
#define PRIORITY_FLAGS_INCR_ONLY	 SLURM_BIT(6) /* Priority can only
						       * increase, never
						       * decrease in value */

#define PRIORITY_FLAGS_NO_NORMAL_ASSOC	 SLURM_BIT(7)
#define PRIORITY_FLAGS_NO_NORMAL_PART	 SLURM_BIT(8)
#define PRIORITY_FLAGS_NO_NORMAL_QOS	 SLURM_BIT(9)
#define PRIORITY_FLAGS_NO_NORMAL_TRES	 SLURM_BIT(10)

/* These bits are set in the bitflags field of job_desc_msg_t */
#define KILL_INV_DEP       SLURM_BIT(0) /* Kill job on invalid dependency */
#define NO_KILL_INV_DEP    SLURM_BIT(1) /* Don't kill job on invalid
					 * dependency */
#define HAS_STATE_DIR      SLURM_BIT(2) /* Used by slurmctld to track
					 * state dir */
#define BACKFILL_TEST      SLURM_BIT(3) /* Backfill test in progress */
#define GRES_ENFORCE_BIND  SLURM_BIT(4) /* Enforce CPU/GRES binding */
#define TEST_NOW_ONLY      SLURM_BIT(5) /* Test for immediately start only */
#define JOB_SEND_ENV       SLURM_BIT(6) /* Send env to the dbd */
/*
 * #define NODE_REBOOT     SLURM_BIT(7)    DEPRECATED in 22.05, can be used in
 *                                         23.11.
 */
#define SPREAD_JOB         SLURM_BIT(8) /* Spread job across max node count */
#define USE_MIN_NODES      SLURM_BIT(9) /* Prefer minimum node count */
#define JOB_KILL_HURRY     SLURM_BIT(10) /* Avoid burst buffer stage out */
#define TRES_STR_CALC      SLURM_BIT(11) /* Avoid calculating TRES strings at
					  * the end of a job. */
#define SIB_JOB_FLUSH      SLURM_BIT(12) /* Don't send complete to origin */
#define HET_JOB_FLAG       SLURM_BIT(13) /* Heterogeneous job management flag */
#define JOB_NTASKS_SET     SLURM_BIT(14) /* --ntasks explicitly set */
#define JOB_CPUS_SET       SLURM_BIT(15) /* --cpus-per-tasks explicitly set */
#define BF_WHOLE_NODE_TEST SLURM_BIT(16) /* Backfill test in progress */
#define TOP_PRIO_TMP       SLURM_BIT(17) /* Temporary flag for top priority job
					  * operation */
#define JOB_ACCRUE_OVER    SLURM_BIT(18) /* We have cleared the accrual count of
					  * a job. */
#define GRES_DISABLE_BIND  SLURM_BIT(19) /* Disable CPU/GRES binding */
#define JOB_WAS_RUNNING    SLURM_BIT(20) /* Job was running */
#define RESET_ACCRUE_TIME  SLURM_BIT(21) /* Reset the job's accrue time */
#define CRON_JOB           SLURM_BIT(22) /* Job submitted through scrontab */
#define JOB_MEM_SET        SLURM_BIT(23) /* Memory limit explicitly set by job */
#define JOB_RESIZED        SLURM_BIT(24) /* Running job added/removed nodes */
#define USE_DEFAULT_ACCT   SLURM_BIT(25) /* Job submitted to default account */
#define USE_DEFAULT_PART   SLURM_BIT(26) /* Job submitted to default
					  * partition */
#define USE_DEFAULT_QOS    SLURM_BIT(27) /* Job submitted with default QOS */
#define USE_DEFAULT_WCKEY  SLURM_BIT(28) /* Job submitted with default WCKEY */
#define JOB_DEPENDENT      SLURM_BIT(29) /* Job dependent or invalid depend */
#define JOB_MAGNETIC       SLURM_BIT(30) /* Job attempting to run in a
					  * magnetic reservation */
#define JOB_PART_ASSIGNED  SLURM_BIT(31) /* Job didn't request a partition */
#define BACKFILL_SCHED     SLURM_BIT(32) /* Job was considered in last
					  * backfill attempt if not set the
					  * normal scheduler set
					  * last_eval_time */
#define BACKFILL_LAST      SLURM_BIT(33) /* Job was considered in last
					  * schedule attempt */
#define TASKS_CHANGED      SLURM_BIT(34) /* Reset licenses per job */
#define JOB_SEND_SCRIPT    SLURM_BIT(35) /* Send script to the dbd */
#define RESET_LIC_TASK     SLURM_BIT(36) /* Reset licenses per task */
#define RESET_LIC_JOB      SLURM_BIT(37) /* Reset licenses per job */

/* These bits are set in the x11 field of job_desc_msg_t */
#define X11_FORWARD_ALL		0x0001	/* all nodes should setup forward */
#define X11_FORWARD_BATCH	0x0002  /* only the batch node */
#define X11_FORWARD_FIRST	0x0004	/* only the first node */
#define X11_FORWARD_LAST	0x0008	/* only the last node */

typedef enum {
	SSF_NONE = 0, /* No flags set */
	SSF_EXCLUSIVE = 1 << 0, /* CPUs not shared with other steps */
	SSF_NO_KILL = 1 << 1, /* Don't kill step on node failure */
	SSF_OVERCOMMIT = 1 << 2, /* Allow the step allocation of more tasks
				  * to a node than available processors. */
	SSF_WHOLE = 1 << 3, /* Use whole nodes in step allocation */
	SSF_INTERACTIVE = 1 << 4, /* Request interactive step allocation */
	SSF_MEM_ZERO = 1 << 5, /* Requested --mem=0; use all memory but do not
				* count against the job's memory allocation */
	SSF_OVERLAP_FORCE = 1 << 6, /* Force this to overlap with all other
				     * steps; resources allocated to this step
				     * are not decremented from the job's
				     * allocation */
} step_spec_flags_t;

/*****************************************************************************\
 *      SLURM LIBRARY INITIALIZATION FUNCTIONS
\*****************************************************************************/

/*
 * MUST be called before any other Slurm library API calls.
 *
 * conf should be a fully qualified path to a slurm.conf configuration file,
 * or more commonly NULL to allow libslurm to automatically locate its own
 * configuration.
 */
extern void slurm_init(const char *conf);

/*
 * Call at process termination to cleanup internal configuration structures.
 *
 * Strongly recommended if valgrind or similar tools will be used to check
 * your application for memory leaks.
 */
extern void slurm_fini(void);

/*
 * Call in a client to load general plugins.
 */
extern void slurm_client_init_plugins(void);

/*
 * Call in a client to unload general plugins.
 */
extern void slurm_client_fini_plugins(void);

/*****************************************************************************\
 *      SLURM HOSTLIST FUNCTIONS
\*****************************************************************************/

/* The hostlist opaque data type
 *
 * A hostlist is a list of hostnames optimized for a prefixXXXX style
 * naming convention, where XXXX  is a decimal, numeric suffix.
 */
#ifndef   __hostlist_t_defined
#  define __hostlist_t_defined
typedef struct hostlist * hostlist_t;
#endif

/*
 * slurm_hostlist_create():
 *
 * Create a new hostlist from a string representation.
 *
 * The string representation (str) may contain one or more hostnames or
 * bracketed hostlists separated by either `,' or whitespace. A bracketed
 * hostlist is denoted by a common prefix followed by a list of numeric
 * ranges contained within brackets: e.g. "tux[0-5,12,20-25]"
 *
 * To support systems with 3-D topography, a rectangular prism may
 * be described using two three digit numbers separated by "x": e.g.
 * "bgl[123x456]". This selects all nodes between 1 and 4 inclusive
 * in the first dimension, between 2 and 5 in the second, and between
 * 3 and 6 in the third dimension for a total of 4*4*4=64 nodes
 *
 * Note: if this module is compiled with WANT_RECKLESS_HOSTRANGE_EXPANSION
 * defined, a much more loose interpretation of host ranges is used.
 * Reckless hostrange expansion allows all of the following (in addition to
 * bracketed hostlists):
 *
 *  o tux0-5,tux12,tux20-25
 *  o tux0-tux5,tux12,tux20-tux25
 *  o tux0-5,12,20-25
 *
 * If str is NULL, and empty hostlist is created and returned.
 *
 * If the create fails, hostlist_create() returns NULL.
 *
 * The returned hostlist must be freed with hostlist_destroy()
 *
 */
extern hostlist_t slurm_hostlist_create(const char *hostlist);

/* slurm_hostlist_count():
 *
 * Return the number of hosts in hostlist hl.
 */
extern int slurm_hostlist_count(hostlist_t hl);

/*
 * slurm_hostlist_destroy():
 *
 * Destroy a hostlist object. Frees all memory allocated to the hostlist.
 */
extern void slurm_hostlist_destroy(hostlist_t hl);

/* slurm_hostlist_find():
 *
 * Searches hostlist hl for the first host matching hostname
 * and returns position in list if found.
 *
 * Returns -1 if host is not found.
 */
extern int slurm_hostlist_find(hostlist_t hl, const char *hostname);

/* slurm_hostlist_push():
 *
 * push a string representation of hostnames onto a hostlist.
 *
 * The hosts argument may take the same form as in slurm_hostlist_create()
 *
 * Returns the number of hostnames inserted into the list,
 * or 0 on failure.
 */
extern int slurm_hostlist_push(hostlist_t hl, const char *hosts);

/* slurm_hostlist_push_host():
 *
 * Push a single host onto the hostlist hl.
 * This function is more efficient than slurm_hostlist_push() for a single
 * hostname, since the argument does not need to be checked for ranges.
 *
 * return value is 1 for success, 0 for failure.
 */
extern int slurm_hostlist_push_host(hostlist_t hl, const char *host);

/* slurm_hostlist_ranged_string():
 *
 * Write the string representation of the hostlist hl into buf,
 * writing at most n chars. Returns the number of bytes written,
 * or -1 if truncation occurred.
 *
 * The result will be NULL terminated.
 *
 * slurm_hostlist_ranged_string() will write a bracketed hostlist representation
 * where possible.
 */
extern ssize_t slurm_hostlist_ranged_string(hostlist_t hl, size_t n, char *buf);

/* slurm_hostlist_ranged_string_malloc():
 *
 * Return the string representation of the hostlist hl.
 *
 * The result must be released using free();
 */
extern char *slurm_hostlist_ranged_string_malloc(hostlist_t hl);

/* hostlist_ranged_string_xmalloc():
 *
 * Wrapper of hostlist_ranged_string(), with result buffer dynamically
 * allocated using xmalloc().
 * The result will be NULL on failure (out of memory).
 *
 * Caller should free the result string using xfree().
 */
extern char *slurm_hostlist_ranged_string_xmalloc(hostlist_t hl);

/*
 * slurm_hostlist_shift():
 *
 * Returns the string representation of the first host in the hostlist
 * or NULL if the hostlist is empty or there was an error allocating memory.
 * The host is removed from the hostlist.
 *
 * Note: Caller is responsible for freeing the returned memory.
 */
extern char *slurm_hostlist_shift(hostlist_t hl);

/* slurm_hostlist_uniq():
 *
 * Sort the hostlist hl and remove duplicate entries.
 *
 */
extern void slurm_hostlist_uniq(hostlist_t hl);

/*****************************************************************************\
 *      SLURM LIST FUNCTIONS
\*****************************************************************************/

#ifndef   __list_datatypes_defined
#  define __list_datatypes_defined
typedef struct xlist * List;
typedef struct xlist list_t;
/*
 *  List opaque data type.
 */

typedef struct listIterator * ListIterator;
typedef struct listIterator list_itr_t;
/*
 *  List Iterator opaque data type.
 */

typedef void (*ListDelF) (void *x);
/*
 *  Function prototype to deallocate data stored in a list.
 *    This function is responsible for freeing all memory associated
 *    with an item, including all subordinate items (if applicable).
 */

typedef int (*ListCmpF) (void *x, void *y);
/*
 *  Function prototype for comparing two items in a list.
 *  Returns less-than-zero if (x<y), zero if (x==y), and
 *    greather-than-zero if (x>y).
 */

typedef int (*ListFindF) (void *x, void *key);
/*
 *  Function prototype for matching items in a list.
 *  Returns non-zero if (x==key); o/w returns zero.
 */

typedef int (*ListForF) (void *x, void *arg);
/*
 *  Function prototype for operating on each item in a list.
 *  Returns less-than-zero on error.
 */
#endif

/* slurm_list_append():
 *
 *  Inserts data [x] at the end of list [l].
 */
extern void slurm_list_append(list_t *l, void *x);

/* slurm_list_count():
 *
 *  Returns the number of items in list [l].
 */
extern int slurm_list_count(list_t *l);

/* slurm_list_create():
 *
 *  Creates and returns a new empty list.
 *  The deletion function [f] is used to deallocate memory used by items
 *    in the list; if this is NULL, memory associated with these items
 *    will not be freed when the list is destroyed.
 *  Note: Abandoning a list without calling slurm_list_destroy() will result
 *    in a memory leak.
 */
extern list_t *slurm_list_create(ListDelF f);

/* slurm_list_destroy():
 *
 *  Destroys list [l], freeing memory used for list iterators and the
 *    list itself; if a deletion function was specified when the list
 *    was created, it will be called for each item in the list.
 */
extern void slurm_list_destroy(list_t *l);

/* slurm_list_find():
 *
 *  Traverses the list from the point of the list iterator [i]
 *    using [f] to match each item with [key].
 *  Returns a ptr to the next item for which the function [f]
 *    returns non-zero, or NULL once the end of the list is reached.
 *  Example: i=slurm_list_iterator_reset(i);
 *	     while ((x=slurm_list_find(i,f,k))) {...}
 */
extern void *slurm_list_find(list_itr_t *i, ListFindF f, void *key);

/* slurm_list_is_empty():
 *
 *  Returns non-zero if list [l] is empty; o/w returns zero.
 */
extern int slurm_list_is_empty(list_t *l);

/*
 *  Creates and returns a list iterator for non-destructively traversing
 *    list [l].
 */
extern list_itr_t *slurm_list_iterator_create(list_t *l);

/* slurm_list_iterator_reset():
 *
 *  Resets the list iterator [i] to start traversal at the beginning
 *    of the list.
 */
extern void slurm_list_iterator_reset(list_itr_t *i);

/*
 *  Destroys the list iterator [i]; list iterators not explicitly destroyed
 *    in this manner will be destroyed when the list is deallocated via
 *    list_destroy().
 */
extern void slurm_list_iterator_destroy(list_itr_t *i);

/* slurm_list_next():
 *
 *  Returns a ptr to the next item's data,
 *    or NULL once the end of the list is reached.
 *  Example: i=slurm_list_iterator_create(i);
 *	     while ((x=slurm_list_next(i))) {...}
 */
extern void *slurm_list_next(list_itr_t *i);

/* slurm_list_sort():
 *
 *  Sorts list [l] into ascending order according to the function [f].
 *  Note: Sorting a list resets all iterators associated with the list.
 *  Note: The sort algorithm is stable.
 */
extern void slurm_list_sort(list_t *l, ListCmpF f);

/* slurm_list_pop():
 *
 *  Pops the data item at the top of the stack [l].
 *  Returns the data's ptr, or NULL if the stack is empty.
 */
extern void *slurm_list_pop(list_t *l);

/*****************************************************************************\
 *      SLURM BITSTR FUNCTIONS
\*****************************************************************************/

#ifndef   __bitstr_datatypes_defined
#  define __bitstr_datatypes_defined

typedef int64_t bitstr_t;
#define BITSTR_SHIFT 		BITSTR_SHIFT_WORD64

typedef bitstr_t bitoff_t;

#endif

#define ALLOC_SID_ADMIN_HOLD	0x00000001	/* admin job hold */
#define ALLOC_SID_USER_HOLD	0x00000002	/* user job hold */

#define JOB_SHARED_NONE         0x0000
#define JOB_SHARED_OK           0x0001
#define JOB_SHARED_USER         0x0002
#define JOB_SHARED_MCS          0x0003

#define SLURM_POWER_FLAGS_LEVEL 0x0001		/* Equal power cap on all nodes */

/*****************************************************************************\
 *	PROTOCOL DATA STRUCTURE DEFINITIONS
\*****************************************************************************/
typedef struct dynamic_plugin_data {
	void *data;
	uint32_t plugin_id;
} dynamic_plugin_data_t;

typedef struct acct_gather_energy {
	uint32_t ave_watts;	  /* average power consump of node, in watts */
	uint64_t base_consumed_energy;
	uint64_t consumed_energy; /* total energy consumed by node, in joules */
	uint32_t current_watts;	  /* current power consump of node, in watts */
	uint64_t previous_consumed_energy;
	time_t poll_time;         /* When information was last retrieved */
} acct_gather_energy_t;

typedef struct ext_sensors_data {
	uint64_t consumed_energy;    /* total energy consumed, in joules */
	uint32_t temperature;	     /* temperature, in celsius */
	time_t   energy_update_time; /* last update time for consumed_energy */
	uint32_t current_watts;      /* current power consumption, in watts */
} ext_sensors_data_t;

typedef struct power_mgmt_data {
	uint32_t cap_watts;	/* power consumption limit of node, in watts */
	uint32_t current_watts;	/* current power consumption, in watts */
	uint64_t joule_counter;	/* total energy consumption by node, in joules */
	uint32_t new_cap_watts;	/* new power consumption limit of node, in watts */
	uint32_t max_watts;	/* maximum power consumption by node, in watts */
	uint32_t min_watts;	/* minimum power consumption by node, in watts */
	time_t new_job_time;	/* set when a new job has been scheduled on the
				 * node, used to trigger higher cap */
	uint16_t state;		/* Power state information */
	uint64_t time_usec;	/* Data timestamp in microseconds since start
				 * of the day */
} power_mgmt_data_t;

#define CORE_SPEC_THREAD 0x8000	/* If set, this is a thread count not core count */

/*
 * Update:
 * _copy_job_desc_to_job_record()
 * slurm_free_job_desc_msg()
 */
typedef struct job_descriptor {	/* For submit, allocate, and update requests */
	char *account;		/* charge to specified account */
	char *acctg_freq;	/* accounting polling intervals (seconds) */
	char *admin_comment;	/* administrator's arbitrary comment (update only) */
	char *alloc_node;	/* node making resource allocation request
				 * NOTE: Normally set by slurm_submit* or
				 * slurm_allocate* function */
	uint16_t alloc_resp_port; /* port to send allocation confirmation to */
	uint32_t alloc_sid;	/* local sid making resource allocation request
				 * NOTE: Normally set by slurm_submit* or
				 * slurm_allocate* function
				 * NOTE: Also used for update flags, see
				 * ALLOC_SID_* flags */
	uint32_t argc;		/* number of arguments to the script */
	char **argv;		/* arguments to the script */
	char *array_inx;	/* job array index values */
	bitstr_t *array_bitmap;	/* NOTE: Set by slurmctld */
	char *batch_features;	/* features required for batch script's node */
	time_t begin_time;	/* delay initiation until this time */
	uint64_t bitflags;      /* bitflags */
	char *burst_buffer;	/* burst buffer specifications */
	char *clusters;		/* cluster names used for multi-cluster jobs */
	char *cluster_features;	/* required cluster feature specification,
				 * default NONE */
	char *comment;		/* arbitrary comment */
	uint16_t contiguous;	/* 1 if job requires contiguous nodes,
				 * 0 otherwise,default=0 */
	char *container;	/* OCI container bundle */
	char *container_id;	/* OCI container ID */
	uint16_t core_spec;	/* specialized core/thread count,
				 * see CORE_SPEC_THREAD */
	char *cpu_bind;		/* binding map for map/mask_cpu - This
				 * currently does not matter to the
				 * job allocation, setting this does
				 * not do anything for steps. */
	uint16_t cpu_bind_type;	/* see cpu_bind_type_t - This
				 * currently does not matter to the
				 * job allocation, setting this does
				 * not do anything for steps. */
	uint32_t cpu_freq_min;  /* Minimum cpu frequency  */
	uint32_t cpu_freq_max;  /* Maximum cpu frequency  */
	uint32_t cpu_freq_gov;  /* cpu frequency governor */
	char *cpus_per_tres;	/* semicolon delimited list of TRES=# values */
	void *crontab_entry;	/* really cron_entry_t */
	time_t deadline;	/* deadline */
	uint32_t delay_boot;	/* delay boot for desired node state */
	char *dependency;	/* synchronize job execution with other jobs */
	time_t end_time;	/* time by which job must complete, used for
				 * job update only now, possible deadline
				 * scheduling in the future */
	char **environment;	/* environment variables to set for job,
				 *  name=value pairs, one per line */
	slurm_hash_t env_hash;  /* hash value of environment NO NOT PACK */
	uint32_t env_size;	/* element count in environment */
	char *exc_nodes;	/* comma separated list of nodes excluded
				 * from job's allocation, default NONE */
	char *extra;		/* Arbitrary string */
	char *features;		/* required feature specification,
				 * default NONE */
	uint64_t fed_siblings_active; /* Bitmap of active fed sibling ids */
	uint64_t fed_siblings_viable; /* Bitmap of viable fed sibling ids */
	uint32_t group_id;	/* group to assume, if run as root. */
	uint32_t het_job_offset; /* HetJob component offset */
	uint16_t immediate;	/* 1 if allocate to run or fail immediately,
				 * 0 if to be queued awaiting resources */
	uint32_t job_id;	/* job ID, default set by Slurm */
	char * job_id_str;      /* string representation of the jobid */
	char *job_size_str;
	uint16_t kill_on_node_fail; /* 1 if node failure to kill job,
				     * 0 otherwise,default=1 */
	char *licenses;		/* licenses required by the job */
	char *licenses_tot;	/* total licenses required by the job included
				 * from tres requests as well, NOT PACKED */
	uint16_t mail_type;	/* see MAIL_JOB_ definitions above */
	char *mail_user;	/* user to receive notification */
	char *mcs_label;	/* mcs_label if mcs plugin in use */
	char *mem_bind;		/* binding map for map/mask_cpu */
	uint16_t mem_bind_type;	/* see mem_bind_type_t */
	char *mem_per_tres;	/* semicolon delimited list of TRES=# values */
	char *name;		/* name of the job, default "" */
	char *network;		/* network use spec */
	uint32_t nice;		/* requested priority change,
				 * NICE_OFFSET == no change */
	uint32_t num_tasks;	/* number of tasks to be started,
				 * for batch only */
	uint8_t open_mode;	/* out/err open mode truncate or append,
				 * see OPEN_MODE_* */
	char *origin_cluster;	/* cluster name that initiated the job. */
	uint16_t other_port;	/* port to send various notification msg to */
	uint8_t overcommit;	/* over subscribe resources, for batch only */
	char *partition;	/* name of requested partition,
				 * default in Slurm config */
	uint16_t plane_size;	/* plane size when task_dist =
				   SLURM_DIST_PLANE */
	uint8_t power_flags;	/* power management flags,
				 * see SLURM_POWER_FLAGS_ */
	char *prefer;		/* soft feature specification,
				 * default NONE */
	uint32_t priority;	/* relative priority of the job,
				 * explicitly set only for user root,
				 * 0 == held (don't initiate) */
	uint32_t profile;	/* Level of acct_gather_profile {all | none} */
	char *qos;		/* Quality of Service */
	uint16_t reboot;	/* force node reboot before startup */
	char *resp_host;	/* NOTE: Set by slurmctld */
	uint16_t restart_cnt;	/* count of job restarts */
	char *req_nodes;	/* comma separated list of required nodes
				 * default NONE */
	uint16_t requeue;	/* enable or disable job requeue option */
	char *reservation;	/* name of reservation to use */
	char *script;		/* the actual job script, default NONE */
	void *script_buf;	/* job script as mmap buf */
	slurm_hash_t script_hash; /* hash value of script NO NOT PACK */
	uint16_t shared;	/* 2 if the job can only share nodes with other
				 *   jobs owned by that user,
				 * 1 if job can share nodes with other jobs,
				 * 0 if job needs exclusive access to the node,
				 * or NO_VAL to accept the system default.
				 * SHARED_FORCE to eliminate user control. */
	uint32_t site_factor;	/* factor to consider in priority */
	char **spank_job_env;	/* environment variables for job prolog/epilog
				 * scripts as set by SPANK plugins */
	uint32_t spank_job_env_size; /* element count in spank_env */
	char *submit_line;      /* The command issued with all it's options in a
				 * string */
	uint32_t task_dist;	/* see enum task_dist_state */
	uint32_t time_limit;	/* maximum run time in minutes, default is
				 * partition limit */
	uint32_t time_min;	/* minimum run time in minutes, default is
				 * time_limit */
	char *tres_bind;	/* Task to TRES binding directives */
	char *tres_freq;	/* TRES frequency directives */
	char *tres_per_job;	/* semicolon delimited list of TRES=# values */
	char *tres_per_node;	/* semicolon delimited list of TRES=# values */
	char *tres_per_socket;	/* semicolon delimited list of TRES=# values */
	char *tres_per_task;	/* semicolon delimited list of TRES=# values */
	uint32_t user_id;	/* set only if different from current UID,
				 * can only be explicitly set by user root */
	uint16_t wait_all_nodes;/* 0 to start job immediately after allocation
				 * 1 to start job after all nodes booted
				 * or NO_VAL to use system default */
	uint16_t warn_flags;	/* flags  related to job signals
				 * (eg. KILL_JOB_BATCH) */
	uint16_t warn_signal;	/* signal to send when approaching end time */
	uint16_t warn_time;	/* time before end to send signal (seconds) */
	char *work_dir;		/* pathname of working directory */

	/* job constraints: */
	uint16_t cpus_per_task;	/* number of processors required for
				 * each task */
	uint32_t min_cpus;	/* minimum number of processors required,
				 * default=0 */
	uint32_t max_cpus;	/* maximum number of processors required,
				 * default=0 */
	uint32_t min_nodes;	/* minimum number of nodes required by job,
				 * default=0 */
	uint32_t max_nodes;	/* maximum number of nodes usable by job,
				 * default=0 */
	uint16_t boards_per_node; /* boards per node required by job  */
	uint16_t sockets_per_board;/* sockets per board required by job */
	uint16_t sockets_per_node;/* sockets per node required by job */
	uint16_t cores_per_socket;/* cores per socket required by job */
	uint16_t threads_per_core;/* threads per core required by job */
	uint16_t ntasks_per_node;/* number of tasks to invoke on each node */
	uint16_t ntasks_per_socket;/* number of tasks to invoke on
				    * each socket */
	uint16_t ntasks_per_core;/* number of tasks to invoke on each core */
	uint16_t ntasks_per_board;/* number of tasks to invoke on each board */
	uint16_t ntasks_per_tres;/* number of tasks that can access each gpu */
	uint16_t pn_min_cpus;    /* minimum # CPUs per node, default=0 */
	uint64_t pn_min_memory;  /* minimum real memory per node OR
				  * real memory per CPU | MEM_PER_CPU,
				  * default=0 (no limit) */
	uint32_t pn_min_tmp_disk;/* minimum tmp disk per node,
				  * default=0 */
	char *req_context;	/* requested selinux context */
	uint32_t req_switch;    /* Minimum number of switches */
	char *selinux_context;	/* used internally in the slurmctld,
				   DON'T PACK */
	char *std_err;		/* pathname of stderr */
	char *std_in;		/* pathname of stdin */
	char *std_out;		/* pathname of stdout */
	uint64_t *tres_req_cnt; /* used internally in the slurmctld,
				   DON'T PACK */
	uint32_t wait4switch;   /* Maximum time to wait for minimum switches */
	char *wckey;            /* wckey for job */
	uint16_t x11;		/* --x11 flags */
	char *x11_magic_cookie;	/* automatically stolen from submit node */
	char *x11_target;	/* target hostname, or unix socket if port == 0 */
	uint16_t x11_target_port; /* target tcp port, 6000 + the display number */
} job_desc_msg_t;

typedef struct job_info {
	char *account;		/* charge to specified account */
	time_t accrue_time;	/* time job is eligible for running */
	char *admin_comment;	/* administrator's arbitrary comment */
	char    *alloc_node;	/* local node making resource alloc */
	uint32_t alloc_sid;	/* local sid making resource alloc */
	bitstr_t *array_bitmap;	/* NOTE: set on unpack */
	uint32_t array_job_id;	/* job_id of a job array or 0 if N/A */
	uint32_t array_task_id;	/* task_id of a job array */
	uint32_t array_max_tasks; /* Maximum number of running tasks */
	char *array_task_str;	/* string expression of task IDs in this record */
	uint32_t assoc_id;	/* association id for job */
	char *batch_features;	/* features required for batch script's node */
	uint16_t batch_flag;	/* 1 if batch: queued job with script */
	char *batch_host;	/* name of host running batch script */
	uint64_t bitflags;      /* Various job flags */
	uint16_t boards_per_node;  /* boards per node required by job   */
	char *burst_buffer;	/* burst buffer specifications */
	char *burst_buffer_state; /* burst buffer state info */
	char *cluster;		/* name of cluster that the job is on */
	char *cluster_features;	/* comma separated list of required cluster
				 * features */
	char *command;		/* command to be executed, built from submitted
				 * job's argv */
	char *comment;		/* arbitrary comment */
	char *container;	/* OCI Container bundle path */
	char *container_id;	/* OCI Container ID */
	uint16_t contiguous;	/* 1 if job requires contiguous nodes */
	uint16_t core_spec;	/* specialized core count */
	uint16_t cores_per_socket; /* cores per socket required by job  */
	double billable_tres;	/* billable TRES cache. updated upon resize */
	uint16_t cpus_per_task;	/* number of processors required for
				 * each task */
	uint32_t cpu_freq_min;  /* Minimum cpu frequency  */
	uint32_t cpu_freq_max;  /* Maximum cpu frequency  */
	uint32_t cpu_freq_gov;  /* cpu frequency governor */
	char *cpus_per_tres;	/* semicolon delimited list of TRES=# values */
	char *cronspec;		/* cron time specification (scrontab jobs) */
	time_t deadline;	/* deadline */
	uint32_t delay_boot;	/* delay boot for desired node state */
	char *dependency;	/* synchronize job execution with other jobs */
	uint32_t derived_ec;	/* highest exit code of all job steps */
	time_t eligible_time;	/* time job is eligible for running */
	time_t end_time;	/* time of termination, actual or expected */
	char *exc_nodes;	/* comma separated list of excluded nodes */
	int32_t *exc_node_inx;	/* excluded list index pairs into node_table:
				 * start_range_1, end_range_1,
				 * start_range_2, .., -1  */
	uint32_t exit_code;	/* exit code for job (status from wait call) */
	char *extra;		/* Arbitrary string */
	char *failed_node;	/* if set, node that caused job to fail */
	char *features;		/* comma separated list of required features */
	char *fed_origin_str;	/* Origin cluster's name */
	uint64_t fed_siblings_active;  /* bitmap of active fed sibling ids */
	char *fed_siblings_active_str; /* string of active sibling names */
	uint64_t fed_siblings_viable;  /* bitmap of viable fed sibling ids */
	char *fed_siblings_viable_str; /* string of viable sibling names */
	uint32_t gres_detail_cnt; /* Count of gres_detail_str records,
				 * one per allocated node */
	char **gres_detail_str;	/* Details of GRES count/index alloc per node */
	char *gres_total;       /* Total count of gres used with names */
	uint32_t group_id;	/* group job submitted as */
	uint32_t het_job_id;	/* job ID of hetjob leader */
	char *het_job_id_set;	/* job IDs for all components */
	uint32_t het_job_offset; /* HetJob component offset from leader */
	uint32_t job_id;	/* job ID */
	job_resources_t *job_resrcs; /* opaque data type, job resources */
	char *job_size_str;
	uint32_t job_state;	/* state of the job, see enum job_states */
	time_t last_sched_eval; /* last time job was evaluated for scheduling */
	char *licenses;		/* licenses required by the job */
	uint16_t mail_type;	/* see MAIL_JOB_ definitions above */
	char *mail_user;	/* user to receive notification */
	uint32_t max_cpus;	/* maximum number of cpus usable by job */
	uint32_t max_nodes;	/* maximum number of nodes usable by job */
	char *mcs_label;	/* mcs_label if mcs plugin in use */
	char *mem_per_tres;	/* semicolon delimited list of TRES=# values */
	char *name;		/* name of the job */
	char *network;		/* network specification */
	char *nodes;		/* list of nodes allocated to job */
	uint32_t nice;	  	/* requested priority change */
	int32_t *node_inx;	/* list index pairs into node_table for *nodes:
				 * start_range_1, end_range_1,
				 * start_range_2, .., -1  */
	uint16_t ntasks_per_core;/* number of tasks to invoke on each core */
	uint16_t ntasks_per_tres;/* number of tasks that can access each gpu */
	uint16_t ntasks_per_node;/* number of tasks to invoke on each node */
	uint16_t ntasks_per_socket;/* number of tasks to invoke on each socket*/
	uint16_t ntasks_per_board; /* number of tasks to invoke on each board */
	uint32_t num_cpus;	/* minimum number of cpus required by job */
	uint32_t num_nodes;	/* minimum number of nodes required by job */
	uint32_t num_tasks;	/* requested task count */
	char *partition;	/* name of assigned partition */
	char *prefer;		/* comma separated list of soft features */
	uint64_t pn_min_memory; /* minimum real memory per node, default=0 */
	uint16_t pn_min_cpus;   /* minimum # CPUs per node, default=0 */
	uint32_t pn_min_tmp_disk; /* minimum tmp disk per node, default=0 */
	uint8_t power_flags;	/* power management flags,
				 * see SLURM_POWER_FLAGS_ */
	time_t preempt_time;	/* preemption signal time */
	time_t preemptable_time;/* job becomes preemptable from
				 *  PreemptExemptTime */
	time_t pre_sus_time;	/* time job ran prior to last suspend */
	uint32_t priority;	/* relative priority of the job,
				 * 0=held, 1=required nodes DOWN/DRAINED */
	uint32_t profile;	/* Level of acct_gather_profile {all | none} */
	char *qos;		/* Quality of Service */
	uint8_t reboot;		/* node reboot requested before start */
	char *req_nodes;	/* comma separated list of required nodes */
	int32_t *req_node_inx;	/* required list index pairs into node_table:
				 * start_range_1, end_range_1,
				 * start_range_2, .., -1  */
	uint32_t req_switch;    /* Minimum number of switches */
	uint16_t requeue;       /* enable or disable job requeue option */
	time_t resize_time;	/* time of latest size change */
	uint16_t restart_cnt;	/* count of job restarts */
	char *resv_name;	/* reservation name */
	char *sched_nodes;	/* list of nodes scheduled to be used for job */
	char *selinux_context;
	uint16_t shared;	/* 1 if job can share nodes with other jobs */
	uint16_t show_flags;	/* conveys level of details requested */
	uint32_t site_factor;	/* factor to consider in priority */
	uint16_t sockets_per_board;/* sockets per board required by job */
	uint16_t sockets_per_node; /* sockets per node required by job  */
	time_t start_time;	/* time execution begins, actual or expected */
	uint16_t start_protocol_ver; /* Slurm version step was started with
				      *	either srun or the lowest slurmd version
				      *	it is talking to */
	char *state_desc;	/* optional details for state_reason */
	uint32_t state_reason;	/* reason job still pending or failed, see
				 * slurm.h:enum job_state_reason */
	char *std_err;		/* pathname of job's stderr file */
	char *std_in;		/* pathname of job's stdin file */
	char *std_out;		/* pathname of job's stdout file */
	time_t submit_time;	/* time of job submission */
	time_t suspend_time;	/* time job last suspended or resumed */
	char *system_comment;	/* slurmctld's arbitrary comment */
	uint32_t time_limit;	/* maximum run time in minutes or INFINITE */
	uint32_t time_min;	/* minimum run time in minutes or INFINITE */
	uint16_t threads_per_core; /* threads per core required by job  */
	char *tres_bind;	/* Task to TRES binding directives */
	char *tres_freq;	/* TRES frequency directives */
	char *tres_per_job;	/* semicolon delimited list of TRES=# values */
	char *tres_per_node;	/* semicolon delimited list of TRES=# values */
	char *tres_per_socket;	/* semicolon delimited list of TRES=# values */
	char *tres_per_task;	/* semicolon delimited list of TRES=# values */
	char *tres_req_str;	/* tres reqeusted in the job */
	char *tres_alloc_str;   /* tres used in the job */
	uint32_t user_id;	/* user the job runs as */
	char *user_name;	/* user_name or null. not always set, but
				 * accurate if set (and can avoid a local
				 * lookup call) */
	uint32_t wait4switch;   /* Maximum time to wait for minimum switches */
	char *wckey;            /* wckey for job */
	char *work_dir;		/* pathname of working directory */
} slurm_job_info_t;

typedef slurm_job_info_t job_info_t;

typedef struct {
	uint32_t nice;
	double	 priority_age;
	double	 priority_assoc;
	double	 priority_fs;
	double	 priority_js;
	double	 priority_part;
	double	 priority_qos;
	uint32_t priority_site;

	double   *priority_tres;/* tres priorities with weights applied. */
	uint32_t  tres_cnt;     /* number of configured tres' on system. */
	char    **tres_names;	/* packed as assoc_mgr_tres_names[] */
	double   *tres_weights; /* PriorityWeightTRES weights as an array */
} priority_factors_t;

typedef struct priority_factors_object {
	char *account;
	char *cluster_name;	/* Cluster name ONLY set in federation */
	double direct_prio; /* Manually set priority. If it is set prio_factors
			     * will be NULL */
	uint32_t job_id;
	char *partition;
	priority_factors_t *prio_factors;
	char *qos;
	uint32_t user_id;
} priority_factors_object_t;

typedef struct priority_factors_response_msg {
	list_t *priority_factors_list;	/* priority_factors_object_t list */
} priority_factors_response_msg_t;

typedef struct job_info_msg {
	time_t last_backfill;	/* time of late backfill run */
	time_t last_update;	/* time of latest info */
	uint32_t record_count;	/* number of records */
	slurm_job_info_t *job_array;	/* the job records */
} job_info_msg_t;

typedef struct step_update_request_msg {
	uint32_t job_id;
	uint32_t step_id;
	uint32_t time_limit;	/* In minutes */
} step_update_request_msg_t;

typedef struct suspend_exc_update_msg {
	char *update_str;
	update_mode_t mode;
} suspend_exc_update_msg_t;

typedef struct {
	char *node_list; /* nodelist corresponding to task layout */
	uint16_t *cpus_per_node; /* cpus per node */
	uint32_t *cpu_count_reps; /* how many nodes have same cpu count */
	uint32_t num_hosts; /* number of hosts we have */
	uint32_t num_tasks; /* number of tasks to distribute across these cpus*/
	uint16_t *cpus_per_task; /* number of cpus per task */
	uint32_t *cpus_task_reps; /* how many nodes have same per task count */
	uint32_t task_dist; /* type of distribution we are using */
	uint16_t plane_size; /* plane size (only needed for plane distribution*/
} slurm_step_layout_req_t;

typedef struct slurm_step_layout {
	char *front_end;	/* If a front-end architecture, the name of
				 * of the node running all tasks,
				 * NULL otherwise */
	uint32_t node_cnt;	/* node count */
	char *node_list;        /* list of nodes in step */
	uint16_t plane_size;	/* plane size when task_dist =
				 * SLURM_DIST_PLANE */
	uint16_t start_protocol_ver; /* Slurm version step was started with
				      *	either srun or the lowest slurmd version
				      *	it is talking to */
	/* Array of length "node_cnt". Each element of the array
	 * is the number of tasks assigned to the corresponding node */
	uint16_t *tasks;
	uint32_t task_cnt;	/* total number of tasks in the step */
	uint32_t task_dist;	/* see enum task_dist_state */
	/* Array (of length "node_cnt") of task ID arrays.  The length
	 * of each subarray is designated by the corresponding value in
	 * the tasks array. */
	uint32_t **tids;	/* host id => task id mapping */
} slurm_step_layout_t;

typedef struct slurm_step_id_msg {
	uint32_t job_id;
	uint32_t step_het_comp;
	uint32_t step_id;
} slurm_step_id_t;

typedef struct slurm_step_io_fds {
	struct {
		int fd;
		uint32_t taskid;
		uint32_t nodeid;
	} input, out, err;
} slurm_step_io_fds_t;

#define SLURM_STEP_IO_FDS_INITIALIZER {{0, (uint32_t)-1, (uint32_t)-1},	\
		{1, (uint32_t)-1, (uint32_t)-1},			\
		{2, (uint32_t)-1, (uint32_t)-1}}

typedef struct launch_tasks_response_msg {
	uint32_t return_code;
	char    *node_name;
	uint32_t srun_node_id;
	uint32_t count_of_pids;
	uint32_t *local_pids;
	slurm_step_id_t step_id;
	uint32_t *task_ids; /* array of length count_of_pids */
} launch_tasks_response_msg_t;

typedef struct task_ext_msg {
	uint32_t num_tasks;
	uint32_t *task_id_list;
	uint32_t return_code;
	slurm_step_id_t step_id;
} task_exit_msg_t;

typedef struct {
	uint32_t job_id;	/* slurm job_id */
	uint32_t flags;		/* flags */
	uint16_t port;		/* target TCP port */
	char *target;		/* target host or UNIX socket */
} net_forward_msg_t;

typedef struct srun_ping_msg {
	uint32_t job_id;	/* slurm job_id */
} srun_ping_msg_t;

typedef slurm_step_id_t srun_job_complete_msg_t;

typedef struct srun_timeout_msg {
	slurm_step_id_t step_id;
	time_t   timeout;	/* when job scheduled to be killed */
} srun_timeout_msg_t;

typedef struct srun_user_msg {
	uint32_t job_id;	/* slurm job_id */
	char *msg;		/* message to user's srun */
} srun_user_msg_t;

typedef struct srun_node_fail_msg {
	char *nodelist;		/* name of failed node(s) */
	slurm_step_id_t step_id;
} srun_node_fail_msg_t;

typedef struct srun_step_missing_msg {
	char *nodelist;		/* name of node(s) lacking this step */
	slurm_step_id_t step_id;
} srun_step_missing_msg_t;

enum suspend_opts {
	SUSPEND_JOB,		/* Suspend a job now */
	RESUME_JOB		/* Resume a job now */
};

/* NOTE: Set either job_id_str (NULL by default) or job_id */
typedef struct suspend_msg {
	uint16_t op;		/* suspend operation, see enum suspend_opts */
	uint32_t job_id;	/* slurm job ID (number) */
	char *   job_id_str;	/* slurm job ID (string) */
} suspend_msg_t;

/* NOTE: Set either job_id_str (NULL by default) or job_id */
typedef struct top_job_msg {
	uint16_t op;		/* suspend operation, see enum suspend_opts */
	uint32_t job_id;	/* slurm job ID (number) */
	char *   job_id_str;	/* slurm job ID (string) */
} top_job_msg_t;

typedef struct {
	char *alias_list;	/* node name/address/hostname aliases */
	uint32_t argc;
	char **argv;
	uint32_t envc;
	char **env;
	char *container; /* OCI Container bundle path */
	char *cwd;
	bool user_managed_io;
	uint32_t msg_timeout; /* timeout set for sending message */
	uint16_t ntasks_per_board;/* number of tasks to invoke on each board */
	uint16_t ntasks_per_core; /* number of tasks to invoke on each core */
	uint16_t ntasks_per_tres;/* number of tasks that can access each gpu */
	uint16_t ntasks_per_socket;/* number of tasks to invoke on
				    * each socket */

	/* START - only used if user_managed_io is false */
	bool buffered_stdio;
	bool labelio;
	char *remote_output_filename;
	char *remote_error_filename;
	char *remote_input_filename;
	slurm_step_io_fds_t local_fds;
	/*  END  - only used if user_managed_io is false */

	bool multi_prog;
	bool no_alloc;
	uint32_t slurmd_debug;  /* remote slurmd debug level */
	uint32_t het_job_node_offset;	/* Hetjob node offset or NO_VAL */
	uint32_t het_job_id;	/* Hetjob ID or NO_VAL */
	uint32_t het_job_nnodes;/* total task count for entire hetjob */
	uint32_t het_job_ntasks;/* total task count for entire hetjob */
	uint32_t het_job_step_cnt;/* total step count for entire hetjob */
	uint16_t *het_job_task_cnts; /* Number of tasks on each node in hetjob */
	uint32_t **het_job_tids;	/* Task IDs on each node in hetjob */
	uint32_t *het_job_tid_offsets;/* map of tasks (by id) to originating
				       * hetjob */
	uint32_t het_job_offset;/* Hetjob offset or NO_VAL */
	uint32_t het_job_task_offset; /* Hetjob task offset or NO_VAL */
	char *het_job_node_list;	/* Hetjob step node list */
	bool parallel_debug;
	uint32_t profile;	/* Level of acct_gather_profile {all | none} */
	char *task_prolog;
	char *task_epilog;
	uint16_t cpu_bind_type;	/* use cpu_bind_type_t */
	char *cpu_bind;
	uint32_t cpu_freq_min;	/* Minimum cpu frequency  */
	uint32_t cpu_freq_max;	/* Maximum cpu frequency  */
	uint32_t cpu_freq_gov;	/* cpu frequency governor */
	uint16_t mem_bind_type;	/* use mem_bind_type_t */
	char *mem_bind;
	uint16_t accel_bind_type; /* --accel-bind= */

	uint16_t max_sockets;
	uint16_t max_cores;
	uint16_t max_threads;
	uint16_t cpus_per_task;
	uint16_t threads_per_core;
	uint32_t task_dist;
	char *partition;
	bool preserve_env;

	char *mpi_plugin_name;
	uint8_t open_mode;
	char *acctg_freq;
	bool pty;
	char **spank_job_env;	/* environment variables for job prolog/epilog
				 * scripts as set by SPANK plugins */
	uint32_t spank_job_env_size;	/* element count in spank_env */
	char *tres_bind;
	char *tres_freq;
} slurm_step_launch_params_t;

typedef struct {
	void (*step_complete)(srun_job_complete_msg_t *);
	void (*step_signal)(int);
	void (*step_timeout)(srun_timeout_msg_t *);
	void (*task_start)(launch_tasks_response_msg_t *);
	void (*task_finish)(task_exit_msg_t *);
} slurm_step_launch_callbacks_t;

typedef struct {
	void (*job_complete)(srun_job_complete_msg_t *);
	void (*timeout)(srun_timeout_msg_t *);
	void (*user_msg)(srun_user_msg_t *);
	void (*node_fail)(srun_node_fail_msg_t *);
	void (*job_suspend)(suspend_msg_t *);
} slurm_allocation_callbacks_t;

typedef struct {
	void (*acct_full)();
	void (*dbd_fail)();
	void (*dbd_resumed)();
	void (*db_fail)();
	void (*db_resumed)();
} slurm_trigger_callbacks_t;

typedef struct {
	uint32_t array_job_id;	/* job_id of a job array or 0 if N/A */
	uint32_t array_task_id;	/* task_id of a job array */
	char *cluster;		/* cluster that the step is running on. */
	char *container;	/* OCI container bundle path */
	char *container_id;	/* OCI container ID */
	uint32_t cpu_freq_min;	/* Minimum cpu frequency  */
	uint32_t cpu_freq_max;	/* Maximum cpu frequency  */
	uint32_t cpu_freq_gov;	/* cpu frequency governor */
	char *cpus_per_tres;	/* comma delimited list of TRES=# values */
	char *mem_per_tres;	/* comma delimited list of TRES=# values */
	char *name;		/* name of job step */
	char *network;		/* network specs for job step */
	char *nodes;		/* list of nodes allocated to job_step */
	int32_t *node_inx;	/* list index pairs into node_table for *nodes:
				 * start_range_1, end_range_1,
				 * start_range_2, .., -1  */
	uint32_t num_cpus;	/* how many cpus are being used by step */
	uint32_t num_tasks;	/* number of tasks */
	char *partition;	/* name of assigned partition */
	char *resv_ports;	/* ports allocated for MPI */
	time_t run_time;	/* net run time (factor out time suspended) */
	char *srun_host;	/* host of srun command */
	uint32_t srun_pid;	/* PID of srun command */
	time_t start_time;	/* step start time */
	uint16_t start_protocol_ver; /* Slurm version step was started with
				      *	either srun or the lowest slurmd version
				      *	it is talking to */
	uint32_t state;		/* state of the step, see enum job_states */
	slurm_step_id_t step_id;
	char *submit_line;      /* The command issued with all it's options in a
				 * string */
	uint32_t task_dist;	/* see enum task_dist_state */
	uint32_t time_limit;	/* step time limit */
	char *tres_alloc_str;   /* tres used in the job */
	char *tres_bind;	/* Task to TRES binding directives */
	char *tres_freq;	/* TRES frequency directives */
	char *tres_per_step;	/* comma delimited list of TRES=# values */
	char *tres_per_node;	/* comma delimited list of TRES=# values */
	char *tres_per_socket;	/* comma delimited list of TRES=# values */
	char *tres_per_task;	/* comma delimited list of TRES=# values */
	uint32_t user_id;	/* user the job runs as */
} job_step_info_t;

typedef struct job_step_info_response_msg {
	time_t last_update;		/* time of latest info */
	uint32_t job_step_count;	/* number of records */
	job_step_info_t *job_steps;	/* the job step records */
} job_step_info_response_msg_t;

typedef struct {
   	char *node_name;
	uint32_t *pid;
	uint32_t pid_cnt;
} job_step_pids_t;

typedef struct {
	list_t *pid_list;	/* list of job_step_pids_t *'s */
	slurm_step_id_t step_id;
} job_step_pids_response_msg_t;

typedef struct {
	jobacctinfo_t *jobacct;
	uint32_t num_tasks;
   	uint32_t return_code;
	job_step_pids_t *step_pids;
} job_step_stat_t;

typedef struct {
	list_t *stats_list;	/* list of job_step_stat_t *'s */
	slurm_step_id_t step_id;
} job_step_stat_response_msg_t;

typedef struct node_info {
	char *arch;		/* computer architecture */
	char *bcast_address;	/* BcastAddr (optional) */
	uint16_t boards;        /* total number of boards per node  */
	time_t boot_time;	/* time of node boot */
	char *cluster_name;	/* Cluster name ONLY set in federation */
	uint16_t cores;         /* number of cores per socket       */
	uint16_t core_spec_cnt; /* number of specialized cores on node */
	uint32_t cpu_bind;	/* Default task binding */
	uint32_t cpu_load;	/* CPU load * 100 */
	uint64_t free_mem;	/* free memory in MiB */
	uint16_t cpus;		/* configured count of cpus running on
				 * the node */
	uint16_t cpus_efctv;	/* count of effective cpus on the node.
				   i.e cpus minus specialized cpus*/
	char *cpu_spec_list;	/* node's specialized cpus */
	acct_gather_energy_t *energy;	 /* energy data */
	ext_sensors_data_t *ext_sensors; /* external sensor data */
	char *extra;		/* arbitrary sting */
	power_mgmt_data_t *power;        /* power management data */
	char *features;		/* list of a node's available features */
	char *features_act;	/* list of a node's current active features,
				 * Same as "features" if NULL */
	char *gres;		/* list of a node's generic resources */
	char *gres_drain;	/* list of drained GRES */
	char *gres_used;	/* list of GRES in current use */
	time_t last_busy;	/* time node was last busy (i.e. no jobs) */
	char *mcs_label;	/* mcs label if mcs plugin in use */
	uint64_t mem_spec_limit; /* MB memory limit for specialization */
	char *name;		/* node name to slurm */
	uint32_t next_state;	/* state after reboot (enum node_states) */
	char *node_addr;	/* communication name (optional) */
	char *node_hostname;	/* node's hostname (optional) */
	uint32_t node_state;	/* see enum node_states */
	char *os;		/* operating system currently running */
	uint32_t owner;		/* User allowed to use this node or NO_VAL */
	char *partitions;	/* Comma separated list of partitions containing
				 * this node, NOT supplied by slurmctld, but
				 * populated by scontrol */
	uint16_t port;		/* TCP port number of the slurmd */
	uint64_t real_memory;	/* configured MB of real memory on the node */
	char *comment;		/* arbitrary comment */
	char *reason;		/* reason for node being DOWN or DRAINING */
	time_t reason_time;	/* Time stamp when reason was set, ignore if
				 * no reason is set. */
	uint32_t reason_uid;   	/* User that set the reason, ignore if
				 * no reason is set. */
	time_t resume_after;    /* automatically resume DOWN or DRAINED node at
			         * this point in time */
	char *resv_name;        /* If node is in a reservation this is
				 * the name of the reservation */
	dynamic_plugin_data_t *select_nodeinfo;  /* opaque data structure,
						  * use
						  * slurm_get_select_nodeinfo()
						  * to access contents */
	time_t slurmd_start_time;/* time of slurmd startup */
	uint16_t sockets;       /* total number of sockets per node */
	uint16_t threads;       /* number of threads per core */
	uint32_t tmp_disk;	/* configured MB of total disk in TMP_FS */
	uint32_t weight;	/* arbitrary priority of node for scheduling */
	char *tres_fmt_str;	/* str representing configured TRES on node */
	char *version;		 /* Slurm version number */
} node_info_t;

typedef struct node_info_msg {
	time_t last_update;		/* time of latest info */
	uint32_t record_count;		/* number of records */
	node_info_t *node_array;	/* the node records */
} node_info_msg_t;

typedef struct front_end_info {
	char *allow_groups;		/* allowed group string */
	char *allow_users;		/* allowed user string */
	time_t boot_time;		/* Time of node boot,
					 * computed from up_time */
	char *deny_groups;		/* denied group string */
	char *deny_users;		/* denied user string */
	char *name;			/* node name */
	uint32_t node_state;		/* see enum node_states */
	char *reason;			/* reason for node being DOWN or
					 * DRAINING */
	time_t reason_time;		/* Time stamp when reason was set,
					 * ignore if no reason is set. */
	uint32_t reason_uid;   		/* User that set the reason,
					 * ignore if no reason is set. */
	time_t slurmd_start_time;	/* Time of slurmd startup */
	char *version;			/* Slurm version number */
} front_end_info_t;

typedef struct front_end_info_msg {
	time_t last_update;		/* time of latest info */
	uint32_t record_count;		/* number of records */
	front_end_info_t *front_end_array;	/* the front_end records */
} front_end_info_msg_t;

typedef struct topo_info {
	uint16_t level;			/* level in hierarchy, leaf=0 */
	uint32_t link_speed;		/* link speed, arbitrary units */
	char *name;			/* switch name */
	char *nodes;			/* name if direct descendent nodes */
	char *switches;			/* name if direct descendent switches */
} topo_info_t;

typedef struct topo_info_response_msg {
	uint32_t record_count;		/* number of records */
	topo_info_t *topo_array;	/* the switch topology records */
} topo_info_response_msg_t;

typedef struct job_alloc_info_msg {
	uint32_t job_id;	/* job ID */
	char    *req_cluster;   /* requesting cluster */
} job_alloc_info_msg_t;

typedef struct {
	uint32_t array_task_id;		/* task_id of a job array or NO_VAL */
	uint32_t het_job_offset;	/* het_job_offset or NO_VAL */
	slurm_step_id_t step_id;
} slurm_selected_step_t;

typedef slurm_selected_step_t step_alloc_info_msg_t;

typedef struct acct_gather_node_resp_msg {
	acct_gather_energy_t *energy;
	char *node_name;	  /* node name */
	uint16_t sensor_cnt;
} acct_gather_node_resp_msg_t;

typedef struct acct_gather_energy_req_msg {
	uint16_t context_id;
	uint16_t delta;
} acct_gather_energy_req_msg_t;

#define JOB_DEF_CPU_PER_GPU	0x0001
#define JOB_DEF_MEM_PER_GPU	0x0002
typedef struct job_defaults {
	uint16_t type;	/* See JOB_DEF_* above */
	uint64_t value;	/* Value */
} job_defaults_t;

/* Current partition state information and used to set partition options
 * using slurm_update_partition(). */
#define PART_FLAG_DEFAULT	SLURM_BIT(0) /* Set if default partition */
#define PART_FLAG_HIDDEN	SLURM_BIT(1) /* Set if partition is hidden */
#define PART_FLAG_NO_ROOT	SLURM_BIT(2) /* Set if user root jobs disabled */
#define PART_FLAG_ROOT_ONLY	SLURM_BIT(3) /* Set if only root can submit jobs */
#define PART_FLAG_REQ_RESV	SLURM_BIT(4) /* Set if reservation is required */
#define PART_FLAG_LLN		SLURM_BIT(5) /* Set if least loaded node selection
					      * is desired */
#define PART_FLAG_EXCLUSIVE_USER SLURM_BIT(6)/* Set if nodes allocated exclusively
					      * by user */
#define PART_FLAG_PDOI		SLURM_BIT(7) /* Set if nodes POWER_DOWN on IDLE,
					      * after running jobs */
/* Used with slurm_update_partition() to clear flags associated with existing
 * partitions. For example, if a partition is currently hidden and you want
 * to make it visible then set flags to PART_FLAG_HIDDEN_CLR and call
 * slurm_update_partition(). */
#define PART_FLAG_DEFAULT_CLR	SLURM_BIT(8)  /* Clear DEFAULT partition flag */
#define PART_FLAG_HIDDEN_CLR	SLURM_BIT(9)  /* Clear HIDDEN partition flag */
#define PART_FLAG_NO_ROOT_CLR	SLURM_BIT(10) /* Clear NO_ROOT partition flag */
#define PART_FLAG_ROOT_ONLY_CLR	SLURM_BIT(11) /* Clear ROOT_ONLY partition flag */
#define PART_FLAG_REQ_RESV_CLR	SLURM_BIT(12) /* Clear RES_REQ partition flag */
#define PART_FLAG_LLN_CLR	SLURM_BIT(13) /* Clear LLN partition flag */
#define PART_FLAG_EXC_USER_CLR	SLURM_BIT(14) /* Clear EXCLUSIVE_USER flag */
#define PART_FLAG_PDOI_CLR	SLURM_BIT(15) /* Clear PDOI partition flag */

typedef struct partition_info {
	char *allow_alloc_nodes;/* list names of allowed allocating
				 * nodes */
	char *allow_accounts;   /* comma delimited list of accounts,
				 * null indicates all */
	char *allow_groups;	/* comma delimited list of groups,
				 * null indicates all */
	char *allow_qos;	/* comma delimited list of qos,
				 * null indicates all */
	char *alternate; 	/* name of alternate partition */
	char *billing_weights_str;/* per TRES billing weights string */
	char *cluster_name;	/* Cluster name ONLY set in federation */
	uint16_t cr_type;	/* see CR_* values */
	uint32_t cpu_bind;	/* Default task binding */
	uint64_t def_mem_per_cpu; /* default MB memory per allocated CPU */
	uint32_t default_time;	/* minutes, NO_VAL or INFINITE */
	char *deny_accounts;    /* comma delimited list of denied accounts */
	char *deny_qos;		/* comma delimited list of denied qos */
	uint16_t flags;		/* see PART_FLAG_* above */
	uint32_t grace_time; 	/* preemption grace time in seconds */
	list_t *job_defaults_list; /* List of job_defaults_t elements */
	char *job_defaults_str;	/* String of job defaults,
				 * used only for partition update RPC */
	uint32_t max_cpus_per_node; /* maximum allocated CPUs per node */
	uint32_t max_cpus_per_socket; /* maximum allocated CPUs per socket */
	uint64_t max_mem_per_cpu; /* maximum MB memory per allocated CPU */
	uint32_t max_nodes;	/* per job or INFINITE */
	uint16_t max_share;	/* number of jobs to gang schedule */
	uint32_t max_time;	/* minutes or INFINITE */
	uint32_t min_nodes;	/* per job */
	char *name;		/* name of the partition */
	int32_t *node_inx;	/* list index pairs into node_table:
				 * start_range_1, end_range_1,
				 * start_range_2, .., -1  */
	char *nodes;		/* list names of nodes in partition */
	char *nodesets;		/* list of nodesets used by partition */
	uint16_t over_time_limit; /* job's time limit can be exceeded by this
				   * number of minutes before cancellation */
	uint16_t preempt_mode;	/* See PREEMPT_MODE_* in slurm/slurm.h */
	uint16_t priority_job_factor;	/* job priority weight factor */
	uint16_t priority_tier;	/* tier for scheduling and preemption */
	char *qos_char;	        /* The partition QOS name */
	uint16_t resume_timeout; /* time required in order to perform a node
				  * resume operation */
	uint16_t state_up;	/* see PARTITION_ states above */
	uint32_t suspend_time;  /* node idle for this long before power save
				 * mode */
	uint16_t suspend_timeout; /* time required in order to perform a node
				   * suspend operation */
	uint32_t total_cpus;	/* total number of cpus in the partition */
	uint32_t total_nodes;	/* total number of nodes in the partition */
	char    *tres_fmt_str;	/* str of configured TRES in partition */
} partition_info_t;

typedef struct delete_partition_msg {
	char *name;		/* name of partition to be delete */
} delete_part_msg_t;

typedef struct resource_allocation_response_msg {
	char *account;          /* allocation account */
	uint32_t job_id;	/* assigned job id */
	char *alias_list;	/* node name/address/hostname aliases */
	char *batch_host;	/* host executing batch script */
	uint32_t cpu_freq_min;  /* Minimum cpu frequency  */
	uint32_t cpu_freq_max;  /* Maximum cpu frequency  */
	uint32_t cpu_freq_gov;  /* cpu frequency governor */
	uint16_t *cpus_per_node;/* cpus per node */
	uint32_t *cpu_count_reps;/* how many nodes have same cpu count */
	uint32_t env_size;	/* element count in environment */
	char **environment;	/* environment variables to set for job,
				 *  name=value pairs, one per line */
	uint32_t error_code;	/* error code for warning message */
	gid_t gid; /* resolved group id of job */
	char *group_name; /* resolved group name of job */
	char *job_submit_user_msg;/* job_submit plugin user_msg */
	slurm_addr_t *node_addr;  /* network addresses */
	uint32_t node_cnt;	/* count of nodes */
	char *node_list;	/* assigned list of nodes */
	uint16_t ntasks_per_board;/* number of tasks to invoke on each board */
	uint16_t ntasks_per_core; /* number of tasks to invoke on each core */
	uint16_t ntasks_per_tres;/* number of tasks that can access each gpu */
	uint16_t ntasks_per_socket;/* number of tasks to invoke on
				    * each socket */
	uint32_t num_cpu_groups;/* size of cpus_per_node and cpu_count_reps */
	char *partition;	/* name of partition used to run job */
	uint64_t pn_min_memory;  /* minimum real memory per node OR
				  * real memory per CPU | MEM_PER_CPU,
				  * default=0 (no limit) */
	char *qos;               /* allocation qos */
	char *resv_name;         /* allocation reservation */
	char *tres_per_node; /* comma delimited list of TRES=# values */
	uid_t uid; /* resolved user id of job */
	char *user_name; /* resolved user name of job */
	void *working_cluster_rec; /* Cluster to direct remaining messages to.
				    * slurmdb_cluster_rec_t* because slurm.h
				    * doesn't know about slurmdb.h. */
} resource_allocation_response_msg_t;

typedef struct partition_info_msg {
	time_t last_update;	/* time of latest info */
	uint32_t record_count;	/* number of records */
	partition_info_t *partition_array; /* the partition records */
} partition_info_msg_t;

typedef struct will_run_response_msg {
	uint32_t job_id;	/* ID of job to start */
	char *job_submit_user_msg; /* job submit plugin user_msg */
	char *node_list;	/* nodes where job will start */
	char *part_name;	/* partition where job will start */
	list_t *preemptee_job_id; /* jobs preempted to start this job */
	uint32_t proc_cnt;	/* CPUs allocated to job at start */
	time_t start_time;	/* time when job will start */
	double sys_usage_per;	/* System usage percentage */
} will_run_response_msg_t;

/*********************************/

/*
 * Resource reservation data structures.
 * Create, show, modify and delete functions are required
 */
#define RESERVE_FLAG_MAINT	   SLURM_BIT(0)	 /* Set MAINT flag */
#define RESERVE_FLAG_NO_MAINT	   SLURM_BIT(1)	 /* Clear MAINT flag */
#define RESERVE_FLAG_DAILY	   SLURM_BIT(2)	 /* Set DAILY flag */
#define RESERVE_FLAG_NO_DAILY	   SLURM_BIT(3)	 /* Clear DAILY flag */
#define RESERVE_FLAG_WEEKLY	   SLURM_BIT(4)	 /* Set WEEKLY flag */
#define RESERVE_FLAG_NO_WEEKLY	   SLURM_BIT(5)	 /* Clear WEEKLY flag */
#define RESERVE_FLAG_IGN_JOBS	   SLURM_BIT(6)	 /* Ignore running jobs */
#define RESERVE_FLAG_NO_IGN_JOB	   SLURM_BIT(7)	 /* Clear ignore running
						  * jobs flag */
#define RESERVE_FLAG_ANY_NODES	   SLURM_BIT(8)	 /* Use any compute nodes */
#define RESERVE_FLAG_NO_ANY_NODES  SLURM_BIT(9)	 /* Clear any compute
						  * node flag */
#define RESERVE_FLAG_STATIC        SLURM_BIT(10) /* Static node allocation */
#define RESERVE_FLAG_NO_STATIC     SLURM_BIT(11) /* Clear static node
						  * allocation */
#define RESERVE_FLAG_PART_NODES	   SLURM_BIT(12) /* Use partition nodes only */
#define RESERVE_FLAG_NO_PART_NODES SLURM_BIT(13) /* Clear partition
						  * nodes only flag */
#define RESERVE_FLAG_OVERLAP	   SLURM_BIT(14) /* Permit to overlap others */
#define RESERVE_FLAG_SPEC_NODES	   SLURM_BIT(15) /* Contains specific nodes */
#define RESERVE_FLAG_FIRST_CORES   SLURM_BIT(16) /* Use only first cores
						  * on each node */
#define RESERVE_FLAG_TIME_FLOAT	   SLURM_BIT(17) /* Time offset is relative */
#define RESERVE_FLAG_REPLACE	   SLURM_BIT(18) /* Replace resources
						  * as assigned to jobs */
#define RESERVE_FLAG_ALL_NODES	   SLURM_BIT(19) /* Use all compute nodes */
#define RESERVE_FLAG_PURGE_COMP	   SLURM_BIT(20) /* Purge reservation
						  * after last job done */
#define RESERVE_FLAG_WEEKDAY	   SLURM_BIT(21) /* Set WEEKDAY flag */
#define RESERVE_FLAG_NO_WEEKDAY	   SLURM_BIT(22) /* Clear WEEKDAY flag */
#define RESERVE_FLAG_WEEKEND	   SLURM_BIT(23) /* Set WEEKEND flag */
#define RESERVE_FLAG_NO_WEEKEND	   SLURM_BIT(24) /* Clear WEEKEND flag */
#define RESERVE_FLAG_FLEX	   SLURM_BIT(25) /* Set FLEX flag */
#define RESERVE_FLAG_NO_FLEX	   SLURM_BIT(26) /* Clear FLEX flag */
#define RESERVE_FLAG_DUR_PLUS	   SLURM_BIT(27) /* Add duration time,
						  * only used on
						  * modifying a
						  * reservation */
#define RESERVE_FLAG_DUR_MINUS	   SLURM_BIT(28) /* Remove duration time,
						  * only used on
						  * modifying a
						  * reservation */
#define RESERVE_FLAG_NO_HOLD_JOBS  SLURM_BIT(29) /* No hold jobs after
						  * end of reservation */
#define RESERVE_FLAG_REPLACE_DOWN  SLURM_BIT(30) /* Replace DOWN or
						  * DRAINED nodes */
#define RESERVE_FLAG_NO_PURGE_COMP SLURM_BIT(31) /* Clear PURGE flag */

#define RESERVE_FLAG_MAGNETIC	   SLURM_BIT(32) /* Allow jobs to run
						  * without specifying
						  * the reservation name
						  * if they meet
						  * eligibility status */
#define RESERVE_FLAG_NO_MAGNETIC   SLURM_BIT(33) /* Clear MAGNETIC flag */
#define RESERVE_FLAG_SKIP	   SLURM_BIT(34) /* Skip/delete
						  * next/current
						  * reservation without
						  * deleting the
						  * reservation proper */
#define RESERVE_FLAG_HOURLY	   SLURM_BIT(35) /* Set HOURLY flag */
#define RESERVE_FLAG_NO_HOURLY	   SLURM_BIT(36) /* Clear HOURLY flag */

#define RESERVE_REOCCURRING	(RESERVE_FLAG_HOURLY | RESERVE_FLAG_DAILY | \
				 RESERVE_FLAG_WEEKLY | RESERVE_FLAG_WEEKDAY | \
				 RESERVE_FLAG_WEEKEND)

typedef struct resv_core_spec {
	char *node_name;	/* Name of reserved node */
	char *core_id;		/* IDs of reserved cores */
} resv_core_spec_t;

typedef struct reserve_info {
	char *accounts;		/* names of accounts permitted to use */
	char *burst_buffer;	/* burst buffer resources to be included */
	char *comment;		/* arbitrary comment */
	uint32_t core_cnt;	/* count of cores required */
	uint32_t core_spec_cnt;	/* count of core_spec records */
	resv_core_spec_t *core_spec; /* reserved cores specification */
	time_t end_time;	/* end time of reservation */
	char *features;		/* required node features */
	uint64_t flags;		/* see RESERVE_FLAG_* above */
	char *groups;		/* names of linux Groups permitted to use */
	char *licenses;		/* names of licenses to be reserved */
	uint32_t max_start_delay;/* Maximum delay in which jobs outside of the
				  * reservation will be permitted to overlap
				  * once any jobs are queued for the
				  * reservation */
	char *name;		/* name of reservation */
	uint32_t node_cnt;	/* count of nodes required */
	int32_t *node_inx;	/* list index pairs into node_table for *nodes:
				 * start_range_1, end_range_1,
				 * start_range_2, .., -1  */
	char *node_list;	/* list of reserved nodes or ALL */
	char *partition;	/* name of partition to be used */
	uint32_t purge_comp_time; /* If PURGE_COMP flag is set the amount of
				   * minutes this reservation will sit idle
				   * until it is revoked.
				   */
	time_t start_time;	/* start time of reservation */
	uint32_t resv_watts;    /* amount of power to reserve  */
	char *tres_str;         /* list of TRES's used by reservation */
	char *users;		/* names of users permitted to use */
} reserve_info_t;

typedef struct reserve_info_msg {
	time_t last_update;	/* time of latest info */
	uint32_t record_count;	/* number of records */
	reserve_info_t *reservation_array; /* the reservation records */
} reserve_info_msg_t;

typedef struct resv_desc_msg {
	char *accounts;		/* names of accounts permitted to use */
	char *burst_buffer;	/* burst buffer resources to be included */
	char *comment;		/* arbitrary comment */
	uint32_t *core_cnt;	/* Count of cores required */
	uint32_t duration;	/* duration of reservation in minutes */
	time_t end_time;	/* end time of reservation */
	char *features;		/* required node features */
	uint64_t flags;		/* see RESERVE_FLAG_* above */
	char *groups;		/* names of linux groups permitted to use */
	char *licenses;		/* names of licenses to be reserved */
	uint32_t max_start_delay;/* Maximum delay in which jobs outside of the
				  * reservation will be permitted to overlap
				  * once any jobs are queued for the
				  * reservation */
	char *name;		/* name of reservation (optional on create) */
	uint32_t *node_cnt;	/* Count of nodes required. Specify set of job
				 * sizes with trailing zero to optimize layout
				 * for those jobs just specify their total size
				 * to ignore optimized topology. For example,
				 * {512,512,1024,0} OR {2048,0}. */
	char *node_list;	/* list of reserved nodes or ALL */
	char *partition;	/* name of partition to be used */
	uint32_t purge_comp_time; /* If PURGE_COMP flag is set the amount of
				   * minutes this reservation will sit idle
				   * until it is revoked.
				   */
	time_t start_time;	/* start time of reservation */
	uint32_t resv_watts;    /* amount of power to reserve  */
	char *tres_str;         /* list of TRES's used by reservation */
	char *users;		/* names of users permitted to use */
} resv_desc_msg_t;

typedef struct reserve_response_msg {
	char *name;		/* name of reservation */
} reserve_response_msg_t;

typedef struct reservation_name_msg {
	char *name;		/* name of reservation just created or
				 * to be delete */
} reservation_name_msg_t;


#define DEBUG_FLAG_SELECT_TYPE	SLURM_BIT(0) /* SelectType plugin */
#define DEBUG_FLAG_STEPS	SLURM_BIT(1) /* slurmctld steps */
#define DEBUG_FLAG_TRIGGERS	SLURM_BIT(2) /* slurmctld triggers */
#define DEBUG_FLAG_CPU_BIND	SLURM_BIT(3) /* CPU binding */
#define DEBUG_FLAG_NET_RAW	SLURM_BIT(4) /* Raw Network dumps */
#define DEBUG_FLAG_NO_CONF_HASH	SLURM_BIT(5) /* no warning about
						    * slurm.conf files checksum
						    * mismatch */
#define DEBUG_FLAG_GRES		SLURM_BIT(6) /* Generic Resource info */
#define DEBUG_FLAG_MPI		SLURM_BIT(7) /* MPI debug */
#define DEBUG_FLAG_DATA 	SLURM_BIT(8) /* data_t logging */
#define DEBUG_FLAG_WORKQ 	SLURM_BIT(9) /* Work Queue */
#define DEBUG_FLAG_NET		SLURM_BIT(10) /* Network logging */
#define DEBUG_FLAG_PRIO 	SLURM_BIT(11) /* debug for priority
						    * plugin */
#define DEBUG_FLAG_BACKFILL	SLURM_BIT(12) /* debug for
						    * sched/backfill */
#define DEBUG_FLAG_GANG		SLURM_BIT(13) /* debug gang scheduler */
#define DEBUG_FLAG_RESERVATION	SLURM_BIT(14) /* advanced reservations */
#define DEBUG_FLAG_FRONT_END	SLURM_BIT(15) /* front-end nodes */
/* #define 		 	SLURM_BIT(16) /\* UNUSED *\/ */
#define DEBUG_FLAG_SWITCH	SLURM_BIT(17) /* SwitchType plugin */
#define DEBUG_FLAG_ENERGY	SLURM_BIT(18) /* AcctGatherEnergy plugin */
#define DEBUG_FLAG_EXT_SENSORS	SLURM_BIT(19) /* ExtSensorsType plugin */
#define DEBUG_FLAG_LICENSE	SLURM_BIT(20) /* AcctGatherProfile
						    * plugin */
#define DEBUG_FLAG_PROFILE	SLURM_BIT(21) /* AcctGatherProfile
						    * plugin */
#define DEBUG_FLAG_INTERCONNECT	SLURM_BIT(22) /* AcctGatherInterconnect
						    * plugin */
/* #define 		 	SLURM_BIT(23) /\* UNUSED *\/ */
#define DEBUG_FLAG_JOB_CONT 	SLURM_BIT(24) /* JobContainer plugin */
/* #define			SLURM_BIT(25) /\* UNUSED *\/ */
#define DEBUG_FLAG_PROTOCOL	SLURM_BIT(26) /* Communication protocol */
#define DEBUG_FLAG_BACKFILL_MAP	SLURM_BIT(27) /* Backfill scheduler node
						    * map */
#define DEBUG_FLAG_TRACE_JOBS   SLURM_BIT(28) /* Trace jobs by id
						    * and state */
#define DEBUG_FLAG_ROUTE 	SLURM_BIT(29) /* Route plugin */
#define DEBUG_FLAG_DB_ASSOC     SLURM_BIT(30) /* Association debug */
#define DEBUG_FLAG_DB_EVENT     SLURM_BIT(31) /* Event debug */
#define DEBUG_FLAG_DB_JOB       SLURM_BIT(32) /* Database job debug */
#define DEBUG_FLAG_DB_QOS       SLURM_BIT(33) /* QOS debug */
#define DEBUG_FLAG_DB_QUERY     SLURM_BIT(34) /* Database query debug */
#define DEBUG_FLAG_DB_RESV      SLURM_BIT(35) /* Reservation debug */
#define DEBUG_FLAG_DB_RES       SLURM_BIT(36) /* Resource debug */
#define DEBUG_FLAG_DB_STEP      SLURM_BIT(37) /* Database step debug */
#define DEBUG_FLAG_DB_USAGE     SLURM_BIT(38) /* Usage/Rollup debug */
#define DEBUG_FLAG_DB_WCKEY     SLURM_BIT(39) /* Database WCKey debug */
#define DEBUG_FLAG_BURST_BUF    SLURM_BIT(40) /* Burst buffer plugin */
#define DEBUG_FLAG_CPU_FREQ     SLURM_BIT(41) /* --cpu_freq debug */
#define DEBUG_FLAG_POWER        SLURM_BIT(42) /* Power plugin debug */
#define DEBUG_FLAG_TIME_CRAY    SLURM_BIT(43) /* Time Cray components */
#define DEBUG_FLAG_DB_ARCHIVE	SLURM_BIT(44) /* DBD Archiving/Purging */
#define DEBUG_FLAG_DB_TRES      SLURM_BIT(45) /* Database TRES debug */
#define DEBUG_FLAG_JOBCOMP      SLURM_BIT(46) /* JobComp debug */
#define DEBUG_FLAG_NODE_FEATURES SLURM_BIT(47) /* Node Features debug */
#define DEBUG_FLAG_FEDR         SLURM_BIT(48) /* Federation debug */
#define DEBUG_FLAG_HETJOB	SLURM_BIT(49) /* Heterogeneous job debug */
#define DEBUG_FLAG_ACCRUE       SLURM_BIT(50) /* Accrue counters debug */
/* #define     		 	SLURM_BIT(51) /\* UNUSED *\/ */
#define DEBUG_FLAG_AGENT	SLURM_BIT(52) /* RPC Agent debug */
#define DEBUG_FLAG_DEPENDENCY	SLURM_BIT(53) /* Dependency debug */
#define DEBUG_FLAG_JAG		SLURM_BIT(54) /* Job Account Gather debug */
#define DEBUG_FLAG_CGROUP	SLURM_BIT(55) /* cgroup debug */
#define DEBUG_FLAG_SCRIPT	SLURM_BIT(56) /* slurmscriptd debug */

#define PREEMPT_MODE_OFF	0x0000	/* disable job preemption */
#define PREEMPT_MODE_SUSPEND	0x0001	/* suspend jobs to preempt */
#define PREEMPT_MODE_REQUEUE	0x0002	/* requeue or kill jobs to preempt */

#define PREEMPT_MODE_CANCEL	0x0008	/* always cancel the job */
#define PREEMPT_MODE_COND_OFF	0x0010	/* represents PREEMPT_MODE_OFF in list*/
#define PREEMPT_MODE_WITHIN	0x4000	/* enable preemption within qos */
#define PREEMPT_MODE_GANG	0x8000	/* enable gang scheduling */

#define RECONFIG_KEEP_PART_INFO SLURM_BIT(0) /* keep dynamic partition info on scontrol reconfig */
#define RECONFIG_KEEP_PART_STAT SLURM_BIT(1) /* keep dynamic partition state on scontrol reconfig */
#define RECONFIG_KEEP_POWER_SAVE_SETTINGS SLURM_BIT(2) /* keep dynamic power save settings on scontrol reconfig */

#define HEALTH_CHECK_NODE_IDLE	0x0001	/* execute on idle nodes */
#define HEALTH_CHECK_NODE_ALLOC	0x0002	/* execute on fully allocated nodes */
#define HEALTH_CHECK_NODE_MIXED	0x0004	/* execute on partially allocated nodes */
#define HEALTH_CHECK_NODE_NONDRAINED_IDLE 0x0008 /* execute on idle nodes that
						  * are not drained */
#define HEALTH_CHECK_CYCLE	0x8000	/* cycle through nodes node */
#define HEALTH_CHECK_NODE_ANY	0x000f	/* execute on all node states */

#define PROLOG_FLAG_ALLOC	0x0001 /* execute prolog upon allocation */
#define PROLOG_FLAG_NOHOLD	0x0002 /* don't block salloc/srun until
					* slurmctld knows the prolog has
					* run on each node in the allocation */
#define PROLOG_FLAG_CONTAIN 	0x0004 /* Use proctrack plugin to create a
					* container upon allocation */
#define PROLOG_FLAG_SERIAL 	0x0008 /* serially execute prolog/epilog */
#define PROLOG_FLAG_X11		0x0010 /* enable slurm x11 forwarding support */
#define PROLOG_FLAG_DEFER_BATCH	0x0020 /* defer REQUEST_BATCH_JOB_LAUNCH until prolog end on all nodes */
#define PROLOG_FLAG_FORCE_REQUEUE_ON_FAIL 0x0040 /* always requeue job on prolog failure */

#define CTL_CONF_OR             SLURM_BIT(0) /*SlurmdParameters=config_overrides*/
#define CTL_CONF_SJC            SLURM_BIT(1) /* AccountingStoreFlags=job_comment*/
#define CTL_CONF_DRJ            SLURM_BIT(2) /* DisableRootJobs */
#define CTL_CONF_ASRU           SLURM_BIT(3) /* AllowSpecResourcesUsage */
#define CTL_CONF_PAM            SLURM_BIT(4) /* UsePam */
#define CTL_CONF_WCKEY          SLURM_BIT(5) /* TrackWCKey */
#define CTL_CONF_IPV4_ENABLED   SLURM_BIT(6) /* IPv4 is enabled */
#define CTL_CONF_IPV6_ENABLED   SLURM_BIT(7) /* IPv6 is enabled */
#define CTL_CONF_SJX            SLURM_BIT(8) /* AccountingStoreFlags=job_extra */
#define CTL_CONF_SJS            SLURM_BIT(9) /* AccountingStoreFlags=job_script */
#define CTL_CONF_SJE            SLURM_BIT(10) /* AccountingStoreFlags=job_env */

#define LOG_FMT_ISO8601_MS      0
#define LOG_FMT_ISO8601         1
#define LOG_FMT_RFC5424_MS      2
#define LOG_FMT_RFC5424         3
#define LOG_FMT_CLOCK           4
#define LOG_FMT_SHORT           5
#define LOG_FMT_THREAD_ID       6
#define LOG_FMT_RFC3339         7

/*
 * If adding to slurm_conf_t contents that need to be used in the slurmstepd
 * please remember to add those to [un]pack_slurm_conf_lite() in
 * src/slurmd/common/slurmstepd_init.[h|c]
 */
typedef struct {
	time_t last_update;	/* last update time of the build parameters */
	char *accounting_storage_tres; /* list of tres */
	uint16_t accounting_storage_enforce; /* job requires valid association:
					      * user/account/partition/cluster */
	char *accounting_storage_backup_host;	/* accounting storage
						 * backup host */
	char *accounting_storage_ext_host; /* accounting storage ext host */
	char *accounting_storage_host;	/* accounting storage host */
	char *accounting_storage_params; /* accounting storage params */
	char *accounting_storage_pass;	/* accounting storage
					 * password */
	uint16_t accounting_storage_port;/* node accounting storage port */
	char *accounting_storage_type; /* accounting storage type */
	char *accounting_storage_user; /* accounting storage user */
	void *acct_gather_conf; /* account gather config */
	char *acct_gather_energy_type; /* energy accounting type */
	char *acct_gather_profile_type; /* profile accounting type */
	char *acct_gather_interconnect_type; /* interconnect accounting type */
	char *acct_gather_filesystem_type; /* filesystem accounting type */
	uint16_t acct_gather_node_freq; /* secs between node acct request */
	char *authalttypes;	/* alternate authentication types */
	char *authinfo;		/* authentication info */
	char *authalt_params;   /* alternate authentication parameters */
	char *authtype;		/* authentication type */
	uint16_t batch_start_timeout;	/* max secs for batch job to start */
	char *bb_type;		/* burst buffer plugin type */
	char *bcast_exclude;	/* Bcast exclude library paths */
	char *bcast_parameters; /* bcast options */
	time_t boot_time;	/* time slurmctld last booted */
	void *cgroup_conf;	/* cgroup support config file */
	char *cli_filter_plugins; /* List of cli_filter plugins to use */
	char *core_spec_plugin;	/* core specialization plugin name */
	char *cluster_name;     /* general name of the entire cluster */
	char *comm_params;     /* Communication parameters */
	uint16_t complete_wait;	/* seconds to wait for job completion before
				 * scheduling another job */
	uint32_t conf_flags;   	/* various CTL_CONF_* flags to determine
				 * settings */
	char **control_addr;	/* comm path of slurmctld
				 * primary server and backups */
	uint32_t control_cnt;	/* Length of control_addr & control_machine */
	char **control_machine;	/* name of slurmctld primary
				 * server and backups */
	uint32_t cpu_freq_def;	/* default cpu frequency / governor */
	uint32_t cpu_freq_govs;	/* cpu freq governors allowed */
	char *cred_type;	/* credential signature plugin */
	uint64_t debug_flags;	/* see DEBUG_FLAG_* above for values */
	uint64_t def_mem_per_cpu; /* default MB memory per allocated CPU */
	char *dependency_params; /* DependencyParameters */
	uint16_t eio_timeout;     /* timeout for the eio thread */
	uint16_t enforce_part_limits;	/* if set, reject job exceeding
					 * partition size and/or time limits */
	char *epilog;		/* pathname of job epilog */
	uint32_t epilog_msg_time;  /* usecs for slurmctld to process an
				    * epilog complete message */
	char *epilog_slurmctld;	/* pathname of job epilog run by slurmctld */
	char *ext_sensors_type; /* external sensors plugin type */
	uint16_t ext_sensors_freq; /* secs between ext sensors sampling */
	void *ext_sensors_conf; /* external sensors config file*/
	char *fed_params;       /* Federation parameters */
	uint32_t first_job_id;	/* first slurm generated job_id to assign */
	uint16_t fs_dampening_factor; /* dampening for Fairshare factor */
	uint16_t getnameinfo_cache_timeout; /* for getnameinfo() cache*/
	uint16_t get_env_timeout; /* timeout for srun --get-user-env option */
	char * gres_plugins;	/* list of generic resource plugins */
	uint16_t group_time;    /* update group time interval */
	uint16_t group_force;   /* update group/partition info even if no change
				 * detected */
	char *gpu_freq_def;	/* default GPU frequency / voltage */
	uint32_t hash_val;      /* Hash value of the slurm.conf file */
	uint16_t health_check_interval;	/* secs between health checks */
	uint16_t health_check_node_state; /* Node states on which to execute
				 * health check program, see
				 * HEALTH_CHECK_NODE_* above */
	char * health_check_program;	/* pathname of health check program */
	uint16_t inactive_limit;/* seconds of inactivity before a
				 * inactive resource allocation is released */
	char *interactive_step_opts; /* InteractiveStepOptions */
	char *job_acct_gather_freq; /* poll frequency for job accounting
					* gather plugins */
	char *job_acct_gather_type; /* job accounting gather type */
	char *job_acct_gather_params; /* job accounting gather parameters */
	uint16_t job_acct_oom_kill; /* Enforce mem limit at runtime y|n */
	char *job_comp_host;	/* job completion logging host */
	char *job_comp_loc;	/* job completion logging location */
	char *job_comp_params;	/* job completion parameters for plugin */
	char *job_comp_pass;	/* job completion storage password */
	uint32_t job_comp_port;	/* job completion storage port */
	char *job_comp_type;	/* job completion storage type */
	char *job_comp_user;	/* job completion storage user */
	char *job_container_plugin; /* job container plugin type */
	char *job_credential_private_key;	/* path to private key */
	char *job_credential_public_certificate;/* path to public certificate*/
	list_t *job_defaults_list; /* list of job_defaults_t elements */
	uint16_t job_file_append; /* if set, append to stdout/err file */
	uint16_t job_requeue;	/* If set, jobs get requeued on node failre */
	char *job_submit_plugins;  /* List of job_submit plugins to use */
	uint32_t keepalive_interval;  /* Interval between keepalive probes */
	uint32_t keepalive_probes;  /* Number of keepalive probe attempts */
	uint32_t keepalive_time;  /* Keep alive time for srun I/O sockets */
	uint16_t kill_on_bad_exit; /* If set, the job will be
				    * terminated immediately when one of
				    * the processes is aborted or crashed */
	uint16_t kill_wait;	/* seconds between SIGXCPU to SIGKILL
				 * on job termination */
	char *launch_params;	/* step launcher plugin options */
	char *licenses;		/* licenses available on this cluster */
	uint16_t log_fmt;       /* Log file timestamp format */
	char *mail_domain;	/* default domain to append to usernames */
	char *mail_prog;	/* pathname of mail program */
	uint32_t max_array_sz;	/* Maximum job array size */
	uint32_t max_batch_requeue; /* maximum number of requeues */
	uint32_t max_dbd_msgs;	/* maximum number of messages queued while DBD
				 * is not connected */
	uint32_t max_job_cnt;	/* maximum number of active jobs */
	uint32_t max_job_id;	/* maximum job id before using first_job_id */
	uint64_t max_mem_per_cpu; /* maximum MB memory per allocated CPU */
	uint32_t max_node_cnt;  /* max number of static + dynamic nodes */
	uint32_t max_step_cnt;	/* maximum number of steps per job */
	uint16_t max_tasks_per_node; /* maximum tasks per node */
	char *mcs_plugin; /* mcs plugin type */
	char *mcs_plugin_params; /* mcs plugin parameters */
	uint32_t min_job_age;	/* COMPLETED jobs over this age (secs)
				 * purged from in memory records */
	void *mpi_conf;		/* MPI support config file */
	char *mpi_default;	/* Default version of MPI in use */
	char *mpi_params;	/* MPI parameters */
	uint16_t msg_timeout;	/* message timeout */
	uint32_t next_job_id;	/* next slurm generated job_id to assign */
	void *node_features_conf; /* Node Features Plugin config file */
	char *node_features_plugins; /* List of node_features plugins to use */
	char *node_prefix;      /* prefix of nodes in partition, only set in
				   bluegene clusters NULL otherwise */
	uint16_t over_time_limit; /* job's time limit can be exceeded by this
				   * number of minutes before cancellation */
	char *plugindir;	/* pathname to plugins */
	char *plugstack;        /* pathname to plugin stack config file */
	char *power_parameters;	/* power management parameters */
	char *power_plugin;	/* power management plugin type */
	uint32_t preempt_exempt_time; /* Time before jobs are preemptable */
	uint16_t preempt_mode;	/* See PREEMPT_MODE_* in slurm/slurm.h */
	char *preempt_params; /* PreemptParameters to tune preemption */
	char *preempt_type;	/* job preemption selection plugin */
	char *prep_params;	/* PrEp parameters */
	char *prep_plugins;	/* PrEp plugins */
	uint32_t priority_decay_hl; /* priority decay half life in
				     * seconds */
	uint32_t priority_calc_period; /* seconds between priority decay
					* calculation */
	uint16_t priority_favor_small; /* favor small jobs over large */
	uint16_t priority_flags; /* set some flags for priority configuration,
				  * see PRIORITY_FLAGS_* above */
	uint32_t priority_max_age; /* time when not to add any more
				    * priority to a job if reached */
    	char *priority_params;	/* priority plugin parameters */
	uint16_t priority_reset_period; /* when to clear usage,
					 * see PRIORITY_RESET_* */
	char *priority_type;    /* priority type plugin */
	uint32_t priority_weight_age; /* weight for age factor */
	uint32_t priority_weight_assoc; /* weight for assoc factor */
	uint32_t priority_weight_fs; /* weight for Fairshare factor */
	uint32_t priority_weight_js; /* weight for Job Size factor */
	uint32_t priority_weight_part; /* weight for Partition factor */
	uint32_t priority_weight_qos; /* weight for QOS factor */
	char    *priority_weight_tres; /* weights (str) for different TRES' */
	uint16_t private_data;	/* block viewing of information,
				 * see PRIVATE_DATA_* */
	char *proctrack_type;	/* process tracking plugin type */
	char *prolog;		/* pathname of job prolog run by slurmd */
	uint16_t prolog_epilog_timeout; /* prolog/epilog timeout */
	char *prolog_slurmctld;	/* pathname of job prolog run by slurmctld */
	uint16_t propagate_prio_process; /* process priority propagation,
					  * see PROP_PRIO_* */
	uint16_t prolog_flags; /* set some flags for prolog configuration
				  see PROLOG_FLAG_* */
	char *propagate_rlimits;/* Propagate (all/specific) resource limits */
	char *propagate_rlimits_except;/* Propagate all rlimits except these */
	char *reboot_program;	/* program to reboot the node */
	uint16_t reconfig_flags;/* see RECONFIG_* */
	char *requeue_exit;      /* requeue exit values */
	char *requeue_exit_hold; /* requeue exit hold values */
	char *resume_fail_program; /* program to handle failed resume tries */
	char *resume_program;	/* program to make nodes full power */
	uint16_t resume_rate;	/* nodes to make full power, per minute */
	uint16_t resume_timeout;/* time required in order to perform a node
				 * resume operation */
	char *resv_epilog;	/* path of reservation epilog run by slurmctld */
	uint16_t resv_over_run;	/* how long a running job can exceed
				 * reservation time */
	char *resv_prolog;	/* path of reservation prolog run by slurmctld */
	uint16_t ret2service;	/* 1 return DOWN node to service at
				 * registration */
	char *route_plugin;     /* route plugin */
	char *sched_logfile;    /* where slurm Scheduler log gets written */
	uint16_t sched_log_level;  /* configured level of slurm Scheduler log */
	char *sched_params;	/* SchedulerParameters OR
				 * contents of scheduler plugin config file */
	uint16_t sched_time_slice;	/* gang scheduler slice time, secs */
	char *schedtype;	/* type of scheduler to use */
	char *scron_params;	/* ScronParameters */
	char *select_type;	/* type of node selector to use */
	void *select_conf_key_pairs; /* key-pair list which can be
				      * listed with slurm_print_key_pairs() */
	uint16_t select_type_param; /* Parameters
				     * describing the select_type plugin */
	char *site_factor_plugin; /* PrioritySiteFactorPlugin */
	char *site_factor_params; /* PrioritySiteFactorParameters */
	char *slurm_conf;	/* pathname of slurm config file */
	uint32_t slurm_user_id;	/* uid of slurm_user_name */
	char *slurm_user_name;	/* user that slurmctld runs as */
	uint32_t slurmd_user_id;/* uid of slurmd_user_name */
	char *slurmd_user_name;	/* user that slurmd runs as */
	char *slurmctld_addr;	/* Address used for communications to the
				 * currently active slurmctld daemon */
	uint16_t slurmctld_debug; /* slurmctld logging level */
	char *slurmctld_logfile;/* where slurmctld error log gets written */
	char *slurmctld_pidfile;/* where to put slurmctld pidfile         */
	uint32_t slurmctld_port;  /* default communications port to slurmctld */
	uint16_t slurmctld_port_count; /* number of slurmctld comm ports */
	char *slurmctld_primary_off_prog; /* Run when becomes slurmctld backup */
	char *slurmctld_primary_on_prog;  /* Run when becomes slurmctld primary */
	uint16_t slurmctld_syslog_debug; /* slurmctld output to
					  * local logfile and syslog*/
	uint16_t slurmctld_timeout;/* seconds that backup controller waits
				    * on non-responding primarly controller */
	char *slurmctld_params;	/* SlurmctldParameters */
	uint16_t slurmd_debug;	/* slurmd logging level */
	char *slurmd_logfile;	/* where slurmd error log gets written */
	char *slurmd_params;	/* SlurmdParameters */
	char *slurmd_pidfile;   /* where to put slurmd pidfile           */
	uint32_t slurmd_port;	/* default communications port to slurmd */
	char *slurmd_spooldir;	/* where slurmd put temporary state info */
	uint16_t slurmd_syslog_debug; /* slurmd output to
				       * local logfile and syslog*/
	uint16_t slurmd_timeout;/* how long slurmctld waits for slurmd before
				 * considering node DOWN */
	char *srun_epilog;      /* srun epilog program */
	uint16_t *srun_port_range; /* port range for srun */
	char *srun_prolog;      /* srun prolog program */
	char *state_save_location;/* pathname of slurmctld state save
				   * directory */
	char *suspend_exc_nodes;/* nodes to not make power saving */
	char *suspend_exc_parts;/* partitions to not make power saving */
	char *suspend_exc_states; /* states that should not be powered down */
	char *suspend_program;	/* program to make nodes power saving */
	uint16_t suspend_rate;	/* nodes to make power saving, per minute */
	uint32_t suspend_time;	/* node idle for this long before power save mode */
	uint16_t suspend_timeout;/* time required in order to perform a node
				  * suspend operation */
	char *switch_type;	/* switch or interconnect type */
	char *switch_param;	/* SwitchParameters */
	char *task_epilog;	/* pathname of task launch epilog */
	char *task_plugin;	/* task launch plugin */
	uint32_t task_plugin_param;	/* see CPU_BIND_* */
	char *task_prolog;	/* pathname of task launch prolog */
	uint16_t tcp_timeout;	/* tcp timeout */
	char *tmp_fs;		/* pathname of temporary file system */
	char *topology_param;	/* network topology parameters */
	char *topology_plugin;	/* network topology plugin */
	uint16_t tree_width;    /* number of threads per node to span */
	char *unkillable_program; /* program run by the slurmstepd when
				   * processes in a job step are unkillable */
	uint16_t unkillable_timeout; /* time in seconds, after processes in a
				      * job step have been signaled, before
				      * they are considered "unkillable". */
	char *version;		/* version of slurmctld */
	uint16_t vsize_factor;	/* virtual memory limit size factor */
	uint16_t wait_time;	/* default job --wait time */
	char *x11_params;	/* X11Parameters */
} slurm_conf_t;

typedef struct slurmd_status_msg {
	time_t booted;			/* when daemon was started */
	time_t last_slurmctld_msg;	/* time of last slurmctld message */
	uint16_t slurmd_debug;		/* logging level */
	uint16_t actual_cpus;		/* actual logical processor count */
	uint16_t actual_boards;	 	/* actual total boards count      */
	uint16_t actual_sockets;	/* actual total sockets count     */
	uint16_t actual_cores;		/* actual core per socket count   */
	uint16_t actual_threads;	/* actual thread per core count */
	uint64_t actual_real_mem;	/* actual real memory in MB */
	uint32_t actual_tmp_disk;	/* actual temp disk space in MB */
	uint32_t pid;			/* process ID */
	char *hostname;			/* local hostname */
	char *slurmd_logfile;		/* slurmd log file location */
	char *step_list;		/* list of active job steps */
	char *version;			/* version running */
} slurmd_status_t;

typedef struct submit_response_msg {
	uint32_t job_id;	/* job ID */
	uint32_t step_id;	/* step ID */
	uint32_t error_code;	/* error code for warning message */
	char *job_submit_user_msg; /* job submit plugin user_msg */
} submit_response_msg_t;

/* NOTE: If setting node_addr and/or node_hostname then comma separate names
 * and include an equal number of node_names */
typedef struct slurm_update_node_msg {
	char *comment;		/* arbitrary comment */
	uint32_t cpu_bind;	/* default CPU binding type */
	char *extra;		/* arbitrary string */
	char *features;		/* new available feature for node */
	char *features_act;	/* new active feature for node */
	char *gres;		/* new generic resources for node */
	char *node_addr;	/* communication name (optional) */
	char *node_hostname;	/* node's hostname (optional) */
	char *node_names;	/* nodelist expression */
	uint32_t node_state;	/* see enum node_states */
	char *reason;		/* reason for node being DOWN or DRAINING */
	uint32_t reason_uid;	/* user ID of sending (needed if user
				 * root is sending message) */
	uint32_t resume_after;	/* automatically resume DOWN or DRAINED node
				 * after this amount of seconds */
	uint32_t weight;	/* new weight for node */
} update_node_msg_t;

typedef struct slurm_update_front_end_msg {
	char *name;		/* comma separated list of front end nodes */
	uint32_t node_state;	/* see enum node_states */
	char *reason;		/* reason for node being DOWN or DRAINING */
	uint32_t reason_uid;	/* user ID of sending (needed if user
				 * root is sending message) */
} update_front_end_msg_t;

typedef struct partition_info update_part_msg_t;

typedef struct job_sbcast_cred_msg {
	uint32_t      job_id;		/* assigned job id */
	char         *node_list;	/* assigned list of nodes */
	sbcast_cred_t *sbcast_cred;	/* opaque data structure */
} job_sbcast_cred_msg_t;

typedef struct {
	uint32_t lifespan;
	char *username;
} token_request_msg_t;

typedef struct {
	char *token;
} token_response_msg_t;

/* Opaque data type for slurm_step_ctx_* functions */
typedef struct slurm_step_ctx_struct slurm_step_ctx_t;

#define STAT_COMMAND_RESET	0x0000
#define STAT_COMMAND_GET	0x0001
typedef struct stats_info_request_msg {
	uint16_t command_id;
} stats_info_request_msg_t;

typedef struct stats_info_response_msg {
	uint32_t parts_packed;
	time_t req_time;
	time_t req_time_start;
	uint32_t server_thread_count;
	uint32_t agent_queue_size;
	uint32_t agent_count;
	uint32_t agent_thread_count;
	uint32_t dbd_agent_queue_size;
	uint32_t gettimeofday_latency;

	uint32_t schedule_cycle_max;
	uint32_t schedule_cycle_last;
	uint32_t schedule_cycle_sum;
	uint32_t schedule_cycle_counter;
	uint32_t schedule_cycle_depth;
	uint32_t schedule_queue_len;

	uint32_t jobs_submitted;
	uint32_t jobs_started;
	uint32_t jobs_completed;
	uint32_t jobs_canceled;
	uint32_t jobs_failed;

	uint32_t jobs_pending;
	uint32_t jobs_running;
	time_t   job_states_ts;

	uint32_t bf_backfilled_jobs;
	uint32_t bf_last_backfilled_jobs;
	uint32_t bf_backfilled_het_jobs;
	uint32_t bf_cycle_counter;
	uint64_t bf_cycle_sum;
	uint32_t bf_cycle_last;
	uint32_t bf_cycle_max;
	uint32_t bf_last_depth;
	uint32_t bf_last_depth_try;
	uint32_t bf_depth_sum;
	uint32_t bf_depth_try_sum;
	uint32_t bf_queue_len;
	uint32_t bf_queue_len_sum;
	uint32_t bf_table_size;
	uint32_t bf_table_size_sum;
	time_t   bf_when_last_cycle;
	uint32_t bf_active;

	uint32_t rpc_type_size;
	uint16_t *rpc_type_id;
	uint32_t *rpc_type_cnt;
	uint64_t *rpc_type_time;

	uint32_t rpc_user_size;
	uint32_t *rpc_user_id;
	uint32_t *rpc_user_cnt;
	uint64_t *rpc_user_time;

	uint32_t rpc_queue_type_count;
	uint32_t *rpc_queue_type_id;
	uint32_t *rpc_queue_count;

	uint32_t rpc_dump_count;
	uint32_t *rpc_dump_types;
	char **rpc_dump_hostlist;
} stats_info_response_msg_t;

#define TRIGGER_FLAG_PERM		0x0001

#define TRIGGER_RES_TYPE_JOB            0x0001
#define TRIGGER_RES_TYPE_NODE           0x0002
#define TRIGGER_RES_TYPE_SLURMCTLD      0x0003
#define TRIGGER_RES_TYPE_SLURMDBD       0x0004
#define TRIGGER_RES_TYPE_DATABASE       0x0005
#define TRIGGER_RES_TYPE_FRONT_END      0x0006
#define TRIGGER_RES_TYPE_OTHER          0x0007

#define TRIGGER_TYPE_UP                 SLURM_BIT(0)
#define TRIGGER_TYPE_DOWN               SLURM_BIT(1)
#define TRIGGER_TYPE_FAIL               SLURM_BIT(2)
#define TRIGGER_TYPE_TIME               SLURM_BIT(3)
#define TRIGGER_TYPE_FINI               SLURM_BIT(4)
#define TRIGGER_TYPE_RECONFIG           SLURM_BIT(5)
/*                                      SLURM_BIT(6), UNUSED */
#define TRIGGER_TYPE_IDLE               SLURM_BIT(7)
#define TRIGGER_TYPE_DRAINED            SLURM_BIT(8)
#define TRIGGER_TYPE_PRI_CTLD_FAIL      SLURM_BIT(9)
#define TRIGGER_TYPE_PRI_CTLD_RES_OP    SLURM_BIT(10)
#define TRIGGER_TYPE_PRI_CTLD_RES_CTRL  SLURM_BIT(11)
#define TRIGGER_TYPE_PRI_CTLD_ACCT_FULL SLURM_BIT(12)
#define TRIGGER_TYPE_BU_CTLD_FAIL       SLURM_BIT(13)
#define TRIGGER_TYPE_BU_CTLD_RES_OP     SLURM_BIT(14)
#define TRIGGER_TYPE_BU_CTLD_AS_CTRL    SLURM_BIT(15)
#define TRIGGER_TYPE_PRI_DBD_FAIL       SLURM_BIT(16)
#define TRIGGER_TYPE_PRI_DBD_RES_OP     SLURM_BIT(17)
#define TRIGGER_TYPE_PRI_DB_FAIL        SLURM_BIT(18)
#define TRIGGER_TYPE_PRI_DB_RES_OP      SLURM_BIT(19)
#define TRIGGER_TYPE_BURST_BUFFER       SLURM_BIT(20)
#define TRIGGER_TYPE_DRAINING           SLURM_BIT(21)
#define TRIGGER_TYPE_RESUME             SLURM_BIT(22)


typedef struct trigger_info {
	uint16_t flags;		/* TRIGGER_FLAG_* */
	uint32_t trig_id;	/* trigger ID */
	uint16_t res_type;	/* TRIGGER_RES_TYPE_* */
	char *   res_id;	/* resource ID */
	uint32_t control_inx;	/* controller index */
	uint32_t trig_type;	/* TRIGGER_TYPE_* */
	uint16_t offset;	/* seconds from trigger, 0x8000 origin */
	uint32_t user_id;	/* user requesting trigger */
	char *   program;	/* program to execute */
} trigger_info_t;

typedef struct trigger_info_msg {
	uint32_t record_count;		/* number of records */
	trigger_info_t *trigger_array;	/* the trigger records */
} trigger_info_msg_t;


/* Individual license information
 */
typedef struct slurm_license_info {
	char *name;          /* license name */
	uint32_t total;      /* total number of available licenses */
	uint32_t in_use;     /* number of license in use */
	uint32_t available;  /* number of available license */
	uint8_t remote;      /* non-zero if remote license (not
			      * defined in slurm.conf) */
	uint32_t reserved;   /* number of licenses reserved */
	uint32_t last_consumed; /* number of licenses last known to be
				   consumed in the license manager
				   (for remote) */
	uint32_t last_deficit;
	time_t last_update;  /* last updated (for remote) */
} slurm_license_info_t;

/* License information array as returned by the controller.
 */
typedef struct license_info_msg {
	time_t last_update;
	uint32_t num_lic;
	slurm_license_info_t *lic_array;
} license_info_msg_t;

typedef struct {
	uint32_t  job_array_count;
	char    **job_array_id; /* Note: The string may be truncated */
	uint32_t *error_code;
	char **err_msg;
} job_array_resp_msg_t;

/* Association manager state running in the slurmctld */
typedef struct {
	list_t *assoc_list; /* list of slurmdb_assoc_rec_t with usage packed */
	list_t *qos_list; /* list of slurmdb_qos_rec_t with usage packed */
	uint32_t tres_cnt;
	char **tres_names;
	list_t *user_list; /* list of slurmdb_user_rec_t */
} assoc_mgr_info_msg_t;

#define ASSOC_MGR_INFO_FLAG_ASSOC 0x00000001
#define ASSOC_MGR_INFO_FLAG_USERS 0x00000002
#define ASSOC_MGR_INFO_FLAG_QOS   0x00000004

typedef struct {
	list_t *acct_list; /* char * list of account names */
	uint32_t flags; /* flags determining what is returned */
	list_t *qos_list; /* char * list of qos names */
	list_t *user_list; /* char * list of user names */
} assoc_mgr_info_request_msg_t;

typedef struct network_callerid_msg {
	unsigned char ip_src[16];
	unsigned char ip_dst[16];
	uint32_t port_src;
	uint32_t port_dst;
	int32_t af;	/* NOTE: un/packed as uint32_t */
} network_callerid_msg_t;

/*****************************************************************************\
 *	RESOURCE ALLOCATION FUNCTIONS
\*****************************************************************************/

/*
 * slurm_init_job_desc_msg - initialize job descriptor with
 *	default values
 * OUT job_desc_msg - user defined job descriptor
 */
extern void slurm_init_job_desc_msg(job_desc_msg_t *job_desc_msg);

/*
 * slurm_allocate_resources - allocate resources for a job request
 *   If the requested resources are not immediately available, the slurmctld
 *   will send the job_alloc_resp_msg to the specified node and port.
 * IN job_desc_msg - description of resource allocation request
 * OUT job_alloc_resp_msg - response to request.  This only represents
 *   a job allocation if resources are immediately.  Otherwise it just contains
 *   the job id of the enqueued job request.
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 * NOTE: free the response using slurm_free_resource_allocation_response_msg()
 */
extern int slurm_allocate_resources(job_desc_msg_t *job_desc_msg,
				    resource_allocation_response_msg_t **job_alloc_resp_msg);

/*
 * slurm_allocate_resources_blocking
 *	allocate resources for a job request.  This call will block until
 *	the allocation is granted, or the specified timeout limit is reached.
 * IN req - description of resource allocation request
 * IN timeout - amount of time, in seconds, to wait for a response before
 * 	giving up.
 *	A timeout of zero will wait indefinitely.
 * IN pending_callback - If the allocation cannot be granted immediately,
 *      the controller will put the job in the PENDING state.  If
 *      pending callback is not NULL, it will be called with the job_id
 *      of the pending job as the sole parameter.
 *
 * RET allocation structure on success, NULL on error set errno to
 *	indicate the error (errno will be ETIMEDOUT if the timeout is reached
 *      with no allocation granted)
 * NOTE: free the response using slurm_free_resource_allocation_response_msg()
 */
extern resource_allocation_response_msg_t *slurm_allocate_resources_blocking(
	const job_desc_msg_t *user_req,
	time_t timeout,
	void (*pending_callback)(uint32_t job_id));

/*
 * slurm_free_resource_allocation_response_msg - free slurm resource
 *	allocation response message
 * IN msg - pointer to allocation response message
 * NOTE: buffer is loaded by slurm_allocate_resources
 */
extern void slurm_free_resource_allocation_response_msg(resource_allocation_response_msg_t *msg);

/*
 * slurm_allocate_het_job_blocking
 *	allocate resources for a list of job requests.  This call will block
 *	until the entire allocation is granted, or the specified timeout limit
 *	is reached.
 * IN job_req_list - list of resource allocation requests, type job_desc_msg_t
 * IN timeout - amount of time, in seconds, to wait for a response before
 * 	giving up.
 *	A timeout of zero will wait indefinitely.
 * IN pending_callback - If the allocation cannot be granted immediately,
 *      the controller will put the job in the PENDING state.  If
 *      pending callback is not NULL, it will be called with the job_id
 *      of the pending job as the sole parameter.
 *
 * RET list of allocation structures on success, NULL on error set errno to
 *	indicate the error (errno will be ETIMEDOUT if the timeout is reached
 *      with no allocation granted)
 * NOTE: free the response using list_destroy()
 */
extern list_t *slurm_allocate_het_job_blocking(
	list_t *job_req_list,
	time_t timeout,
	void(*pending_callback)(uint32_t job_id));

/*
 * slurm_allocation_lookup - retrieve info for an existing resource
 *			     allocation
 * IN job_id - job allocation identifier
 * OUT resp - job allocation information
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 * NOTE: free the response using slurm_free_resource_allocation_response_msg()
 */
extern int slurm_allocation_lookup(uint32_t job_id,
				   resource_allocation_response_msg_t **resp);

/*
 * slurm_het_job_lookup - retrieve info for an existing heterogeneous job
 * 			   allocation without the addrs and such
 * IN jobid - job allocation identifier
 * OUT resp - list of job allocation information, type
 *	      resource_allocation_response_msg_t
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 * NOTE: returns information an individual job as well
 * NOTE: free the response using list_destroy()
 */
extern int slurm_het_job_lookup(uint32_t jobid, list_t **resp);

/*
 * slurm_read_hostfile - Read a Slurm hostfile specified by "filename".
 *	"filename" must contain a list of Slurm NodeNames, one per line.
 *	Reads up to "n" number of hostnames from the file. Returns a
 *	string representing a hostlist ranged string of the contents of
 *	the file.  This is a helper function, it does not contact any
 *	Slurm daemons.
 *
 * IN filename - name of Slurm Hostlist file to be read.
 * IN n - number of NodeNames required
 * RET - a string representing the hostlist.  Returns NULL if there are
 *	fewer than "n" hostnames in the file, or if an error occurs.
 *
 * NOTE: Returned string must be freed with free().
 */
extern char *slurm_read_hostfile(const char *filename, int n);

/*
 * slurm_allocation_msg_thr_create - startup a message handler talking
 * with the controller dealing with messages from the controller during an
 * allocation.
 * IN port - port we are listening for messages on from the controller
 * IN callbacks - callbacks for different types of messages
 * RET allocation_msg_thread_t * or NULL on failure
 */
extern allocation_msg_thread_t *slurm_allocation_msg_thr_create(uint16_t *port,
				const slurm_allocation_callbacks_t *callbacks);

/*
 * slurm_allocation_msg_thr_destroy - shutdown the message handler talking
 * with the controller dealing with messages from the controller during an
 * allocation.
 * IN msg_thr - allocation_msg_thread_t pointer allocated with
 *              slurm_allocation_msg_thr_create
 */
extern void slurm_allocation_msg_thr_destroy(allocation_msg_thread_t *msg_thr);

/*
 * slurm_submit_batch_job - issue RPC to submit a job for later execution
 * NOTE: free the response using slurm_free_submit_response_response_msg
 * IN job_desc_msg - description of batch job request
 * OUT slurm_alloc_msg - response to request
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_submit_batch_job(job_desc_msg_t *job_desc_msg,
				  submit_response_msg_t **slurm_alloc_msg);

/*
 * slurm_submit_batch_het_job - issue RPC to submit a heterogeneous job for
 *				 later execution
 * NOTE: free the response using slurm_free_submit_response_response_msg
 * IN job_req_list - list of resource allocation requests, type job_desc_msg_t
 * OUT slurm_alloc_msg - response to request
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_submit_batch_het_job(list_t *job_req_list,
				      submit_response_msg_t **slurm_alloc_msg);

/*
 * slurm_free_submit_response_response_msg - free slurm
 *	job submit response message
 * IN msg - pointer to job submit response message
 * NOTE: buffer is loaded by slurm_submit_batch_job
 */
extern void slurm_free_submit_response_response_msg(submit_response_msg_t *msg);

/*
 * slurm_job_batch_script - retrieve the batch script for a given jobid
 * returns SLURM_SUCCESS, or appropriate error code
 */
extern int slurm_job_batch_script(FILE *out, uint32_t jobid);

/*
 * slurm_job_will_run - determine if a job would execute immediately if
 *	submitted now
 * IN job_desc_msg - description of resource allocation request
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_job_will_run(job_desc_msg_t *job_desc_msg);

/*
 * slurm_het_job_will_run - determine if a heterogeneous job would execute
 *	immediately if submitted now
 * IN job_req_list - list of job_desc_msg_t structures describing the resource
 *		allocation request
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_het_job_will_run(list_t *job_req_list);


/*
 * slurm_job_will_run2 - determine if a job would execute immediately if
 *      submitted now
 * IN job_desc_msg - description of resource allocation request
 * OUT will_run_resp - job run time data
 *      free using slurm_free_will_run_response_msg()
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_job_will_run2(job_desc_msg_t *req,
			       will_run_response_msg_t **will_run_resp);

/*
 * slurm_sbcast_lookup - retrieve info for an existing resource allocation
 *	including a credential needed for sbcast.
 * IN selected_step - filled in with step_id and het_job_offset
 * OUT info - job allocation information including a credential for sbcast
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 * NOTE: free the "resp" using slurm_free_sbcast_cred_msg
 */
extern int slurm_sbcast_lookup(slurm_selected_step_t *selected_step,
			       job_sbcast_cred_msg_t **info);

extern void slurm_free_sbcast_cred_msg(job_sbcast_cred_msg_t *msg);

/* slurm_load_licenses()
 *
 * Retrieve license information from the controller.
 * IN feature - feature name or NULL
 * OUT
 *
 */
extern int slurm_load_licenses(time_t, license_info_msg_t **, uint16_t);
extern void slurm_free_license_info_msg(license_info_msg_t *);

/* get the running assoc_mgr info
 * IN assoc_mgr_info_request_msg_t: request filtering data returned
 * OUT assoc_mgr_info_msg_t: returned structure filled in with
 * assoc_mgr lists, must be freed by slurm_free_assoc_mgr_info_msg
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_load_assoc_mgr_info(assoc_mgr_info_request_msg_t *,
				     assoc_mgr_info_msg_t **);
extern void slurm_free_assoc_mgr_info_msg(assoc_mgr_info_msg_t *);
extern void slurm_free_assoc_mgr_info_request_members(assoc_mgr_info_request_msg_t *);
extern void slurm_free_assoc_mgr_info_request_msg(assoc_mgr_info_request_msg_t *);


/*****************************************************************************\
 *	JOB/STEP SIGNALING FUNCTIONS
\*****************************************************************************/

typedef struct job_step_kill_msg {
	char *sjob_id;
	uint16_t signal;
	uint16_t flags;
	char *sibling;
	slurm_step_id_t step_id;
} job_step_kill_msg_t;

/*
 * NOTE:  See _signal_batch_job() controller and _rpc_signal_tasks() in slurmd.
 */
#define KILL_JOB_BATCH   SLURM_BIT(0) /* signal batch shell only */
#define KILL_JOB_ARRAY   SLURM_BIT(1) /* kill all elements of a job array */
#define KILL_STEPS_ONLY  SLURM_BIT(2) /* Do not signal batch script */
#define KILL_FULL_JOB    SLURM_BIT(3) /* Signal all steps, including batch
				       * script */
#define KILL_FED_REQUEUE SLURM_BIT(4) /* Mark job as requeued when requeued */
#define KILL_HURRY       SLURM_BIT(5) /* Skip burst buffer stage out */
#define KILL_OOM         SLURM_BIT(6) /* Kill due to Out-Of-Memory */
#define KILL_NO_SIBS     SLURM_BIT(7) /* Don't kill other sibling jobs */
#define KILL_JOB_RESV    SLURM_BIT(8) /* Job is willing to run on nodes in a
				       * magnetic reservation. */
#define KILL_NO_CRON SLURM_BIT(9) /* request killing cron Jobs */

/* Use top bit of uint16_t in conjuction with KILL_* flags to indicate signal
 * has been sent to job previously. Does not need to be passed to slurmd. */
#define WARN_SENT        SLURM_BIT(15) /* warn already sent, clear this on
					* requeue */

/*
 * slurm_kill_job - send the specified signal to all steps of an existing job
 * IN job_id     - the job's id
 * IN signal     - signal number
 * IN flags      - see KILL_JOB_* flags above
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_kill_job(uint32_t job_id, uint16_t signal, uint16_t flags);

/*
 * slurm_kill_job_step - send the specified signal to an existing job step
 * IN job_id  - the job's id
 * IN step_id - the job step's id
 * IN signal  - signal number
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_kill_job_step(uint32_t job_id,
			       uint32_t step_id,
			       uint16_t signal);
/*
 * slurm_kill_job2 - send REQUEST_KILL_JOB msg to an existing job or step.
 * IN job_id - the job's id (in a string format)
 * IN signal - signal to send
 * IN flags - see KILL_* flags above (such as KILL_JOB_BATCH)
 * IN sibling - optional string of sibling cluster to send the message to.
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_kill_job2(const char *job_id, uint16_t signal, uint16_t flags,
			   const char *sibling);

/*
 * slurm_signal_job - send the specified signal to all steps of an existing job
 * IN job_id     - the job's id
 * IN signal     - signal number
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_signal_job(uint32_t job_id, uint16_t signal);

/*
 * slurm_signal_job_step - send the specified signal to an existing job step
 * IN job_id  - the job's id
 * IN step_id - the job step's id - use SLURM_BATCH_SCRIPT as the step_id
 *              to send a signal to a job's batch script
 * IN signal  - signal number
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_signal_job_step(uint32_t job_id,
				 uint32_t step_id,
				 uint32_t signal);


/*****************************************************************************\
 *	JOB/STEP COMPLETION FUNCTIONS
\*****************************************************************************/

/*
 * slurm_complete_job - note the completion of a job and all of its steps
 * IN job_id - the job's id
 * IN job_return_code - the highest exit code of any task of the job
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_complete_job(uint32_t job_id, uint32_t job_return_code);

/*
 * slurm_terminate_job_step - terminates a job step by sending a
 * 	REQUEST_TERMINATE_TASKS rpc to all slurmd of a job step, and then
 *	calls slurm_complete_job_step() after verifying that all
 *	nodes in the job step no longer have running tasks from the job
 *	step.  (May take over 35 seconds to return.)
 * IN job_id  - the job's id
 * IN step_id - the job step's id - use SLURM_BATCH_SCRIPT as the step_id
 *              to terminate a job's batch script
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_terminate_job_step(uint32_t job_id, uint32_t step_id);

/*****************************************************************************\
 *	SLURM TASK SPAWNING FUNCTIONS
\*****************************************************************************/

/*
 * slurm_step_launch_params_t_init - initialize a user-allocated
 *      slurm_step_launch_params_t structure with default values.
 *	default values.  This function will NOT allocate any new memory.
 * IN ptr - pointer to a structure allocated by the use.  The structure will
 *      be initialized.
 */
extern void slurm_step_launch_params_t_init(slurm_step_launch_params_t *ptr);

/*
 * slurm_step_launch - launch a parallel job step
 * IN ctx - job step context generated by slurm_step_ctx_create
 * IN params - job step parameters
 * IN callbacks - Identify functions to be called when various events occur
 * IN het_job_step_cnt - Total count of hetjob steps to be launched, -1 otherwise
 * RET SLURM_SUCCESS or SLURM_ERROR (with errno set)
 */
extern int slurm_step_launch(slurm_step_ctx_t *ctx,
			     const slurm_step_launch_params_t *params,
			     const slurm_step_launch_callbacks_t *callbacks);

/*
 * slurm_step_launch_add - Add tasks to a step that was already started
 * IN ctx - job step context generated by slurm_step_ctx_create
 * IN first_ctx - job step context generated by slurm_step_ctx_create for
 *		first component of the job step
 * IN params - job step parameters
 * IN node_list - list of extra nodes to add
 * IN start_nodeid - in the global scheme which node id is the first
 *                   node in node_list.
 * RET SLURM_SUCCESS or SLURM_ERROR (with errno set)
 */
extern int slurm_step_launch_add(slurm_step_ctx_t *ctx,
				 slurm_step_ctx_t *first_ctx,
				 const slurm_step_launch_params_t *params,
				 char *node_list);

/*
 * Block until all tasks have started.
 */
extern int slurm_step_launch_wait_start(slurm_step_ctx_t *ctx);

/*
 * Block until all tasks have finished (or failed to start altogether).
 */
extern void slurm_step_launch_wait_finish(slurm_step_ctx_t *ctx);

/*
 * Abort an in-progress launch, or terminate the fully launched job step.
 *
 * Can be called from a signal handler.
 */
extern void slurm_step_launch_abort(slurm_step_ctx_t *ctx);

/*
 * Forward a signal to all those nodes with running tasks
 */
extern void slurm_step_launch_fwd_signal(slurm_step_ctx_t *ctx, int signo);

/*
 * Wake tasks stopped for debugging on nodes with running tasks
 */
extern void slurm_step_launch_fwd_wake(slurm_step_ctx_t *ctx);

/*****************************************************************************\
 *	SLURM CONTROL CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
\*****************************************************************************/

/*
 * slurm_api_version - Return a single number reflecting the Slurm API's
 *	version number. Use the macros SLURM_VERSION_NUM, SLURM_VERSION_MAJOR,
 *	SLURM_VERSION_MINOR, and SLURM_VERSION_MICRO to work with this value
 * RET API's version number
 */
extern long slurm_api_version(void);

/*
 * slurm_load_ctl_conf - issue RPC to get slurm control configuration
 *	information if changed since update_time
 * IN update_time - time of current configuration data
 * IN slurm_ctl_conf_ptr - place to store slurm control configuration
 *	pointer
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 * NOTE: free the response using slurm_free_ctl_conf
 */
extern int slurm_load_ctl_conf(time_t update_time,
                               slurm_conf_t **slurm_ctl_conf_ptr);

/*
 * slurm_free_ctl_conf - free slurm control information response message
 * IN msg - pointer to slurm control information response message
 * NOTE: buffer is loaded by slurm_load_ctl_conf
 */
extern void slurm_free_ctl_conf(slurm_conf_t *slurm_ctl_conf_ptr);

/*
 * slurm_print_ctl_conf - output the contents of slurm control configuration
 *	message as loaded using slurm_load_ctl_conf
 * IN out - file to write to
 * IN slurm_ctl_conf_ptr - slurm control configuration pointer
 */
extern void slurm_print_ctl_conf(FILE *out, slurm_conf_t *slurm_ctl_conf_ptr);

/*
 * slurm_write_ctl_conf - write the contents of slurm control configuration
 *	message as loaded using slurm_load_ctl_conf to a file
 * IN out - file to write to
 * IN slurm_ctl_conf_ptr - slurm control configuration pointer
 * IN node_info_ptr - pointer to node table of information
 * IN part_info_ptr - pointer to partition information
 */
extern void slurm_write_ctl_conf(slurm_conf_t *slurm_ctl_conf_ptr,
                                 node_info_msg_t *node_info_ptr,
                                 partition_info_msg_t *part_info_ptr);

/*
 * slurm_ctl_conf_2_key_pairs - put the slurm_conf_t variables into
 *	a list_t of opaque data type config_key_pair_t
 * IN slurm_ctl_conf_ptr - slurm control configuration pointer
 * RET list of opaque data type config_key_pair_t
 */
extern void *slurm_ctl_conf_2_key_pairs(slurm_conf_t *slurm_ctl_conf_ptr);

/*
 * slurm_print_key_pairs - output the contents of key_pairs
 * which is a list of opaque data type config_key_pair_t
 * IN out - file to write to
 * IN key_pairs - list containing key pairs to be printed
 * IN title - title of key pair list
 */
extern void slurm_print_key_pairs(FILE *out, void *key_pairs, char *title);

/*
 * slurm_load_slurmd_status - issue RPC to get the status of slurmd
 *	daemon on this machine
 * IN slurmd_status_ptr - place to store slurmd status information
 * RET 0 or -1 on error
 * NOTE: free the response using slurm_free_slurmd_status()
 */
extern int slurm_load_slurmd_status(slurmd_status_t **slurmd_status_ptr);

/*
 * slurm_free_slurmd_status - free slurmd state information
 * IN msg - pointer to slurmd state information
 * NOTE: buffer is loaded by slurm_load_slurmd_status
 */
extern void slurm_free_slurmd_status(slurmd_status_t* slurmd_status_ptr);

/*
 * slurm_print_slurmd_status - output the contents of slurmd status
 *	message as loaded using slurm_load_slurmd_status
 * IN out - file to write to
 * IN slurmd_status_ptr - slurmd status pointer
 */
void slurm_print_slurmd_status(FILE *out, slurmd_status_t *slurmd_status_ptr);

/*
 * slurm_init_update_step_msg - initialize step update message with default
 *	values before calling slurm_update_step()
 * OUT step_msg - step update messasge descriptor
 */
extern void slurm_init_update_step_msg(step_update_request_msg_t *step_msg);

/* Get scheduling statistics */
extern int slurm_get_statistics(stats_info_response_msg_t **buf,
				stats_info_request_msg_t *req);

/* Reset scheduling statistics */
extern int slurm_reset_statistics(stats_info_request_msg_t *req);

/*****************************************************************************\
 *	SLURM JOB RESOURCES READ/PRINT FUNCTIONS
\*****************************************************************************/

/*
 * slurm_job_cpus_allocated_on_node_id -
 *                        get the number of cpus allocated to a job
 *			  on a node by node id
 * IN job_resrcs_ptr	- pointer to job_resources structure
 * IN node_id		- zero-origin node id in allocation
 * RET number of CPUs allocated to job on this node or -1 on error
 */
extern int slurm_job_cpus_allocated_on_node_id(job_resources_t *job_resrcs_ptr,
					       int node_id);

/*
 * slurm_job_cpus_allocated_on_node -
 *                        get the number of cpus allocated to a job
 *			  on a node by node name
 * IN job_resrcs_ptr	- pointer to job_resources structure
 * IN node_name		- name of node
 * RET number of CPUs allocated to job on this node or -1 on error
 */
extern int slurm_job_cpus_allocated_on_node(job_resources_t *job_resrcs_ptr,
					    const char *node_name);

/*
 * slurm_job_cpus_allocated_str_on_node_id -
 *                        get the string representation of cpus allocated
 *                        to a job on a node by node id
 * IN cpus		- str where the resulting cpu list is returned
 * IN cpus_len		- max size of cpus str
 * IN job_resrcs_ptr	- pointer to job_resources structure
 * IN node_id		- zero-origin node id in allocation
 * RET 0 on success or -1 on error
 */
extern int slurm_job_cpus_allocated_str_on_node_id(char *cpus,
						   size_t cpus_len,
						   job_resources_t *job_resrcs_ptr,
						   int node_id);

/*
 * slurm_job_cpus_allocated_str_on_node -
 *                        get the string representation of cpus allocated
 *                        to a job on a node by node name
 * IN cpus		- str where the resulting cpu list is returned
 * IN cpus_len		- max size of cpus str
 * IN job_resrcs_ptr	- pointer to job_resources structure
 * IN node_name		- name of node
 * RET 0 on success or -1 on error
 */
extern int slurm_job_cpus_allocated_str_on_node(char *cpus,
						size_t cpus_len,
						job_resources_t *job_resrcs_ptr,
						const char *node_name);

/*****************************************************************************\
 *	SLURM JOB CONTROL CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
\*****************************************************************************/

/*
 * slurm_free_job_info_msg - free the job information response message
 * IN msg - pointer to job information response message
 * NOTE: buffer is loaded by slurm_load_jobs()
 */
extern void slurm_free_job_info_msg(job_info_msg_t *job_buffer_ptr);

/*
 * slurm_free_priority_factors_response_msg - free the job priority factor
 *	information response message
 * IN msg - pointer to job priority information response message
 * NOTE: buffer is loaded by slurm_load_job_prio()
 */
extern void slurm_free_priority_factors_response_msg(
	priority_factors_response_msg_t *factors_resp);

/*
 * slurm_get_end_time - get the expected end time for a given slurm job
 * IN jobid     - slurm job id
 * end_time_ptr - location in which to store scheduled end time for job
 * RET 0 or -1 on error
 */
extern int slurm_get_end_time(uint32_t jobid, time_t *end_time_ptr);

/* Given a job record pointer, return its stderr path */
extern void slurm_get_job_stderr(char *buf, int buf_size, job_info_t *job_ptr);

/* Given a job record pointer, return its stdin path */
extern void slurm_get_job_stdin(char *buf, int buf_size, job_info_t *job_ptr);

/* Given a job record pointer, return its stdout path */
extern void slurm_get_job_stdout(char *buf, int buf_size, job_info_t *job_ptr);

/*
 * slurm_get_rem_time - get the expected time remaining for a given job
 * IN jobid     - slurm job id
 * RET remaining time in seconds or -1 on error
 */
extern long slurm_get_rem_time(uint32_t jobid);

/*
 * slurm_job_node_ready - report if nodes are ready for job to execute now
 * IN job_id - slurm job id
 * RET: READY_* values defined above
 */
extern int slurm_job_node_ready(uint32_t job_id);

/*
 * slurm_load_job - issue RPC to get job information for one job ID
 * IN job_info_msg_pptr - place to store a job configuration pointer
 * IN job_id -  ID of job we want information about
 * IN show_flags - job filtering options
 * RET 0 or -1 on error
 * NOTE: free the response using slurm_free_job_info_msg
 */
extern int slurm_load_job(job_info_msg_t **resp,
			  uint32_t job_id,
			  uint16_t show_flags);

/*
 * slurm_load_job_prio - issue RPC to get job priority information for jobs
 * OUT factors_resp - job priority factors
 * IN show_flags -  job filtering option: 0 or SHOW_LOCAL
 * RET 0 or -1 on error
 * NOTE: free the response using slurm_free_priority_factors_response_msg()
 */
extern int slurm_load_job_prio(priority_factors_response_msg_t **factors_resp,
			       uint16_t show_flags);

/*
 * slurm_load_job_user - issue RPC to get slurm information about all jobs
 *	to be run as the specified user
 * IN/OUT job_info_msg_pptr - place to store a job configuration pointer
 * IN user_id - ID of user we want information for
 * IN show_flags - job filtering options
 * RET 0 or -1 on error
 * NOTE: free the response using slurm_free_job_info_msg
 */
extern int slurm_load_job_user(job_info_msg_t **job_info_msg_pptr,
			       uint32_t user_id,
			       uint16_t show_flags);

/*
 * slurm_load_jobs - issue RPC to get slurm all job configuration
 *	information if changed since update_time
 * IN update_time - time of current configuration data
 * IN/OUT job_info_msg_pptr - place to store a job configuration pointer
 * IN show_flags - job filtering options
 * RET 0 or -1 on error
 * NOTE: free the response using slurm_free_job_info_msg
 */
extern int slurm_load_jobs(time_t update_time,
			   job_info_msg_t **job_info_msg_pptr,
			   uint16_t show_flags);

/*
 * slurm_notify_job - send message to the job's stdout,
 *	usable only by user root
 * IN job_id - slurm job_id or 0 for all jobs
 * IN message - arbitrary message
 * RET 0 or -1 on error
 */
extern int slurm_notify_job(uint32_t job_id, char *message);

/*
 * slurm_pid2jobid - issue RPC to get the slurm job_id given a process_id
 *	on this machine
 * IN job_pid - process_id of interest on this machine
 * OUT job_id_ptr - place to store a slurm job_id
 * RET 0 or -1 on error
 */
extern int slurm_pid2jobid(pid_t job_pid, uint32_t *job_id_ptr);

/*
 * slurm_print_job_info - output information about a specific Slurm
 *	job based upon message as loaded using slurm_load_jobs
 * IN out - file to write to
 * IN job_ptr - an individual job information record pointer
 * IN one_liner - print as a single line if true
 */
extern void slurm_print_job_info(FILE *out,
				 slurm_job_info_t *job_ptr,
				 int one_liner);

/*
 * slurm_print_job_info_msg - output information about all Slurm
 *	jobs based upon message as loaded using slurm_load_jobs
 * IN out - file to write to
 * IN job_info_msg_ptr - job information message pointer
 * IN one_liner - print as a single line if true
 */
extern void slurm_print_job_info_msg(FILE *out,
				     job_info_msg_t *job_info_msg_ptr,
				     int one_liner);

/*
 * slurm_sprint_job_info - output information about a specific Slurm
 *	job based upon message as loaded using slurm_load_jobs
 * IN job_ptr - an individual job information record pointer
 * IN one_liner - print as a single line if true
 * RET out - char * containing formatted output (must be freed after call)
 *           NULL is returned on failure.
 */
extern char *slurm_sprint_job_info(slurm_job_info_t *job_ptr,
				   int one_liner);

/*
 * slurm_update_job - issue RPC to a job's configuration per request,
 *	only usable by user root or (for some parameters) the job's owner
 * IN job_msg - description of job updates
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_update_job(job_desc_msg_t *job_msg);

/*
 * slurm_update_job2 - issue RPC to a job's configuration per request,
 *	only usable by user root or (for some parameters) the job's owner
 * IN job_msg - description of job updates
 * OUT resp - per task response to the request,
 *	      free using slurm_free_job_array_resp()
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_update_job2(job_desc_msg_t *job_msg,
			     job_array_resp_msg_t **resp);

/*
 * slurm_xlate_job_id - Translate a Slurm job ID string into a slurm job ID
 *	number. If this job ID contains an array index, map this to the
 *	equivalent Slurm job ID number (e.g. "123_2" to 124)
 *
 * IN job_id_str - String containing a single job ID number
 * RET - equivalent job ID number or 0 on error
 */
extern uint32_t slurm_xlate_job_id(char *job_id_str);


/*****************************************************************************\
 *	SLURM JOB STEP CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
\*****************************************************************************/

/*
 * slurm_get_job_steps - issue RPC to get specific slurm job step
 *	configuration information if changed since update_time.
 *	a job_id value of NO_VAL implies all jobs, a step_id value of
 *	NO_VAL implies all steps
 * IN update_time - time of current configuration data
 * IN job_id - get information for specific job id, NO_VAL for all jobs
 * IN step_id - get information for specific job step id, NO_VAL for all
 *	job steps
 * IN step_response_pptr - place to store a step response pointer
 * IN show_flags - job step filtering options
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 * NOTE: free the response using slurm_free_job_step_info_response_msg
 */
extern int slurm_get_job_steps(time_t update_time,
			       uint32_t job_id,
			       uint32_t step_id,
			       job_step_info_response_msg_t **step_response_pptr,
			       uint16_t show_flags);

/*
 * Issue RPC to find all steps matching container id and uid (unless uid=NO_VAL)
 * IN show_flags - job step filtering options
 * IN/OUT steps - List (of slurm_step_id_t*) to populate.
 * 	Must free step ids with slurm_free_step_id().
 * RET SLURM_SUCCESS or error
 */
extern int slurm_find_step_ids_by_container_id(uint16_t show_flags, uid_t uid,
					       const char *container_id,
					       list_t *steps);

/*
 * slurm_free_job_step_info_response_msg - free the job step
 *	information response message
 * IN msg - pointer to job step information response message
 * NOTE: buffer is loaded by slurm_get_job_steps.
 */
extern void slurm_free_job_step_info_response_msg(job_step_info_response_msg_t *msg);

/*
 * slurm_print_job_step_info_msg - output information about all Slurm
 *	job steps based upon message as loaded using slurm_get_job_steps
 * IN out - file to write to
 * IN job_step_info_msg_ptr - job step information message pointer
 * IN one_liner - print as a single line if true
 */
extern void slurm_print_job_step_info_msg(FILE *out,
					  job_step_info_response_msg_t *job_step_info_msg_ptr,
					  int one_liner);

/*
 * slurm_print_job_step_info - output information about a specific Slurm
 *	job step based upon message as loaded using slurm_get_job_steps
 * IN out - file to write to
 * IN job_ptr - an individual job step information record pointer
 * IN one_liner - print as a single line if true
 */
extern void slurm_print_job_step_info(FILE *out,
				      job_step_info_t *step_ptr,
				      int one_liner);

/*
 * slurm_job_step_layout_get - get the slurm_step_layout_t structure for
 *      a particular job step
 *
 * IN step_id
 * RET pointer to a slurm_step_layout_t (free with
 *   slurm_free_step_layout) on success, and NULL on error.
 */
extern slurm_step_layout_t *slurm_job_step_layout_get(slurm_step_id_t *step_id);

/*
 * slurm_sprint_job_step_info - output information about a specific Slurm
 *	job step based upon message as loaded using slurm_get_job_steps
 * IN job_ptr - an individual job step information record pointer
 * IN one_liner - print as a single line if true
 * RET out - char * containing formatted output (must be freed after call)
 *           NULL is returned on failure.
 */
extern char *slurm_sprint_job_step_info(job_step_info_t *step_ptr,
					int one_liner);

/*
 * slurm_job_step_stat - status a current step
 *
 * IN step_id
 * IN node_list, optional, if NULL then all nodes in step are returned.
 * OUT resp
 * RET SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurm_job_step_stat(slurm_step_id_t *step_id,
			       char *node_list,
			       uint16_t use_protocol_ver,
			       job_step_stat_response_msg_t **resp);

/*
 * slurm_job_step_get_pids - get the complete list of pids for a given
 *      job step
 *
 * IN step_id
 * OUT resp
 * RET SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurm_job_step_get_pids(slurm_step_id_t *step_id,
				   char *node_list,
				   job_step_pids_response_msg_t **resp);

extern void slurm_job_step_layout_free(slurm_step_layout_t *layout);
extern void slurm_job_step_pids_free(job_step_pids_t *object);
extern void slurm_job_step_pids_response_msg_free(void *object);
extern void slurm_job_step_stat_free(job_step_stat_t *object);
extern void slurm_job_step_stat_response_msg_free(void *object);

/* Update the time limit of a job step,
 * IN step_msg - step update messasge descriptor
 * RET 0 or -1 on error */
extern int slurm_update_step(step_update_request_msg_t *step_msg);

extern void slurm_destroy_selected_step(void *object);

/*****************************************************************************\
 *	SLURM NODE CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
\*****************************************************************************/

/*
 * slurm_load_node - issue RPC to get slurm all node configuration information
 *	if changed since update_time
 * IN update_time - time of current configuration data
 * OUT resp - place to store a node configuration pointer
 * IN show_flags - node filtering options (e.g. SHOW_FEDERATION)
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_node_info_msg
 */
extern int slurm_load_node(time_t update_time, node_info_msg_t **resp,
			   uint16_t show_flags);

/*
 * slurm_load_node2 - equivalent to slurm_load_node() with addition
 *	of cluster record for communications in a federation
 */
extern int slurm_load_node2(time_t update_time, node_info_msg_t **resp,
			    uint16_t show_flags,
			    slurmdb_cluster_rec_t *cluster);

/*
 * slurm_load_node_single - issue RPC to get slurm configuration information
 *	for a specific node
 * OUT resp - place to store a node configuration pointer
 * IN node_name - name of the node for which information is requested
 * IN show_flags - node filtering options
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_node_info_msg
 */
extern int slurm_load_node_single(node_info_msg_t **resp, char *node_name,
				  uint16_t show_flags);

/*
 * slurm_load_node_single2 - equivalent to slurm_load_node_single() with
 *	addition of cluster record for communications in a federation
 */
extern int slurm_load_node_single2(node_info_msg_t **resp, char *node_name,
				   uint16_t show_flags,
				   slurmdb_cluster_rec_t *cluster);

/* Given data structures containing information about nodes and partitions,
 * populate the node's "partitions" field */
void
slurm_populate_node_partitions(node_info_msg_t *node_buffer_ptr,
			       partition_info_msg_t *part_buffer_ptr);

/*
 * slurm_get_node_energy - issue RPC to get the energy data of all
 * configured sensors on the target machine
 * IN  host  - name of node to query, NULL if localhost
 * IN  delta - Use cache if data is newer than this in seconds
 * OUT sensor_cnt - number of sensors
 * OUT energy - array of acct_gather_energy_t structures on success or
 *                NULL other wise
 * RET 0 on success or a slurm error code
 * NOTE: free the response using xfree
 */
extern int slurm_get_node_energy(char *host,
				 uint16_t context_id,
				 uint16_t delta,
				 uint16_t *sensors_cnt,
				 acct_gather_energy_t **energy);

/*
 * slurm_free_node_info_msg - free the node information response message
 * IN msg - pointer to node information response message
 * NOTE: buffer is loaded by slurm_load_node.
 */
extern void slurm_free_node_info_msg(node_info_msg_t *node_buffer_ptr);

/*
 * slurm_print_node_info_msg - output information about all Slurm nodes
 *	based upon message as loaded using slurm_load_node
 * IN out - file to write to
 * IN node_info_msg_ptr - node information message pointer
 * IN one_liner - print as a single line if true
 */
extern void slurm_print_node_info_msg(FILE *out,
				      node_info_msg_t *node_info_msg_ptr,
				      int one_liner);

/*
 * slurm_print_node_table - output information about a specific Slurm nodes
 *	based upon message as loaded using slurm_load_node
 * IN out - file to write to
 * IN node_ptr - an individual node information record pointer
 * IN one_liner - print as a single line if true
 */
extern void slurm_print_node_table(FILE *out,
				   node_info_t *node_ptr,
				   int one_liner);

/*
 * slurm_sprint_node_table - output information about a specific Slurm nodes
 *	based upon message as loaded using slurm_load_node
 * IN node_ptr - an individual node information record pointer
 * IN one_liner - print as a single line if true
 * RET out - char * containing formatted output (must be freed after call)
 *           NULL is returned on failure.
 */
extern char *slurm_sprint_node_table(node_info_t *node_ptr,
				     int one_liner);

/*
 * slurm_init_update_node_msg - initialize node update message
 * OUT update_node_msg - user defined node descriptor
 */
void slurm_init_update_node_msg(update_node_msg_t *update_node_msg);

/*
 * slurm_create_node - issue RPC to create node(s), only usable by user root
 * IN node_msg - node definition(s)
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_create_node(update_node_msg_t *node_msg);

/*
 * slurm_update_node - issue RPC to a node's configuration per request,
 *	only usable by user root
 * IN node_msg - description of node updates
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_update_node(update_node_msg_t *node_msg);

/*
 * slurm_delete_node - issue RPC to delete a node, only usable by user root
 * IN node_msg - use to pass nodelist of names to delete
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
int slurm_delete_node(update_node_msg_t *node_msg);

/*****************************************************************************\
 *	SLURM FRONT_END CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
\*****************************************************************************/

/*
 * slurm_load_front_end - issue RPC to get slurm all front_end configuration
 *	information if changed since update_time
 * IN update_time - time of current configuration data
 * IN front_end_info_msg_pptr - place to store a front_end configuration pointer
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_front_end_info_msg
 */
extern int slurm_load_front_end(time_t update_time,
				front_end_info_msg_t **resp);

/*
 * slurm_free_front_end_info_msg - free the front_end information response
 *	message
 * IN msg - pointer to front_end information response message
 * NOTE: buffer is loaded by slurm_load_front_end.
 */
extern void slurm_free_front_end_info_msg(front_end_info_msg_t *front_end_buffer_ptr);

/*
 * slurm_print_front_end_info_msg - output information about all Slurm
 *	front_ends based upon message as loaded using slurm_load_front_end
 * IN out - file to write to
 * IN front_end_info_msg_ptr - front_end information message pointer
 * IN one_liner - print as a single line if true
 */
extern void slurm_print_front_end_info_msg(FILE *out,
					   front_end_info_msg_t *front_end_info_msg_ptr,
					   int one_liner);
/*
 * slurm_print_front_end_table - output information about a specific Slurm
 *	front_ends based upon message as loaded using slurm_load_front_end
 * IN out - file to write to
 * IN front_end_ptr - an individual front_end information record pointer
 * IN one_liner - print as a single line if true
 */
extern void slurm_print_front_end_table(FILE *out,
					front_end_info_t *front_end_ptr,
					int one_liner);

/*
 * slurm_sprint_front_end_table - output information about a specific Slurm
 *	front_end based upon message as loaded using slurm_load_front_end
 * IN front_end_ptr - an individual front_end information record pointer
 * IN one_liner - print as a single line if true
 * RET out - char * containing formatted output (must be freed after call)
 *           NULL is returned on failure.
 */
extern char *slurm_sprint_front_end_table(front_end_info_t *front_end_ptr,
					  int one_liner);

/*
 * slurm_init_update_front_end_msg - initialize front_end node update message
 * OUT update_front_end_msg - user defined node descriptor
 */
void slurm_init_update_front_end_msg(update_front_end_msg_t *update_front_end_msg);

/*
 * slurm_update_front_end - issue RPC to a front_end node's configuration per
 *	request, only usable by user root
 * IN front_end_msg - description of front_end node updates
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_update_front_end(update_front_end_msg_t *front_end_msg);


/*****************************************************************************\
 *	SLURM SWITCH TOPOLOGY CONFIGURATION READ/PRINT FUNCTIONS
\*****************************************************************************/

/*
 * slurm_load_topo - issue RPC to get slurm all switch topology configuration
 *	information
 * IN node_info_msg_pptr - place to store a node configuration pointer
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_topo_info_msg
 */
extern int slurm_load_topo(topo_info_response_msg_t **topo_info_msg_pptr);

/*
 * slurm_free_topo_info_msg - free the switch topology configuration
 *	information response message
 * IN msg - pointer to switch topology configuration response message
 * NOTE: buffer is loaded by slurm_load_topo.
 */
extern void slurm_free_topo_info_msg(topo_info_response_msg_t *msg);

/*
 * slurm_print_topo_info_msg - output information about all switch topology
 *	configuration information based upon message as loaded using
 *	slurm_load_topo
 * IN out - file to write to
 * IN topo_info_msg_ptr - switch topology information message pointer
 * IN one_liner - print as a single line if not zero
 */
extern void slurm_print_topo_info_msg(FILE *out,
				      topo_info_response_msg_t *topo_info_msg_ptr,
				      int one_liner);

/*
 * slurm_print_topo_record - output information about a specific Slurm topology
 *	record based upon message as loaded using slurm_load_topo
 * IN out - file to write to
 * IN topo_ptr - an individual switch information record pointer
 * IN one_liner - print as a single line if not zero
 * RET out - char * containing formatted output (must be freed after call)
 *	   NULL is returned on failure.
 */
extern void slurm_print_topo_record(FILE *out,
				    topo_info_t *topo_ptr,
				    int one_liner);

/*****************************************************************************\
 *	SLURM SELECT READ/PRINT/UPDATE FUNCTIONS
\*****************************************************************************/

/*
 * slurm_get_select_nodeinfo - get data from a select node credential
 * IN nodeinfo  - updated select node credential
 * IN data_type - type of data to enter into node credential
 * IN state     - state of node query
 * IN/OUT data  - the data to enter into node credential
 * RET 0 or -1 on error
 */
extern int slurm_get_select_nodeinfo(dynamic_plugin_data_t *nodeinfo,
				     enum select_nodedata_type data_type,
				     enum node_states state,
				     void *data);

/*****************************************************************************\
 *	SLURM PARTITION CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
\*****************************************************************************/

/*
 * slurm_init_part_desc_msg - initialize partition descriptor with
 *	default values
 * IN/OUT update_part_msg - user defined partition descriptor
 */
extern void slurm_init_part_desc_msg(update_part_msg_t *update_part_msg);

/*
 * slurm_load_partitions - issue RPC to get slurm all partition configuration
 *	information if changed since update_time
 * IN update_time - time of current configuration data
 * IN partition_info_msg_pptr - place to store a partition configuration
 *	pointer
 * IN show_flags - partitions filtering options (e.g. SHOW_FEDERATION)
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_partition_info_msg
 */
extern int slurm_load_partitions(time_t update_time,
				 partition_info_msg_t **part_buffer_ptr,
				 uint16_t show_flags);

/*
 * slurm_load_partitions2 - equivalent to slurm_load_partitions() with addition
 *	of cluster record for communications in a federation
 */
extern int slurm_load_partitions2(time_t update_time,
				  partition_info_msg_t **resp,
				  uint16_t show_flags,
				  slurmdb_cluster_rec_t *cluster);

/*
 * slurm_free_partition_info_msg - free the partition information
 *	response message
 * IN msg - pointer to partition information response message
 * NOTE: buffer is loaded by slurm_load_partitions
 */
extern void slurm_free_partition_info_msg(partition_info_msg_t *part_info_ptr);

/*
 * slurm_print_partition_info_msg - output information about all Slurm
 *	partitions based upon message as loaded using slurm_load_partitions
 * IN out - file to write to
 * IN part_info_ptr - partitions information message pointer
 * IN one_liner - print as a single line if true
 */
extern void slurm_print_partition_info_msg(FILE *out, partition_info_msg_t *part_info_ptr, int one_liner);

/*
 * slurm_print_partition_info - output information about a specific Slurm
 *	partition based upon message as loaded using slurm_load_partitions
 * IN out - file to write to
 * IN part_ptr - an individual partition information record pointer
 * IN one_liner - print as a single line if true
 */
extern void slurm_print_partition_info(FILE *out,
				       partition_info_t *part_ptr,
				       int one_liner);

/*
 * slurm_sprint_partition_info - output information about a specific Slurm
 *	partition based upon message as loaded using slurm_load_partitions
 * IN part_ptr - an individual partition information record pointer
 * IN one_liner - print as a single line if true
 * RET out - char * with formatted output (must be freed after call)
 *           NULL is returned on failure.
 */
extern char *slurm_sprint_partition_info(partition_info_t *part_ptr,
					 int one_liner);

/*
 * slurm_create_partition - create a new partition, only usable by user root
 * IN part_msg - description of partition configuration
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_create_partition(update_part_msg_t *part_msg);

/*
 * slurm_update_partition - issue RPC to update a partition's configuration
 *	per request, only usable by user root
 * IN part_msg - description of partition updates
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_update_partition(update_part_msg_t *part_msg);

/*
 * slurm_delete_partition - issue RPC to delete a partition, only usable
 *	by user root
 * IN part_msg - description of partition to delete
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_delete_partition(delete_part_msg_t *part_msg);

/*****************************************************************************\
 *	SLURM RESERVATION CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
\*****************************************************************************/

/*
 * slurm_init_resv_desc_msg - initialize reservation descriptor with
 *	default values
 * OUT job_desc_msg - user defined partition descriptor
 */
extern void slurm_init_resv_desc_msg(resv_desc_msg_t *update_resv_msg);
/*
 * slurm_create_reservation - create a new reservation, only usable by user root
 * IN resv_msg - description of reservation
 * RET name of reservation on success (caller must free the memory),
 *	otherwise return NULL and set errno to indicate the error
 */
extern char *slurm_create_reservation(resv_desc_msg_t *resv_msg);

/*
 * slurm_update_reservation - modify an existing reservation, only usable by
 *	user root
 * IN resv_msg - description of reservation
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_update_reservation(resv_desc_msg_t *resv_msg);

/*
 * slurm_delete_reservation - issue RPC to delete a reservation, only usable
 *	by user root
 * IN resv_msg - description of reservation to delete
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_delete_reservation(reservation_name_msg_t *resv_msg);

/*
 * slurm_load_reservations - issue RPC to get all slurm reservation
 *	configuration information if changed since update_time
 * IN update_time - time of current configuration data
 * IN reserve_info_msg_pptr - place to store a reservation configuration
 *	pointer
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_reservation_info_msg
 */
extern int slurm_load_reservations(time_t update_time,
				   reserve_info_msg_t **resp);

/*
 * slurm_print_reservation_info_msg - output information about all Slurm
 *	reservations based upon message as loaded using slurm_load_reservation
 * IN out - file to write to
 * IN resv_info_ptr - reservation information message pointer
 * IN one_liner - print as a single line if true
 */
void slurm_print_reservation_info_msg(FILE* out,
				      reserve_info_msg_t *resv_info_ptr,
				      int one_liner);

/*
 * slurm_print_reservation_info - output information about a specific Slurm
 *	reservation based upon message as loaded using slurm_load_reservation
 * IN out - file to write to
 * IN resv_ptr - an individual reservation information record pointer
 * IN one_liner - print as a single line if true
 */
void slurm_print_reservation_info(FILE* out,
				  reserve_info_t *resv_ptr,
				  int one_liner);

/*
 * slurm_sprint_reservation_info - output information about a specific Slurm
 *	reservation based upon message as loaded using slurm_load_reservations
 * IN resv_ptr - an individual reservation information record pointer
 * IN one_liner - print as a single line if true
 * RET out - char * containing formatted output (must be freed after call)
 *           NULL is returned on failure.
 */
char *slurm_sprint_reservation_info(reserve_info_t *resv_ptr, int one_liner);

/*
 * slurm_free_reservation_info_msg - free the reservation information
 *	response message
 * IN msg - pointer to reservation information response message
 * NOTE: buffer is loaded by slurm_load_reservation
 */
extern void slurm_free_reservation_info_msg(reserve_info_msg_t *resv_info_ptr);

/*****************************************************************************\
 *	SLURM PING/RECONFIGURE/SHUTDOWN STRUCTURES
\*****************************************************************************/

typedef struct {
	char *hostname; /* symlink - do not xfree() */
	bool pinged; /* true on successful ping */
	long latency; /* time to ping or timeout on !pinged */
	/*
	 * controller offset which defines default mode:
	 * 0: primary
	 * 1: backup
	 * 2+: backup#
	 */
	int offset;
} controller_ping_t;

/*****************************************************************************\
 *	SLURM PING/RECONFIGURE/SHUTDOWN FUNCTIONS
\*****************************************************************************/

/*
 * slurm_ping - issue RPC to have Slurm controller (slurmctld)
 * IN dest - controller to contact (0=primary, 1=backup, 2=backup2, etc.)
 * RET 0 or a slurm error code
 */
extern int slurm_ping(int dest);

/*
 * RET array of each ping result (NULL terminated).
 * Caller must xfree() the result.
 */
extern controller_ping_t *ping_all_controllers();

/*
 * slurm_reconfigure - issue RPC to have Slurm controller (slurmctld)
 *	reload its configuration file
 * RET 0 or a slurm error code
 */
extern int slurm_reconfigure(void);

/*
 * slurm_shutdown - issue RPC to have Slurm controller (slurmctld)
 *	cease operations, both the primary and all backup controllers
 *	are shutdown.
 * IN options - 0: all slurm daemons are shutdown
 *              1: slurmctld generates a core file
 *              2: only the slurmctld is shutdown (no core file)
 * RET 0 or a slurm error code
 */
extern int slurm_shutdown(uint16_t options);

/*
 * slurm_takeover - issue RPC to have a Slurm backup controller take over the
 *                  primary controller. REQUEST_CONTROL is sent by the backup
 *                  to the primary controller to take control
 * backup_inx IN - Index of BackupController to assume controller (typically 1)
 * RET 0 or a slurm error code
 */
extern int slurm_takeover(int backup_inx);

/*
 * slurm_set_debugflags - issue RPC to set slurm controller debug flags
 * IN debug_flags_plus  - debug flags to be added
 * IN debug_flags_minus - debug flags to be removed
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_set_debugflags(uint64_t debug_flags_plus,
				uint64_t debug_flags_minus);
/*
 * slurm_set_slurmd_debug_flags - issue RPC to set slurmd debug flags
 * IN debug_flags_plus  - debug flags to be added
 * IN debug_flags_minus - debug flags to be removed
 * IN debug_flags_set   - new debug flags value
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR
 */
extern int slurm_set_slurmd_debug_flags(char *node_list,
				        uint64_t debug_flags_plus,
				        uint64_t debug_flags_minus);

/*
 * slurm_set_slurmd_debug_level - issue RPC to set slurmd debug level
 * IN debug_level - requested debug level
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR
 */
extern int slurm_set_slurmd_debug_level(char *node_list, uint32_t debug_level);

/*
 * slurm_set_debug_level - issue RPC to set slurm controller debug level
 * IN debug_level - requested debug level
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_set_debug_level(uint32_t debug_level);

/*
 * slurm_set_schedlog_level - issue RPC to set slurm scheduler log level
 * IN schedlog_level - requested scheduler log level
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_set_schedlog_level(uint32_t schedlog_level);

/*
 * slurm_set_fs_dampeningfactor - issue RPC to set slurm fs dampening factor
 * IN factor - requested fs dampening factor
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_set_fs_dampeningfactor(uint16_t factor);

/*
 * slurm_update_suspend_exc_nodes - issue RPC to set SuspendExcNodes
 * IN nodes - string to set
 * IN mode - Whether to set, append or remove nodes from the setting
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_update_suspend_exc_nodes(char *nodes, update_mode_t mode);

/*
 * slurm_update_suspend_exc_parts - issue RPC to set SuspendExcParts
 * IN parts - string to set
 * IN mode - Whether to set, append or remove partitions from the setting
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_update_suspend_exc_parts(char *parts, update_mode_t mode);

/*
 * slurm_update_suspend_exc_states - issue RPC to set SuspendExcStates
 * IN states - string to set
 * IN mode - Whether to set, append or remove states from the setting
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_update_suspend_exc_states(char *states, update_mode_t mode);

/*****************************************************************************\
 *      SLURM JOB SUSPEND FUNCTIONS
\*****************************************************************************/

/*
 * slurm_suspend - suspend execution of a job.
 * IN job_id  - job on which to perform operation
 * RET 0 or a slurm error code
 */
extern int slurm_suspend(uint32_t job_id);

/*
 * slurm_suspend2 - suspend execution of a job.
 * IN job_id in string form  - job on which to perform operation, may be job
 *            array specification (e.g. "123_1-20,44");
 * OUT resp - per task response to the request,
 *	      free using slurm_free_job_array_resp()
 * RET 0 or a slurm error code
 */
extern int slurm_suspend2(char *job_id, job_array_resp_msg_t **resp);

/*
 * slurm_resume - resume execution of a previously suspended job.
 * IN job_id  - job on which to perform operation
 * RET 0 or a slurm error code
 */
extern int slurm_resume(uint32_t job_id);

/*
 * slurm_resume2 - resume execution of a previously suspended job.
 * IN job_id in string form  - job on which to perform operation, may be job
 *            array specification (e.g. "123_1-20,44");
 * OUT resp - per task response to the request,
 *	      free using slurm_free_job_array_resp()
 * RET 0 or a slurm error code
 */
extern int slurm_resume2(char *job_id, job_array_resp_msg_t **resp);

/* Free job array oriented response with individual return codes by task ID */
extern void slurm_free_job_array_resp(job_array_resp_msg_t *resp);

/*
 * slurm_requeue - re-queue a batch job, if already running
 *	then terminate it first
 * IN job_id  - job on which to perform operation
 * IN flags - JOB_SPECIAL_EXIT - job should be placed special exit state and
 *		  held.
 *            JOB_REQUEUE_HOLD - job should be placed JOB_PENDING state and
 *		  held.
 *            JOB_RECONFIG_FAIL - Node configuration for job failed
 *            JOB_RUNNING - Operate only on jobs in a state of
 *		  CONFIGURING, RUNNING, STOPPED or SUSPENDED.
 * RET 0 or a slurm error code
 */
extern int slurm_requeue(uint32_t job_id, uint32_t flags);

/*
 * slurm_requeue2 - re-queue a batch job, if already running
 *	then terminate it first
 * IN job_id in string form  - job on which to perform operation, may be job
 *            array specification (e.g. "123_1-20,44");
 * IN flags - JOB_SPECIAL_EXIT - job should be placed special exit state and
 *		  held.
 *            JOB_REQUEUE_HOLD - job should be placed JOB_PENDING state and
 *		  held.
 *            JOB_RECONFIG_FAIL - Node configuration for job failed
 *            JOB_RUNNING - Operate only on jobs in a state of
 *		  CONFIGURING, RUNNING, STOPPED or SUSPENDED.
 * OUT resp - per task response to the request,
 *	      free using slurm_free_job_array_resp()
 * RET 0 or a slurm error code
 */
extern int slurm_requeue2(char *job_id, uint32_t flags,
			  job_array_resp_msg_t **resp);

/*****************************************************************************\
 *      SLURM TRIGGER FUNCTIONS
\*****************************************************************************/

/*
 * slurm_set_trigger - Set an event trigger
 * RET 0 or a slurm error code
 */
extern int slurm_set_trigger(trigger_info_t *trigger_set);

/*
 * slurm_clear_trigger - Clear (remove) an existing event trigger
 * RET 0 or a slurm error code
 */
extern int slurm_clear_trigger(trigger_info_t *trigger_clear);

/*
 * slurm_get_triggers - Get all event trigger information
 * Use slurm_free_trigger_msg() to free the memory allocated by this function
 * RET 0 or a slurm error code
 */
extern int slurm_get_triggers(trigger_info_msg_t **trigger_get);

/*
 * slurm_pull_trigger - Pull an event trigger
 * RET 0 or a slurm error code
 */
extern int slurm_pull_trigger(trigger_info_t *trigger_pull);

/*
 * slurm_free_trigger_msg - Free data structure returned by
 * slurm_get_triggers()
 */
extern void slurm_free_trigger_msg(trigger_info_msg_t *trigger_free);

/*
 * slurm_init_trigger_msg - initialize trigger clear/update message
 * OUT trigger_info_msg - user defined trigger descriptor
 */
void slurm_init_trigger_msg(trigger_info_t *trigger_info_msg);

/*****************************************************************************\
 *      SLURM BURST BUFFER FUNCTIONS
\*****************************************************************************/
#define BB_FLAG_DISABLE_PERSISTENT	0x0001	/* Disable regular user to create
						 * and destroy persistent burst
						 * buffers */
#define BB_FLAG_ENABLE_PERSISTENT	0x0002	/* Allow regular user to create
						 * and destroy persistent burst
						 * buffers */
#define BB_FLAG_EMULATE_CRAY		0x0004	/* Using dw_wlm_cli emulator */
#define BB_FLAG_PRIVATE_DATA		0x0008	/* Buffers only visible to owner */
#define BB_FLAG_TEARDOWN_FAILURE	0x0010	/* Teardown after failed staged in/out */

#define BB_SIZE_IN_NODES	0x8000000000000000
/*
 * Burst buffer states: Keep in sync with bb_state_string() and bb_state_num()
 * in slurm_protocol_defs.c.
 */
#define BB_STATE_PENDING	0x0000		/* Placeholder: no action started */
#define BB_STATE_ALLOCATING	0x0001		/* Cray: bbs_setup started */
#define BB_STATE_ALLOCATED	0x0002		/* Cray: bbs_setup started */
#define BB_STATE_DELETING	0x0005		/* Cray: bbs_setup started */
#define BB_STATE_DELETED	0x0006		/* Cray: bbs_setup started */
#define BB_STATE_STAGING_IN	0x0011		/* Cray: bbs_data_in started */
#define BB_STATE_STAGED_IN	0x0012		/* Cray: bbs_data_in complete */
#define BB_STATE_PRE_RUN	0x0018		/* Cray: bbs_pre_run started */
#define BB_STATE_ALLOC_REVOKE	0x001a		/* Cray: allocation revoked */
#define BB_STATE_RUNNING	0x0021		/* Job is running */
#define BB_STATE_SUSPEND	0x0022		/* Job is suspended (future) */
#define BB_STATE_POST_RUN	0x0029		/* Cray: bbs_post_run started */
#define BB_STATE_STAGING_OUT	0x0031		/* Cray: bbs_data_out started */
#define BB_STATE_STAGED_OUT	0x0032		/* Cray: bbs_data_out complete */
#define BB_STATE_TEARDOWN	0x0041		/* Cray: bbs_teardown started */
#define BB_STATE_TEARDOWN_FAIL	0x0043		/* Cray: bbs_teardown failed, retrying */
#define BB_STATE_COMPLETE	0x0045		/* Cray: bbs_teardown complete */

/* Information about alternate pools or other burst buffer resources */
typedef struct {
	uint64_t granularity;	/* Granularity of resource allocation size */
	char *name;		/* Resource (pool) name */
	uint64_t total_space;	/* Total size of available resources, unused
				 * by burst_buffer_resv_t */
	uint64_t used_space;	/* Allocated space, in bytes */
	uint64_t unfree_space;	/* used plus drained space, units are bytes */
} burst_buffer_pool_t;

typedef struct {
	char *account;		/* Associated account (for limits) */
	uint32_t array_job_id;
	uint32_t array_task_id;
	time_t create_time;	/* Time of creation */
	uint32_t job_id;
	char *name;		/* Name of persistent burst buffer */
	char *partition;	/* Associated partition (for limits) */
	char *pool;		/* Resource (pool) name */
	char *qos;		/* Associated QOS (for limits) */
	uint64_t size;		/* In bytes by default */
	uint16_t state;		/* See BB_STATE_* */
	uint32_t user_id;
} burst_buffer_resv_t;

typedef struct {
	uint32_t user_id;
	uint64_t used;
} burst_buffer_use_t;

typedef struct {
	char *allow_users;
	char *default_pool;		/* Name of default pool to use */
	char *create_buffer;
	char *deny_users;
	char *destroy_buffer;
	uint32_t flags;			/* See BB_FLAG_* above */
	char *get_sys_state;
	char *get_sys_status;
	uint64_t granularity;		/* Granularity of resource allocation */
	uint32_t pool_cnt;		/* Count of records in pool_ptr */
	burst_buffer_pool_t *pool_ptr;
	char *name;			/* Plugin name */
	uint32_t other_timeout;		/* Seconds or zero */
	uint32_t stage_in_timeout;	/* Seconds or zero */
	uint32_t stage_out_timeout;	/* Seconds or zero */
	char *start_stage_in;
	char *start_stage_out;
	char *stop_stage_in;
	char *stop_stage_out;
	uint64_t total_space;		/* In bytes */
	uint64_t unfree_space;		/* Allocated or drained, in bytes */
	uint64_t used_space;		/* Allocated, in bytes */
	uint32_t validate_timeout;	/* Seconds or zero */

	uint32_t  buffer_count;
	burst_buffer_resv_t *burst_buffer_resv_ptr;

	uint32_t  use_count;
	burst_buffer_use_t *burst_buffer_use_ptr;
} burst_buffer_info_t;

typedef struct {
	burst_buffer_info_t *burst_buffer_array;
	uint32_t  record_count;		/* Elements in burst_buffer_array */
} burst_buffer_info_msg_t;

/*
 * slurm_burst_buffer_state_string - translate burst buffer state number to
 *	it string equivalent
 */
extern char *slurm_burst_buffer_state_string(uint16_t state);

/*
 * slurm_load_burst_buffer_stat - issue RPC to get burst buffer status
 * IN argc - count of status request options
 * IN argv - status request options
 * OUT status_resp - status response, memory must be released using xfree()
 * RET 0 or a slurm error code
 */
extern int slurm_load_burst_buffer_stat(int argc, char **argv,
					char **status_resp);

/*
 * slurm_load_burst_buffer_info - issue RPC to get slurm all burst buffer plugin
 *	information
 * OUT burst_buffer_info_msg_pptr - place to store a burst buffer configuration
 *	pointer
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_burst_buffer_info_msg
 */
extern int slurm_load_burst_buffer_info(burst_buffer_info_msg_t **burst_buffer_info_msg_pptr);

/*
 * slurm_free_burst_buffer_info_msg - free buffer returned by
 *	slurm_load_burst_buffer
 * IN burst_buffer_info_msg_ptr - pointer to burst_buffer_info_msg_t
 * RET 0 or a slurm error code
 */
extern void slurm_free_burst_buffer_info_msg(burst_buffer_info_msg_t *burst_buffer_info_msg);

/*
 * slurm_print_burst_buffer_info_msg - output information about burst buffers
 *	based upon message as loaded using slurm_load_burst_buffer
 * IN out - file to write to
 * IN info_ptr - burst_buffer information message pointer
 * IN one_liner - print as a single line if true
 * IN verbose - higher values to log additional details
 */
extern void slurm_print_burst_buffer_info_msg(FILE *out,
					      burst_buffer_info_msg_t *info_ptr,
					      int one_liner,
					      int verbosity);

/*
 * slurm_print_burst_buffer_record - output information about a specific Slurm
 *	burst_buffer record based upon message as loaded using
 *	slurm_load_burst_buffer_info()
 * IN out - file to write to
 * IN burst_buffer_ptr - an individual burst buffer record pointer
 * IN one_liner - print as a single line if not zero
 * IN verbose - higher values to log additional details
 * RET out - char * containing formatted output (must be freed after call)
 *	   NULL is returned on failure.
 */
extern void slurm_print_burst_buffer_record(FILE *out,
					    burst_buffer_info_t *burst_buffer_ptr,
			 		    int one_liner,
					    int verbose);

/*
 * slurm_network_callerid - issue RPC to get the job id of a job from a remote
 * slurmd based upon network socket information.
 *
 * IN req - Information about network connection in question
 * OUT job_id -  ID of the job or NO_VAL
 * OUT node_name - name of the remote slurmd
 * IN node_name_size - size of the node_name buffer
 * RET SLURM_SUCCESS or SLURM_ERROR on error
 */
extern int slurm_network_callerid(network_callerid_msg_t req,
				  uint32_t *job_id,
				  char *node_name,
				  int node_name_size);

/*
 * Move the specified job ID to the top of the queue for a given user ID,
 *	partition, account, and QOS.
 * IN job_id_str - a job id
 * RET 0 or -1 on error */
extern int slurm_top_job(char *job_id_str);

/*
 * Fetch an auth token for a given username.
 * IN username - NULL, or a specific username if run as SlurmUser/root.
 * IN lifespan - lifespan the token should be valid for.
 */
extern char *slurm_fetch_token(char *username, int lifespan);

/*****************************************************************************\
 *      SLURM FEDERATION FUNCTIONS
\*****************************************************************************/

/*
 * slurm_load_federation - issue RPC to get federation status from controller
 * IN/OUT fed_pptr - place to store returned federation information.
 * 		     slurmdb_federation_rec_t treated as a void pointer to since
 * 		     slurm.h doesn't have ties to slurmdb.h.
 * NOTE: Use slurm_destroy_federation_rec() to release the returned memory
 * RET 0 or -1 on error
 */
extern int slurm_load_federation(void **fed_pptr);

/*
 * slurm_print_federation - prints slurmdb_federation_rec_t (passed as void*
 * 			    since slurm.h doesn't know about slurmdb.h).
 */
extern void slurm_print_federation(void *fed);

/*
 * slurm_destroy_federation_rec - Release memory allocated by
 *				  slurm_load_federation()
 */
extern void slurm_destroy_federation_rec(void *fed);

/*****************************************************************************\
 *      SLURM CRONTAB FUNCTIONS
\*****************************************************************************/

extern int slurm_request_crontab(uid_t uid, char **crontab,
				 char **disabled_lines);

typedef struct {
	char *err_msg;
	char *failed_lines;
	uint32_t *jobids;
	uint32_t jobids_count;
	char *job_submit_user_msg;
	uint32_t return_code;
} crontab_update_response_msg_t;

extern crontab_update_response_msg_t *slurm_update_crontab(uid_t uid, gid_t gid,
							   char *crontab,
							   list_t *jobs);

extern int slurm_remove_crontab(uid_t uid, gid_t gid);

#ifdef __cplusplus
}
#endif

#endif
