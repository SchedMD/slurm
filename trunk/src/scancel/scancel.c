/*****************************************************************************\
 * scancel - cancel specified job(s) and/or job step(s)
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette@llnl.gov>
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if HAVE_INTTYPES_H
#  include <inttypes.h>
#else  /* !HAVE_INTTYPES_H */
#  if HAVE_STDINT_H
#    include <stdint.h>
#  endif
#endif  /* HAVE_INTTYPES_H */

#include <src/api/slurm.h>
#include <src/common/log.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/xmalloc.h>

#define MAX_CANCEL_RETRY 10

int confirmation (uint32_t job_id, int has_step, uint32_t step_id);
void job_cancel (char *name, int interactive);
void usage (char * command);

int
main (int argc, char *argv[]) 
{
	int interactive = 0, pos;
	log_options_t opts = LOG_OPTS_STDERR_ONLY ;

	if (argc < 2) {
		usage (argv[0]);
		exit (1);
	}

	log_init (argv[0], opts, SYSLOG_FACILITY_DAEMON, NULL);

	for (pos = 1; pos < argc; pos++) {
		if (argv[pos][0] != '-')
			break;
		else if (strncmp (argv[pos], "-help", 2) == 0) 
			usage (argv[0]);
		else if (strcmp (argv[pos], "-i") == 0) 
			interactive = 1;
		else if (strcmp (argv[pos], "-v") == 0) 
			printf ("Version %s\n", VERSION);
		else {
			fprintf (stderr, "Invalid option %s\n", argv[pos]);
			exit (1);
		}
	}

	for ( ; pos<argc; pos++) {
		job_cancel (argv[pos], interactive);
	}

	exit (0);
}

/* job_cancel - process request to cancel a specific job or job step */
void
job_cancel (char *name, int interactive)
{
	int error_code = 0, i;
	long tmp_l;
	uint32_t job_id, step_id;
	char *next_str;

	tmp_l = strtol(name, &next_str, 10);
	if (tmp_l <= 0) {
		fprintf (stderr, "Invalid job_id %s\n", name);
		exit (1);
	}
	job_id = tmp_l;

	/* cancelling individual job step */
	if (next_str[0] == '.') {
		tmp_l = strtol(&next_str[1], NULL, 10);
		if (tmp_l < 0) {
			fprintf (stderr, "Invalid step_id %s\n", name);
			exit (1);
		}
		step_id = tmp_l;

		if (interactive && (confirmation (job_id, 1, step_id) == 0 ))
			return;

		for (i=0; i<MAX_CANCEL_RETRY; i++) {
			error_code = slurm_cancel_job_step (job_id, step_id);
			if ((error_code == 0) || 
			    (errno != ESLURM_TRANSITION_STATE_NO_UPDATE))
				break;
			printf ("Job is in transistional state, retrying\n");
			sleep ( 5 + i );
		}
	}

	/* cancelling entire job, no job step */
	else {
		if (interactive && (confirmation (job_id, 0, 0) == 0 ))
			return;

		for (i=0; i<MAX_CANCEL_RETRY; i++) {
			error_code = slurm_cancel_job (job_id);
			if ((error_code == 0) || 
			    (errno != ESLURM_TRANSITION_STATE_NO_UPDATE))
				break;
			printf ("Job is in transistional state, retrying\n");
			sleep ( 5 + i );
		}
	}

	if (error_code) {
		slurm_perror ("Cancel job error: ");
		exit (1);
	}
}

/* confirmation - Confirm job cancel request interactively */
int 
confirmation (uint32_t job_id, int has_step, uint32_t step_id)
{
	char in_line[128];

	while (1) {
		if (has_step)
			printf ("Cancel job step %u.%u [y/n]? ", job_id, step_id);
		else
			printf ("Cancel job %u [y/n]? ", job_id);

		fgets (in_line, sizeof (in_line), stdin);
		if ((in_line[0] == 'y') || (in_line[0] == 'Y'))
			return 1;
		if ((in_line[0] == 'n') || (in_line[0] == 'N'))
			return 0;
	}

}

/* usage - print message describing command lone options for scancel */
void
usage (char *command) 
{
	printf ("Usage: %s [-i] [-v] job_id[.step_id] [job_id[.step_id] ...]\n", command);
}


