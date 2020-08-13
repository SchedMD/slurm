/*****************************************************************************\
 *  diag.c - Slurm REST API diag http operations handlers
 *****************************************************************************
 *  Copyright (C) 2019-2020 SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
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

#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/operations.h"
#include "src/slurmrestd/ref.h"
#include "src/slurmrestd/xjson.h"

typedef enum {
	URL_TAG_UNKNOWN = 0,
	URL_TAG_DIAG,
	URL_TAG_PING,
} url_tag_t;

static int _op_handler_diag(const char *context_id,
			    http_request_method_t method, data_t *parameters,
			    data_t *query, int tag, data_t *resp_ptr)
{
	int rc;
	stats_info_response_msg_t *resp = NULL;
	stats_info_request_msg_t *req = xmalloc(sizeof(*req));
	req->command_id = STAT_COMMAND_GET;

	data_t *p = data_set_dict(resp_ptr);
	data_t *errors = data_set_list(data_key_set(p, "errors"));
	data_t *d = data_set_dict(data_key_set(p, "statistics"));
	debug4("%s:[%s] diag handler called", __func__, context_id);

	if ((rc = slurm_get_statistics(&resp, req)))
		goto cleanup;

	data_set_int(data_key_set(d, "parts_packed"), resp->parts_packed);
	data_set_int(data_key_set(d, "req_time"), resp->req_time);
	data_set_int(data_key_set(d, "req_time_start"), resp->req_time_start);
	data_set_int(data_key_set(d, "server_thread_count"),
		     resp->server_thread_count);
	data_set_int(data_key_set(d, "agent_queue_size"),
		     resp->agent_queue_size);
	data_set_int(data_key_set(d, "agent_count"), resp->agent_count);
	data_set_int(data_key_set(d, "dbd_agent_queue_size"),
		     resp->dbd_agent_queue_size);
	data_set_int(data_key_set(d, "gettimeofday_latency"),
		     resp->gettimeofday_latency);
	data_set_int(data_key_set(d, "schedule_cycle_max"),
		     resp->schedule_cycle_max);
	data_set_int(data_key_set(d, "schedule_cycle_last"),
		     resp->schedule_cycle_last);
	data_set_int(data_key_set(d, "schedule_cycle_sum"),
		     resp->schedule_cycle_sum);
	data_set_int(data_key_set(d, "schedule_cycle_counter"),
		     resp->schedule_cycle_counter);
	data_set_int(data_key_set(d, "schedule_cycle_depth"),
		     resp->schedule_cycle_depth);
	data_set_int(data_key_set(d, "schedule_queue_len"),
		     resp->schedule_queue_len);
	data_set_int(data_key_set(d, "jobs_submitted"), resp->jobs_submitted);
	data_set_int(data_key_set(d, "jobs_started"), resp->jobs_started);
	data_set_int(data_key_set(d, "jobs_completed"), resp->jobs_completed);
	data_set_int(data_key_set(d, "jobs_canceled"), resp->jobs_canceled);
	data_set_int(data_key_set(d, "jobs_failed"), resp->jobs_failed);
	data_set_int(data_key_set(d, "jobs_pending"), resp->jobs_pending);
	data_set_int(data_key_set(d, "jobs_running"), resp->jobs_running);
	data_set_int(data_key_set(d, "job_states_ts"), resp->job_states_ts);
	data_set_int(data_key_set(d, "bf_backfilled_jobs"),
		     resp->bf_backfilled_jobs);
	data_set_int(data_key_set(d, "bf_last_backfilled_jobs"),
		     resp->bf_last_backfilled_jobs);
	data_set_int(data_key_set(d, "bf_backfilled_het_jobs"),
		     resp->bf_backfilled_het_jobs);
	data_set_int(data_key_set(d, "bf_cycle_counter"),
		     resp->bf_cycle_counter);
	data_set_int(data_key_set(d, "bf_cycle_sum"), resp->bf_cycle_sum);
	data_set_int(data_key_set(d, "bf_cycle_last"), resp->bf_cycle_last);
	data_set_int(data_key_set(d, "bf_cycle_max"), resp->bf_cycle_max);
	data_set_int(data_key_set(d, "bf_last_depth"), resp->bf_last_depth);
	data_set_int(data_key_set(d, "bf_last_depth_try"),
		     resp->bf_last_depth_try);
	data_set_int(data_key_set(d, "bf_depth_sum"), resp->bf_depth_sum);
	data_set_int(data_key_set(d, "bf_depth_try_sum"),
		     resp->bf_depth_try_sum);
	data_set_int(data_key_set(d, "bf_queue_len"), resp->bf_queue_len);
	data_set_int(data_key_set(d, "bf_queue_len_sum"),
		     resp->bf_queue_len_sum);
	data_set_int(data_key_set(d, "bf_when_last_cycle"),
		     resp->bf_when_last_cycle);
	data_set_int(data_key_set(d, "bf_active"), resp->bf_active);

cleanup:
	if (rc) {
		data_t *e = data_set_dict(data_list_append(errors));
		data_set_string(data_key_set(e, "error"),
				slurm_strerror(rc));
		data_set_int(data_key_set(e, "errno"), rc);
	}

	slurm_free_stats_response_msg(resp);
	xfree(req);
	return rc;
}

#define _ping_error(...)                                                     \
	do {                                                                 \
		const char *error_string = xstrdup_printf(__VA_ARGS__);      \
		error("%s", error_string);                                   \
		data_t *error = data_list_append(errors);                    \
		data_set_dict(error);                                        \
		data_set_string(data_key_set(error, "error"), error_string); \
		xfree(error_string);                                         \
		if (errno) {                                                 \
			data_set_int(data_key_set(error, "errno"), errno);   \
			rc = errno;                                       \
			errno = 0;                                           \
		} else                                                       \
			rc = SLURM_ERROR;                                 \
	} while (0)

static int _op_handler_ping(const char *context_id,
			    http_request_method_t method, data_t *parameters,
			    data_t *query, int tag, data_t *resp_ptr)
{
	//based on _print_ping() from scontrol
	int rc = SLURM_SUCCESS;
	slurm_ctl_conf_info_msg_t *slurm_ctl_conf_ptr = NULL;

	data_t *errors = data_set_list(
		data_key_set(data_set_dict(resp_ptr), "errors"));

	if (slurm_load_ctl_conf((time_t) NULL, &slurm_ctl_conf_ptr))
		_ping_error("%s: slurmctl config is unable to load: %m",
			    __func__);

	if (slurm_ctl_conf_ptr) {
		data_t *pings = data_key_set(resp_ptr, "pings");
		data_set_list(pings);

		xassert(slurm_ctl_conf_ptr->control_cnt);
		for (size_t i = 0; i < slurm_ctl_conf_ptr->control_cnt; i++) {
			const int status = slurm_ping(i);
			char mode[64];

			if (i == 0)
				snprintf(mode, sizeof(mode), "primary");
			else if ((i == 1) &&
				 (slurm_ctl_conf_ptr->control_cnt == 2))
				snprintf(mode, sizeof(mode), "backup");
			else
				snprintf(mode, sizeof(mode), "backup%zu", i);

			data_t *ping = data_set_dict(data_list_append(pings));

			data_set_string(data_key_set(ping, "hostname"),
					slurm_ctl_conf_ptr->control_machine[i]);

			data_set_string(data_key_set(ping, "ping"),
					(status == SLURM_SUCCESS ? "UP" :
								   "DOWN"));
			data_set_int(data_key_set(ping, "status"), status);
			data_set_string(data_key_set(ping, "mode"), mode);
		}
	} else {
		_ping_error("%s: slurmctld config is missing", __func__);
	}

	slurm_free_ctl_conf(slurm_ctl_conf_ptr);

	return rc;
}

extern void init_op_diag(void)
{
	bind_operation_handler("/slurm/v0.0.36/diag/", _op_handler_diag, 0);
	bind_operation_handler("/slurm/v0.0.36/ping/", _op_handler_ping, 0);
}

extern void destroy_op_diag(void)
{
	unbind_operation_handler(_op_handler_diag);
	unbind_operation_handler(_op_handler_ping);
}
