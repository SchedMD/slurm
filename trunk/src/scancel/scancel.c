/*****************************************************************************\
 * scancel - cancel the specified job id or step id
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

void usage(char * command);

int 
main (int argc, char *argv[]) 
{
	int error_code = 0, i;
	log_options_t opts = LOG_OPTS_STDERR_ONLY ;
	long tmp_l;
	uint32_t job_id, step_id;
	char *next_str;

	if (argc < 2) 
		usage(argv[0]);

	log_init(argv[0], opts, SYSLOG_FACILITY_DAEMON, NULL);

	tmp_l = strtol(argv[1], &next_str, 10);
	if (tmp_l <= 0)
		usage(argv[0]);
	job_id = tmp_l;

	/* cancelling individual job step */
	if (next_str[0] == '.') {
		tmp_l = strtol(&next_str[1], NULL, 10);
		if (tmp_l < 0)
			usage(argv[0]);
		step_id = tmp_l;

		for (i=0; i<10; i++) {
			error_code = slurm_cancel_job_step (job_id, step_id);
			if (error_code != ESLURM_TRANSISTION_STATE_NO_UPDATE)
				break;
			printf ("Job is in transistional state, retrying\n");
			sleep ( 5 + i );
		}
	}

	/* job only, no job step */
	else {
		for (i=0; i<10; i++) {
			error_code = slurm_cancel_job (job_id);
			if (error_code != ESLURM_TRANSISTION_STATE_NO_UPDATE)
				break;
			printf ("Job is in transistional state, retrying\n");
			sleep ( 5 + i );
		}
	}

	if (error_code) {
		printf ("slurm_cancel_job error %d %s\n",
			error_code, slurm_strerror(error_code));
		exit (1);
	}
	exit (0);
}

void
usage(char *command) 
{
	printf ("Usage: %s job_id[.step_id]\n", command);
	exit (1);
}


