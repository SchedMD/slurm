/*****************************************************************************\
 *  sacct.c - job accounting reports for Slurm's jobacct/log plugin
 *****************************************************************************
 *
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
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

#include "sacct.h"

/*
 * Globals
 */
sacct_parameters_t params;
print_field_t fields[] = {
	{10, "Account", print_fields_str, PRINT_ACCOUNT},
	{15, "AdminComment", print_fields_str, PRINT_ADMIN_COMMENT},
	{10, "AllocCPUS", print_fields_uint, PRINT_ALLOC_CPUS},
	{12, "AllocGRES", print_fields_str, PRINT_ALLOC_GRES},
	{10, "AllocNodes", print_fields_str, PRINT_ALLOC_NODES},
	{10, "AllocTRES", print_fields_str, PRINT_TRESA},
	{7,  "AssocID", print_fields_uint, PRINT_ASSOCID},
	{10, "AveCPU", print_fields_str, PRINT_AVECPU},
	{10, "AveCPUFreq", print_fields_str, PRINT_ACT_CPUFREQ},
	{14, "AveDiskRead", print_fields_str, PRINT_AVEDISKREAD},
	{14, "AveDiskWrite", print_fields_str, PRINT_AVEDISKWRITE},
	{10, "AvePages", print_fields_str, PRINT_AVEPAGES},
	{10, "AveRSS", print_fields_str, PRINT_AVERSS},
	{10, "AveVMSize", print_fields_str, PRINT_AVEVSIZE},
	{16, "BlockID", print_fields_str, PRINT_BLOCKID},
	{10, "Cluster", print_fields_str, PRINT_CLUSTER},
	{14, "Comment", print_fields_str, PRINT_COMMENT},
	{19, "Constraints", print_fields_str, PRINT_CONSTRAINTS},
	{14, "ConsumedEnergy", print_fields_str, PRINT_CONSUMED_ENERGY},
	{17, "ConsumedEnergyRaw", print_fields_uint64,
	 PRINT_CONSUMED_ENERGY_RAW},
	{10, "CPUTime", print_fields_time_from_secs, PRINT_CPU_TIME},
	{10, "CPUTimeRAW", print_fields_uint64, PRINT_CPU_TIME_RAW},
	{15, "DerivedExitCode", print_fields_str, PRINT_DERIVED_EC},
	{10, "Elapsed", print_fields_time_from_secs, PRINT_ELAPSED},
	{10, "ElapsedRaw", print_fields_uint32, PRINT_ELAPSED_RAW},
	{19, "Eligible", print_fields_date, PRINT_ELIGIBLE},
	{19, "End", print_fields_date, PRINT_END},
	{8,  "ExitCode", print_fields_str, PRINT_EXITCODE},
	{19, "Flags", print_fields_str, PRINT_FLAGS},
	{6,  "GID", print_fields_uint, PRINT_GID},
	{9,  "Group", print_fields_str, PRINT_GROUP},
	{-12, "JobID", print_fields_str, PRINT_JOBID},
	{-12, "JobIDRaw", print_fields_str, PRINT_JOBIDRAW},
	{10, "JobName", print_fields_str, PRINT_JOBNAME},
	{9,  "Layout", print_fields_str, PRINT_LAYOUT},
	{12, "MaxDiskRead", print_fields_str, PRINT_MAXDISKREAD},
	{15, "MaxDiskReadNode", print_fields_str, PRINT_MAXDISKREADNODE},
	{15, "MaxDiskReadTask", print_fields_uint, PRINT_MAXDISKREADTASK},
	{12, "MaxDiskWrite", print_fields_str, PRINT_MAXDISKWRITE},
	{16, "MaxDiskWriteNode", print_fields_str, PRINT_MAXDISKWRITENODE},
	{16, "MaxDiskWriteTask", print_fields_uint, PRINT_MAXDISKWRITETASK},
	{8,  "MaxPages", print_fields_str, PRINT_MAXPAGES},
	{12, "MaxPagesNode", print_fields_str, PRINT_MAXPAGESNODE},
	{14, "MaxPagesTask", print_fields_uint, PRINT_MAXPAGESTASK},
	{10, "MaxRSS", print_fields_str, PRINT_MAXRSS},
	{10, "MaxRSSNode", print_fields_str, PRINT_MAXRSSNODE},
	{10, "MaxRSSTask", print_fields_uint, PRINT_MAXRSSTASK},
	{10, "MaxVMSize", print_fields_str, PRINT_MAXVSIZE},
	{14, "MaxVMSizeNode", print_fields_str, PRINT_MAXVSIZENODE},
	{14, "MaxVMSizeTask", print_fields_uint, PRINT_MAXVSIZETASK},
	{12, "McsLabel", print_fields_str, PRINT_MCS_LABEL},
	{10, "MinCPU", print_fields_str, PRINT_MINCPU},
	{10, "MinCPUNode", print_fields_str, PRINT_MINCPUNODE},
	{10, "MinCPUTask", print_fields_uint, PRINT_MINCPUTASK},
	{10, "NCPUS", print_fields_uint, PRINT_ALLOC_CPUS},
	{8,  "NNodes", print_fields_uint, PRINT_NNODES},
	{15, "NodeList", print_fields_str, PRINT_NODELIST},
	{8,  "NTasks", print_fields_uint, PRINT_NTASKS},
	{10, "Priority", print_fields_uint, PRINT_PRIO},
	{10, "Partition", print_fields_str, PRINT_PARTITION},
	{10, "QOS", print_fields_str, PRINT_QOS},
	{6,  "QOSRAW", print_fields_uint, PRINT_QOSRAW},
	{22, "Reason", print_fields_str, PRINT_REASON},
	{10, "ReqCPUFreq", print_fields_str, PRINT_REQ_CPUFREQ_MAX}, /* vestigial */
	{13, "ReqCPUFreqMin", print_fields_str, PRINT_REQ_CPUFREQ_MIN},
	{13, "ReqCPUFreqMax", print_fields_str, PRINT_REQ_CPUFREQ_MAX},
	{13, "ReqCPUFreqGov", print_fields_str, PRINT_REQ_CPUFREQ_GOV},
	{8,  "ReqCPUS", print_fields_uint, PRINT_REQ_CPUS},
	{12, "ReqGRES", print_fields_str, PRINT_REQ_GRES},
	{10, "ReqMem", print_fields_str, PRINT_REQ_MEM},
	{8,  "ReqNodes", print_fields_str, PRINT_REQ_NODES},
	{10, "ReqTRES", print_fields_str, PRINT_TRESR},
	{20, "Reservation",  print_fields_str, PRINT_RESERVATION},
	{8,  "ReservationId",  print_fields_uint, PRINT_RESERVATION_ID},
	{10, "Reserved", print_fields_time_from_secs, PRINT_RESV},
	{10, "ResvCPU", print_fields_time_from_secs, PRINT_RESV_CPU},
	{10, "ResvCPURAW", print_fields_uint, PRINT_RESV_CPU},
	{19, "Start", print_fields_date, PRINT_START},
	{10, "State", print_fields_str, PRINT_STATE},
	{19, "Submit", print_fields_date, PRINT_SUBMIT},
	{10, "Suspended", print_fields_time_from_secs, PRINT_SUSPENDED},
	{10, "SystemCPU", print_fields_str, PRINT_SYSTEMCPU},
	{15, "SystemComment", print_fields_str, PRINT_SYSTEM_COMMENT},
	{10, "Timelimit", print_fields_str, PRINT_TIMELIMIT},
	{10, "TimelimitRaw", print_fields_str, PRINT_TIMELIMIT_RAW},
	{10, "TotalCPU", print_fields_str, PRINT_TOTALCPU},
	{14, "TRESUsageInAve", print_fields_str, PRINT_TRESUIA},
	{14, "TRESUsageInMax", print_fields_str, PRINT_TRESUIM},
	{18, "TRESUsageInMaxNode", print_fields_str, PRINT_TRESUIMN},
	{18, "TRESUsageInMaxTask", print_fields_str, PRINT_TRESUIMT},
	{14, "TRESUsageInMin", print_fields_str, PRINT_TRESUIMI},
	{18, "TRESUsageInMinNode", print_fields_str, PRINT_TRESUIMIN},
	{18, "TRESUsageInMinTask", print_fields_str, PRINT_TRESUIMIT},
	{14, "TRESUsageInTot", print_fields_str, PRINT_TRESUIT},
	{15, "TRESUsageOutAve", print_fields_str, PRINT_TRESUOA},
	{15, "TRESUsageOutMax", print_fields_str, PRINT_TRESUOM},
	{19, "TRESUsageOutMaxNode", print_fields_str, PRINT_TRESUOMN},
	{19, "TRESUsageOutMaxTask", print_fields_str, PRINT_TRESUOMT},
	{15, "TRESUsageOutMin", print_fields_str, PRINT_TRESUOMI},
	{19, "TRESUsageOutMinNode", print_fields_str, PRINT_TRESUOMIN},
	{19, "TRESUsageOutMinTask", print_fields_str, PRINT_TRESUOMIT},
	{15, "TRESUsageOutTot", print_fields_str, PRINT_TRESUOT},
	{6,  "UID", print_fields_uint, PRINT_UID},
	{9,  "User", print_fields_str, PRINT_USER},
	{10, "UserCPU", print_fields_str, PRINT_USERCPU},
	{10, "WCKey", print_fields_str, PRINT_WCKEY},
	{10, "WCKeyID", print_fields_uint, PRINT_WCKEYID},
	{20, "WorkDir", print_fields_str, PRINT_WORK_DIR},
	{0,  NULL, NULL, 0}};

List jobs = NULL;

int main(int argc, char **argv)
{
	enum {
		SACCT_LIST,
		SACCT_HELP,
		SACCT_USAGE
	} op;
	int rc = 0;

	slurm_conf_init(NULL);
	sacct_init();
	parse_command_line(argc, argv);

	/* What are we doing? Requests for help take highest priority,
	 * but then check for illogical switch combinations.
	 */

	if (params.opt_help)
		op = SACCT_HELP;
	else
		op = SACCT_LIST;


	switch (op) {
	case SACCT_LIST:
		print_fields_header(print_fields_list);
		if (get_data() == SLURM_ERROR)
			exit(errno);
		if (params.opt_completion)
			do_list_completion();
		else
			do_list();
		break;
	case SACCT_HELP:
		do_help();
		break;
	default:
		fprintf(stderr, "sacct bug: should never get here\n");
		sacct_fini();
		exit(2);
	}

	sacct_fini();
	return (rc);
}
