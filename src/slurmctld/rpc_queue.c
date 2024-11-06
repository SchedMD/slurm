/*****************************************************************************\
 *  rpc_queue.c
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

#include "config.h"

#include <inttypes.h>

#if HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/serializer.h"

#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/state_save.h"

bool enabled = true;

static void *_rpc_queue_worker(void *arg)
{
	slurmctld_rpc_t *q = (slurmctld_rpc_t *) arg;
	int processed = 0;
	long processed_usec = 0;

#if HAVE_SYS_PRCTL_H
	char *name = xstrdup_printf("rpcq-%u", q->msg_type);
	if (prctl(PR_SET_NAME, name, NULL, NULL, NULL) < 0) {
		error("%s: cannot set my name to %s %m", __func__, "sstate");
	}
	xfree(name);
#endif

	/*
	 * Acquire on init to simplify the inner loop.
	 * On rpc_queue_init() this will proceed directly to slurm_cond_wait().
	 */
	lock_slurmctld(q->locks);

	/*
	 * Process as many queued messages as possible in one slurmctld_lock()
	 * acquisition, then fall back to sleep until additional work is queued.
	 */
	while (true) {
		slurm_msg_t *msg = NULL;
		bool highload = false;
		long sleep_usec = 0;

		/* apply per-cycle rate limiting, if configured */
		if ((q->max_per_cycle && (processed == q->max_per_cycle)) ||
		    (q->max_usec_per_cycle &&
		     (processed_usec >= q->max_usec_per_cycle)))
			highload = true;
		else
			msg = list_dequeue(q->work);

		if (!msg) {
			unlock_slurmctld(q->locks);

			if (processed && q->post_func)
				q->post_func();

			if (processed) {
				slurm_mutex_lock(&q->mutex);
				q->cycle_last = processed;
				if (processed > q->cycle_max)
					q->cycle_max = processed;
				record_rpc_queue_stats(q);
				slurm_mutex_unlock(&q->mutex);
			}

			/*
			 * Use yield_sleep if there's more work to be done,
			 * otherwise interval if set, otherwise 500 usec.
			 */
			if (highload && (q->yield_sleep > 0))
				sleep_usec = q->yield_sleep;
			else if (q->interval > 0)
				sleep_usec = q->interval;
			else
				sleep_usec = 500;

			/*
			 * Rate limit RPC processing. Ensure that when we
			 * stop processing we don't immediately start again
			 * by inserting a slight delay.
			 *
			 * This encourages additional RPCs to accumulate,
			 * which is desirable as it lowers pressure on the
			 * slurmctld locks.
			 *
			 * This extends the race described below, but this
			 * is handled properly.
			 */

			log_flag(PROTOCOL, "%s(%s): sleeping %ld usec after processing %d/%u msgs (processed_usec=%ld/%d)",
				 __func__, q->msg_name, sleep_usec,
				 processed, q->max_per_cycle,
				 processed_usec, q->max_usec_per_cycle);
			processed = 0;
			processed_usec = 0;
			usleep(sleep_usec);

			slurm_mutex_lock(&q->mutex);

			if (q->shutdown) {
				log_flag(PROTOCOL, "%s(%s): shutting down",
					 __func__, q->msg_name);
				slurm_mutex_unlock(&q->mutex);
				return NULL;
			}

			/*
			 * Verify list is empty. Since list_dequeue() above is
			 * called without the mutex held, there is a race with
			 * rpc_enqueue() that this check will solve.
			 */
			if (!list_count(q->work))
				slurm_cond_wait(&q->cond, &q->mutex);

			slurm_mutex_unlock(&q->mutex);
			log_flag(PROTOCOL, "%s(%s): woke up",
				 __func__, q->msg_name);
			lock_slurmctld(q->locks);
		} else {
			DEF_TIMERS;
			START_TIMER;
			if (q->max_queued) {
				slurm_mutex_lock(&q->mutex);
				q->queued--;
				record_rpc_queue_stats(q);
				slurm_mutex_unlock(&q->mutex);
			}
			msg->flags |= CTLD_QUEUE_PROCESSING;
			q->func(msg);
			if ((msg->conn_fd >= 0) && (close(msg->conn_fd) < 0))
				error("close(%d): %m", msg->conn_fd);

			END_TIMER;
			record_rpc_stats(msg, DELTA_TIMER);
			slurm_free_msg(msg);
			processed++;
			processed_usec += DELTA_TIMER;
		}
	}

	return NULL;
}

static data_t *_load_config(void)
{
	char *file = get_extra_conf_path("rpc_queue.yaml");
	buf_t *buf = create_mmap_buf(file);
	data_t *conf = NULL;

	if (!buf) {
		debug("%s: could not load %s, ignoring", __func__, file);
		xfree(file);
		return NULL;
	}

	if (serialize_g_string_to_data(&conf, buf->head, buf->size,
				       MIME_TYPE_YAML))
		fatal("Failed to decode %s", file);

	FREE_NULL_BUFFER(buf);
	xfree(file);
	return conf;
}

static bool _find_msg_name(const data_t *data, void *needle)
{
	const data_t *type = NULL;

	if (data_get_type(data) != DATA_TYPE_DICT)
		return false;

	type = data_key_get_const(data, "type");

	if (data_get_type(type) != DATA_TYPE_STRING)
		return false;

	return !xstrcasecmp(data_get_string(type), needle);
}

static void _apply_config(data_t *conf, slurmctld_rpc_t *q)
{
	data_t *rpc_queue = NULL, *settings = NULL, *field = NULL;
	int64_t int64_tmp;

	if (!conf || !q)
		return;

	rpc_queue = data_key_get(conf, "rpc_queue");
	if (data_get_type(rpc_queue) != DATA_TYPE_LIST)
		return;

	if (!(settings = data_list_find_first(rpc_queue, _find_msg_name,
					      (void *) q->msg_name)))
		return;

	if ((field = data_key_get(settings, "disabled"))) {
		bool disabled = false;
		if (!data_get_bool_converted(field, &disabled)) {
			q->queue_enabled = false;
			return;
		}
	}

	if ((field = data_key_get(settings, "hard_drop")))
		(void) data_get_bool_converted(field, &q->hard_drop);

	if ((field = data_key_get(settings, "max_per_cycle")))
		if (!data_get_int_converted(field, &int64_tmp))
			q->max_per_cycle = int64_tmp;

	if ((field = data_key_get(settings, "max_usec_per_cycle")))
		if (!data_get_int_converted(field, &int64_tmp))
			q->max_usec_per_cycle = int64_tmp;

	if ((field = data_key_get(settings, "max_queued")))
		if (!data_get_int_converted(field, &int64_tmp))
			q->max_queued = int64_tmp;

	if ((field = data_key_get(settings, "yield_sleep")))
		if (!data_get_int_converted(field, &int64_tmp))
			q->yield_sleep = int64_tmp;

	if ((field = data_key_get(settings, "interval")))
		if (!data_get_int_converted(field, &int64_tmp))
			q->interval = int64_tmp;
}

extern void rpc_queue_init(void)
{
	data_t *conf = NULL;

	if (!xstrcasestr(slurm_conf.slurmctld_params, "enable_rpc_queue")) {
		enabled = false;
		return;
	}

	error("enabled experimental rpc queuing system");

	conf = _load_config();

	for (slurmctld_rpc_t *q = slurmctld_rpcs; q->msg_type; q++) {
		if (!q->queue_enabled)
			continue;

		q->msg_name = rpc_num2string(q->msg_type);

		_apply_config(conf, q);

		/* config may have disabled this queue, check again */
		if (!q->queue_enabled) {
			verbose("disabled rpc_queue for %s", q->msg_name);
			continue;
		}

		q->work = list_create(NULL);
		slurm_cond_init(&q->cond, NULL);
		slurm_mutex_init(&q->mutex);
		q->shutdown = false;

		verbose("starting rpc_queue for %s: max_per_cycle=%u max_usec_per_cycle=%u max_queued=%d hard_drop=%d yield_sleep=%d interval=%d",
			q->msg_name, q->max_per_cycle, q->max_usec_per_cycle,
			q->max_queued, q->hard_drop, q->yield_sleep,
			q->interval);
		slurm_thread_create(&q->thread, _rpc_queue_worker, q);
	}

	FREE_NULL_DATA(conf);
}

extern void rpc_queue_shutdown(void)
{
	if (!enabled)
		return;

	enabled = false;

	/* mark all as shut down */
	for (slurmctld_rpc_t *q = slurmctld_rpcs; q->msg_type; q++) {
		if (!q->queue_enabled)
			continue;

		slurm_mutex_lock(&q->mutex);
		q->shutdown = true;
		slurm_cond_signal(&q->cond);
		slurm_mutex_unlock(&q->mutex);
	}

	/* wait for completion and cleanup */
	for (slurmctld_rpc_t *q = slurmctld_rpcs; q->msg_type; q++) {
		if (!q->queue_enabled)
			continue;

		slurm_thread_join(q->thread);
		FREE_NULL_LIST(q->work);
	}
}

extern bool rpc_queue_enabled(void)
{
	return enabled;
}

extern int rpc_enqueue(slurm_msg_t *msg)
{
	if (!enabled)
		return ESLURM_NOT_SUPPORTED;

	for (slurmctld_rpc_t *q = slurmctld_rpcs; q->msg_type; q++) {
		if (q->msg_type == msg->msg_type) {
			if (!q->queue_enabled)
				break;

			if (q->max_queued) {
				slurm_mutex_lock(&q->mutex);
				if (q->queued >= q->max_queued) {
					q->dropped++;
					record_rpc_queue_stats(q);
					slurm_mutex_unlock(&q->mutex);
					if (q->hard_drop)
						return SLURMCTLD_COMMUNICATIONS_HARD_DROP;
					else
						return SLURMCTLD_COMMUNICATIONS_BACKOFF;
				}
				q->queued++;
				record_rpc_queue_stats(q);
				slurm_mutex_unlock(&q->mutex);
			}

			list_enqueue(q->work, msg);
			slurm_mutex_lock(&q->mutex);
			slurm_cond_signal(&q->cond);
			slurm_mutex_unlock(&q->mutex);
			return SLURM_SUCCESS;
		}
	}

	/* RPC does not have a dedicated queue */
	return ESLURM_NOT_SUPPORTED;
}
