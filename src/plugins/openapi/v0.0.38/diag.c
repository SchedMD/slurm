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
#include "src/common/ref.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/operations.h"

#include "src/plugins/openapi/v0.0.38/api.h"

typedef enum {
	URL_TAG_UNKNOWN = 0,
	URL_TAG_DIAG,
	URL_TAG_PING,
} url_tag_t;

static int _op_handler_diag(const char *context_id,
			    http_request_method_t method, data_t *parameters,
			    data_t *query, int tag, data_t *p, void *auth)
{
	int rc;
	stats_info_response_msg_t *resp = NULL;
	stats_info_request_msg_t *req = xmalloc(sizeof(*req));
	req->command_id = STAT_COMMAND_GET;

	data_t *errors = populate_response_format(p);
	data_t *d = data_set_dict(data_key_set(p, "statistics"));
	data_t *rpcm = data_set_list(data_key_set(d, "rpcs_by_message_type"));
	data_t *rpcu = data_set_list(data_key_set(d, "rpcs_by_user"));
	debug4("%s:[%s] diag handler called", __func__, context_id);

	if ((rc = slurm_get_statistics(&resp, req))) {
		resp_error(errors, rc, "slurm_get_statistics",
			   "request failed");
		goto cleanup;
	}

	data_set_int(data_key_set(d, "parts_packed"), resp->parts_packed);
	data_set_int(data_key_set(d, "req_time"), resp->req_time);
	data_set_int(data_key_set(d, "req_time_start"), resp->req_time_start);
	data_set_int(data_key_set(d, "server_thread_count"),
		     resp->server_thread_count);
	data_set_int(data_key_set(d, "agent_queue_size"),
		     resp->agent_queue_size);
	data_set_int(data_key_set(d, "agent_count"), resp->agent_count);
	data_set_int(data_key_set(d, "agent_thread_count"),
		     resp->agent_thread_count);
	data_set_int(data_key_set(d, "dbd_agent_queue_size"),
		     resp->dbd_agent_queue_size);
	data_set_int(data_key_set(d, "gettimeofday_latency"),
		     resp->gettimeofday_latency);
	data_set_int(data_key_set(d, "schedule_cycle_max"),
		     resp->schedule_cycle_max);
	data_set_int(data_key_set(d, "schedule_cycle_last"),
		     resp->schedule_cycle_last);
	data_set_int(data_key_set(d, "schedule_cycle_total"),
		     resp->schedule_cycle_counter);
	data_set_int(data_key_set(d, "schedule_cycle_mean"),
		     (resp->schedule_cycle_counter ?
		      (resp->schedule_cycle_sum /
		       resp->schedule_cycle_counter) : 0));
	data_set_int(data_key_set(d, "schedule_cycle_mean_depth"),
		     (resp->schedule_cycle_counter ?
		      (resp->schedule_cycle_depth /
		       resp->schedule_cycle_counter) : 0));
	data_set_int(data_key_set(d, "schedule_cycle_per_minute"),
		     (((resp->req_time - resp->req_time_start) > 60) ?
		     ((uint32_t)(resp->schedule_cycle_counter /
		      ((resp->req_time - resp->req_time_start) / 60))) : 0));
	data_set_int(data_key_set(d, "schedule_queue_length"),
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
	data_set_int(data_key_set(d, "bf_cycle_mean"),
		     (resp->bf_cycle_counter > 0) ?
		      (resp->bf_cycle_sum / resp->bf_cycle_counter) : 0);
	data_set_int(data_key_set(d, "bf_depth_mean"),
		     (resp->bf_cycle_counter > 0) ?
		      (resp->bf_depth_sum / resp->bf_cycle_counter) : 0);
	data_set_int(data_key_set(d, "bf_depth_mean_try"),
		     (resp->bf_cycle_counter > 0) ?
		      (resp->bf_depth_try_sum / resp->bf_cycle_counter) : 0);
	data_set_int(data_key_set(d, "bf_cycle_last"), resp->bf_cycle_last);
	data_set_int(data_key_set(d, "bf_cycle_max"), resp->bf_cycle_max);
	data_set_int(data_key_set(d, "bf_queue_len"), resp->bf_queue_len);
	data_set_int(data_key_set(d, "bf_queue_len_mean"),
		     (resp->bf_cycle_counter > 0) ?
		      (resp->bf_queue_len_sum / resp->bf_cycle_counter) : 0);
	data_set_int(data_key_set(d, "bf_table_size"), resp->bf_table_size);
	data_set_int(data_key_set(d, "bf_table_size_mean"),
		     (resp->bf_cycle_counter > 0) ?
		      (resp->bf_table_size_sum / resp->bf_cycle_counter) : 0);
	data_set_int(data_key_set(d, "bf_when_last_cycle"),
		     resp->bf_when_last_cycle);
	data_set_bool(data_key_set(d, "bf_active"), (resp->bf_active != 0));

	if (resp->rpc_type_size) {
		uint32_t *rpc_type_ave_time = xcalloc(
			resp->rpc_type_size, sizeof(*rpc_type_ave_time));

		for (int i = 0; i < resp->rpc_type_size; i++) {
			rpc_type_ave_time[i] = resp->rpc_type_time[i] /
					       resp->rpc_type_cnt[i];
		}

		for (int i = 0; i < resp->rpc_type_size; i++) {
			data_t *r = data_set_dict(data_list_append(rpcm));
			data_set_string(data_key_set(r, "message_type"),
					rpc_num2string(resp->rpc_type_id[i]));
			data_set_int(data_key_set(r, "type_id"),
				     resp->rpc_type_id[i]);
			data_set_int(data_key_set(r, "count"),
				     resp->rpc_type_cnt[i]);
			data_set_int(data_key_set(r, "average_time"),
				     rpc_type_ave_time[i]);
			data_set_int(data_key_set(r, "total_time"),
				     resp->rpc_type_time[i]);
		}

		xfree(rpc_type_ave_time);
	}

	if (resp->rpc_user_size) {
		uint32_t *rpc_user_ave_time = xcalloc(
			resp->rpc_user_size, sizeof(*rpc_user_ave_time));

		for (int i = 0; i < resp->rpc_user_size; i++) {
			rpc_user_ave_time[i] = resp->rpc_user_time[i] /
					       resp->rpc_user_cnt[i];
		}

		for (int i = 0; i < resp->rpc_user_size; i++) {
			data_t *u = data_set_dict(data_list_append(rpcu));
			data_t *un = data_key_set(u, "user");
			char *user = uid_to_string_or_null(
				resp->rpc_user_id[i]);

			data_set_int(data_key_set(u, "user_id"),
				     resp->rpc_user_id[i]);
			data_set_int(data_key_set(u, "count"),
				     resp->rpc_user_cnt[i]);
			data_set_int(data_key_set(u, "average_time"),
				     rpc_user_ave_time[i]);
			data_set_int(data_key_set(u, "total_time"),
				     resp->rpc_user_time[i]);

			if (!user)
				data_set_string_fmt(un, "%u",
						    resp->rpc_user_id[i]);
			else
				data_set_string_own(un, user);
		}

		xfree(rpc_user_ave_time);
	}

cleanup:
	slurm_free_stats_response_msg(resp);
	xfree(req);
	return rc;
}

static int _op_handler_ping(const char *context_id,
			    http_request_method_t method, data_t *parameters,
			    data_t *query, int tag, data_t *resp_ptr,
			    void *auth)
{
	//based on _print_ping() from scontrol
	int rc = SLURM_SUCCESS;
	slurm_ctl_conf_info_msg_t *slurm_ctl_conf_ptr = NULL;

	data_t *errors = populate_response_format(resp_ptr);

	if ((rc = slurm_load_ctl_conf((time_t) NULL, &slurm_ctl_conf_ptr)))
		return resp_error(errors, rc, "slurm_load_ctl_conf",
				  "slurmctld config is unable to load");

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
		rc = resp_error(errors, ESLURM_INTERNAL, "slurm_load_ctl_conf",
				"slurmctld config is missing");
	}

	slurm_free_ctl_conf(slurm_ctl_conf_ptr);

	return rc;
}

/* based on _print_license_info() from scontrol */
static int _op_handler_licenses(const char *context_id,
				http_request_method_t method,
				data_t *parameters, data_t *query, int tag,
				data_t *resp_ptr, void *auth)
{
	int rc;
	license_info_msg_t *msg;
	const uint16_t show_flags = 0;
	const time_t last_update = 0;
	data_t *licenses, *errors;

	errors = populate_response_format(resp_ptr);

	if ((rc = slurm_load_licenses(last_update, &msg, show_flags))) {
		slurm_free_license_info_msg(msg);
		return resp_error(errors, rc, "slurm_load_licenses",
				  "slurmctld unable to load licenses");
	}

	licenses = data_set_list(data_key_set(resp_ptr, "licenses"));

	for (int cc = 0; cc < msg->num_lic; cc++) {
		data_t *lic = data_set_dict(data_list_append(licenses));
		data_set_string(data_key_set(lic, "LicenseName"),
				msg->lic_array[cc].name);
		data_set_int(data_key_set(lic, "Total"),
			     msg->lic_array[cc].total);
		data_set_int(data_key_set(lic, "Used"),
			     msg->lic_array[cc].in_use);
		data_set_int(data_key_set(lic, "Free"),
			     msg->lic_array[cc].available);
		data_set_int(data_key_set(lic, "Reserved"),
			     msg->lic_array[cc].reserved);
		data_set_bool(data_key_set(lic, "Remote"),
			      msg->lic_array[cc].remote);
	}

	slurm_free_license_info_msg(msg);
	return rc;
}

extern void init_op_diag(void)
{
	bind_operation_handler("/slurm/v0.0.38/diag/", _op_handler_diag, 0);
	bind_operation_handler("/slurm/v0.0.38/ping/", _op_handler_ping, 0);
	bind_operation_handler("/slurm/v0.0.38/licenses/", _op_handler_licenses, 0);
}

extern void destroy_op_diag(void)
{
	unbind_operation_handler(_op_handler_diag);
	unbind_operation_handler(_op_handler_ping);
	unbind_operation_handler(_op_handler_licenses);
}
