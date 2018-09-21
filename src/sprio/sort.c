/*****************************************************************************\
 *  sort.c - sprio sorting functions
 *****************************************************************************
 *  Copyright (C) 2018 SchedMD LLC
 *  Written by Broderick Gardner <broderick@schedmd.com>
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

#include <sys/types.h>

#include "src/common/uid.h"
#include "src/common/xstring.h"

#include "src/sprio/sprio.h"

/* Constants */
#define DEFAULT_SORT "i"

/* Macros */
#define COND_NEGATE(cond,val) ((cond)?(-(val)):(val))
#define CMP_INT(a,b) (((a) < (b)) ? -1 : ((a) > (b)))

/* Global variables */
static bool sort_descend;

/* Local sort functions */
static int _sort_by_cluster_name(void *v1, void *v2);
static int _sort_by_job_id(void *v1, void *v2);
static int _sort_by_nice_level(void *v1, void *v2);
static int _sort_by_partition(void *v1, void *v2);
static int _sort_by_username(void *v1, void *v2);
static int _sort_by_age_prio(void *v1, void *v2);
static int _sort_by_fairshare_prio(void *v1, void *v2);
static int _sort_by_jobsize_prio(void *v1, void *v2);
static int _sort_by_partition_prio(void *v1, void *v2);
static int _sort_by_qos_prio(void *v1, void *v2);
static int _sort_by_job_prio(void *v1, void *v2);
static int _sort_by_tres_prio(void *v1, void *v2);


extern void sort_job_list(List job_list)
{
	int i;
	char c;
	if (params.sort == NULL)
		params.sort = xstrdup(DEFAULT_SORT); /* default: job priority */
	/*
	 * parse sort spec string from end, so the sorts happen in reverse
	 * order.
	 */
	for (i = strlen(params.sort); i --> 0;) {
		c = params.sort[i];
		if (c == ',' || c == '-' || c == '+')
			continue;

		/* + means ascending (default) and - means descending */
		sort_descend = false;
		if (params.sort[i-1] == '-')
			sort_descend = true;

		/*
		 * handle list sorting - weighted and normalized should
		 * sort the same, so sort by weighted
		 */
		switch (c) {
		case 'c': /* sort by cluster name */
			list_sort(job_list, _sort_by_cluster_name);
			break;
		case 'i': /* sort by job id */
			list_sort(job_list, _sort_by_job_id);
			break;
		case 'N': /* sort by nice level ??? */
			list_sort(job_list, _sort_by_nice_level);
			break;
		case 'r': /* sort by partition name */
			list_sort(job_list, _sort_by_partition);
			break;
		case 'u': /* sort by username */
			list_sort(job_list, _sort_by_username);
			break;
		case 'A': /* sort by age priority */
		case 'a':
			list_sort(job_list, _sort_by_age_prio);
			break;
		case 'F': /* sort by fair share priority */
		case 'f':
			list_sort(job_list, _sort_by_fairshare_prio);
			break;
		case 'J': /* sort by job size priority */
		case 'j':
			list_sort(job_list, _sort_by_jobsize_prio);
			break;
		case 'P': /* sort by partition priority */
		case 'p':
			list_sort(job_list, _sort_by_partition_prio);
			break;
		case 'Q': /* sort by qos priority */
		case 'q':
			list_sort(job_list, _sort_by_qos_prio);
			break;
		case 'T': /* sort by TRES priority */
		case 't':
			list_sort(job_list, _sort_by_tres_prio);
			break;
		case 'Y': /* sort by job priority */
		case 'y':
			list_sort(job_list, _sort_by_job_prio);
			break;
		default:
			error("Invalid sort specification: %c",
			      params.sort[i]);
			exit(1);
		}
	}
}


/*****************************************************************************
 * Local utility functions
 *****************************************************************************/
static inline void _get_job_prio_from_void(priority_factors_object_t **j1,
					   priority_factors_object_t **j2,
					   void *v1, void *v2)
{
	*j1 = *(priority_factors_object_t **) v1;
	*j2 = *(priority_factors_object_t **) v2;
}

static inline double _compare_double(double a, double b)
{
	if (fuzzy_equal(a, b))
		return 0;
	return (a < b) ? -1 : 1;
}

/*****************************************************************************
 * Local sort functions
 *****************************************************************************/
static int _sort_by_cluster_name(void *v1, void *v2)
{
	int cmp;
	priority_factors_object_t *job1, *job2;

	_get_job_prio_from_void(&job1, &job2, v1, v2);

	cmp = xstrcmp(job1->cluster_name, job2->cluster_name);
	return COND_NEGATE(sort_descend, cmp);
}

static int _sort_by_job_id(void *v1, void *v2)
{
	int cmp;
	priority_factors_object_t *job1, *job2;

	_get_job_prio_from_void(&job1, &job2, v1, v2);

	cmp = CMP_INT(job1->job_id, job2->job_id);
	return COND_NEGATE(sort_descend, cmp);
}

static int _sort_by_nice_level(void *v1, void *v2)
{
	int cmp;
	priority_factors_object_t *job1, *job2;

	_get_job_prio_from_void(&job1, &job2, v1, v2);

	cmp = CMP_INT(job1->nice, job2->nice);
	return COND_NEGATE(sort_descend, cmp);
}

static int _sort_by_partition(void *v1, void *v2)
{
	int cmp;
	priority_factors_object_t *job1, *job2;

	_get_job_prio_from_void(&job1, &job2, v1, v2);

	cmp = xstrcmp(job1->partition, job2->partition);
	return COND_NEGATE(sort_descend, cmp);
}

static int _sort_by_username(void *v1, void *v2)
{
	int cmp;
	priority_factors_object_t *job1, *job2;
	char *name1, *name2;

	_get_job_prio_from_void(&job1, &job2, v1, v2);

	name1 = uid_to_string_cached((uid_t) job1->user_id);
	name2 = uid_to_string_cached((uid_t) job2->user_id);
	cmp = xstrcmp(name1, name2);
	return COND_NEGATE(sort_descend, cmp);
}

static int _sort_by_age_prio(void *v1, void *v2)
{
	int cmp;
	priority_factors_object_t *job1, *job2;

	_get_job_prio_from_void(&job1, &job2, v1, v2);

	cmp = _compare_double(job1->priority_age, job2->priority_age);
	return COND_NEGATE(sort_descend, cmp);
}

static int _sort_by_fairshare_prio(void *v1, void *v2)
{
	int cmp;
	priority_factors_object_t *job1, *job2;

	_get_job_prio_from_void(&job1, &job2, v1, v2);

	cmp = _compare_double(job1->priority_fs, job2->priority_fs);
	return COND_NEGATE(sort_descend, cmp);
}


static int _sort_by_jobsize_prio(void *v1, void *v2)
{
	int cmp;
	priority_factors_object_t *job1, *job2;

	_get_job_prio_from_void(&job1, &job2, v1, v2);

	cmp = _compare_double(job1->priority_js, job2->priority_js);
	return COND_NEGATE(sort_descend, cmp);
}

static int _sort_by_partition_prio(void *v1, void *v2)
{
	int cmp;
	priority_factors_object_t *job1, *job2;

	_get_job_prio_from_void(&job1, &job2, v1, v2);

	cmp = _compare_double(job1->priority_part, job2->priority_part);
	return COND_NEGATE(sort_descend, cmp);
}

static int _sort_by_qos_prio(void *v1, void *v2)
{
	int cmp;
	priority_factors_object_t *job1, *job2;

	_get_job_prio_from_void(&job1, &job2, v1, v2);

	cmp = _compare_double(job1->priority_qos, job2->priority_qos);
	return COND_NEGATE(sort_descend, cmp);
}

static int _sort_by_tres_prio(void *v1, void *v2)
{
	int cmp, i;
	priority_factors_object_t *job1, *job2;
	double job1_sum, job2_sum;

	_get_job_prio_from_void(&job1, &job2, v1, v2);

	/* is this ever not true? */
	xassert(job1->tres_cnt == job2->tres_cnt);

	for (i = 0, job1_sum = 0; i < job1->tres_cnt; i++)
		job1_sum += job1->priority_tres[i];
	for (i = 0, job2_sum = 0; i < job2->tres_cnt; i++)
		job2_sum += job2->priority_tres[i];

	cmp = _compare_double(job1_sum, job2_sum);
	return COND_NEGATE(sort_descend, cmp);
}

static int _sort_by_job_prio(void *v1, void *v2)
{
	int cmp;
	priority_factors_object_t *job1, *job2;
	double j1_prio, j2_prio;

	_get_job_prio_from_void(&job1, &job2, v1, v2);

	j1_prio = get_priority_from_factors(job1);
	j2_prio = get_priority_from_factors(job2);
	cmp = _compare_double(j1_prio, j2_prio);
	return COND_NEGATE(sort_descend, cmp);
}

