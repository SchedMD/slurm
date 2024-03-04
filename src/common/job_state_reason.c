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

/*
 * Any strong_alias() needs to be defined in slurm_xlator.h as well.
 */
strong_alias(job_state_reason_string, slurm_job_state_reason_string);
strong_alias(job_state_reason_num, slurm_job_state_reason_num);

extern char *job_state_reason_string(enum job_state_reason inx)
{
	static char val[32];

	switch (inx) {
	case WAIT_NO_REASON:
		return "None";
	case WAIT_PROLOG:
		return "Prolog";
	case WAIT_PRIORITY:
		return "Priority";
	case WAIT_DEPENDENCY:
		return "Dependency";
	case WAIT_RESOURCES:
		return "Resources";
	case WAIT_PART_NODE_LIMIT:
		return "PartitionNodeLimit";
	case WAIT_PART_TIME_LIMIT:
		return "PartitionTimeLimit";
	case WAIT_PART_DOWN:
		return "PartitionDown";
	case WAIT_PART_INACTIVE:
		return "PartitionInactive";
	case WAIT_HELD:
		return "JobHeldAdmin";
	case WAIT_HELD_USER:
		return "JobHeldUser";
	case WAIT_TIME:
		return "BeginTime";
	case WAIT_LICENSES:
		return "Licenses";
	case WAIT_ASSOC_JOB_LIMIT:
		return "AssociationJobLimit";
	case WAIT_ASSOC_RESOURCE_LIMIT:
		return "AssociationResourceLimit";
	case WAIT_ASSOC_TIME_LIMIT:
		return "AssociationTimeLimit";
	case WAIT_RESERVATION:
		return "Reservation";
	case WAIT_NODE_NOT_AVAIL:
		return "ReqNodeNotAvail";
	case WAIT_FRONT_END:
		return "FrontEndDown";
	case FAIL_DEFER:
		return "SchedDefer";
	case FAIL_DOWN_PARTITION:
		return "PartitionDown";
	case FAIL_DOWN_NODE:
		return "NodeDown";
	case FAIL_BAD_CONSTRAINTS:
		return "BadConstraints";
	case FAIL_SYSTEM:
		return "SystemFailure";
	case FAIL_LAUNCH:
		return "JobLaunchFailure";
	case FAIL_EXIT_CODE:
		return "NonZeroExitCode";
	case FAIL_SIGNAL:
		return "RaisedSignal";
	case FAIL_TIMEOUT:
		return "TimeLimit";
	case FAIL_INACTIVE_LIMIT:
		return "InactiveLimit";
	case FAIL_ACCOUNT:
		return "InvalidAccount";
	case FAIL_QOS:
		return "InvalidQOS";
	case WAIT_QOS_THRES:
		return "QOSUsageThreshold";
	case WAIT_QOS_JOB_LIMIT:
		return "QOSJobLimit";
	case WAIT_QOS_RESOURCE_LIMIT:
		return "QOSResourceLimit";
	case WAIT_QOS_TIME_LIMIT:
		return "QOSTimeLimit";
	case WAIT_CLEANING:
		return "Cleaning";
	case WAIT_QOS:
		return "QOSNotAllowed";
	case WAIT_ACCOUNT:
		return "AccountNotAllowed";
	case WAIT_DEP_INVALID:
		return "DependencyNeverSatisfied";
	case WAIT_QOS_GRP_CPU:
		return "QOSGrpCpuLimit";
	case WAIT_QOS_GRP_CPU_MIN:
		return "QOSGrpCPUMinutesLimit";
	case WAIT_QOS_GRP_CPU_RUN_MIN:
		return "QOSGrpCPURunMinutesLimit";
	case WAIT_QOS_GRP_JOB:
		return"QOSGrpJobsLimit";
	case WAIT_QOS_GRP_MEM:
		return "QOSGrpMemLimit";
	case WAIT_QOS_GRP_NODE:
		return "QOSGrpNodeLimit";
	case WAIT_QOS_GRP_SUB_JOB:
		return "QOSGrpSubmitJobsLimit";
	case WAIT_QOS_GRP_WALL:
		return "QOSGrpWallLimit";
	case WAIT_QOS_MAX_CPU_PER_JOB:
		return "QOSMaxCpuPerJobLimit";
	case WAIT_QOS_MAX_CPU_MINS_PER_JOB:
		return "QOSMaxCpuMinutesPerJobLimit";
	case WAIT_QOS_MAX_NODE_PER_JOB:
		return "QOSMaxNodePerJobLimit";
	case WAIT_QOS_MAX_WALL_PER_JOB:
		return "QOSMaxWallDurationPerJobLimit";
	case WAIT_QOS_MAX_CPU_PER_USER:
		return "QOSMaxCpuPerUserLimit";
	case WAIT_QOS_MAX_JOB_PER_USER:
		return "QOSMaxJobsPerUserLimit";
	case WAIT_QOS_MAX_NODE_PER_USER:
		return "QOSMaxNodePerUserLimit";
	case WAIT_QOS_MAX_SUB_JOB:
		return "QOSMaxSubmitJobPerUserLimit";
	case WAIT_QOS_MIN_CPU:
		return "QOSMinCpuNotSatisfied";
	case WAIT_ASSOC_GRP_CPU:
		return "AssocGrpCpuLimit";
	case WAIT_ASSOC_GRP_CPU_MIN:
		return "AssocGrpCPUMinutesLimit";
	case WAIT_ASSOC_GRP_CPU_RUN_MIN:
		return "AssocGrpCPURunMinutesLimit";
	case WAIT_ASSOC_GRP_JOB:
		return"AssocGrpJobsLimit";
	case WAIT_ASSOC_GRP_MEM:
		return "AssocGrpMemLimit";
	case WAIT_ASSOC_GRP_NODE:
		return "AssocGrpNodeLimit";
	case WAIT_ASSOC_GRP_SUB_JOB:
		return "AssocGrpSubmitJobsLimit";
	case WAIT_ASSOC_GRP_WALL:
		return "AssocGrpWallLimit";
	case WAIT_ASSOC_MAX_JOBS:
		return "AssocMaxJobsLimit";
	case WAIT_ASSOC_MAX_CPU_PER_JOB:
		return "AssocMaxCpuPerJobLimit";
	case WAIT_ASSOC_MAX_CPU_MINS_PER_JOB:
		return "AssocMaxCpuMinutesPerJobLimit";
	case WAIT_ASSOC_MAX_NODE_PER_JOB:
		return "AssocMaxNodePerJobLimit";
	case WAIT_ASSOC_MAX_WALL_PER_JOB:
		return "AssocMaxWallDurationPerJobLimit";
	case WAIT_ASSOC_MAX_SUB_JOB:
		return "AssocMaxSubmitJobLimit";
	case WAIT_MAX_REQUEUE:
		return "JobHoldMaxRequeue";
	case WAIT_ARRAY_TASK_LIMIT:
		return "JobArrayTaskLimit";
	case WAIT_BURST_BUFFER_RESOURCE:
		return "BurstBufferResources";
	case WAIT_BURST_BUFFER_STAGING:
		return "BurstBufferStageIn";
	case FAIL_BURST_BUFFER_OP:
		return "BurstBufferOperation";
	case WAIT_POWER_NOT_AVAIL:
		return "PowerNotAvail";
	case WAIT_POWER_RESERVED:
		return "PowerReserved";
	case WAIT_ASSOC_GRP_UNK:
		return "AssocGrpUnknown";
	case WAIT_ASSOC_GRP_UNK_MIN:
		return "AssocGrpUnknownMinutes";
	case WAIT_ASSOC_GRP_UNK_RUN_MIN:
		return "AssocGrpUnknownRunMinutes";
	case WAIT_ASSOC_MAX_UNK_PER_JOB:
		return "AssocMaxUnknownPerJob";
	case WAIT_ASSOC_MAX_UNK_PER_NODE:
		return "AssocMaxUnknownPerNode";
	case WAIT_ASSOC_MAX_UNK_MINS_PER_JOB:
		return "AssocMaxUnknownMinutesPerJob";
	case WAIT_ASSOC_MAX_CPU_PER_NODE:
		return "AssocMaxCpuPerNode";
	case WAIT_ASSOC_GRP_MEM_MIN:
		return "AssocGrpMemMinutes";
	case WAIT_ASSOC_GRP_MEM_RUN_MIN:
		return "AssocGrpMemRunMinutes";
	case WAIT_ASSOC_MAX_MEM_PER_JOB:
		return "AssocMaxMemPerJob";
	case WAIT_ASSOC_MAX_MEM_PER_NODE:
		return "AssocMaxMemPerNode";
	case WAIT_ASSOC_MAX_MEM_MINS_PER_JOB:
		return "AssocMaxMemMinutesPerJob";
	case WAIT_ASSOC_GRP_NODE_MIN:
		return "AssocGrpNodeMinutes";
	case WAIT_ASSOC_GRP_NODE_RUN_MIN:
		return "AssocGrpNodeRunMinutes";
	case WAIT_ASSOC_MAX_NODE_MINS_PER_JOB:
		return "AssocMaxNodeMinutesPerJob";
	case WAIT_ASSOC_GRP_ENERGY:
		return "AssocGrpEnergy";
	case WAIT_ASSOC_GRP_ENERGY_MIN:
		return "AssocGrpEnergyMinutes";
	case WAIT_ASSOC_GRP_ENERGY_RUN_MIN:
		return "AssocGrpEnergyRunMinutes";
	case WAIT_ASSOC_MAX_ENERGY_PER_JOB:
		return "AssocMaxEnergyPerJob";
	case WAIT_ASSOC_MAX_ENERGY_PER_NODE:
		return "AssocMaxEnergyPerNode";
	case WAIT_ASSOC_MAX_ENERGY_MINS_PER_JOB:
		return "AssocMaxEnergyMinutesPerJob";
	case WAIT_ASSOC_GRP_GRES:
		return "AssocGrpGRES";
	case WAIT_ASSOC_GRP_GRES_MIN:
		return "AssocGrpGRESMinutes";
	case WAIT_ASSOC_GRP_GRES_RUN_MIN:
		return "AssocGrpGRESRunMinutes";
	case WAIT_ASSOC_MAX_GRES_PER_JOB:
		return "AssocMaxGRESPerJob";
	case WAIT_ASSOC_MAX_GRES_PER_NODE:
		return "AssocMaxGRESPerNode";
	case WAIT_ASSOC_MAX_GRES_MINS_PER_JOB:
		return "AssocMaxGRESMinutesPerJob";
	case WAIT_ASSOC_GRP_LIC:
		return "AssocGrpLicense";
	case WAIT_ASSOC_GRP_LIC_MIN:
		return "AssocGrpLicenseMinutes";
	case WAIT_ASSOC_GRP_LIC_RUN_MIN:
		return "AssocGrpLicenseRunMinutes";
	case WAIT_ASSOC_MAX_LIC_PER_JOB:
		return "AssocMaxLicensePerJob";
	case WAIT_ASSOC_MAX_LIC_MINS_PER_JOB:
		return "AssocMaxLicenseMinutesPerJob";
	case WAIT_ASSOC_GRP_BB:
		return "AssocGrpBB";
	case WAIT_ASSOC_GRP_BB_MIN:
		return "AssocGrpBBMinutes";
	case WAIT_ASSOC_GRP_BB_RUN_MIN:
		return "AssocGrpBBRunMinutes";
	case WAIT_ASSOC_MAX_BB_PER_JOB:
		return "AssocMaxBBPerJob";
	case WAIT_ASSOC_MAX_BB_PER_NODE:
		return "AssocMaxBBPerNode";
	case WAIT_ASSOC_MAX_BB_MINS_PER_JOB:
		return "AssocMaxBBMinutesPerJob";

	case WAIT_QOS_GRP_UNK:
		return "QOSGrpUnknown";
	case WAIT_QOS_GRP_UNK_MIN:
		return "QOSGrpUnknownMinutes";
	case WAIT_QOS_GRP_UNK_RUN_MIN:
		return "QOSGrpUnknownRunMinutes";
	case WAIT_QOS_MAX_UNK_PER_JOB:
		return "QOSMaxUnknownPerJob";
	case WAIT_QOS_MAX_UNK_PER_NODE:
		return "QOSMaxUnknownPerNode";
	case WAIT_QOS_MAX_UNK_PER_USER:
		return "QOSMaxUnknownPerUser";
	case WAIT_QOS_MAX_UNK_MINS_PER_JOB:
		return "QOSMaxUnknownMinutesPerJob";
	case WAIT_QOS_MIN_UNK:
		return "QOSMinUnknown";
	case WAIT_QOS_MAX_CPU_PER_NODE:
		return "QOSMaxCpuPerNode";
	case WAIT_QOS_GRP_MEM_MIN:
		return "QOSGrpMemoryMinutes";
	case WAIT_QOS_GRP_MEM_RUN_MIN:
		return "QOSGrpMemoryRunMinutes";
	case WAIT_QOS_MAX_MEM_PER_JOB:
		return "QOSMaxMemoryPerJob";
	case WAIT_QOS_MAX_MEM_PER_NODE:
		return "QOSMaxMemoryPerNode";
	case WAIT_QOS_MAX_MEM_PER_USER:
		return "QOSMaxMemoryPerUser";
	case WAIT_QOS_MAX_MEM_MINS_PER_JOB:
		return "QOSMaxMemoryMinutesPerJob";
	case WAIT_QOS_MIN_MEM:
		return "QOSMinMemory";
	case WAIT_QOS_GRP_NODE_MIN:
		return "QOSGrpNodeMinutes";
	case WAIT_QOS_GRP_NODE_RUN_MIN:
		return "QOSGrpNodeRunMinutes";
	case WAIT_QOS_MAX_NODE_MINS_PER_JOB:
		return "QOSMaxNodeMinutesPerJob";
	case WAIT_QOS_MIN_NODE:
		return "QOSMinNode";
	case WAIT_QOS_GRP_ENERGY:
		return "QOSGrpEnergy";
	case WAIT_QOS_GRP_ENERGY_MIN:
		return "QOSGrpEnergyMinutes";
	case WAIT_QOS_GRP_ENERGY_RUN_MIN:
		return "QOSGrpEnergyRunMinutes";
	case WAIT_QOS_MAX_ENERGY_PER_JOB:
		return "QOSMaxEnergyPerJob";
	case WAIT_QOS_MAX_ENERGY_PER_NODE:
		return "QOSMaxEnergyPerNode";
	case WAIT_QOS_MAX_ENERGY_PER_USER:
		return "QOSMaxEnergyPerUser";
	case WAIT_QOS_MAX_ENERGY_MINS_PER_JOB:
		return "QOSMaxEnergyMinutesPerJob";
	case WAIT_QOS_MIN_ENERGY:
		return "QOSMinEnergy";
	case WAIT_QOS_GRP_GRES:
		return "QOSGrpGRES";
	case WAIT_QOS_GRP_GRES_MIN:
		return "QOSGrpGRESMinutes";
	case WAIT_QOS_GRP_GRES_RUN_MIN:
		return "QOSGrpGRESRunMinutes";
	case WAIT_QOS_MAX_GRES_PER_JOB:
		return "QOSMaxGRESPerJob";
	case WAIT_QOS_MAX_GRES_PER_NODE:
		return "QOSMaxGRESPerNode";
	case WAIT_QOS_MAX_GRES_PER_USER:
		return "QOSMaxGRESPerUser";
	case WAIT_QOS_MAX_GRES_MINS_PER_JOB:
		return "QOSMaxGRESMinutesPerJob";
	case WAIT_QOS_MIN_GRES:
		return "QOSMinGRES";
	case WAIT_QOS_GRP_LIC:
		return "QOSGrpLicense";
	case WAIT_QOS_GRP_LIC_MIN:
		return "QOSGrpLicenseMinutes";
	case WAIT_QOS_GRP_LIC_RUN_MIN:
		return "QOSGrpLicenseRunMinutes";
	case WAIT_QOS_MAX_LIC_PER_JOB:
		return "QOSMaxLicensePerJob";
	case WAIT_QOS_MAX_LIC_PER_USER:
		return "QOSMaxLicensePerUser";
	case WAIT_QOS_MAX_LIC_MINS_PER_JOB:
		return "QOSMaxLicenseMinutesPerJob";
	case WAIT_QOS_MIN_LIC:
		return "QOSMinLicense";
	case WAIT_QOS_GRP_BB:
		return "QOSGrpBB";
	case WAIT_QOS_GRP_BB_MIN:
		return "QOSGrpBBMinutes";
	case WAIT_QOS_GRP_BB_RUN_MIN:
		return "QOSGrpBBRunMinutes";
	case WAIT_QOS_MAX_BB_PER_JOB:
		return "QOSMaxBBPerJob";
	case WAIT_QOS_MAX_BB_PER_NODE:
		return "QOSMaxBBPerNode";
	case WAIT_QOS_MAX_BB_PER_USER:
		return "QOSMaxBBPerUser";
	case WAIT_QOS_MAX_BB_MINS_PER_JOB:
		return "AssocMaxBBMinutesPerJob";
	case WAIT_QOS_MIN_BB:
		return "QOSMinBB";
	case FAIL_DEADLINE:
		return "DeadLine";
	case WAIT_QOS_MAX_BB_PER_ACCT:
		return "MaxBBPerAccount";
	case WAIT_QOS_MAX_CPU_PER_ACCT:
		return "MaxCpuPerAccount";
	case WAIT_QOS_MAX_ENERGY_PER_ACCT:
		return "MaxEnergyPerAccount";
	case WAIT_QOS_MAX_GRES_PER_ACCT:
		return "MaxGRESPerAccount";
	case WAIT_QOS_MAX_NODE_PER_ACCT:
		return "MaxNodePerAccount";
	case WAIT_QOS_MAX_LIC_PER_ACCT:
		return "MaxLicensePerAccount";
	case WAIT_QOS_MAX_MEM_PER_ACCT:
		return "MaxMemoryPerAccount";
	case WAIT_QOS_MAX_UNK_PER_ACCT:
		return "MaxUnknownPerAccount";
	case WAIT_QOS_MAX_JOB_PER_ACCT:
		return "MaxJobsPerAccount";
	case WAIT_QOS_MAX_SUB_JOB_PER_ACCT:
		return "MaxSubmitJobsPerAccount";
	case WAIT_PART_CONFIG:
		return "PartitionConfig";
	case WAIT_ACCOUNT_POLICY:
		return "AccountingPolicy";
	case WAIT_FED_JOB_LOCK:
		return "FedJobLock";
	case FAIL_OOM:
		return "OutOfMemory";
	case WAIT_PN_MEM_LIMIT:
		return "MaxMemPerLimit";
	case WAIT_ASSOC_GRP_BILLING:
		return "AssocGrpBilling";
	case WAIT_ASSOC_GRP_BILLING_MIN:
		return "AssocGrpBillingMinutes";
	case WAIT_ASSOC_GRP_BILLING_RUN_MIN:
		return "AssocGrpBillingRunMinutes";
	case WAIT_ASSOC_MAX_BILLING_PER_JOB:
		return "AssocMaxBillingPerJob";
	case WAIT_ASSOC_MAX_BILLING_PER_NODE:
		return "AssocMaxBillingPerNode";
	case WAIT_ASSOC_MAX_BILLING_MINS_PER_JOB:
		return "AssocMaxBillingMinutesPerJob";
	case WAIT_QOS_GRP_BILLING:
		return "QOSGrpBilling";
	case WAIT_QOS_GRP_BILLING_MIN:
		return "QOSGrpBillingMinutes";
	case WAIT_QOS_GRP_BILLING_RUN_MIN:
		return "QOSGrpBillingRunMinutes";
	case WAIT_QOS_MAX_BILLING_PER_JOB:
		return "QOSMaxBillingPerJob";
	case WAIT_QOS_MAX_BILLING_PER_NODE:
		return "QOSMaxBillingPerNode";
	case WAIT_QOS_MAX_BILLING_PER_USER:
		return "QOSMaxBillingPerUser";
	case WAIT_QOS_MAX_BILLING_MINS_PER_JOB:
		return "QOSMaxBillingMinutesPerJob";
	case WAIT_QOS_MAX_BILLING_PER_ACCT:
		return "MaxBillingPerAccount";
	case WAIT_QOS_MIN_BILLING:
		return "QOSMinBilling";
	case WAIT_RESV_DELETED:
		return "ReservationDeleted";
	case WAIT_RESV_INVALID:
		return "ReservationInvalid";
	case FAIL_CONSTRAINTS:
		return "Constraints";
	default:
		snprintf(val, sizeof(val), "%d", inx);
		return val;
	}
}

extern enum job_state_reason job_state_reason_num(char *reason)
{
	if (!xstrcasecmp(reason, "None"))
		return WAIT_NO_REASON;
	if (!xstrcasecmp(reason, "Prolog"))
		return WAIT_PROLOG;
	if (!xstrcasecmp(reason, "Priority"))
		return WAIT_PRIORITY;
	if (!xstrcasecmp(reason, "Dependency"))
		return WAIT_DEPENDENCY;
	if (!xstrcasecmp(reason, "Resources"))
		return WAIT_RESOURCES;
	if (!xstrcasecmp(reason, "PartitionNodeLimit"))
		return WAIT_PART_NODE_LIMIT;
	if (!xstrcasecmp(reason, "PartitionTimeLimit"))
		return WAIT_PART_TIME_LIMIT;
	if (!xstrcasecmp(reason, "PartitionDown"))
		return WAIT_PART_DOWN;
	if (!xstrcasecmp(reason, "PartitionInactive"))
		return WAIT_PART_INACTIVE;
	if (!xstrcasecmp(reason, "JobHeldAdmin"))
		return WAIT_HELD;
	if (!xstrcasecmp(reason, "JobHeldUser"))
		return WAIT_HELD_USER;
	if (!xstrcasecmp(reason, "BeginTime"))
		return WAIT_TIME;
	if (!xstrcasecmp(reason, "Licenses"))
		return WAIT_LICENSES;
	if (!xstrcasecmp(reason, "AssociationJobLimit"))
		return WAIT_ASSOC_JOB_LIMIT;
	if (!xstrcasecmp(reason, "AssociationResourceLimit"))
		return WAIT_ASSOC_RESOURCE_LIMIT;
	if (!xstrcasecmp(reason, "AssociationTimeLimit"))
		return WAIT_ASSOC_TIME_LIMIT;
	if (!xstrcasecmp(reason, "Reservation"))
		return WAIT_RESERVATION;
	if (!xstrcasecmp(reason, "ReqNodeNotAvail"))
		return WAIT_NODE_NOT_AVAIL;
	if (!xstrcasecmp(reason, "FrontEndDown"))
		return WAIT_FRONT_END;
	if (!xstrcasecmp(reason, "PartitionDown"))
		return FAIL_DOWN_PARTITION;
	if (!xstrcasecmp(reason, "NodeDown"))
		return FAIL_DOWN_NODE;
	if (!xstrcasecmp(reason, "BadConstraints"))
		return FAIL_BAD_CONSTRAINTS;
	if (!xstrcasecmp(reason, "SystemFailure"))
		return FAIL_SYSTEM;
	if (!xstrcasecmp(reason, "JobLaunchFailure"))
		return FAIL_LAUNCH;
	if (!xstrcasecmp(reason, "NonZeroExitCode"))
		return FAIL_EXIT_CODE;
	if (!xstrcasecmp(reason, "RaisedSignal"))
		return FAIL_SIGNAL;
	if (!xstrcasecmp(reason, "TimeLimit"))
		return FAIL_TIMEOUT;
	if (!xstrcasecmp(reason, "InactiveLimit"))
		return FAIL_INACTIVE_LIMIT;
	if (!xstrcasecmp(reason, "InvalidAccount"))
		return FAIL_ACCOUNT;
	if (!xstrcasecmp(reason, "InvalidQOS"))
		return FAIL_QOS;
	if (!xstrcasecmp(reason, "QOSUsageThreshold"))
		return WAIT_QOS_THRES;
	if (!xstrcasecmp(reason, "QOSJobLimit"))
		return WAIT_QOS_JOB_LIMIT;
	if (!xstrcasecmp(reason, "QOSResourceLimit"))
		return WAIT_QOS_RESOURCE_LIMIT;
	if (!xstrcasecmp(reason, "QOSTimeLimit"))
		return WAIT_QOS_TIME_LIMIT;
	if (!xstrcasecmp(reason, "Cleaning"))
		return WAIT_CLEANING;
	if (!xstrcasecmp(reason, "QOSNotAllowed"))
		return WAIT_QOS;
	if (!xstrcasecmp(reason, "AccountNotAllowed"))
		return WAIT_ACCOUNT;
	if (!xstrcasecmp(reason, "DependencyNeverSatisfied"))
		return WAIT_DEP_INVALID;
	if (!xstrcasecmp(reason, "QOSGrpCpuLimit"))
		return WAIT_QOS_GRP_CPU;
	if (!xstrcasecmp(reason, "QOSGrpCPUMinutesLimit"))
		return WAIT_QOS_GRP_CPU_MIN;
	if (!xstrcasecmp(reason, "QOSGrpCPURunMinutesLimit"))
		return WAIT_QOS_GRP_CPU_RUN_MIN;
	if (!xstrcasecmp(reason, "QOSGrpJobsLimit"))
		return WAIT_QOS_GRP_JOB;
	if (!xstrcasecmp(reason, "QOSGrpMemLimit"))
		return WAIT_QOS_GRP_MEM;
	if (!xstrcasecmp(reason, "QOSGrpNodeLimit"))
		return WAIT_QOS_GRP_NODE;
	if (!xstrcasecmp(reason, "QOSGrpSubmitJobsLimit"))
		return WAIT_QOS_GRP_SUB_JOB;
	if (!xstrcasecmp(reason, "QOSGrpWallLimit"))
		return WAIT_QOS_GRP_WALL;
	if (!xstrcasecmp(reason, "QOSMaxCpuPerJobLimit"))
		return WAIT_QOS_MAX_CPU_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMaxCpuMinutesPerJobLimit"))
		return WAIT_QOS_MAX_CPU_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMaxNodePerJobLimit"))
		return WAIT_QOS_MAX_NODE_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMaxWallDurationPerJobLimit"))
		return WAIT_QOS_MAX_WALL_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMaxCpuPerUserLimit"))
		return WAIT_QOS_MAX_CPU_PER_USER;
	if (!xstrcasecmp(reason, "QOSMaxJobsPerUserLimit"))
		return WAIT_QOS_MAX_JOB_PER_USER;
	if (!xstrcasecmp(reason, "QOSMaxNodePerUserLimit"))
		return WAIT_QOS_MAX_NODE_PER_USER;
	if (!xstrcasecmp(reason, "QOSMaxSubmitJobPerUserLimit"))
		return WAIT_QOS_MAX_SUB_JOB;
	if (!xstrcasecmp(reason, "QOSMinCpuNotSatisfied"))
		return WAIT_QOS_MIN_CPU;
	if (!xstrcasecmp(reason, "AssocGrpCpuLimit"))
		return WAIT_ASSOC_GRP_CPU;
	if (!xstrcasecmp(reason, "AssocGrpCPUMinutesLimit"))
		return WAIT_ASSOC_GRP_CPU_MIN;
	if (!xstrcasecmp(reason, "AssocGrpCPURunMinutesLimit"))
		return WAIT_ASSOC_GRP_CPU_RUN_MIN;
	if (!xstrcasecmp(reason, "AssocGrpJobsLimit"))
		return WAIT_ASSOC_GRP_JOB;
	if (!xstrcasecmp(reason, "AssocGrpMemLimit"))
		return WAIT_ASSOC_GRP_MEM;
	if (!xstrcasecmp(reason, "AssocGrpNodeLimit"))
		return WAIT_ASSOC_GRP_NODE;
	if (!xstrcasecmp(reason, "AssocGrpSubmitJobsLimit"))
		return WAIT_ASSOC_GRP_SUB_JOB;
	if (!xstrcasecmp(reason, "AssocGrpWallLimit"))
		return WAIT_ASSOC_GRP_WALL;
	if (!xstrcasecmp(reason, "AssocMaxJobsLimit"))
		return WAIT_ASSOC_MAX_JOBS;
	if (!xstrcasecmp(reason, "AssocMaxCpuPerJobLimit"))
		return WAIT_ASSOC_MAX_CPU_PER_JOB;
	if (!xstrcasecmp(reason, "AssocMaxCpuMinutesPerJobLimit"))
		return WAIT_ASSOC_MAX_CPU_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "AssocMaxNodePerJobLimit"))
		return WAIT_ASSOC_MAX_NODE_PER_JOB;
	if (!xstrcasecmp(reason, "AssocMaxWallDurationPerJobLimit"))
		return WAIT_ASSOC_MAX_WALL_PER_JOB;
	if (!xstrcasecmp(reason, "AssocMaxSubmitJobLimit"))
		return WAIT_ASSOC_MAX_SUB_JOB;
	if (!xstrcasecmp(reason, "JobHoldMaxRequeue"))
		return WAIT_MAX_REQUEUE;
	if (!xstrcasecmp(reason, "JobArrayTaskLimit"))
		return WAIT_ARRAY_TASK_LIMIT;
	if (!xstrcasecmp(reason, "BurstBufferResources"))
		return WAIT_BURST_BUFFER_RESOURCE;
	if (!xstrcasecmp(reason, "BurstBufferStageIn"))
		return WAIT_BURST_BUFFER_STAGING;
	if (!xstrcasecmp(reason, "BurstBufferOperation"))
		return FAIL_BURST_BUFFER_OP;
	if (!xstrcasecmp(reason, "PowerNotAvail"))
		return WAIT_POWER_NOT_AVAIL;
	if (!xstrcasecmp(reason, "PowerReserved"))
		return WAIT_POWER_RESERVED;
	if (!xstrcasecmp(reason, "AssocGrpUnknown"))
		return WAIT_ASSOC_GRP_UNK;
	if (!xstrcasecmp(reason, "AssocGrpUnknownMinutes"))
		return WAIT_ASSOC_GRP_UNK_MIN;
	if (!xstrcasecmp(reason, "AssocGrpUnknownRunMinutes"))
		return WAIT_ASSOC_GRP_UNK_RUN_MIN;
	if (!xstrcasecmp(reason, "AssocMaxUnknownPerJob"))
		return WAIT_ASSOC_MAX_UNK_PER_JOB;
	if (!xstrcasecmp(reason, "AssocMaxUnknownPerNode"))
		return WAIT_ASSOC_MAX_UNK_PER_NODE;
	if (!xstrcasecmp(reason, "AssocMaxUnknownMinutesPerJob"))
		return WAIT_ASSOC_MAX_UNK_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "AssocMaxCpuPerNode"))
		return WAIT_ASSOC_MAX_CPU_PER_NODE;
	if (!xstrcasecmp(reason, "AssocGrpMemMinutes"))
		return WAIT_ASSOC_GRP_MEM_MIN;
	if (!xstrcasecmp(reason, "AssocGrpMemRunMinutes"))
		return WAIT_ASSOC_GRP_MEM_RUN_MIN;
	if (!xstrcasecmp(reason, "AssocMaxMemPerJob"))
		return WAIT_ASSOC_MAX_MEM_PER_JOB;
	if (!xstrcasecmp(reason, "AssocMaxMemPerNode"))
		return WAIT_ASSOC_MAX_MEM_PER_NODE;
	if (!xstrcasecmp(reason, "AssocMaxMemMinutesPerJob"))
		return WAIT_ASSOC_MAX_MEM_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "AssocGrpNodeMinutes"))
		return WAIT_ASSOC_GRP_NODE_MIN;
	if (!xstrcasecmp(reason, "AssocGrpNodeRunMinutes"))
		return WAIT_ASSOC_GRP_NODE_RUN_MIN;
	if (!xstrcasecmp(reason, "AssocMaxNodeMinutesPerJob"))
		return WAIT_ASSOC_MAX_NODE_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "AssocGrpEnergy"))
		return WAIT_ASSOC_GRP_ENERGY;
	if (!xstrcasecmp(reason, "AssocGrpEnergyMinutes"))
		return WAIT_ASSOC_GRP_ENERGY_MIN;
	if (!xstrcasecmp(reason, "AssocGrpEnergyRunMinutes"))
		return WAIT_ASSOC_GRP_ENERGY_RUN_MIN;
	if (!xstrcasecmp(reason, "AssocMaxEnergyPerJob"))
		return WAIT_ASSOC_MAX_ENERGY_PER_JOB;
	if (!xstrcasecmp(reason, "AssocMaxEnergyPerNode"))
		return WAIT_ASSOC_MAX_ENERGY_PER_NODE;
	if (!xstrcasecmp(reason, "AssocMaxEnergyMinutesPerJob"))
		return WAIT_ASSOC_MAX_ENERGY_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "AssocGrpGRES"))
		return WAIT_ASSOC_GRP_GRES;
	if (!xstrcasecmp(reason, "AssocGrpGRESMinutes"))
		return WAIT_ASSOC_GRP_GRES_MIN;
	if (!xstrcasecmp(reason, "AssocGrpGRESRunMinutes"))
		return WAIT_ASSOC_GRP_GRES_RUN_MIN;
	if (!xstrcasecmp(reason, "AssocMaxGRESPerJob"))
		return WAIT_ASSOC_MAX_GRES_PER_JOB;
	if (!xstrcasecmp(reason, "AssocMaxGRESPerNode"))
		return WAIT_ASSOC_MAX_GRES_PER_NODE;
	if (!xstrcasecmp(reason, "AssocMaxGRESMinutesPerJob"))
		return WAIT_ASSOC_MAX_GRES_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "AssocGrpLicense"))
		return WAIT_ASSOC_GRP_LIC;
	if (!xstrcasecmp(reason, "AssocGrpLicenseMinutes"))
		return WAIT_ASSOC_GRP_LIC_MIN;
	if (!xstrcasecmp(reason, "AssocGrpLicenseRunMinutes"))
		return WAIT_ASSOC_GRP_LIC_RUN_MIN;
	if (!xstrcasecmp(reason, "AssocMaxLicensePerJob"))
		return WAIT_ASSOC_MAX_LIC_PER_JOB;
	if (!xstrcasecmp(reason, "AssocMaxLicenseMinutesPerJob"))
		return WAIT_ASSOC_MAX_LIC_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "AssocGrpBB"))
		return WAIT_ASSOC_GRP_BB;
	if (!xstrcasecmp(reason, "AssocGrpBBMinutes"))
		return WAIT_ASSOC_GRP_BB_MIN;
	if (!xstrcasecmp(reason, "AssocGrpBBRunMinutes"))
		return WAIT_ASSOC_GRP_BB_RUN_MIN;
	if (!xstrcasecmp(reason, "AssocMaxBBPerJob"))
		return WAIT_ASSOC_MAX_BB_PER_JOB;
	if (!xstrcasecmp(reason, "AssocMaxBBPerNode"))
		return WAIT_ASSOC_MAX_BB_PER_NODE;
	if (!xstrcasecmp(reason, "AssocMaxBBMinutesPerJob"))
		return WAIT_ASSOC_MAX_BB_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "QOSGrpUnknown"))
		return WAIT_QOS_GRP_UNK;
	if (!xstrcasecmp(reason, "QOSGrpUnknownMinutes"))
		return WAIT_QOS_GRP_UNK_MIN;
	if (!xstrcasecmp(reason, "QOSGrpUnknownRunMinutes"))
		return WAIT_QOS_GRP_UNK_RUN_MIN;
	if (!xstrcasecmp(reason, "QOSMaxUnknownPerJob"))
		return WAIT_QOS_MAX_UNK_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMaxUnknownPerNode"))
		return WAIT_QOS_MAX_UNK_PER_NODE;
	if (!xstrcasecmp(reason, "QOSMaxUnknownPerUser"))
		return WAIT_QOS_MAX_UNK_PER_USER;
	if (!xstrcasecmp(reason, "QOSMaxUnknownMinutesPerJob"))
		return WAIT_QOS_MAX_UNK_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMinUnknown"))
		return WAIT_QOS_MIN_UNK;
	if (!xstrcasecmp(reason, "QOSMaxCpuPerNode"))
		return WAIT_QOS_MAX_CPU_PER_NODE;
	if (!xstrcasecmp(reason, "QOSGrpMemoryMinutes"))
		return WAIT_QOS_GRP_MEM_MIN;
	if (!xstrcasecmp(reason, "QOSGrpMemoryRunMinutes"))
		return WAIT_QOS_GRP_MEM_RUN_MIN;
	if (!xstrcasecmp(reason, "QOSMaxMemoryPerJob"))
		return WAIT_QOS_MAX_MEM_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMaxMemoryPerNode"))
		return WAIT_QOS_MAX_MEM_PER_NODE;
	if (!xstrcasecmp(reason, "QOSMaxMemoryPerUser"))
		return WAIT_QOS_MAX_MEM_PER_USER;
	if (!xstrcasecmp(reason, "QOSMaxMemoryMinutesPerJob"))
		return WAIT_QOS_MAX_MEM_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMinMemory"))
		return WAIT_QOS_MIN_MEM;
	if (!xstrcasecmp(reason, "QOSGrpNodeMinutes"))
		return WAIT_QOS_GRP_NODE_MIN;
	if (!xstrcasecmp(reason, "QOSGrpNodeRunMinutes"))
		return WAIT_QOS_GRP_NODE_RUN_MIN;
	if (!xstrcasecmp(reason, "QOSMaxNodeMinutesPerJob"))
		return WAIT_QOS_MAX_NODE_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMinNode"))
		return WAIT_QOS_MIN_NODE;
	if (!xstrcasecmp(reason, "QOSGrpEnergy"))
		return WAIT_QOS_GRP_ENERGY;
	if (!xstrcasecmp(reason, "QOSGrpEnergyMinutes"))
		return WAIT_QOS_GRP_ENERGY_MIN;
	if (!xstrcasecmp(reason, "QOSGrpEnergyRunMinutes"))
		return WAIT_QOS_GRP_ENERGY_RUN_MIN;
	if (!xstrcasecmp(reason, "QOSMaxEnergyPerJob"))
		return WAIT_QOS_MAX_ENERGY_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMaxEnergyPerNode"))
		return WAIT_QOS_MAX_ENERGY_PER_NODE;
	if (!xstrcasecmp(reason, "QOSMaxEnergyPerUser"))
		return WAIT_QOS_MAX_ENERGY_PER_USER;
	if (!xstrcasecmp(reason, "QOSMaxEnergyMinutesPerJob"))
		return WAIT_QOS_MAX_ENERGY_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMinEnergy"))
		return WAIT_QOS_MIN_ENERGY;
	if (!xstrcasecmp(reason, "QOSGrpGRES"))
		return WAIT_QOS_GRP_GRES;
	if (!xstrcasecmp(reason, "QOSGrpGRESMinutes"))
		return WAIT_QOS_GRP_GRES_MIN;
	if (!xstrcasecmp(reason, "QOSGrpGRESRunMinutes"))
		return WAIT_QOS_GRP_GRES_RUN_MIN;
	if (!xstrcasecmp(reason, "QOSMaxGRESPerJob"))
		return WAIT_QOS_MAX_GRES_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMaxGRESPerNode"))
		return WAIT_QOS_MAX_GRES_PER_NODE;
	if (!xstrcasecmp(reason, "QOSMaxGRESPerUser"))
		return WAIT_QOS_MAX_GRES_PER_USER;
	if (!xstrcasecmp(reason, "QOSMaxGRESMinutesPerJob"))
		return WAIT_QOS_MAX_GRES_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMinGRES"))
		return WAIT_QOS_MIN_GRES;
	if (!xstrcasecmp(reason, "QOSGrpLicense"))
		return WAIT_QOS_GRP_LIC;
	if (!xstrcasecmp(reason, "QOSGrpLicenseMinutes"))
		return WAIT_QOS_GRP_LIC_MIN;
	if (!xstrcasecmp(reason, "QOSGrpLicenseRunMinutes"))
		return WAIT_QOS_GRP_LIC_RUN_MIN;
	if (!xstrcasecmp(reason, "QOSMaxLicensePerJob"))
		return WAIT_QOS_MAX_LIC_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMaxLicensePerUser"))
		return WAIT_QOS_MAX_LIC_PER_USER;
	if (!xstrcasecmp(reason, "QOSMaxLicenseMinutesPerJob"))
		return WAIT_QOS_MAX_LIC_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMinLicense"))
		return WAIT_QOS_MIN_LIC;
	if (!xstrcasecmp(reason, "QOSGrpBB"))
		return WAIT_QOS_GRP_BB;
	if (!xstrcasecmp(reason, "QOSGrpBBMinutes"))
		return WAIT_QOS_GRP_BB_MIN;
	if (!xstrcasecmp(reason, "QOSGrpBBRunMinutes"))
		return WAIT_QOS_GRP_BB_RUN_MIN;
	if (!xstrcasecmp(reason, "QOSMaxBBPerJob"))
		return WAIT_QOS_MAX_BB_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMaxBBPerNode"))
		return WAIT_QOS_MAX_BB_PER_NODE;
	if (!xstrcasecmp(reason, "QOSMaxBBPerUser"))
		return WAIT_QOS_MAX_BB_PER_USER;
	if (!xstrcasecmp(reason, "AssocMaxBBMinutesPerJob"))
		return WAIT_QOS_MAX_BB_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMinBB"))
		return WAIT_QOS_MIN_BB;
	if (!xstrcasecmp(reason, "DeadLine"))
		return FAIL_DEADLINE;
	if (!xstrcasecmp(reason, "MaxBBPerAccount"))
		return WAIT_QOS_MAX_BB_PER_ACCT;
	if (!xstrcasecmp(reason, "MaxCpuPerAccount"))
		return WAIT_QOS_MAX_CPU_PER_ACCT;
	if (!xstrcasecmp(reason, "MaxEnergyPerAccount"))
		return WAIT_QOS_MAX_ENERGY_PER_ACCT;
	if (!xstrcasecmp(reason, "MaxGRESPerAccount"))
		return WAIT_QOS_MAX_GRES_PER_ACCT;
	if (!xstrcasecmp(reason, "MaxNodePerAccount"))
		return WAIT_QOS_MAX_NODE_PER_ACCT;
	if (!xstrcasecmp(reason, "MaxLicensePerAccount"))
		return WAIT_QOS_MAX_LIC_PER_ACCT;
	if (!xstrcasecmp(reason, "MaxMemoryPerAccount"))
		return WAIT_QOS_MAX_MEM_PER_ACCT;
	if (!xstrcasecmp(reason, "MaxUnknownPerAccount"))
		return WAIT_QOS_MAX_UNK_PER_ACCT;
	if (!xstrcasecmp(reason, "MaxJobsPerAccount"))
		return WAIT_QOS_MAX_JOB_PER_ACCT;
	if (!xstrcasecmp(reason, "MaxSubmitJobsPerAccount"))
		return WAIT_QOS_MAX_SUB_JOB_PER_ACCT;
	if (!xstrcasecmp(reason, "PartitionConfig"))
		return WAIT_PART_CONFIG;
	if (!xstrcasecmp(reason, "AccountingPolicy"))
		return WAIT_ACCOUNT_POLICY;
	if (!xstrcasecmp(reason, "FedJobLock"))
		return WAIT_FED_JOB_LOCK;
	if (!xstrcasecmp(reason, "OutOfMemory"))
		return FAIL_OOM;
	if (!xstrcasecmp(reason, "MaxMemPerLimit"))
		return WAIT_PN_MEM_LIMIT;
	if (!xstrcasecmp(reason, "AssocGrpBilling"))
		return WAIT_ASSOC_GRP_BILLING;
	if (!xstrcasecmp(reason, "AssocGrpBillingMinutes"))
		return WAIT_ASSOC_GRP_BILLING_MIN;
	if (!xstrcasecmp(reason, "AssocGrpBillingRunMinutes"))
		return WAIT_ASSOC_GRP_BILLING_RUN_MIN;
	if (!xstrcasecmp(reason, "AssocMaxBillingPerJob"))
		return WAIT_ASSOC_MAX_BILLING_PER_JOB;
	if (!xstrcasecmp(reason, "AssocMaxBillingPerNode"))
		return WAIT_ASSOC_MAX_BILLING_PER_NODE;
	if (!xstrcasecmp(reason, "AssocMaxBillingMinutesPerJob"))
		return WAIT_ASSOC_MAX_BILLING_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "QOSGrpBilling"))
		return WAIT_QOS_GRP_BILLING;
	if (!xstrcasecmp(reason, "QOSGrpBillingMinutes"))
		return WAIT_QOS_GRP_BILLING_MIN;
	if (!xstrcasecmp(reason, "QOSGrpBillingRunMinutes"))
		return WAIT_QOS_GRP_BILLING_RUN_MIN;
	if (!xstrcasecmp(reason, "QOSMaxBillingPerJob"))
		return WAIT_QOS_MAX_BILLING_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMaxBillingPerNode"))
		return WAIT_QOS_MAX_BILLING_PER_NODE;
	if (!xstrcasecmp(reason, "QOSMaxBillingPerUser"))
		return WAIT_QOS_MAX_BILLING_PER_USER;
	if (!xstrcasecmp(reason, "QOSMaxBillingMinutesPerJob"))
		return WAIT_QOS_MAX_BILLING_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "MaxBillingPerAccount"))
		return WAIT_QOS_MAX_BILLING_PER_ACCT;
	if (!xstrcasecmp(reason, "QOSMinBilling"))
		return WAIT_QOS_MIN_BILLING;
	if (!xstrcasecmp(reason, "ReservationDeleted"))
		return WAIT_RESV_DELETED;
	if (!xstrcasecmp(reason, "ReservationInvalid"))
		return WAIT_RESV_INVALID;
	if (!xstrcasecmp(reason, "Constraints"))
		return FAIL_CONSTRAINTS;

	return NO_VAL;
}

extern bool job_state_qos_grp_limit(enum job_state_reason state_reason)
{
	if ((state_reason >= WAIT_QOS_GRP_CPU &&
	     state_reason <= WAIT_QOS_GRP_WALL) ||
	    (state_reason == WAIT_QOS_GRP_MEM_MIN) ||
	    (state_reason == WAIT_QOS_GRP_MEM_RUN_MIN) ||
	    (state_reason >= WAIT_QOS_GRP_ENERGY &&
	     state_reason <= WAIT_QOS_GRP_ENERGY_RUN_MIN) ||
	    (state_reason == WAIT_QOS_GRP_NODE_MIN) ||
	    (state_reason == WAIT_QOS_GRP_NODE_RUN_MIN) ||
	    (state_reason >= WAIT_QOS_GRP_GRES &&
	     state_reason <= WAIT_QOS_GRP_GRES_RUN_MIN) ||
	    (state_reason >= WAIT_QOS_GRP_LIC &&
	     state_reason <= WAIT_QOS_GRP_LIC_RUN_MIN) ||
	    (state_reason >= WAIT_QOS_GRP_BB &&
	     state_reason <= WAIT_QOS_GRP_BB_RUN_MIN) ||
	    (state_reason >= WAIT_QOS_GRP_BILLING &&
	     state_reason <= WAIT_QOS_GRP_BILLING_RUN_MIN))
		return true;
	return false;
}
