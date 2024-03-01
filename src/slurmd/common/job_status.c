/*****************************************************************************\
 *  job_status.c - functions for determining job status
 *****************************************************************************
 *  Copyright (C) 2024 SchedMD LLC.
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

#include "src/common/stepd_api.h"
#include "src/slurmd/common/job_status.h"
#include "src/slurmd/slurmd/slurmd.h"

extern bool is_job_running(uint32_t job_id, bool ignore_extern)
{
	bool retval = false;
	list_t *steps;
	list_itr_t *i;
	step_loc_t *s = NULL;

	steps = stepd_available(conf->spooldir, conf->node_name);
	i = list_iterator_create(steps);
	while ((s = list_next(i))) {
		int fd;
		if (s->step_id.job_id != job_id)
			continue;
		if (ignore_extern && (s->step_id.step_id == SLURM_EXTERN_CONT))
			continue;

		fd = stepd_connect(s->directory, s->nodename,
				   &s->step_id, &s->protocol_version);
		if (fd == -1)
			continue;

		if (stepd_state(fd, s->protocol_version)
		    != SLURMSTEPD_NOT_RUNNING) {
			retval = true;
			close(fd);
			break;
		}
		close(fd);
	}
	list_iterator_destroy(i);
	FREE_NULL_LIST(steps);

	return retval;
}
