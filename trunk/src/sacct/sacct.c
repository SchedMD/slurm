/*****************************************************************************\
 *  sacct.c - job accounting reports for SLURM's jobacct/log plugin
 *****************************************************************************
 *
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
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

#include "sacct.h"

void invalidSwitchCombo(char *good, char *bad);

/*
 * Globals
 */
sacct_parameters_t params;
print_field_t fields[] = {
	{10, "AllocCPUS", print_fields_int, PRINT_ALLOC_CPUS},
	{10, "Account", print_fields_str, PRINT_ACCOUNT},
	{7, "AssocID", print_fields_int, PRINT_ASSOCID},
	{10, "AveCPU", print_fields_str, PRINT_AVECPU}, 
	{10, "AvePages", print_fields_str, PRINT_AVEPAGES}, 
	{10, "AveRSS", print_fields_str, PRINT_AVERSS}, 
	{10, "AveVMSize", print_fields_str, PRINT_AVEVSIZE}, 
	{16, "BlockID", print_fields_str, PRINT_BLOCKID}, 
	{10, "Cluster", print_fields_str, PRINT_CLUSTER},
	{10, "CPUTime", print_fields_time_from_secs, PRINT_CPU_TIME},
	{10, "CPUTimeRAW", print_fields_int, PRINT_CPU_TIME_RAW},
	{10, "Elapsed", print_fields_time_from_secs, PRINT_ELAPSED},
	{19, "Eligible", print_fields_date, PRINT_ELIGIBLE},
	{19, "End", print_fields_date, PRINT_END},
	{8, "ExitCode", print_fields_str, PRINT_EXITCODE},
	{6, "GID", print_fields_int, PRINT_GID}, 
	{9, "Group", print_fields_str, PRINT_GROUP}, 
	{10, "JobID", print_fields_str, PRINT_JOBID}, 
	{10, "JobName", print_fields_str, PRINT_JOBNAME},
	{9,  "Layout", print_fields_str, PRINT_LAYOUT},
	{8, "MaxPages", print_fields_str, PRINT_MAXPAGES}, 
	{12, "MaxPagesNode", print_fields_str, PRINT_MAXPAGESNODE}, 
	{14, "MaxPagesTask", print_fields_int, PRINT_MAXPAGESTASK}, 
	{10, "MaxRSS", print_fields_str, PRINT_MAXRSS},
	{10, "MaxRSSNode", print_fields_str, PRINT_MAXRSSNODE},
	{10, "MaxRSSTask", print_fields_int, PRINT_MAXRSSTASK},
	{10, "MaxVMSize", print_fields_str, PRINT_MAXVSIZE}, 
	{14, "MaxVMSizeNode", print_fields_str, PRINT_MAXVSIZENODE}, 
	{14, "MaxVMSizeTask", print_fields_int, PRINT_MAXVSIZETASK}, 
	{10, "MinCPU", print_fields_str, PRINT_MINCPU}, 
	{10, "MinCPUNode", print_fields_str, PRINT_MINCPUNODE}, 
	{10, "MinCPUTask", print_fields_int, PRINT_MINCPUTASK}, 
	{10, "NCPUS", print_fields_int, PRINT_ALLOC_CPUS},
	{15, "NodeList", print_fields_str, PRINT_NODELIST}, 
	{8, "NNodes", print_fields_str, PRINT_NNODES}, 
	{8, "NTasks", print_fields_int, PRINT_NTASKS},
	{10, "Priority", print_fields_int, PRINT_PRIO}, 
	{10, "Partition", print_fields_str, PRINT_PARTITION}, 
	{10, "QOS", print_fields_str, PRINT_QOS},
	{6, "QOSRAW", print_fields_int, PRINT_QOSRAW},
	{8, "ReqCPUS", print_fields_int, PRINT_REQ_CPUS},
	{10, "Reserved", print_fields_time_from_secs, PRINT_RESV},
	{10, "ResvCPU", print_fields_time_from_secs, PRINT_RESV_CPU},
	{10, "ResvCPURAW", print_fields_int, PRINT_RESV_CPU},
	{19, "Start", print_fields_date, PRINT_START}, 
	{10, "State", print_fields_str, PRINT_STATE}, 
	{19, "Submit", print_fields_date, PRINT_SUBMIT}, 
	{10, "Suspended", print_fields_time_from_secs, PRINT_SUSPENDED}, 
	{10, "SystemCPU", print_fields_str, PRINT_SYSTEMCPU}, 
	{10, "Timelimit", print_fields_time_from_secs, PRINT_TIMELIMIT},
	{10, "TotalCPU", print_fields_str, PRINT_TOTALCPU}, 
	{6, "UID", print_fields_int, PRINT_UID}, 
	{9, "User", print_fields_str, PRINT_USER}, 
	{10, "UserCPU", print_fields_str, PRINT_USERCPU}, 
	{10, "WCKey", print_fields_str, PRINT_WCKEY}, 		     
	{10, "WCKeyID", print_fields_int, PRINT_WCKEYID}, 		     
	{0, NULL, NULL, 0}};

List jobs = NULL;

int main(int argc, char **argv)
{
	enum {
		SACCT_DUMP,
		SACCT_FDUMP,
		SACCT_LIST,
		SACCT_HELP,
		SACCT_USAGE
	} op;
	int rc = 0;
	
	sacct_init();
	parse_command_line(argc, argv);

	/* What are we doing? Requests for help take highest priority,
	 * but then check for illogical switch combinations.
	 */

	if (params.opt_help)
		op = SACCT_HELP;
	else if (params.opt_dump) {
		op = SACCT_DUMP;
	} else if (params.opt_fdump) {
		op = SACCT_FDUMP;
	} else
		op = SACCT_LIST;

	
	switch (op) {
	case SACCT_DUMP:
		if(get_data() == SLURM_ERROR)
			exit(errno);
		if(params.opt_completion) 
			do_dump_completion();
		else 
			do_dump();
		break;
	case SACCT_FDUMP:
		if(get_data() == SLURM_ERROR)
			exit(errno);
		break;
	case SACCT_LIST:
		print_fields_header(print_fields_list);
		if(get_data() == SLURM_ERROR)
			exit(errno);
		if(params.opt_completion) 
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


void invalidSwitchCombo(char *good, char *bad)
{
	fprintf(stderr, "\"%s\" may not be used with %s\n", good, bad);
	return;
}
