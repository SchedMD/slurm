/*****************************************************************************\
 *  slurmd_common.c - functions for determining job status
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
#include "src/common/stepd_api.h"
#include "src/common/xstring.h"

#include "src/interfaces/prep.h"

#include "src/slurmd/common/slurmd_common.h"
#include "src/slurmd/slurmd/slurmd.h"

typedef struct {
	uint32_t job_id;
	uint16_t msg_timeout;
	bool *prolog_fini;
	pthread_cond_t *timer_cond;
	pthread_mutex_t *timer_mutex;
} timer_struct_t;

static pthread_mutex_t prolog_serial_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Delay a message based upon the host index, total host count and RPC_TIME.
 * This logic depends upon synchronized clocks across the cluster.
 */
static void _delay_rpc(int host_inx, int host_cnt, int usec_per_rpc)
{
	struct timeval tv1;
	uint32_t cur_time;	/* current time in usec (just 9 digits) */
	uint32_t tot_time;	/* total time expected for all RPCs */
	uint32_t offset_time;	/* relative time within tot_time */
	uint32_t target_time;	/* desired time to issue the RPC */
	uint32_t delta_time;

again:
	if (gettimeofday(&tv1, NULL)) {
		usleep(host_inx * usec_per_rpc);
		return;
	}

	cur_time = ((tv1.tv_sec % 1000) * 1000000) + tv1.tv_usec;
	tot_time = host_cnt * usec_per_rpc;
	offset_time = cur_time % tot_time;
	target_time = host_inx * usec_per_rpc;

	if (target_time < offset_time)
		delta_time = target_time - offset_time + tot_time;
	else
		delta_time = target_time - offset_time;

	if (usleep(delta_time)) {
		if (errno == EINVAL) /* usleep for more than 1 sec */
			usleep(900000);
		/* errno == EINTR */
		goto again;
	}
}


/*
 * On a parallel job, every slurmd may send the EPILOG_COMPLETE message to the
 * slurmctld at the same time, resulting in lost messages. We add a delay here
 * to spead out the message traffic assuming synchronized clocks across the
 * cluster. Allow 10 msec processing time in slurmctld for each RPC.
 */
static void _sync_messages_kill(char *node_list)
{
	int host_cnt, host_inx;
	char *host;
	hostset_t *hosts;

	hosts = hostset_create(node_list);
	host_cnt = hostset_count(hosts);

	if (host_cnt <= 64)
		goto fini;
	if (!conf->hostname)
		goto fini;	/* should never happen */

	for (host_inx = 0; host_inx < host_cnt; host_inx++) {
		host = hostset_shift(hosts);
		if (!host)
			break;
		if (!xstrcmp(host, conf->node_name)) {
			free(host);
			break;
		}
		free(host);
	}

	_delay_rpc(host_inx, host_cnt, slurm_conf.epilog_msg_time);

fini:
	hostset_destroy(hosts);
}


/*
 * Send epilog complete message to currently active controller.
 * Returns SLURM_SUCCESS if message sent successfully,
 *         SLURM_ERROR if epilog complete message fails to be sent.
 */
extern int epilog_complete(uint32_t jobid, char *node_list, int rc)
{
	slurm_msg_t msg;
	epilog_complete_msg_t req;
	int ctld_rc;

	_sync_messages_kill(node_list);
	slurm_msg_t_init(&msg);
	memset(&req, 0, sizeof(req));

	req.job_id = jobid;
	req.return_code = rc;
	req.node_name = conf->node_name;

	msg.msg_type = MESSAGE_EPILOG_COMPLETE;
	msg.data = &req;

	/*
	 * Note: Return code is only used within the communication layer
	 * to back off the send. No other return code should be seen here.
	 * slurmctld will resend TERMINATE_JOB request if message send fails.
	 */
	if (slurm_send_recv_controller_rc_msg(&msg, &ctld_rc,
					      working_cluster_rec) < 0) {
		error("Unable to send epilog complete message: %m");
		return SLURM_ERROR;
	}

	debug("JobId=%u: sent epilog complete msg: rc = %d", jobid, rc);

	return SLURM_SUCCESS;
}

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

/*
 *  Wait for up to max_time seconds.
 *  If max_time == 0, send SIGKILL to tasks repeatedly
 *
 *  Returns true if all job processes are gone
 */
extern bool pause_for_job_completion(uint32_t job_id, int max_time,
				     bool ignore_extern)
{
	int sec = 0;
	int pause = 1;
	bool rc = false;
	int count = 0;

	while ((sec < max_time) || (max_time == 0)) {
		rc = is_job_running(job_id, ignore_extern);
		if (!rc)
			break;
		if ((max_time == 0) && (sec > 1)) {
			terminate_all_steps(job_id, true, !ignore_extern);
		}
		if (sec > 10) {
			/* Reduce logging frequency about unkillable tasks */
			if (max_time)
				pause = MIN((max_time - sec), 10);
			else
				pause = 10;
		}

		/*
		 * The job will usually finish up within the first .02 sec.  If
		 * not gradually increase the sleep until we get to a second.
		 */
		if (count == 0) {
			usleep(20000);
			count++;
		} else if (count == 1) {
			usleep(50000);
			count++;
		} else if (count == 2) {
			usleep(100000);
			count++;
		} else if (count == 3) {
			usleep(500000);
			count++;
			sec = 1;
		} else {
			sleep(pause);
			sec += pause;
		}
	}

	/*
	 * Return true if job is NOT running
	 */
	return (!rc);
}

/*
 * terminate_all_steps - signals the container of all steps of a job
 * jobid IN - id of job to signal
 * batch IN - if true signal batch script, otherwise skip it
 * extern_step IN - if true signal extern step, otherwise skip it

 * RET count of signaled job steps (plus batch script, if applicable)
 */
extern int terminate_all_steps(uint32_t jobid, bool batch, bool extern_step)
{
	list_t *steps;
	list_itr_t *i;
	step_loc_t *stepd;
	int step_cnt  = 0;
	int fd;

	steps = stepd_available(conf->spooldir, conf->node_name);
	i = list_iterator_create(steps);
	while ((stepd = list_next(i))) {
		if (stepd->step_id.job_id != jobid) {
			/* multiple jobs expected on shared nodes */
			debug3("Step from other job: jobid=%u (this jobid=%u)",
			       stepd->step_id.job_id, jobid);
			continue;
		}

		if ((stepd->step_id.step_id == SLURM_BATCH_SCRIPT) && !batch)
			continue;
		if ((stepd->step_id.step_id == SLURM_EXTERN_CONT) &&
		    !extern_step)
			continue;

		step_cnt++;

		fd = stepd_connect(stepd->directory, stepd->nodename,
				   &stepd->step_id, &stepd->protocol_version);
		if (fd == -1) {
			debug3("Unable to connect to %ps", &stepd->step_id);
			continue;
		}

		debug2("terminate %ps", &stepd->step_id);
		if (stepd_terminate(fd, stepd->protocol_version) < 0)
			debug("kill %ps failed: %m", &stepd->step_id);
		close(fd);
	}
	list_iterator_destroy(i);
	FREE_NULL_LIST(steps);
	if (step_cnt == 0)
		debug2("No steps in job %u to terminate", jobid);
	return step_cnt;
}

static void *_prolog_timer(void *x)
{
	int delay_time, rc = SLURM_SUCCESS;
	struct timespec ts;
	struct timeval now;
	slurm_msg_t msg;
	job_notify_msg_t notify_req;
	char srun_msg[128];
	timer_struct_t *timer_struct = (timer_struct_t *) x;

	delay_time = MAX(2, (timer_struct->msg_timeout - 2));
	gettimeofday(&now, NULL);
	ts.tv_sec = now.tv_sec + delay_time;
	ts.tv_nsec = now.tv_usec * 1000;
	slurm_mutex_lock(timer_struct->timer_mutex);
	if (!(*timer_struct->prolog_fini)) {
		rc = pthread_cond_timedwait(timer_struct->timer_cond,
					    timer_struct->timer_mutex, &ts);
	}
	slurm_mutex_unlock(timer_struct->timer_mutex);

	if (rc != ETIMEDOUT)
		return NULL;

	slurm_msg_t_init(&msg);
	snprintf(srun_msg, sizeof(srun_msg), "Prolog hung on node %s",
		 conf->node_name);
	memset(&notify_req, 0, sizeof(notify_req));
	notify_req.step_id.job_id	= timer_struct->job_id;
	notify_req.step_id.step_id = NO_VAL;
	notify_req.step_id.step_het_comp = NO_VAL;
	notify_req.message	= srun_msg;
	msg.msg_type	= REQUEST_JOB_NOTIFY;
	msg.data	= &notify_req;
	slurm_send_only_controller_msg(&msg, working_cluster_rec);
	return NULL;
}

/*
 * run_prolog - executes and times the prolog script
 * job_env IN - pointer to the job environment structure
 * cred IN: pointer to the credential structure
 *
 * RET 0 or error code
 */
extern int run_prolog(job_env_t *job_env, slurm_cred_t *cred)
{
	int diff_time, rc;
	time_t start_time = time(NULL);
	pthread_t       timer_id;
	pthread_cond_t  timer_cond  = PTHREAD_COND_INITIALIZER;
	pthread_mutex_t timer_mutex = PTHREAD_MUTEX_INITIALIZER;
	timer_struct_t  timer_struct;
	bool prolog_fini = false;
	bool script_lock = false;

	if (slurm_conf.prolog_flags & PROLOG_FLAG_SERIAL) {
		/*
		 * When PROLOG_FLAG_RUN_IN_JOB is set, PROLOG_FLAG_SERIAL does
		 * nothing because this function runs in the slurmstepd and
		 * therefore this mutex doesn't block other jobs from running
		 * their prolog.
		 */
		slurm_mutex_lock(&prolog_serial_mutex);
		script_lock = true;
	}

	timer_struct.job_id      = job_env->jobid;
	timer_struct.msg_timeout = slurm_conf.msg_timeout;
	timer_struct.prolog_fini = &prolog_fini;
	timer_struct.timer_cond  = &timer_cond;
	timer_struct.timer_mutex = &timer_mutex;
	slurm_thread_create(&timer_id, _prolog_timer, &timer_struct);

	rc = prep_g_prolog(job_env, cred);

	slurm_mutex_lock(&timer_mutex);
	prolog_fini = true;
	slurm_cond_broadcast(&timer_cond);
	slurm_mutex_unlock(&timer_mutex);

	diff_time = difftime(time(NULL), start_time);
	if (diff_time >= (slurm_conf.msg_timeout / 2)) {
		info("prolog for job %u ran for %d seconds",
		     job_env->jobid, diff_time);
	}

	slurm_thread_join(timer_id);
	if (script_lock)
		slurm_mutex_unlock(&prolog_serial_mutex);

	return rc;
}

/*
 * run_epilog - executes and times the epilog script
 * job_env IN - pointer to the job environment structure
 * cred IN: pointer to the credential structure
 *
 * RET 0 or error code
 */
extern int run_epilog(job_env_t *job_env, slurm_cred_t *cred)
{
	time_t start_time = time(NULL);
	int error_code, diff_time;
	bool script_lock = false;

	if (slurm_conf.prolog_flags & PROLOG_FLAG_SERIAL) {
		/*
		 * When PROLOG_FLAG_RUN_IN_JOB is set, PROLOG_FLAG_SERIAL does
		 * nothing because this function runs in the slurmstepd and
		 * therefore this mutex doesn't block other jobs from running
		 * their epilog.
		 */
		slurm_mutex_lock(&prolog_serial_mutex);
		script_lock = true;
	}

	error_code = prep_g_epilog(job_env, cred);

	diff_time = difftime(time(NULL), start_time);
	if (diff_time >= (slurm_conf.msg_timeout / 2)) {
		info("epilog for job %u ran for %d seconds",
		     job_env->jobid, diff_time);
	}

	if (script_lock)
		slurm_mutex_unlock(&prolog_serial_mutex);

	return error_code;
}
