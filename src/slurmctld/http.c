/*****************************************************************************\
 *  http.c - Implementation for handling HTTP requests
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

#include "src/common/http.h"
#include "src/common/http_con.h"
#include "src/common/http_router.h"
#include "src/common/pack.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/conmgr/conmgr.h"

#include "src/interfaces/metrics.h"

#include "src/slurmctld/http.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

static int _reply_error(http_con_t *hcon, const char *name,
			const http_con_request_t *request, int err)
{
	char *body = NULL, *at = NULL;
	int rc = EINVAL;

	xstrfmtcatat(body, &at, "slurmctld HTTP server request for '%s %s':\n",
		     get_http_method_string(request->method),
		     request->url.path);

	if (err)
		xstrfmtcatat(body, &at, "Failed: %s\n", slurm_strerror(err));

	rc = http_con_send_response(hcon, http_status_from_error(err), NULL,
				    true,
				    &SHADOW_BUF_INITIALIZER(body, strlen(body)),
				    MIME_TYPE_TEXT);

	xfree(body);
	return rc;
}

static int _req_not_found(http_con_t *hcon, const char *name,
			  const http_con_request_t *request, void *arg)
{
	return _reply_error(hcon, name, request, ESLURM_URL_INVALID_PATH);
}

static int _req_metrics(http_con_t *hcon, const char *name,
			const http_con_request_t *request, void *arg)
{
	static const char body[] =
		"slurmctld index of metrics endpoints:\n"
		"  '/metrics/jobs': get job metrics\n"
		"  '/metrics/nodes': get node metrics\n"
		"  '/metrics/partitions': get partition metrics\n"
		"  '/metrics/jobs-users-accts': get user and account jobs metrics\n"
		"  '/metrics/scheduler': get scheduler metrics\n";

	return http_con_send_response(hcon,
				      http_status_from_error(SLURM_SUCCESS),
				      NULL, true,
				      &SHADOW_BUF_INITIALIZER(body,
							      strlen(body)),
				      MIME_TYPE_TEXT);
}

static int _req_root(http_con_t *hcon, const char *name,
		     const http_con_request_t *request, void *arg)
{
	static const char body[] =
		"slurmctld index of endpoints:\n"
		"  '/readyz': check slurmctld is servicing RPCs\n"
		"  '/livez': check slurmctld is running\n"
		"  '/healthz': check slurmctld is running\n"
		"  '/metrics': print available metric endpoints\n";

	return http_con_send_response(hcon,
				      http_status_from_error(SLURM_SUCCESS),
				      NULL, true,
				      &SHADOW_BUF_INITIALIZER(body,
							      strlen(body)),
				      MIME_TYPE_TEXT);
}

static int _req_readyz(http_con_t *hcon, const char *name,
		       const http_con_request_t *request, void *arg)
{
	http_status_code_t status = HTTP_STATUS_CODE_SRVERR_INTERNAL;

	if (!listeners_quiesced() && is_primary() && !is_reconfiguring() &&
	    !conmgr_is_quiesced())
		status = HTTP_STATUS_CODE_SUCCESS_NO_CONTENT;

	return http_con_send_response(hcon, status, NULL, true, NULL, NULL);
}

static int _send_metrics_resp(http_con_t *hcon, char *stats_str)
{
	http_status_code_t status = HTTP_STATUS_CODE_SUCCESS_OK;
	int rc;

	if (!stats_str) {
		static const char body[] = "";
		return http_con_send_response(
			hcon, status, NULL, true,
			&SHADOW_BUF_INITIALIZER(body, strlen(body)),
			MIME_TYPE_TEXT);
	}

	rc = http_con_send_response(hcon, status, NULL, true,
				    &SHADOW_BUF_INITIALIZER(stats_str,
							    strlen(stats_str)),
				    MIME_TYPE_TEXT);
	xfree(stats_str);

	return rc;
}

static int _check_metrics_authorized(http_con_t *hcon, int *rc)
{
	http_status_code_t status = HTTP_STATUS_CODE_ERROR_UNAUTHORIZED;

	if (slurm_conf.private_data) {
		*rc = http_con_send_response(hcon, status, NULL, true, NULL,
					     NULL);
		return false;
	}
	return true;
}

extern int _req_metrics_jobs(http_con_t *hcon, const char *name,
			     const http_con_request_t *request, void *arg)
{
	jobs_stats_t *stats;
	char *stats_str = NULL;
	int rc = SLURM_SUCCESS;

	if (!_check_metrics_authorized(hcon, &rc))
		return rc;

	stats = statistics_get_jobs(true);

	stats_str = metrics_serialize_struct(METRICS_CTLD_JOBS, stats);

	statistics_free_jobs(stats);

	return _send_metrics_resp(hcon, stats_str);
}

extern int _req_metrics_nodes(http_con_t *hcon, const char *name,
			      const http_con_request_t *request, void *arg)
{
	nodes_stats_t *stats;
	char *stats_str;
	int rc = SLURM_SUCCESS;

	if (!_check_metrics_authorized(hcon, &rc))
		return rc;

	stats = statistics_get_nodes(true);

	stats_str = metrics_serialize_struct(METRICS_CTLD_NODES, stats);

	statistics_free_nodes(stats);

	return _send_metrics_resp(hcon, stats_str);
}

extern int _req_metrics_partitions(http_con_t *hcon, const char *name,
				   const http_con_request_t *request, void *arg)
{
	jobs_stats_t *jobs_stats;
	nodes_stats_t *nodes_stats;
	partitions_stats_t *parts_stats;
	char *stats_str;
	int rc = SLURM_SUCCESS;
	slurmctld_lock_t part_read_lock = {
		.conf = READ_LOCK,
		.job = READ_LOCK,
		.node = WRITE_LOCK, /* required by statistics_get_nodes() */
		.part = READ_LOCK,
	};

	if (!_check_metrics_authorized(hcon, &rc))
		return rc;

	lock_slurmctld(part_read_lock);
	nodes_stats = statistics_get_nodes(false);
	jobs_stats = statistics_get_jobs(false);
	parts_stats = statistics_get_parts(nodes_stats, jobs_stats, false);
	unlock_slurmctld(part_read_lock);

	stats_str = metrics_serialize_struct(METRICS_CTLD_PARTS, parts_stats);

	statistics_free_nodes(nodes_stats);
	statistics_free_parts(parts_stats);
	statistics_free_jobs(jobs_stats);

	return _send_metrics_resp(hcon, stats_str);
}

extern int _req_metrics_ua(http_con_t *hcon, const char *name,
			   const http_con_request_t *request, void *arg)
{
	jobs_stats_t *jobs_stats;
	users_accts_stats_t *ua_stats;
	char *stats_str;
	int rc = SLURM_SUCCESS;

	if (!_check_metrics_authorized(hcon, &rc))
		return rc;

	jobs_stats = statistics_get_jobs(true);
	ua_stats = statistics_get_users_accounts(jobs_stats);

	stats_str = metrics_serialize_struct(METRICS_CTLD_UA, ua_stats);

	statistics_free_jobs(jobs_stats);
	statistics_free_users_accounts(ua_stats);

	return _send_metrics_resp(hcon, stats_str);
}

extern int _req_metrics_sched(http_con_t *hcon, const char *name,
			      const http_con_request_t *request, void *arg)
{
	scheduling_stats_t *stats;
	char *stats_str;
	int rc = SLURM_SUCCESS;

	if (!_check_metrics_authorized(hcon, &rc))
		return rc;

	stats = statistics_get_sched();

	stats_str = metrics_serialize_struct(METRICS_CTLD_SCHED, stats);

	statistics_free_sched(stats);

	return _send_metrics_resp(hcon, stats_str);
}

static int _req_livez(http_con_t *hcon, const char *name,
		      const http_con_request_t *request, void *arg)
{
	return http_con_send_response(hcon, HTTP_STATUS_CODE_SUCCESS_NO_CONTENT,
				      NULL, true, NULL, NULL);
}

static int _req_healthz(http_con_t *hcon, const char *name,
			const http_con_request_t *request, void *arg)
{
	return http_con_send_response(hcon, HTTP_STATUS_CODE_SUCCESS_NO_CONTENT,
				      NULL, true, NULL, NULL);
}

extern void http_init(void)
{
	http_router_init(_req_not_found);
	http_router_bind(HTTP_REQUEST_GET, "/", _req_root);
	http_router_bind(HTTP_REQUEST_GET, "/readyz", _req_readyz);
	http_router_bind(HTTP_REQUEST_GET, "/livez", _req_livez);
	http_router_bind(HTTP_REQUEST_GET, "/healthz", _req_healthz);
	http_router_bind(HTTP_REQUEST_GET, "/metrics", _req_metrics);
	http_router_bind(HTTP_REQUEST_GET, "/metrics/jobs", _req_metrics_jobs);
	http_router_bind(HTTP_REQUEST_GET, "/metrics/nodes",
			 _req_metrics_nodes);
	http_router_bind(HTTP_REQUEST_GET, "/metrics/partitions",
			 _req_metrics_partitions);
	http_router_bind(HTTP_REQUEST_GET, "/metrics/scheduler",
			 _req_metrics_sched);
	http_router_bind(HTTP_REQUEST_GET, "/metrics/jobs-users-accts",
			 _req_metrics_ua);
}

extern void http_fini(void)
{
	http_router_fini();
}

extern int on_http_connection(conmgr_fd_t *con)
{
	static const http_con_server_events_t events = {
		.on_request = http_router_on_request,
	};
	int rc = EINVAL;
	conmgr_fd_ref_t *ref = conmgr_fd_new_ref(con);

	rc = http_con_assign_server(ref, NULL, &events, NULL);

	conmgr_fd_free_ref(&ref);

	return rc;
}
