/*****************************************************************************\
 *  sacct.c - job accounting reports for SLURM's jobacct/log plugin
 *****************************************************************************
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
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

/*
 * HISTORY
 * $Log$
 * Revision 1.8  2006/06/09   ciclouston
 * Added new account entry to begining of fields[].
 *
 * Revision 1.7  2005/06/29 20:41:23  da
 * New Tag HP's patch applied for mutex issue in jobacct.
 *
 * Revision 1.6  2005/06/24 01:19:52  jette
 * Additional documenation for job accounting. Some bug fixes too. All from
 * Andy Riebs/HP.
 *
 * Revision 1.5  2005/06/11 00:49:43  jette
 * Get all the latest accounting software patches.
 *
 * Revision 1.1  2005/06/01 17:26:11  jette
 * Extensive mods checked it for HP work, see NEWS for details.
 *
 * Revision 1.4  2005/05/31 20:28:20  riebs
 * Include "errors" in the default sacct display.
 *
 * Revision 1.3  2005/05/27 17:37:43  riebs
 * Don't discard JOB_START and JOB_END records when selecting on job
 * steps. ("sacct -J 246.1" would report "Error: No JOB_START record for
 * job 246"). This was not a problem when --dump was specified.
 *
 * Revision 1.2  2005/05/19 20:42:11  riebs
 * 1. Fix problem of double-flush of .expired records when scontrol is
 *    unavailable
 * 2. Handle "--expire=1d" as "expire everything through yesterday,"
 *    rather than "expire everything up to  exactly 24 hours ago."
 *
 * Revision 1.1  2005/05/13 20:11:14  riebs
 * Add the jobacct plugins and the sacct utility, and upgrade to
 * slurm-0.4.22-1.
 *
 * Revision 1.9  2005/05/03 12:38:35  riebs
 * Implement "sacct --expire" to facilitate logfile rotation.
 *
 * Revision 1.8  2005/04/15 23:01:39  riebs
 * Check in the changes for dynamic SLURM job accounting (that is, the
 * code to capture runtime data for psize and vsize).
 *
 * Revision 1.7  2005/04/11 21:05:44  riebs
 * Check in a work-around for a getopt_long() bug.
 *
 * Revision 1.6  2005/04/07 18:43:46  riebs
 * Fix a hand full of off-by-one problems, and add --version
 *
 * Revision 1.2  2005/04/07 18:41:42  riebs
 * updat the rev
 *
 * Revision 1.1  2005/04/07 18:33:08  riebs
 * Initial revision
 *
 * Revision 1.5  2005/04/06 19:37:40  riebs
 * Clean up sacct output.
 *
 * Revision 1.4  2005/04/05 15:28:01  riebs
 * - Implement --all
 * - Clean up output formatting for elapsed time
 * - Expand output field for jobname
 *
 * Revision 1.3  2005/04/02 19:46:44  riebs
 * Remove the setuid-related code, initialize job[].cstatus properly, fix
 * formatting of the JOB_STEP record, and fix printing of elapsed time.
 *
 * Revision 1.2  2005/04/01 17:10:43  riebs
 * Replace the Perl version of sacct with sacct.c
 *
 * Revision 1.1  2005/03/31 21:57:45  riebs
 * Initial revision
 *
 * Revision 1.1  2005/03/31 21:19:28  riebs
 * Add the .c version of sacct to CVS in anticipation of retiring the
 * .pl version.
 *
 * Revision 1.8  2005/03/31 19:25:19  riebs
 * Solid version of sacct with all functionality!
 *
 * Revision 1.7  2005/03/31 13:24:41  riebs
 * Good version of formatted_dump implemented.
 *
 * Revision 1.6  2005/03/31 00:33:45  riebs
 * Pretty good implementation of fdump now.
 *
 * Revision 1.5  2005/03/30 23:57:31  riebs
 * Version that handles all print fields.
 *
 * Revision 1.4  2005/03/30 20:51:13  riebs
 * A precautionary version before I radically change
 * the fields struct.
 *
 * Revision 1.3  2005/03/30 18:26:24  riebs
 * Pretty solid version of --dump
 *
 * Revision 1.2  2005/03/29 14:43:20  riebs
 * All data are aggregated; just need to print it now!
 *
 * Revision 1.1  2005/03/28 18:21:26  andy
 * Initial revision
 *
 * Revision 1.1  2005/03/28 16:18:38  riebs
 * Initial revision
 *
 *
 * $EndLog$
 */

#include "sacct.h"

void invalidSwitchCombo(char *good, char *bad);
void _print_header(void);

/*
 * Globals
 */
sacct_parameters_t params;
fields_t fields[] = {{"account", print_account},
		     {"associd", print_assoc},
		     {"cluster", print_cluster},
		     {"cpu", print_cpu},
		     {"cputime", print_cputime}, 
		     {"elapsed", print_elapsed},
		     {"end", print_end}, 
		     {"exitcode", print_exitcode},
		     {"finished", print_end},		/* Defunct name */ 
		     {"gid", print_gid}, 
		     {"group", print_group}, 
		     {"job", print_job},
		     {"jobid", print_jobid}, 
		     {"jobname", print_name}, 
		     {"ncpus", print_ncpus}, 
		     {"nodes", print_nodes}, 
		     {"nnodes", print_nnodes}, 
		     {"nprocs", print_ntasks},
		     {"ntasks", print_ntasks}, 
		     {"pages", print_pages}, 
		     {"partition", print_partition}, 
		     {"rss", print_rss},
		     {"start", print_start}, 
		     {"state", print_state}, 
		     {"status", print_state}, 
		     {"submit", print_submit}, 
		     {"timelimit", print_timelimit}, 
		     {"submitted", print_submit},	/* Defunct name */
		     {"systemcpu", print_systemcpu}, 
		     {"uid", print_uid}, 
		     {"user", print_user}, 
		     {"usercpu", print_usercpu}, 
		     {"vsize", print_vsize}, 
		     {"blockid", print_blockid}, 
		     {"connection", print_connection}, 
		     {"geo", print_geo}, 
		     {"max_procs", print_max_procs}, 
		     {"reboot", print_reboot}, 
		     {"rotate", print_rotate}, 
		     {"bg_start_point", print_bg_start_point}, 		     
		     {"wckey", print_wckey}, 		     
		     {NULL, NULL}};

List jobs = NULL;

int printfields[MAX_PRINTFIELDS],	/* Indexed into fields[] */
	nprintfields = 0;

int main(int argc, char **argv)
{
	enum {
		SACCT_DUMP,
		SACCT_EXPIRE,
		SACCT_FDUMP,
		SACCT_LIST,
		SACCT_STAT,
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
		if (params.opt_long || params.opt_total 
		    || params.opt_field_list || params.opt_expire) {
			if (params.opt_verbose)
				fprintf(stderr,
					"Switch conflict,\n"
					"\topt_long=%d\n"
					"\topt_total=%d\n"
					"\topt_field_list=%s\n",
					params.opt_long, 
					params.opt_total, 
					params.opt_field_list);
			invalidSwitchCombo("--dump",
					   "--brief, --long, "
					   "--fields, --total");
			rc = 1;
			goto finished;
		}
	} else if (params.opt_fdump) {
		op = SACCT_FDUMP;
	} else if (params.opt_stat) {
		op = SACCT_STAT;
	} else if (params.opt_expire) {
		op = SACCT_EXPIRE;
		if (params.opt_long || params.opt_total 
		    || params.opt_field_list || 
		    (params.opt_gid>=0) || (params.opt_uid>=0) ||
		    params.opt_job_list || params.opt_state_list ) {
			if (params.opt_verbose)
				fprintf(stderr,
					"Switch conflict,\n"
					"\topt_long=%d\n"
					"\topt_total=%d\n"
					"\topt_field_list=%s\n"
					"\topt_gid=%d\n"
					"\topt_uid=%d\n",
					params.opt_long, 
					params.opt_total, 
					params.opt_field_list,
					params.opt_gid, 
					params.opt_uid);
			invalidSwitchCombo("--expire",
					   "--brief, --long, --fields, "
					   "--total, --gid, --uid, --jobs, "
					   "--state");
			rc = 1;
			goto finished;
		}
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
	case SACCT_EXPIRE:
		do_expire();
		break;
	case SACCT_FDUMP:
		if(get_data() == SLURM_ERROR)
			exit(errno);
		break;
	case SACCT_LIST:
		if (!params.opt_noheader)/* give them something to look */
			_print_header();/* at while we think...        */
		if(get_data() == SLURM_ERROR)
			exit(errno);
		if(params.opt_completion) 
			do_list_completion();
		else 
			do_list();
		break;
	case SACCT_STAT:
		fprintf(stderr,
			"This functionality has been replaced with 'sstat' "
			"in the future please make note this will "
			"not be supported.\n");
		
		if (!params.opt_noheader)/* give them something to look */
			_print_header();/* at while we think...        */
		do_stat();
		break;
	case SACCT_HELP:
		do_help();
		break;
	default:
		fprintf(stderr, "sacct bug: should never get here\n");
		sacct_fini();
		exit(2);
	}
finished:
	sacct_fini();
	return (rc);
}


void invalidSwitchCombo(char *good, char *bad)
{
	fprintf(stderr, "\"%s\" may not be used with %s\n", good, bad);
	return;
}

void _print_header(void)
{
	int	i,j;
	for (i=0; i<nprintfields; i++) {
		if (i)
			printf(" ");
		j=printfields[i];
		(fields[j].print_routine)(HEADLINE, 0);
	}
	printf("\n");
	for (i=0; i<nprintfields; i++) {
		if (i)
			printf(" ");
		j=printfields[i];
		(fields[j].print_routine)(UNDERSCORE, 0);
	}
	printf("\n");
}
