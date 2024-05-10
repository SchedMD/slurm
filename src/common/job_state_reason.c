/*****************************************************************************\
 *  job_state_reason.c
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#include "src/common/job_state_reason.h"
#include "src/common/xstring.h"
#include "src/common/xassert.h"

/*
 * Any strong_alias() needs to be defined in slurm_xlator.h as well.
 */
strong_alias(job_state_reason_string, slurm_job_state_reason_string);
strong_alias(job_state_reason_num, slurm_job_state_reason_num);
strong_alias(job_state_reason_check, slurm_job_state_reason_check);

typedef struct {
	uint32_t flags;
	const char *str;
} entry_t;

const static entry_t jsra[] = {
	[WAIT_NO_REASON] = {
		.str = "None",
	},
	[WAIT_PROLOG] = {
		.str = "Prolog",
	},
	[WAIT_PRIORITY] = {
		.str = "Priority",
	},
	[WAIT_DEPENDENCY] = {
		.str = "Dependency",
	},
	[WAIT_RESOURCES] = {
		.str = "Resources",
	},
	[WAIT_PART_NODE_LIMIT] = {
		.flags = JSR_PART,
		.str = "PartitionNodeLimit",
	},
	[WAIT_PART_TIME_LIMIT] = {
		.flags = JSR_PART,
		.str = "PartitionTimeLimit",
	},
	[WAIT_PART_DOWN] = {
		.flags = JSR_PART,
		.str = "PartitionDown",
	},
	[WAIT_PART_INACTIVE] = {
		.flags = JSR_PART,
		.str = "PartitionInactive",
	},
	[WAIT_HELD] = {
		.str = "JobHeldAdmin",
	},
	[WAIT_HELD_USER] = {
		.str = "JobHeldUser",
	},
	[WAIT_TIME] = {
		.str = "BeginTime",
	},
	[WAIT_LICENSES] = {
		.str = "Licenses",
	},
	[WAIT_ASSOC_JOB_LIMIT] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssociationJobLimit",
	},
	[WAIT_ASSOC_RESOURCE_LIMIT] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssociationResourceLimit",
	},
	[WAIT_ASSOC_TIME_LIMIT] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssociationTimeLimit",
	},
	[WAIT_RESERVATION] = {
		.str = "Reservation",
	},
	[WAIT_NODE_NOT_AVAIL] = {
		.flags = JSR_MISC,
		.str = "ReqNodeNotAvail",
	},
	[WAIT_FRONT_END] = {
		.str = "FrontEndDown",
	},
	[FAIL_DEFER] = {
		.str = "SchedDefer",
	},
	[FAIL_DOWN_PARTITION] = {
		.str = "PartitionDown",
	},
	[FAIL_DOWN_NODE] = {
		.str = "NodeDown",
	},
	[FAIL_BAD_CONSTRAINTS] = {
		.str = "BadConstraints",
	},
	[FAIL_SYSTEM] = {
		.str = "SystemFailure",
	},
	[FAIL_LAUNCH] = {
		.str = "JobLaunchFailure",
	},
	[FAIL_EXIT_CODE] = {
		.str = "NonZeroExitCode",
	},
	[FAIL_SIGNAL] = {
		.str = "RaisedSignal",
	},
	[FAIL_TIMEOUT] = {
		.str = "TimeLimit",
	},
	[FAIL_INACTIVE_LIMIT] = {
		.str = "InactiveLimit",
	},
	[FAIL_ACCOUNT] = {
		.flags = JSR_MISC,
		.str = "InvalidAccount",
	},
	[FAIL_QOS] = {
		.flags = JSR_MISC,
		.str = "InvalidQOS",
	},
	[WAIT_QOS_THRES] = {
		.flags = JSR_QOS_ASSOC | JSR_PART,
		.str = "QOSUsageThreshold",
	},
	[WAIT_QOS_JOB_LIMIT] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSJobLimit",
	},
	[WAIT_QOS_RESOURCE_LIMIT] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSResourceLimit",
	},
	[WAIT_QOS_TIME_LIMIT] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSTimeLimit",
	},
	[WAIT_CLEANING] = {
		.str = "Cleaning",
	},
	[WAIT_QOS] = {
		.str = "QOSNotAllowed",
	},
	[WAIT_ACCOUNT] = {
		.str = "AccountNotAllowed",
	},
	[WAIT_DEP_INVALID] = {
		.str = "DependencyNeverSatisfied",
	},
	[WAIT_QOS_GRP_CPU] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpCpuLimit",
	},
	[WAIT_QOS_GRP_CPU_MIN] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpCPUMinutesLimit",
	},
	[WAIT_QOS_GRP_CPU_RUN_MIN] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpCPURunMinutesLimit",
	},
	[WAIT_QOS_GRP_JOB] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpJobsLimit"
	},
	[WAIT_QOS_GRP_MEM] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpMemLimit",
	},
	[WAIT_QOS_GRP_NODE] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpNodeLimit",
	},
	[WAIT_QOS_GRP_SUB_JOB] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpSubmitJobsLimit",
	},
	[WAIT_QOS_GRP_WALL] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpWallLimit",
	},
	[WAIT_QOS_MAX_CPU_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxCpuPerJobLimit",
	},
	[WAIT_QOS_MAX_CPU_MINS_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxCpuMinutesPerJobLimit",
	},
	[WAIT_QOS_MAX_NODE_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxNodePerJobLimit",
	},
	[WAIT_QOS_MAX_WALL_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxWallDurationPerJobLimit",
	},
	[WAIT_QOS_MAX_CPU_PER_USER] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxCpuPerUserLimit",
	},
	[WAIT_QOS_MAX_JOB_PER_USER] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxJobsPerUserLimit",
	},
	[WAIT_QOS_MAX_NODE_PER_USER] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxNodePerUserLimit",
	},
	[WAIT_QOS_MAX_SUB_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxSubmitJobPerUserLimit",
	},
	[WAIT_QOS_MIN_CPU] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMinCpuNotSatisfied",
	},
	[WAIT_ASSOC_GRP_CPU] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpCpuLimit",
	},
	[WAIT_ASSOC_GRP_CPU_MIN] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpCPUMinutesLimit",
	},
	[WAIT_ASSOC_GRP_CPU_RUN_MIN] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpCPURunMinutesLimit",
	},
	[WAIT_ASSOC_GRP_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpJobsLimit"
	},
	[WAIT_ASSOC_GRP_MEM] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpMemLimit",
	},
	[WAIT_ASSOC_GRP_NODE] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpNodeLimit",
	},
	[WAIT_ASSOC_GRP_SUB_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpSubmitJobsLimit",
	},
	[WAIT_ASSOC_GRP_WALL] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpWallLimit",
	},
	[WAIT_ASSOC_MAX_JOBS] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxJobsLimit",
	},
	[WAIT_ASSOC_MAX_CPU_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxCpuPerJobLimit",
	},
	[WAIT_ASSOC_MAX_CPU_MINS_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxCpuMinutesPerJobLimit",
	},
	[WAIT_ASSOC_MAX_NODE_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxNodePerJobLimit",
	},
	[WAIT_ASSOC_MAX_WALL_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxWallDurationPerJobLimit",
	},
	[WAIT_ASSOC_MAX_SUB_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxSubmitJobLimit",
	},
	[WAIT_MAX_REQUEUE] = {
		.str = "JobHoldMaxRequeue",
	},
	[WAIT_ARRAY_TASK_LIMIT] = {
		.str = "JobArrayTaskLimit",
	},
	[WAIT_BURST_BUFFER_RESOURCE] = {
		.str = "BurstBufferResources",
	},
	[WAIT_BURST_BUFFER_STAGING] = {
		.str = "BurstBufferStageIn",
	},
	[FAIL_BURST_BUFFER_OP] = {
		.str = "BurstBufferOperation",
	},
	[WAIT_ASSOC_GRP_UNK] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpUnknown",
	},
	[WAIT_ASSOC_GRP_UNK_MIN] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpUnknownMinutes",
	},
	[WAIT_ASSOC_GRP_UNK_RUN_MIN] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpUnknownRunMinutes",
	},
	[WAIT_ASSOC_MAX_UNK_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxUnknownPerJob",
	},
	[WAIT_ASSOC_MAX_UNK_PER_NODE] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxUnknownPerNode",
	},
	[WAIT_ASSOC_MAX_UNK_MINS_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxUnknownMinutesPerJob",
	},
	[WAIT_ASSOC_MAX_CPU_PER_NODE] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxCpuPerNode",
	},
	[WAIT_ASSOC_GRP_MEM_MIN] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpMemMinutes",
	},
	[WAIT_ASSOC_GRP_MEM_RUN_MIN] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpMemRunMinutes",
	},
	[WAIT_ASSOC_MAX_MEM_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxMemPerJob",
	},
	[WAIT_ASSOC_MAX_MEM_PER_NODE] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxMemPerNode",
	},
	[WAIT_ASSOC_MAX_MEM_MINS_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxMemMinutesPerJob",
	},
	[WAIT_ASSOC_GRP_NODE_MIN] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpNodeMinutes",
	},
	[WAIT_ASSOC_GRP_NODE_RUN_MIN] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpNodeRunMinutes",
	},
	[WAIT_ASSOC_MAX_NODE_MINS_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxNodeMinutesPerJob",
	},
	[WAIT_ASSOC_GRP_ENERGY] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpEnergy",
	},
	[WAIT_ASSOC_GRP_ENERGY_MIN] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpEnergyMinutes",
	},
	[WAIT_ASSOC_GRP_ENERGY_RUN_MIN] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpEnergyRunMinutes",
	},
	[WAIT_ASSOC_MAX_ENERGY_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxEnergyPerJob",
	},
	[WAIT_ASSOC_MAX_ENERGY_PER_NODE] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxEnergyPerNode",
	},
	[WAIT_ASSOC_MAX_ENERGY_MINS_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxEnergyMinutesPerJob",
	},
	[WAIT_ASSOC_GRP_GRES] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpGRES",
	},
	[WAIT_ASSOC_GRP_GRES_MIN] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpGRESMinutes",
	},
	[WAIT_ASSOC_GRP_GRES_RUN_MIN] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpGRESRunMinutes",
	},
	[WAIT_ASSOC_MAX_GRES_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxGRESPerJob",
	},
	[WAIT_ASSOC_MAX_GRES_PER_NODE] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxGRESPerNode",
	},
	[WAIT_ASSOC_MAX_GRES_MINS_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxGRESMinutesPerJob",
	},
	[WAIT_ASSOC_GRP_LIC] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpLicense",
	},
	[WAIT_ASSOC_GRP_LIC_MIN] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpLicenseMinutes",
	},
	[WAIT_ASSOC_GRP_LIC_RUN_MIN] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpLicenseRunMinutes",
	},
	[WAIT_ASSOC_MAX_LIC_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxLicensePerJob",
	},
	[WAIT_ASSOC_MAX_LIC_MINS_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxLicenseMinutesPerJob",
	},
	[WAIT_ASSOC_GRP_BB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpBB",
	},
	[WAIT_ASSOC_GRP_BB_MIN] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpBBMinutes",
	},
	[WAIT_ASSOC_GRP_BB_RUN_MIN] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpBBRunMinutes",
	},
	[WAIT_ASSOC_MAX_BB_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxBBPerJob",
	},
	[WAIT_ASSOC_MAX_BB_PER_NODE] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxBBPerNode",
	},
	[WAIT_ASSOC_MAX_BB_MINS_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxBBMinutesPerJob",
	},
	[WAIT_QOS_GRP_UNK] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpUnknown",
	},
	[WAIT_QOS_GRP_UNK_MIN] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpUnknownMinutes",
	},
	[WAIT_QOS_GRP_UNK_RUN_MIN] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpUnknownRunMinutes",
	},
	[WAIT_QOS_MAX_UNK_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxUnknownPerJob",
	},
	[WAIT_QOS_MAX_UNK_PER_NODE] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxUnknownPerNode",
	},
	[WAIT_QOS_MAX_UNK_PER_USER] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxUnknownPerUser",
	},
	[WAIT_QOS_MAX_UNK_MINS_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxUnknownMinutesPerJob",
	},
	[WAIT_QOS_MIN_UNK] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMinUnknown",
	},
	[WAIT_QOS_MAX_CPU_PER_NODE] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxCpuPerNode",
	},
	[WAIT_QOS_GRP_MEM_MIN] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpMemoryMinutes",
	},
	[WAIT_QOS_GRP_MEM_RUN_MIN] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpMemoryRunMinutes",
	},
	[WAIT_QOS_MAX_MEM_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxMemoryPerJob",
	},
	[WAIT_QOS_MAX_MEM_PER_NODE] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxMemoryPerNode",
	},
	[WAIT_QOS_MAX_MEM_PER_USER] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxMemoryPerUser",
	},
	[WAIT_QOS_MAX_MEM_MINS_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxMemoryMinutesPerJob",
	},
	[WAIT_QOS_MIN_MEM] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMinMemory",
	},
	[WAIT_QOS_GRP_NODE_MIN] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpNodeMinutes",
	},
	[WAIT_QOS_GRP_NODE_RUN_MIN] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpNodeRunMinutes",
	},
	[WAIT_QOS_MAX_NODE_MINS_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxNodeMinutesPerJob",
	},
	[WAIT_QOS_MIN_NODE] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMinNode",
	},
	[WAIT_QOS_GRP_ENERGY] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpEnergy",
	},
	[WAIT_QOS_GRP_ENERGY_MIN] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpEnergyMinutes",
	},
	[WAIT_QOS_GRP_ENERGY_RUN_MIN] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpEnergyRunMinutes",
	},
	[WAIT_QOS_MAX_ENERGY_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxEnergyPerJob",
	},
	[WAIT_QOS_MAX_ENERGY_PER_NODE] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxEnergyPerNode",
	},
	[WAIT_QOS_MAX_ENERGY_PER_USER] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxEnergyPerUser",
	},
	[WAIT_QOS_MAX_ENERGY_MINS_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxEnergyMinutesPerJob",
	},
	[WAIT_QOS_MIN_ENERGY] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMinEnergy",
	},
	[WAIT_QOS_GRP_GRES] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpGRES",
	},
	[WAIT_QOS_GRP_GRES_MIN] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpGRESMinutes",
	},
	[WAIT_QOS_GRP_GRES_RUN_MIN] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpGRESRunMinutes",
	},
	[WAIT_QOS_MAX_GRES_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxGRESPerJob",
	},
	[WAIT_QOS_MAX_GRES_PER_NODE] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxGRESPerNode",
	},
	[WAIT_QOS_MAX_GRES_PER_USER] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxGRESPerUser",
	},
	[WAIT_QOS_MAX_GRES_MINS_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxGRESMinutesPerJob",
	},
	[WAIT_QOS_MIN_GRES] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMinGRES",
	},
	[WAIT_QOS_GRP_LIC] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpLicense",
	},
	[WAIT_QOS_GRP_LIC_MIN] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpLicenseMinutes",
	},
	[WAIT_QOS_GRP_LIC_RUN_MIN] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpLicenseRunMinutes",
	},
	[WAIT_QOS_MAX_LIC_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxLicensePerJob",
	},
	[WAIT_QOS_MAX_LIC_PER_USER] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxLicensePerUser",
	},
	[WAIT_QOS_MAX_LIC_MINS_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxLicenseMinutesPerJob",
	},
	[WAIT_QOS_MIN_LIC] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMinLicense",
	},
	[WAIT_QOS_GRP_BB] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpBB",
	},
	[WAIT_QOS_GRP_BB_MIN] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpBBMinutes",
	},
	[WAIT_QOS_GRP_BB_RUN_MIN] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpBBRunMinutes",
	},
	[WAIT_QOS_MAX_BB_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxBBPerJob",
	},
	[WAIT_QOS_MAX_BB_PER_NODE] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxBBPerNode",
	},
	[WAIT_QOS_MAX_BB_PER_USER] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxBBPerUser",
	},
	[WAIT_QOS_MAX_BB_MINS_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxBBMinutesPerJob",
	},
	[WAIT_QOS_MIN_BB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMinBB",
	},
	[FAIL_DEADLINE] = {
		.str = "DeadLine",
	},
	[WAIT_QOS_MAX_BB_PER_ACCT] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxBBPerAccount",
	},
	[WAIT_QOS_MAX_CPU_PER_ACCT] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxCpuPerAccount",
	},
	[WAIT_QOS_MAX_ENERGY_PER_ACCT] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxEnergyPerAccount",
	},
	[WAIT_QOS_MAX_GRES_PER_ACCT] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxGRESPerAccount",
	},
	[WAIT_QOS_MAX_NODE_PER_ACCT] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxNodePerAccount",
	},
	[WAIT_QOS_MAX_LIC_PER_ACCT] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxLicensePerAccount",
	},
	[WAIT_QOS_MAX_MEM_PER_ACCT] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxMemoryPerAccount",
	},
	[WAIT_QOS_MAX_UNK_PER_ACCT] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxUnknownPerAccount",
	},
	[WAIT_QOS_MAX_JOB_PER_ACCT] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxJobsPerAccount",
	},
	[WAIT_QOS_MAX_SUB_JOB_PER_ACCT] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxSubmitJobsPerAccount",
	},
	[WAIT_PART_CONFIG] = {
		.str = "PartitionConfig",
	},
	[WAIT_ACCOUNT_POLICY] = {
		.str = "AccountingPolicy",
	},
	[WAIT_FED_JOB_LOCK] = {
		.str = "FedJobLock",
	},
	[FAIL_OOM] = {
		.str = "OutOfMemory",
	},
	[WAIT_PN_MEM_LIMIT] = {
		.str = "MaxMemPerLimit",
	},
	[WAIT_ASSOC_GRP_BILLING] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpBilling",
	},
	[WAIT_ASSOC_GRP_BILLING_MIN] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpBillingMinutes",
	},
	[WAIT_ASSOC_GRP_BILLING_RUN_MIN] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocGrpBillingRunMinutes",
	},
	[WAIT_ASSOC_MAX_BILLING_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxBillingPerJob",
	},
	[WAIT_ASSOC_MAX_BILLING_PER_NODE] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxBillingPerNode",
	},
	[WAIT_ASSOC_MAX_BILLING_MINS_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "AssocMaxBillingMinutesPerJob",
	},
	[WAIT_QOS_GRP_BILLING] = {
		.flags = JSR_QOS_GRP | JSR_QOS_ASSOC,
		.str = "QOSGrpBilling",
	},
	[WAIT_QOS_GRP_BILLING_MIN] = {
		.flags = JSR_QOS_GRP| JSR_QOS_ASSOC,
		.str = "QOSGrpBillingMinutes",
	},
	[WAIT_QOS_GRP_BILLING_RUN_MIN] = {
		.flags = JSR_QOS_GRP| JSR_QOS_ASSOC,
		.str = "QOSGrpBillingRunMinutes",
	},
	[WAIT_QOS_MAX_BILLING_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxBillingPerJob",
	},
	[WAIT_QOS_MAX_BILLING_PER_NODE] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxBillingPerNode",
	},
	[WAIT_QOS_MAX_BILLING_PER_USER] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxBillingPerUser",
	},
	[WAIT_QOS_MAX_BILLING_MINS_PER_JOB] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMaxBillingMinutesPerJob",
	},
	[WAIT_QOS_MAX_BILLING_PER_ACCT] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxBillingPerAccount",
	},
	[WAIT_QOS_MIN_BILLING] = {
		.flags = JSR_QOS_ASSOC,
		.str = "QOSMinBilling",
	},
	[WAIT_RESV_DELETED] = {
		.str = "ReservationDeleted",
	},
	[WAIT_RESV_INVALID] = {
		.str = "ReservationInvalid",
	},
	[FAIL_CONSTRAINTS] = {
		.str = "Constraints",
	},
	[WAIT_QOS_MAX_BB_RUN_MINS_PER_ACCT] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxBBRunMinsPerAccount",
	},
	[WAIT_QOS_MAX_BILLING_RUN_MINS_PER_ACCT] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxBillingRunMinsPerAccount",
	},
	[WAIT_QOS_MAX_CPU_RUN_MINS_PER_ACCT] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxCpuRunMinsPerAccount",
	},
	[WAIT_QOS_MAX_ENERGY_RUN_MINS_PER_ACCT] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxEnergyRunMinsPerAccount",
	},
	[WAIT_QOS_MAX_GRES_RUN_MINS_PER_ACCT] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxGRESRunMinsPerAccount",
	},
	[WAIT_QOS_MAX_NODE_RUN_MINS_PER_ACCT] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxNodeRunMinsPerAccount",
	},
	[WAIT_QOS_MAX_LIC_RUN_MINS_PER_ACCT] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxLicenseRunMinsPerAccount",
	},
	[WAIT_QOS_MAX_MEM_RUN_MINS_PER_ACCT] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxMemoryRunMinsPerAccount",
	},
	[WAIT_QOS_MAX_UNK_RUN_MINS_PER_ACCT] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxUnknownRunMinsPerAccount",
	},
	[WAIT_QOS_MAX_BB_RUN_MINS_PER_USER] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxBBRunMinsPerUser",
	},
	[WAIT_QOS_MAX_BILLING_RUN_MINS_PER_USER] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxBillingRunMinsPerUser",
	},
	[WAIT_QOS_MAX_CPU_RUN_MINS_PER_USER] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxCpuRunMinsPerUser",
	},
	[WAIT_QOS_MAX_ENERGY_RUN_MINS_PER_USER] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxEnergyRunMinsPerUser",
	},
	[WAIT_QOS_MAX_GRES_RUN_MINS_PER_USER] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxGRESRunMinsPerUser",
	},
	[WAIT_QOS_MAX_NODE_RUN_MINS_PER_USER] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxNodeRunMinsPerUser",
	},
	[WAIT_QOS_MAX_LIC_RUN_MINS_PER_USER] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxLicenseRunMinsPerUser",
	},
	[WAIT_QOS_MAX_MEM_RUN_MINS_PER_USER] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxMemoryRunMinsPerUser",
	},
	[WAIT_QOS_MAX_UNK_RUN_MINS_PER_USER] = {
		.flags = JSR_QOS_ASSOC,
		.str = "MaxUnknownRunMinsPerUser",
	},
	[WAIT_MAX_POWERED_NODES] = {
		.flags = JSR_MISC,
		.str = "MaxPoweredUpNodes",
	},
	[WAIT_MPI_PORTS_BUSY] = {
		.str = "MpiPortsBusy",
	},
};

extern const char *job_state_reason_string(enum job_state_reason inx)
{
	const char *ret_str = "InvaildReason";

	if ((inx < REASON_END) && jsra[inx].str)
		ret_str = jsra[inx].str;

	return ret_str;
}

extern enum job_state_reason job_state_reason_num(char *reason)
{
	for (int inx = 0; inx < REASON_END; inx++) {
		if (!xstrcasecmp(reason, jsra[inx].str))
			return inx;
	}

	return NO_VAL;
}

extern bool job_state_reason_check(enum job_state_reason inx, uint32_t flags)
{
	xassert(inx < REASON_END);

	if (jsra[inx].flags & flags)
		return true;

	return false;
}
