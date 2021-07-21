/*****************************************************************************\
 *  gres_sched.c - Scheduling functions used by cons_tres
 *****************************************************************************
 *  Copyright (C) 2020 SchedMD LLC.
 *  Derived in large part from code previously in common/gres.c
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

#include "src/common/slurm_xlator.h"

#include "gres_sched.h"

#include "src/common/xstring.h"

/*
 * Find a gres_state_t job record in a list by matching the plugin_id and
 *	type_id from a sock_gres_t record
 * IN x - a gres_state_t record (from a job) to test
 * IN key - the sock_gres_t record we want to match
 * RET 1 on match, otherwise 0
 */
static int _find_job_by_sock_gres(void *x, void *key)
{
	gres_state_t *job_gres_state = (gres_state_t *) x;
	gres_job_state_t *job_data;
	sock_gres_t *sock_data = (sock_gres_t *) key;

	job_data = (gres_job_state_t *) job_gres_state->gres_data;
	if ((sock_data->plugin_id == job_gres_state->plugin_id) &&
	    (sock_data->type_id  == job_data->type_id))
		return 1;
	return 0;
}

/*
 * Given a List of sock_gres_t entries, return a string identifying the
 * count of each GRES available on this set of nodes
 * IN sock_gres_list - count of GRES available in this group of nodes
 * IN job_gres_list - job GRES specification, used only to get GRES name/type
 * RET xfree the returned string
 */
extern char *gres_sched_str(List sock_gres_list, List job_gres_list)
{
	ListIterator iter;
	sock_gres_t *sock_data;
	gres_state_t *job_gres_state;
	gres_job_state_t *job_data;
	char *out_str = NULL, *sep;

	if (!sock_gres_list)
		return NULL;

	iter = list_iterator_create(sock_gres_list);
	while ((sock_data = (sock_gres_t *) list_next(iter))) {
		job_gres_state = list_find_first(job_gres_list,
					   _find_job_by_sock_gres, sock_data);
		if (!job_gres_state) {	/* Should never happen */
			error("%s: Could not find job GRES for type %u:%u",
			      __func__, sock_data->plugin_id,
			      sock_data->type_id);
			continue;
		}
		job_data = (gres_job_state_t *) job_gres_state->gres_data;
		if (out_str)
			sep = ",";
		else
			sep = "GRES:";
		if (job_data->type_name) {
			xstrfmtcat(out_str, "%s%s:%s:%"PRIu64, sep,
				   job_data->gres_name, job_data->type_name,
				   sock_data->total_cnt);
		} else {
			xstrfmtcat(out_str, "%s%s:%"PRIu64, sep,
				   job_data->gres_name, sock_data->total_cnt);
		}
	}
	list_iterator_destroy(iter);

	return out_str;
}

/*
 * Clear GRES allocation info for all job GRES at start of scheduling cycle
 * Return TRUE if any gres_per_job constraints to satisfy
 */
extern bool gres_sched_init(List job_gres_list)
{
	ListIterator iter;
	gres_state_t *job_gres_state;
	gres_job_state_t *job_data;
	bool rc = false;

	if (!job_gres_list)
		return rc;

	iter = list_iterator_create(job_gres_list);
	while ((job_gres_state = list_next(iter))) {
		job_data = (gres_job_state_t *) job_gres_state->gres_data;
		if (!job_data->gres_per_job)
			continue;
		job_data->total_gres = 0;
		rc = true;
	}
	list_iterator_destroy(iter);

	return rc;
}

/*
 * Return TRUE if all gres_per_job specifications are satisfied
 */
extern bool gres_sched_test(List job_gres_list, uint32_t job_id)
{
	ListIterator iter;
	gres_state_t *job_gres_state;
	gres_job_state_t *job_data;
	bool rc = true;

	if (!job_gres_list)
		return rc;

	iter = list_iterator_create(job_gres_list);
	while ((job_gres_state = list_next(iter))) {
		job_data = (gres_job_state_t *) job_gres_state->gres_data;
		if (job_data->gres_per_job &&
		    (job_data->gres_per_job > job_data->total_gres)) {
			rc = false;
			break;
		}
	}
	list_iterator_destroy(iter);

	return rc;
}

/*
 * Return TRUE if all gres_per_job specifications will be satisfied with
 *	the addtitional resources provided by a single node
 * IN job_gres_list - List of job's GRES requirements (job_gres_state_t)
 * IN sock_gres_list - Per socket GRES availability on this node (sock_gres_t)
 * IN job_id - The job being tested
 */
extern bool gres_sched_test2(List job_gres_list, List sock_gres_list,
			     uint32_t job_id)
{
	ListIterator iter;
	gres_state_t *job_gres_state;
	gres_job_state_t *job_data;
	sock_gres_t *sock_data;
	bool rc = true;

	if (!job_gres_list)
		return rc;

	iter = list_iterator_create(job_gres_list);
	while ((job_gres_state = list_next(iter))) {
		job_data = (gres_job_state_t *) job_gres_state->gres_data;
		if ((job_data->gres_per_job == 0) ||
		    (job_data->gres_per_job < job_data->total_gres))
			continue;
		sock_data = list_find_first(sock_gres_list,
					    gres_find_sock_by_job_state,
					    job_gres_state);
		if (!sock_data ||
		    (job_data->gres_per_job >
		     (job_data->total_gres + sock_data->total_cnt))) {
			rc = false;
			break;
		}
	}
	list_iterator_destroy(iter);

	return rc;
}

/*
 * Update a job's total_gres counter as we add a node to potential allocation
 * IN job_gres_list - List of job's GRES requirements (job_gres_state_t)
 * IN sock_gres_list - Per socket GRES availability on this node (sock_gres_t)
 * IN/OUT avail_cpus - CPUs currently available on this node
 */
extern void gres_sched_add(List job_gres_list, List sock_gres_list,
			   uint16_t *avail_cpus)
{
	ListIterator iter;
	gres_state_t *job_gres_state;
	gres_job_state_t *job_data;
	sock_gres_t *sock_data;
	uint64_t gres_limit;
	uint16_t gres_cpus = 0;

	if (!job_gres_list || !(*avail_cpus))
		return;

	iter = list_iterator_create(job_gres_list);
	while ((job_gres_state = list_next(iter))) {
		job_data = (gres_job_state_t *) job_gres_state->gres_data;
		if (!job_data->gres_per_job)	/* Don't care about totals */
			continue;
		sock_data = list_find_first(sock_gres_list,
					    gres_find_sock_by_job_state,
					    job_gres_state);
		if (!sock_data)		/* None of this GRES available */
			continue;
		if (job_data->cpus_per_gres) {
			gres_limit = *avail_cpus / job_data->cpus_per_gres;
			gres_limit = MIN(gres_limit, sock_data->total_cnt);
			gres_cpus = MAX(gres_cpus,
					gres_limit * job_data->cpus_per_gres);
		} else
			gres_limit = sock_data->total_cnt;
		job_data->total_gres += gres_limit;
	}
	list_iterator_destroy(iter);
	if (gres_cpus)
		*avail_cpus = gres_cpus;
}

/*
 * Create/update List GRES that can be made available on the specified node
 * IN/OUT consec_gres - List of sock_gres_t that can be made available on
 *			a set of nodes
 * IN job_gres_list - List of job's GRES requirements (gres_job_state_t)
 * IN sock_gres_list - Per socket GRES availability on this node (sock_gres_t)
 */
extern void gres_sched_consec(List *consec_gres, List job_gres_list,
			      List sock_gres_list)
{
	ListIterator iter;
	gres_state_t *job_gres_state;
	gres_job_state_t *job_data;
	sock_gres_t *sock_data, *consec_data;

	if (!job_gres_list)
		return;

	iter = list_iterator_create(job_gres_list);
	while ((job_gres_state = list_next(iter))) {
		job_data = (gres_job_state_t *) job_gres_state->gres_data;
		if (!job_data->gres_per_job)	/* Don't care about totals */
			continue;
		sock_data = list_find_first(sock_gres_list,
					    gres_find_sock_by_job_state,
					    job_gres_state);
		if (!sock_data)		/* None of this GRES available */
			continue;
		if (*consec_gres == NULL)
			*consec_gres = list_create(gres_sock_delete);
		consec_data = list_find_first(*consec_gres,
					      gres_find_sock_by_job_state,
					      job_gres_state);
		if (!consec_data) {
			consec_data = xmalloc(sizeof(sock_gres_t));
			consec_data->plugin_id = sock_data->plugin_id;
			consec_data->type_id   = sock_data->type_id;
			list_append(*consec_gres, consec_data);
		}
		consec_data->total_cnt += sock_data->total_cnt;
	}
	list_iterator_destroy(iter);
}

/*
 * Determine if the additional sock_gres_list resources will result in
 * satisfying the job's gres_per_job constraints
 * IN job_gres_list - job's GRES requirements
 * IN sock_gres_list - available GRES in a set of nodes, data structure built
 *		       by gres_job_sched_consec()
 */
extern bool gres_sched_sufficient(List job_gres_list, List sock_gres_list)
{
	ListIterator iter;
	gres_state_t *job_gres_state;
	gres_job_state_t *job_data;
	sock_gres_t *sock_data;
	bool rc = true;

	if (!job_gres_list)
		return true;
	if (!sock_gres_list)
		return false;

	iter = list_iterator_create(job_gres_list);
	while ((job_gres_state = list_next(iter))) {
		job_data = (gres_job_state_t *) job_gres_state->gres_data;
		if (!job_data->gres_per_job)	/* Don't care about totals */
			continue;
		if (job_data->total_gres >= job_data->gres_per_job)
			continue;
		sock_data = list_find_first(sock_gres_list,
					    gres_find_sock_by_job_state,
					    job_gres_state);
		if (!sock_data)	{	/* None of this GRES available */
			rc = false;
			break;
		}
		if ((job_data->total_gres + sock_data->total_cnt) <
		    job_data->gres_per_job) {
			rc = false;
			break;
		}
	}
	list_iterator_destroy(iter);

	return rc;
}
