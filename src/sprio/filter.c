/*****************************************************************************\
 *  filter.c - filter for sprio.
 *****************************************************************************
 *  Copyright (C) 2022 SchedMD LLC
 *  Written by Ben Glines <ben.glines@schedmd.com>
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

#include "src/sprio/sprio.h"
#include "src/common/xstring.h"

static int _list_find_job_id(void *x, void *key)
{
	return (*(uint32_t *) x == *(uint32_t *) key);
}

static int _list_find_user(void *x, void *key)
{
	return (*(uint32_t *) x == *(uint32_t *) key);
}

static int _list_find_part(void *x, void *key)
{
	return !xstrcmp(x, key);
}

/*
 * _filter_job - filter an individual job
 *
 * IN x - job to be filtered
 * IN key - NULL (the "keys" are in the global variable params)
 * RET - true (delete job) or false (do not delete job)
 */
static int _filter_job(void *x, void *key)
{
	priority_factors_object_t *job_ptr = x;

	if (params.job_list) {
		if (!list_find_first(params.job_list, _list_find_job_id,
				     &job_ptr->job_id)) {
			return true;
		}
	}

	if (params.user_list) {
		if (!list_find_first(params.user_list, _list_find_user,
				     &job_ptr->user_id)) {
			return true;
		}
	}

	if (params.part_list) {
		if (!list_find_first(params.part_list, _list_find_part,
				     job_ptr->partition)) {
			return true;
		}
	}

	return false;
}

extern void filter_job_list(List job_list)
{
	if ((!params.job_list && !params.part_list && !params.user_list) ||
	    (!job_list))
		return;

	list_delete_all(job_list, _filter_job, NULL);
}
