/*****************************************************************************\
 *  id_util.c
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

#include "src/common/identity.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

extern char *user_from_job(job_record_t *job_ptr)
{
	xassert(job_ptr);

	if (job_ptr->id)
		return xstrdup(job_ptr->id->pw_name);

	return uid_to_string_or_null(job_ptr->user_id);
}

extern char *group_from_job(job_record_t *job_ptr)
{
	xassert(job_ptr);

	if (job_ptr->id && job_ptr->id->gr_names) {
		for (int i = 0; i < job_ptr->id->ngids; i++) {
			if (job_ptr->id->gids[i] == job_ptr->group_id)
				return xstrdup(job_ptr->id->gr_names[i]);
		}
	}

	return gid_to_string_or_null(job_ptr->group_id);
}
