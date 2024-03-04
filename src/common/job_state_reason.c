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
		.str = "PartitionNodeLimit",
	},
	[WAIT_PART_TIME_LIMIT] = {
		.str = "PartitionTimeLimit",
	},
	[WAIT_PART_DOWN] = {
		.str = "PartitionDown",
	},
	[WAIT_PART_INACTIVE] = {
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
		.str = "AssociationJobLimit",
	},
	[WAIT_ASSOC_RESOURCE_LIMIT] = {
		.str = "AssociationResourceLimit",
	},
	[WAIT_ASSOC_TIME_LIMIT] = {
		.str = "AssociationTimeLimit",
	},
	[WAIT_RESERVATION] = {
		.str = "Reservation",
	},
	[WAIT_NODE_NOT_AVAIL] = {
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
		.str = "InvalidAccount",
	},
	[FAIL_QOS] = {
		.str = "InvalidQOS",
	},
	[WAIT_QOS_THRES] = {
		.str = "QOSUsageThreshold",
	},
	[WAIT_QOS_JOB_LIMIT] = {
		.str = "QOSJobLimit",
	},
	[WAIT_QOS_RESOURCE_LIMIT] = {
		.str = "QOSResourceLimit",
	},
	[WAIT_QOS_TIME_LIMIT] = {
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
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpCpuLimit",
	},
	[WAIT_QOS_GRP_CPU_MIN] = {
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpCPUMinutesLimit",
	},
	[WAIT_QOS_GRP_CPU_RUN_MIN] = {
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpCPURunMinutesLimit",
	},
	[WAIT_QOS_GRP_JOB] = {
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpJobsLimit"
	},
	[WAIT_QOS_GRP_MEM] = {
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpMemLimit",
	},
	[WAIT_QOS_GRP_NODE] = {
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpNodeLimit",
	},
	[WAIT_QOS_GRP_SUB_JOB] = {
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpSubmitJobsLimit",
	},
	[WAIT_QOS_GRP_WALL] = {
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpWallLimit",
	},
	[WAIT_QOS_MAX_CPU_PER_JOB] = {
		.str = "QOSMaxCpuPerJobLimit",
	},
	[WAIT_QOS_MAX_CPU_MINS_PER_JOB] = {
		.str = "QOSMaxCpuMinutesPerJobLimit",
	},
	[WAIT_QOS_MAX_NODE_PER_JOB] = {
		.str = "QOSMaxNodePerJobLimit",
	},
	[WAIT_QOS_MAX_WALL_PER_JOB] = {
		.str = "QOSMaxWallDurationPerJobLimit",
	},
	[WAIT_QOS_MAX_CPU_PER_USER] = {
		.str = "QOSMaxCpuPerUserLimit",
	},
	[WAIT_QOS_MAX_JOB_PER_USER] = {
		.str = "QOSMaxJobsPerUserLimit",
	},
	[WAIT_QOS_MAX_NODE_PER_USER] = {
		.str = "QOSMaxNodePerUserLimit",
	},
	[WAIT_QOS_MAX_SUB_JOB] = {
		.str = "QOSMaxSubmitJobPerUserLimit",
	},
	[WAIT_QOS_MIN_CPU] = {
		.str = "QOSMinCpuNotSatisfied",
	},
	[WAIT_ASSOC_GRP_CPU] = {
		.str = "AssocGrpCpuLimit",
	},
	[WAIT_ASSOC_GRP_CPU_MIN] = {
		.str = "AssocGrpCPUMinutesLimit",
	},
	[WAIT_ASSOC_GRP_CPU_RUN_MIN] = {
		.str = "AssocGrpCPURunMinutesLimit",
	},
	[WAIT_ASSOC_GRP_JOB] = {
		.str = "AssocGrpJobsLimit"
	},
	[WAIT_ASSOC_GRP_MEM] = {
		.str = "AssocGrpMemLimit",
	},
	[WAIT_ASSOC_GRP_NODE] = {
		.str = "AssocGrpNodeLimit",
	},
	[WAIT_ASSOC_GRP_SUB_JOB] = {
		.str = "AssocGrpSubmitJobsLimit",
	},
	[WAIT_ASSOC_GRP_WALL] = {
		.str = "AssocGrpWallLimit",
	},
	[WAIT_ASSOC_MAX_JOBS] = {
		.str = "AssocMaxJobsLimit",
	},
	[WAIT_ASSOC_MAX_CPU_PER_JOB] = {
		.str = "AssocMaxCpuPerJobLimit",
	},
	[WAIT_ASSOC_MAX_CPU_MINS_PER_JOB] = {
		.str = "AssocMaxCpuMinutesPerJobLimit",
	},
	[WAIT_ASSOC_MAX_NODE_PER_JOB] = {
		.str = "AssocMaxNodePerJobLimit",
	},
	[WAIT_ASSOC_MAX_WALL_PER_JOB] = {
		.str = "AssocMaxWallDurationPerJobLimit",
	},
	[WAIT_ASSOC_MAX_SUB_JOB] = {
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
	[WAIT_POWER_NOT_AVAIL] = {
		.str = "PowerNotAvail",
	},
	[WAIT_POWER_RESERVED] = {
		.str = "PowerReserved",
	},
	[WAIT_ASSOC_GRP_UNK] = {
		.str = "AssocGrpUnknown",
	},
	[WAIT_ASSOC_GRP_UNK_MIN] = {
		.str = "AssocGrpUnknownMinutes",
	},
	[WAIT_ASSOC_GRP_UNK_RUN_MIN] = {
		.str = "AssocGrpUnknownRunMinutes",
	},
	[WAIT_ASSOC_MAX_UNK_PER_JOB] = {
		.str = "AssocMaxUnknownPerJob",
	},
	[WAIT_ASSOC_MAX_UNK_PER_NODE] = {
		.str = "AssocMaxUnknownPerNode",
	},
	[WAIT_ASSOC_MAX_UNK_MINS_PER_JOB] = {
		.str = "AssocMaxUnknownMinutesPerJob",
	},
	[WAIT_ASSOC_MAX_CPU_PER_NODE] = {
		.str = "AssocMaxCpuPerNode",
	},
	[WAIT_ASSOC_GRP_MEM_MIN] = {
		.str = "AssocGrpMemMinutes",
	},
	[WAIT_ASSOC_GRP_MEM_RUN_MIN] = {
		.str = "AssocGrpMemRunMinutes",
	},
	[WAIT_ASSOC_MAX_MEM_PER_JOB] = {
		.str = "AssocMaxMemPerJob",
	},
	[WAIT_ASSOC_MAX_MEM_PER_NODE] = {
		.str = "AssocMaxMemPerNode",
	},
	[WAIT_ASSOC_MAX_MEM_MINS_PER_JOB] = {
		.str = "AssocMaxMemMinutesPerJob",
	},
	[WAIT_ASSOC_GRP_NODE_MIN] = {
		.str = "AssocGrpNodeMinutes",
	},
	[WAIT_ASSOC_GRP_NODE_RUN_MIN] = {
		.str = "AssocGrpNodeRunMinutes",
	},
	[WAIT_ASSOC_MAX_NODE_MINS_PER_JOB] = {
		.str = "AssocMaxNodeMinutesPerJob",
	},
	[WAIT_ASSOC_GRP_ENERGY] = {
		.str = "AssocGrpEnergy",
	},
	[WAIT_ASSOC_GRP_ENERGY_MIN] = {
		.str = "AssocGrpEnergyMinutes",
	},
	[WAIT_ASSOC_GRP_ENERGY_RUN_MIN] = {
		.str = "AssocGrpEnergyRunMinutes",
	},
	[WAIT_ASSOC_MAX_ENERGY_PER_JOB] = {
		.str = "AssocMaxEnergyPerJob",
	},
	[WAIT_ASSOC_MAX_ENERGY_PER_NODE] = {
		.str = "AssocMaxEnergyPerNode",
	},
	[WAIT_ASSOC_MAX_ENERGY_MINS_PER_JOB] = {
		.str = "AssocMaxEnergyMinutesPerJob",
	},
	[WAIT_ASSOC_GRP_GRES] = {
		.str = "AssocGrpGRES",
	},
	[WAIT_ASSOC_GRP_GRES_MIN] = {
		.str = "AssocGrpGRESMinutes",
	},
	[WAIT_ASSOC_GRP_GRES_RUN_MIN] = {
		.str = "AssocGrpGRESRunMinutes",
	},
	[WAIT_ASSOC_MAX_GRES_PER_JOB] = {
		.str = "AssocMaxGRESPerJob",
	},
	[WAIT_ASSOC_MAX_GRES_PER_NODE] = {
		.str = "AssocMaxGRESPerNode",
	},
	[WAIT_ASSOC_MAX_GRES_MINS_PER_JOB] = {
		.str = "AssocMaxGRESMinutesPerJob",
	},
	[WAIT_ASSOC_GRP_LIC] = {
		.str = "AssocGrpLicense",
	},
	[WAIT_ASSOC_GRP_LIC_MIN] = {
		.str = "AssocGrpLicenseMinutes",
	},
	[WAIT_ASSOC_GRP_LIC_RUN_MIN] = {
		.str = "AssocGrpLicenseRunMinutes",
	},
	[WAIT_ASSOC_MAX_LIC_PER_JOB] = {
		.str = "AssocMaxLicensePerJob",
	},
	[WAIT_ASSOC_MAX_LIC_MINS_PER_JOB] = {
		.str = "AssocMaxLicenseMinutesPerJob",
	},
	[WAIT_ASSOC_GRP_BB] = {
		.str = "AssocGrpBB",
	},
	[WAIT_ASSOC_GRP_BB_MIN] = {
		.str = "AssocGrpBBMinutes",
	},
	[WAIT_ASSOC_GRP_BB_RUN_MIN] = {
		.str = "AssocGrpBBRunMinutes",
	},
	[WAIT_ASSOC_MAX_BB_PER_JOB] = {
		.str = "AssocMaxBBPerJob",
	},
	[WAIT_ASSOC_MAX_BB_PER_NODE] = {
		.str = "AssocMaxBBPerNode",
	},
	[WAIT_ASSOC_MAX_BB_MINS_PER_JOB] = {
		.str = "AssocMaxBBMinutesPerJob",
	},
	[WAIT_QOS_GRP_UNK] = {
		.str = "QOSGrpUnknown",
	},
	[WAIT_QOS_GRP_UNK_MIN] = {
		.str = "QOSGrpUnknownMinutes",
	},
	[WAIT_QOS_GRP_UNK_RUN_MIN] = {
		.str = "QOSGrpUnknownRunMinutes",
	},
	[WAIT_QOS_MAX_UNK_PER_JOB] = {
		.str = "QOSMaxUnknownPerJob",
	},
	[WAIT_QOS_MAX_UNK_PER_NODE] = {
		.str = "QOSMaxUnknownPerNode",
	},
	[WAIT_QOS_MAX_UNK_PER_USER] = {
		.str = "QOSMaxUnknownPerUser",
	},
	[WAIT_QOS_MAX_UNK_MINS_PER_JOB] = {
		.str = "QOSMaxUnknownMinutesPerJob",
	},
	[WAIT_QOS_MIN_UNK] = {
		.str = "QOSMinUnknown",
	},
	[WAIT_QOS_MAX_CPU_PER_NODE] = {
		.str = "QOSMaxCpuPerNode",
	},
	[WAIT_QOS_GRP_MEM_MIN] = {
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpMemoryMinutes",
	},
	[WAIT_QOS_GRP_MEM_RUN_MIN] = {
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpMemoryRunMinutes",
	},
	[WAIT_QOS_MAX_MEM_PER_JOB] = {
		.str = "QOSMaxMemoryPerJob",
	},
	[WAIT_QOS_MAX_MEM_PER_NODE] = {
		.str = "QOSMaxMemoryPerNode",
	},
	[WAIT_QOS_MAX_MEM_PER_USER] = {
		.str = "QOSMaxMemoryPerUser",
	},
	[WAIT_QOS_MAX_MEM_MINS_PER_JOB] = {
		.str = "QOSMaxMemoryMinutesPerJob",
	},
	[WAIT_QOS_MIN_MEM] = {
		.str = "QOSMinMemory",
	},
	[WAIT_QOS_GRP_NODE_MIN] = {
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpNodeMinutes",
	},
	[WAIT_QOS_GRP_NODE_RUN_MIN] = {
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpNodeRunMinutes",
	},
	[WAIT_QOS_MAX_NODE_MINS_PER_JOB] = {
		.str = "QOSMaxNodeMinutesPerJob",
	},
	[WAIT_QOS_MIN_NODE] = {
		.str = "QOSMinNode",
	},
	[WAIT_QOS_GRP_ENERGY] = {
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpEnergy",
	},
	[WAIT_QOS_GRP_ENERGY_MIN] = {
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpEnergyMinutes",
	},
	[WAIT_QOS_GRP_ENERGY_RUN_MIN] = {
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpEnergyRunMinutes",
	},
	[WAIT_QOS_MAX_ENERGY_PER_JOB] = {
		.str = "QOSMaxEnergyPerJob",
	},
	[WAIT_QOS_MAX_ENERGY_PER_NODE] = {
		.str = "QOSMaxEnergyPerNode",
	},
	[WAIT_QOS_MAX_ENERGY_PER_USER] = {
		.str = "QOSMaxEnergyPerUser",
	},
	[WAIT_QOS_MAX_ENERGY_MINS_PER_JOB] = {
		.str = "QOSMaxEnergyMinutesPerJob",
	},
	[WAIT_QOS_MIN_ENERGY] = {
		.str = "QOSMinEnergy",
	},
	[WAIT_QOS_GRP_GRES] = {
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpGRES",
	},
	[WAIT_QOS_GRP_GRES_MIN] = {
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpGRESMinutes",
	},
	[WAIT_QOS_GRP_GRES_RUN_MIN] = {
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpGRESRunMinutes",
	},
	[WAIT_QOS_MAX_GRES_PER_JOB] = {
		.str = "QOSMaxGRESPerJob",
	},
	[WAIT_QOS_MAX_GRES_PER_NODE] = {
		.str = "QOSMaxGRESPerNode",
	},
	[WAIT_QOS_MAX_GRES_PER_USER] = {
		.str = "QOSMaxGRESPerUser",
	},
	[WAIT_QOS_MAX_GRES_MINS_PER_JOB] = {
		.str = "QOSMaxGRESMinutesPerJob",
	},
	[WAIT_QOS_MIN_GRES] = {
		.str = "QOSMinGRES",
	},
	[WAIT_QOS_GRP_LIC] = {
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpLicense",
	},
	[WAIT_QOS_GRP_LIC_MIN] = {
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpLicenseMinutes",
	},
	[WAIT_QOS_GRP_LIC_RUN_MIN] = {
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpLicenseRunMinutes",
	},
	[WAIT_QOS_MAX_LIC_PER_JOB] = {
		.str = "QOSMaxLicensePerJob",
	},
	[WAIT_QOS_MAX_LIC_PER_USER] = {
		.str = "QOSMaxLicensePerUser",
	},
	[WAIT_QOS_MAX_LIC_MINS_PER_JOB] = {
		.str = "QOSMaxLicenseMinutesPerJob",
	},
	[WAIT_QOS_MIN_LIC] = {
		.str = "QOSMinLicense",
	},
	[WAIT_QOS_GRP_BB] = {
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpBB",
	},
	[WAIT_QOS_GRP_BB_MIN] = {
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpBBMinutes",
	},
	[WAIT_QOS_GRP_BB_RUN_MIN] = {
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpBBRunMinutes",
	},
	[WAIT_QOS_MAX_BB_PER_JOB] = {
		.str = "QOSMaxBBPerJob",
	},
	[WAIT_QOS_MAX_BB_PER_NODE] = {
		.str = "QOSMaxBBPerNode",
	},
	[WAIT_QOS_MAX_BB_PER_USER] = {
		.str = "QOSMaxBBPerUser",
	},
	[WAIT_QOS_MAX_BB_MINS_PER_JOB] = {
		.str = "AssocMaxBBMinutesPerJob",
	},
	[WAIT_QOS_MIN_BB] = {
		.str = "QOSMinBB",
	},
	[FAIL_DEADLINE] = {
		.str = "DeadLine",
	},
	[WAIT_QOS_MAX_BB_PER_ACCT] = {
		.str = "MaxBBPerAccount",
	},
	[WAIT_QOS_MAX_CPU_PER_ACCT] = {
		.str = "MaxCpuPerAccount",
	},
	[WAIT_QOS_MAX_ENERGY_PER_ACCT] = {
		.str = "MaxEnergyPerAccount",
	},
	[WAIT_QOS_MAX_GRES_PER_ACCT] = {
		.str = "MaxGRESPerAccount",
	},
	[WAIT_QOS_MAX_NODE_PER_ACCT] = {
		.str = "MaxNodePerAccount",
	},
	[WAIT_QOS_MAX_LIC_PER_ACCT] = {
		.str = "MaxLicensePerAccount",
	},
	[WAIT_QOS_MAX_MEM_PER_ACCT] = {
		.str = "MaxMemoryPerAccount",
	},
	[WAIT_QOS_MAX_UNK_PER_ACCT] = {
		.str = "MaxUnknownPerAccount",
	},
	[WAIT_QOS_MAX_JOB_PER_ACCT] = {
		.str = "MaxJobsPerAccount",
	},
	[WAIT_QOS_MAX_SUB_JOB_PER_ACCT] = {
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
		.str = "AssocGrpBilling",
	},
	[WAIT_ASSOC_GRP_BILLING_MIN] = {
		.str = "AssocGrpBillingMinutes",
	},
	[WAIT_ASSOC_GRP_BILLING_RUN_MIN] = {
		.str = "AssocGrpBillingRunMinutes",
	},
	[WAIT_ASSOC_MAX_BILLING_PER_JOB] = {
		.str = "AssocMaxBillingPerJob",
	},
	[WAIT_ASSOC_MAX_BILLING_PER_NODE] = {
		.str = "AssocMaxBillingPerNode",
	},
	[WAIT_ASSOC_MAX_BILLING_MINS_PER_JOB] = {
		.str = "AssocMaxBillingMinutesPerJob",
	},
	[WAIT_QOS_GRP_BILLING] = {
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpBilling",
	},
	[WAIT_QOS_GRP_BILLING_MIN] = {
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpBillingMinutes",
	},
	[WAIT_QOS_GRP_BILLING_RUN_MIN] = {
		.flags = JSR_QOS_GRP,
		.str = "QOSGrpBillingRunMinutes",
	},
	[WAIT_QOS_MAX_BILLING_PER_JOB] = {
		.str = "QOSMaxBillingPerJob",
	},
	[WAIT_QOS_MAX_BILLING_PER_NODE] = {
		.str = "QOSMaxBillingPerNode",
	},
	[WAIT_QOS_MAX_BILLING_PER_USER] = {
		.str = "QOSMaxBillingPerUser",
	},
	[WAIT_QOS_MAX_BILLING_MINS_PER_JOB] = {
		.str = "QOSMaxBillingMinutesPerJob",
	},
	[WAIT_QOS_MAX_BILLING_PER_ACCT] = {
		.str = "MaxBillingPerAccount",
	},
	[WAIT_QOS_MIN_BILLING] = {
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

extern bool job_state_reason_qos_grp_limit(enum job_state_reason state_reason)
{
	xassert(state_reason < REASON_END);

	if (jsra[state_reason].flags & JSR_QOS_GRP)
		return true;

	return false;
}
