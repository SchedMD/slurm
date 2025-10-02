/*****************************************************************************\
 *  job_mem_limit.c
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

#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/stepd_api.h"
#include "src/common/xmalloc.h"

#include "src/interfaces/jobacct_gather.h"

#include "src/slurmd/slurmd/job_mem_limit.h"
#include "src/slurmd/slurmd/slurmd.h"

typedef struct {
	uint32_t job_id;
	uint64_t job_mem;
} job_mem_limits_t;

typedef struct {
	uint32_t job_id;
	uint64_t mem_limit; /* MB */
	uint64_t mem_used; /* MB */
	uint64_t vsize_limit; /* MB */
	uint64_t vsize_used; /* MB */
} job_mem_info_t;

static int _extract_limit_from_step(void *x, void *arg);

static pthread_mutex_t job_limits_mutex = PTHREAD_MUTEX_INITIALIZER;
static list_t *job_limits_list = NULL;

extern void job_mem_limit_init(void)
{
	list_t *steps = NULL;

	if (!slurm_conf.job_acct_oom_kill) {
		debug("%s: disabled", __func__);
	}

	debug("%s: enabled", __func__);

	slurm_mutex_lock(&job_limits_mutex);
	job_limits_list = list_create(xfree_ptr);

	/* set up limits from currently running steps */
	steps = stepd_available(conf->spooldir, conf->node_name);
	list_for_each(steps, _extract_limit_from_step, NULL);
	FREE_NULL_LIST(steps);

	slurm_mutex_unlock(&job_limits_mutex);
}

extern void job_mem_limit_fini(void)
{
	slurm_mutex_lock(&job_limits_mutex);
	FREE_NULL_LIST(job_limits_list);
	slurm_mutex_unlock(&job_limits_mutex);
}

static int _match_job(void *x, void *key)
{
	job_mem_limits_t *job_limits_ptr = x;
	uint32_t *job_id = key;

	if (job_limits_ptr->job_id == *job_id)
		return 1;
	return 0;
}

static void _cancel_step_mem_limit(uint32_t job_id, uint32_t step_id)
{
	slurm_msg_t msg;
	job_notify_msg_t notify_req;
	job_step_kill_msg_t kill_req;

	/* NOTE: Batch jobs may have no srun to get this message */
	slurm_msg_t_init(&msg);
	memset(&notify_req, 0, sizeof(notify_req));
	notify_req.step_id.job_id = job_id;
	notify_req.step_id.step_id = step_id;
	notify_req.step_id.step_het_comp = NO_VAL;
	notify_req.message = "Exceeded job memory limit";
	msg.msg_type = REQUEST_JOB_NOTIFY;
	msg.data = &notify_req;
	slurm_send_only_controller_msg(&msg, working_cluster_rec);

	memset(&kill_req, 0, sizeof(kill_req));
	memcpy(&kill_req.step_id, &notify_req, sizeof(kill_req.step_id));
	kill_req.signal = SIGKILL;
	kill_req.flags = KILL_OOM;
	msg.msg_type = REQUEST_CANCEL_JOB_STEP;
	msg.data = &kill_req;
	slurm_send_only_controller_msg(&msg, working_cluster_rec);
}

static int _extract_limit_from_step(void *x, void *arg)
{
	step_loc_t *stepd = x;
	int fd;
	job_mem_limits_t *step_limits;
	slurmstepd_mem_info_t stepd_mem_info;

	fd = stepd_connect(stepd->directory, stepd->nodename, &stepd->step_id,
			   &stepd->protocol_version);
	if (fd == -1)
		return 1; /* step completed */

	if (stepd_get_mem_limits(fd, stepd->protocol_version,
				 &stepd_mem_info) != SLURM_SUCCESS) {
		error("Error reading %ps memory limits from slurmstepd",
		      &stepd->step_id);
		close(fd);
		return 1;
	}

	if (stepd_mem_info.job_mem_limit) {
		step_limits = list_find_first(job_limits_list, _match_job,
					      &stepd->step_id.job_id);

		if (step_limits) {
			if (stepd_mem_info.job_mem_limit > step_limits->job_mem)
				step_limits->job_mem =
					stepd_mem_info.job_mem_limit;
		} else {
			/* create entry for this step */
			step_limits = xmalloc(sizeof(*step_limits));
			step_limits->job_id = stepd->step_id.job_id;
			step_limits->job_mem = stepd_mem_info.job_mem_limit;
			debug2("%s: RecLim JobId=%u job_mem:%"PRIu64,
			       __func__, stepd->step_id.job_id, step_limits->job_mem);
			list_append(job_limits_list, step_limits);
		}
	}
	close(fd);

	return 1;
}

static int _add_step_usage(void *x, void *arg)
{
	step_loc_t *stepd = x;
	job_mem_info_t *job_mem_info_ptr = arg;
	job_step_stat_t *resp = NULL;
	uint64_t step_rss = 0, step_vsize = 0;
	int fd = -1;

	while (job_mem_info_ptr->job_id) {
		if (job_mem_info_ptr->job_id == stepd->step_id.job_id)
			break;
		job_mem_info_ptr++;
	}

	/* job memory limit unknown or unlimited */
	if (!job_mem_info_ptr->job_id)
		return 1;

	fd = stepd_connect(stepd->directory, stepd->nodename, &stepd->step_id,
			   &stepd->protocol_version);
	if (fd == -1)
		return 1; /* step completed */

	resp = xmalloc(sizeof(job_step_stat_t));

	if ((!stepd_stat_jobacct(fd, stepd->protocol_version, &stepd->step_id,
				 resp)) &&
	    (resp->jobacct)) {
		/* resp->jobacct is NULL if account is disabled */
		jobacctinfo_getinfo(resp->jobacct, JOBACCT_DATA_TOT_RSS,
				    &step_rss, stepd->protocol_version);
		jobacctinfo_getinfo(resp->jobacct, JOBACCT_DATA_TOT_VSIZE,
				    &step_vsize, stepd->protocol_version);
		debug2("%s: %ps RSS:%"PRIu64" B VSIZE:%"PRIu64" B",
		       __func__, &stepd->step_id, step_rss, step_vsize);

		if (step_rss != INFINITE64) {
			step_rss /= 1048576; /* B to MB */
			step_rss = MAX(step_rss, 1);
			job_mem_info_ptr->mem_used += step_rss;
		}

		if (step_vsize != INFINITE64) {
			step_vsize /= 1048576; /* B to MB */
			step_vsize = MAX(step_vsize, 1);
			job_mem_info_ptr->vsize_used += step_vsize;
		}
	}
	slurm_free_job_step_stat(resp);
	close(fd);

	return 1;
}

/* Enforce job memory limits here in slurmd. Step memory limits are
 * enforced within slurmstepd (using jobacct_gather plugin). */
extern void job_mem_limit_enforce(void)
{
	list_t *steps;
	list_itr_t *job_limits_iter;
	job_mem_limits_t *job_limits_ptr;
	int job_cnt;
	job_mem_info_t *job_mem_info_ptr = NULL;

	if (!slurm_conf.job_acct_oom_kill)
		return;

	slurm_mutex_lock(&job_limits_mutex);
	if (list_count(job_limits_list) == 0) {
		slurm_mutex_unlock(&job_limits_mutex);
		return;
	}

	/* Build table of job limits, use highest mem limit recorded */
	job_mem_info_ptr = xcalloc((list_count(job_limits_list) + 1),
				   sizeof(job_mem_info_t));
	job_cnt = 0;
	job_limits_iter = list_iterator_create(job_limits_list);
	while ((job_limits_ptr = list_next(job_limits_iter))) {
		job_mem_info_ptr[job_cnt].job_id = job_limits_ptr->job_id;
		job_mem_info_ptr[job_cnt].mem_limit = job_limits_ptr->job_mem;
		job_mem_info_ptr[job_cnt].vsize_limit = job_limits_ptr->job_mem;
		job_mem_info_ptr[job_cnt].vsize_limit *=
			(slurm_conf.vsize_factor / 100.0);
		job_cnt++;
	}
	list_iterator_destroy(job_limits_iter);
	slurm_mutex_unlock(&job_limits_mutex);

	steps = stepd_available(conf->spooldir, conf->node_name);
	list_for_each(steps, _add_step_usage, job_mem_info_ptr);
	FREE_NULL_LIST(steps);

	for (int i = 0; i < job_cnt; i++) {
		if (job_mem_info_ptr[i].mem_used == 0) {
			/* no steps found,
			 * purge records for all steps of this job */
			slurm_mutex_lock(&job_limits_mutex);
			list_delete_all(job_limits_list, _match_job,
					&job_mem_info_ptr[i].job_id);
			slurm_mutex_unlock(&job_limits_mutex);
			break;
		}

		if ((job_mem_info_ptr[i].mem_limit != 0) &&
		    (job_mem_info_ptr[i].mem_used >
		     job_mem_info_ptr[i].mem_limit)) {
			info("Job %u exceeded memory limit "
			     "(%"PRIu64">%"PRIu64"), cancelling it",
			     job_mem_info_ptr[i].job_id,
			     job_mem_info_ptr[i].mem_used,
			     job_mem_info_ptr[i].mem_limit);
			_cancel_step_mem_limit(job_mem_info_ptr[i].job_id,
					       NO_VAL);
		} else if ((job_mem_info_ptr[i].vsize_limit != 0) &&
			   (job_mem_info_ptr[i].vsize_used >
			    job_mem_info_ptr[i].vsize_limit)) {
			info("Job %u exceeded virtual memory limit "
			     "(%"PRIu64">%"PRIu64"), cancelling it",
			     job_mem_info_ptr[i].job_id,
			     job_mem_info_ptr[i].vsize_used,
			     job_mem_info_ptr[i].vsize_limit);
			_cancel_step_mem_limit(job_mem_info_ptr[i].job_id,
					       NO_VAL);
		}
	}
	xfree(job_mem_info_ptr);
}

extern void job_mem_limit_register(uint32_t job_id, uint64_t job_mem_limit)
{
	job_mem_limits_t *job_limits_ptr = NULL;

	if (!slurm_conf.job_acct_oom_kill)
		return;

	if (!job_mem_limit)
		return;

	slurm_mutex_lock(&job_limits_mutex);
	job_limits_ptr = list_find_first(job_limits_list, _match_job, &job_id);
	if (!job_limits_ptr) {
		job_limits_ptr = xmalloc(sizeof(job_mem_limits_t));
		job_limits_ptr->job_id = job_id;
		job_limits_ptr->job_mem = job_mem_limit;
		debug2("%s: AddLim JobId=%u job_mem:%"PRIu64,
		       __func__, job_limits_ptr->job_id,
		       job_limits_ptr->job_mem);
		list_append(job_limits_list, job_limits_ptr);
	} else if (job_mem_limit > job_limits_ptr->job_mem) {
		job_limits_ptr->job_mem = job_mem_limit;
	}
	slurm_mutex_unlock(&job_limits_mutex);
}
