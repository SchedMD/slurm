/*****************************************************************************\
 *  burst_buffer_info.c - get/print the burst buffer state information
 *****************************************************************************
 *  Copyright (C) 2014-2015 SchedMD LLC.
 *  Written by Morris Jette <jette@schedmd.com>
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

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/parse_time.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/* Reformat a numeric value with an appropriate suffix.
 * The input units are bytes */
static void _get_size_str(char *buf, size_t buf_size, uint64_t num)
{
	uint64_t tmp64;

	if ((num == NO_VAL64) || (num == INFINITE64)) {
		snprintf(buf, buf_size, "INFINITE");
	} else if (num == 0) {
		snprintf(buf, buf_size, "0");

	} else if ((num % ((uint64_t)1024 * 1024 * 1024 * 1024 * 1024)) == 0) {
		tmp64 = num / ((uint64_t)1024 * 1024 * 1024 * 1024 * 1024);
		snprintf(buf, buf_size, "%"PRIu64"PiB", tmp64);
	} else if ((num % ((uint64_t)1000 * 1000 * 1000 * 1000 * 1000)) == 0) {
		tmp64 = num / ((uint64_t)1000 * 1000 * 1000 * 1000 * 1000);
		snprintf(buf, buf_size, "%"PRIu64"PB", tmp64);

	} else if ((num % ((uint64_t)1024 * 1024 * 1024 * 1024)) == 0) {
		tmp64 = num / ((uint64_t)1024 * 1024 * 1024 * 1024);
		snprintf(buf, buf_size, "%"PRIu64"TiB", tmp64);
	} else if ((num % ((uint64_t)1000 * 1000 * 1000 * 1000)) == 0) {
		tmp64 = num / ((uint64_t)1000 * 1000 * 1000 * 1000);
		snprintf(buf, buf_size, "%"PRIu64"TB", tmp64);

	} else if ((num % ((uint64_t)1024 * 1024 * 1024)) == 0) {
		tmp64 = num / ((uint64_t)1024 * 1024 * 1024);
		snprintf(buf, buf_size, "%"PRIu64"GiB", tmp64);
	} else if ((num % ((uint64_t)1000 * 1000 * 1000)) == 0) {
		tmp64 = num / ((uint64_t)1000 * 1000 * 1000);
		snprintf(buf, buf_size, "%"PRIu64"GB", tmp64);

	} else if ((num % ((uint64_t)1024 * 1024)) == 0) {
		tmp64 = num / ((uint64_t)1024 * 1024);
		snprintf(buf, buf_size, "%"PRIu64"MiB", tmp64);
	} else if ((num % ((uint64_t)1000 * 1000)) == 0) {
		tmp64 = num / ((uint64_t)1000 * 1000);
		snprintf(buf, buf_size, "%"PRIu64"MB", tmp64);

	} else if ((num % 1024) == 0) {
		tmp64 = num / 1024;
		snprintf(buf, buf_size, "%"PRIu64"KiB", tmp64);
	} else if ((num % 1000) == 0) {
		tmp64 = num / 1000;
		snprintf(buf, buf_size, "%"PRIu64"KB", tmp64);

	} else {
		tmp64 = num;
		snprintf(buf, buf_size, "%"PRIu64"", tmp64);
	}
}

/*
 * slurm_load_burst_buffer_stat - issue RPC to get burst buffer status
 * IN argc - count of status request options
 * IN argv - status request options
 * OUT status_resp - status response, memory must be released using xfree()
 * RET 0 or a slurm error code
 */
extern int slurm_load_burst_buffer_stat(int argc, char **argv,
					char **status_resp)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	bb_status_req_msg_t status_req_msg;
	bb_status_resp_msg_t *status_resp_msg;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	status_req_msg.argc = argc;
	status_req_msg.argv = argv;
	req_msg.msg_type = REQUEST_BURST_BUFFER_STATUS;
	req_msg.data     = &status_req_msg;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					   working_cluster_rec) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_BURST_BUFFER_STATUS:
		status_resp_msg = (bb_status_resp_msg_t *) resp_msg.data;
		*status_resp = status_resp_msg->status_resp;
		status_resp_msg->status_resp = NULL;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (rc)
			slurm_seterrno_ret(rc);
		*status_resp = NULL;
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_SUCCESS;
}

/*
 * slurm_load_burst_buffer_info - issue RPC to get slurm all burst buffer plugin
 *	information
 * OUT burst_buffer_info_msg_pptr - place to store a burst buffer configuration
 *	pointer
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_burst_buffer_info_msg
 */
extern int slurm_load_burst_buffer_info(burst_buffer_info_msg_t **
					burst_buffer_info_msg_pptr)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	req_msg.msg_type = REQUEST_BURST_BUFFER_INFO;
	req_msg.data     = NULL;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					   working_cluster_rec) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_BURST_BUFFER_INFO:
		*burst_buffer_info_msg_pptr = (burst_buffer_info_msg_t *)
					      resp_msg.data;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (rc)
			slurm_seterrno_ret(rc);
		*burst_buffer_info_msg_pptr = NULL;
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_SUCCESS;
}

/*
 * slurm_print_burst_buffer_info_msg - output information about burst buffers
 *	based upon message as loaded using slurm_load_burst_buffer
 * IN out - file to write to
 * IN info_ptr - burst_buffer information message pointer
 * IN one_liner - print as a single line if true
 * IN verbose - higher values to log additional details
 */
extern void slurm_print_burst_buffer_info_msg(FILE *out,
		 burst_buffer_info_msg_t *info_ptr, int one_liner,
		 int verbose)
{
	int i;
	burst_buffer_info_t *burst_buffer_ptr;

	if (info_ptr->record_count == 0) {
		error("No burst buffer information available");
		return;
	}

	for (i = 0, burst_buffer_ptr = info_ptr->burst_buffer_array;
	     i < info_ptr->record_count; i++, burst_buffer_ptr++) {
		slurm_print_burst_buffer_record(out, burst_buffer_ptr,
						one_liner, verbose);
	}
}

static void _print_burst_buffer_resv(FILE *out,
				     burst_buffer_resv_t* burst_buffer_ptr,
				     int one_liner, bool verbose)
{
	char sz_buf[32], time_buf[64];
	char *out_buf = NULL, *user_name;

	/****** Line 1 ******/
	if (burst_buffer_ptr->job_id &&
	    (burst_buffer_ptr->array_task_id == NO_VAL)) {
		xstrfmtcat(out_buf, "    JobID=%u ", burst_buffer_ptr->job_id);
	} else if (burst_buffer_ptr->job_id) {
		xstrfmtcat(out_buf, "    JobID=%u_%u(%u) ",
			   burst_buffer_ptr->array_job_id,
			   burst_buffer_ptr->array_task_id,
			   burst_buffer_ptr->job_id);
	} else {
		xstrfmtcat(out_buf, "    Name=%s ", burst_buffer_ptr->name);
	}

	_get_size_str(sz_buf, sizeof(sz_buf), burst_buffer_ptr->size);
	if (burst_buffer_ptr->create_time) {
		slurm_make_time_str(&burst_buffer_ptr->create_time, time_buf,
				    sizeof(time_buf));
	} else {
		time_t now = time(NULL);
		slurm_make_time_str(&now, time_buf, sizeof(time_buf));
	}

	user_name = uid_to_string(burst_buffer_ptr->user_id);
	if (verbose) {
		xstrfmtcat(out_buf, "Account=%s CreateTime=%s Partition=%s Pool=%s QOS=%s Size=%s State=%s UserID=%s(%u)",
			   burst_buffer_ptr->account, time_buf,
			   burst_buffer_ptr->partition, burst_buffer_ptr->pool,
			   burst_buffer_ptr->qos, sz_buf,
			   bb_state_string(burst_buffer_ptr->state),
			   user_name, burst_buffer_ptr->user_id);
	} else {
		xstrfmtcat(out_buf, "CreateTime=%s Pool=%s Size=%s State=%s UserID=%s(%u)",
			   time_buf, burst_buffer_ptr->pool, sz_buf,
			   bb_state_string(burst_buffer_ptr->state),
			   user_name, burst_buffer_ptr->user_id);
	}
	xfree(user_name);

	xstrcat(out_buf, "\n");
	fprintf(out, "%s", out_buf);
	xfree(out_buf);
}

static void _print_burst_buffer_use(FILE *out,
				    burst_buffer_use_t* usage_ptr,
				    int one_liner)
{
	char sz_buf[32];
	char *out_buf = NULL, *user_name;

	user_name = uid_to_string(usage_ptr->user_id);
	_get_size_str(sz_buf, sizeof(sz_buf), usage_ptr->used);
	xstrfmtcat(out_buf, "    UserID=%s(%u) Used=%s",
		   user_name, usage_ptr->user_id, sz_buf);
	xfree(user_name);

	xstrcat(out_buf, "\n");
	fprintf(out, "%s", out_buf);
	xfree(out_buf);
}

/*
 * slurm_print_burst_buffer_record - output information about a specific Slurm
 *	burst_buffer record based upon message as loaded using
 *	slurm_load_burst_buffer_info()
 * IN out - file to write to
 * IN burst_buffer_ptr - an individual burst buffer record pointer
 * IN one_liner - print as a single line if not zero
 * IN verbose - higher values to log additional details
 * RET out - char * containing formatted output (must be freed after call)
 *	   NULL is returned on failure.
 */
extern void slurm_print_burst_buffer_record(FILE *out,
		burst_buffer_info_t *burst_buffer_ptr, int one_liner,
		int verbose)
{
	char f_sz_buf[32], g_sz_buf[32], t_sz_buf[32], u_sz_buf[32];
	char *out_buf = NULL;
	char *line_end = (one_liner) ? " " : "\n  ";
	uint64_t free_space;
	burst_buffer_resv_t *bb_resv_ptr;
	burst_buffer_use_t  *bb_use_ptr;
	int i;

	/****** Line ******/
	free_space = burst_buffer_ptr->total_space -
		     burst_buffer_ptr->unfree_space;
	_get_size_str(f_sz_buf, sizeof(f_sz_buf), free_space);
	_get_size_str(g_sz_buf, sizeof(g_sz_buf),
		      burst_buffer_ptr->granularity);
	_get_size_str(t_sz_buf, sizeof(t_sz_buf),
		      burst_buffer_ptr->total_space);
	_get_size_str(u_sz_buf, sizeof(u_sz_buf),
		      burst_buffer_ptr->used_space);
	xstrfmtcat(out_buf, "Name=%s DefaultPool=%s Granularity=%s TotalSpace=%s FreeSpace=%s UsedSpace=%s",
		   burst_buffer_ptr->name, burst_buffer_ptr->default_pool,
		   g_sz_buf, t_sz_buf, f_sz_buf, u_sz_buf);

	/****** Line (optional) ******/
	/* Alternate pool information */
	for (i = 0; i < burst_buffer_ptr->pool_cnt; i++) {
		xstrcat(out_buf, line_end);
		free_space = burst_buffer_ptr->pool_ptr[i].total_space -
			     burst_buffer_ptr->pool_ptr[i].unfree_space;
		_get_size_str(f_sz_buf, sizeof(f_sz_buf), free_space);
		_get_size_str(g_sz_buf, sizeof(g_sz_buf),
			      burst_buffer_ptr->pool_ptr[i].granularity);
		_get_size_str(t_sz_buf, sizeof(t_sz_buf),
			      burst_buffer_ptr->pool_ptr[i].total_space);
		_get_size_str(u_sz_buf, sizeof(u_sz_buf),
			      burst_buffer_ptr->pool_ptr[i].used_space);
		xstrfmtcat(out_buf, "AltPoolName[%d]=%s Granularity=%s TotalSpace=%s FreeSpace=%s UsedSpace=%s",
			   i, burst_buffer_ptr->pool_ptr[i].name,
			   g_sz_buf, t_sz_buf, f_sz_buf, u_sz_buf);
	}

	/****** Line ******/
	xstrcat(out_buf, line_end);
	xstrfmtcat(out_buf, "Flags=%s",
		   slurm_bb_flags2str(burst_buffer_ptr->flags));

	/****** Line ******/
	xstrcat(out_buf, line_end);
	xstrfmtcat(out_buf, "StageInTimeout=%u StageOutTimeout=%u ValidateTimeout=%u OtherTimeout=%u",
		   burst_buffer_ptr->stage_in_timeout,
		   burst_buffer_ptr->stage_out_timeout,
		   burst_buffer_ptr->validate_timeout,
		   burst_buffer_ptr->other_timeout);

	/****** Line (optional) ******/
	if (burst_buffer_ptr->allow_users) {
		xstrcat(out_buf, line_end);
		xstrfmtcat(out_buf, "AllowUsers=%s",
			   burst_buffer_ptr->allow_users);
	} else if (burst_buffer_ptr->deny_users) {
		xstrcat(out_buf, line_end);
		xstrfmtcat(out_buf, "DenyUsers=%s",
			   burst_buffer_ptr->deny_users);
	}

	/****** Line (optional) ******/
	if (burst_buffer_ptr->create_buffer) {
		xstrcat(out_buf, line_end);
		xstrfmtcat(out_buf, "CreateBuffer=%s",
			   burst_buffer_ptr->create_buffer);
	}

	/****** Line (optional) ******/
	if (burst_buffer_ptr->destroy_buffer) {
		xstrcat(out_buf, line_end);
		xstrfmtcat(out_buf, "DestroyBuffer=%s",
			   burst_buffer_ptr->destroy_buffer);
	}

	/****** Line ******/
	xstrcat(out_buf, line_end);
	xstrfmtcat(out_buf, "GetSysState=%s",
		   burst_buffer_ptr->get_sys_state);

	/****** Line ******/
	xstrcat(out_buf, line_end);
	xstrfmtcat(out_buf, "GetSysStatus=%s",
		   burst_buffer_ptr->get_sys_status);

	/****** Line (optional) ******/
	if (burst_buffer_ptr->start_stage_in) {
		xstrcat(out_buf, line_end);
		xstrfmtcat(out_buf, "StartStageIn=%s",
			   burst_buffer_ptr->start_stage_in);
	}

	/****** Line ******/
	if (burst_buffer_ptr->start_stage_out) {
		xstrcat(out_buf, line_end);
		xstrfmtcat(out_buf, "StartStageIn=%s",
			   burst_buffer_ptr->start_stage_out);
	}

	/****** Line (optional) ******/
	if (burst_buffer_ptr->stop_stage_in) {
		xstrcat(out_buf, line_end);
		xstrfmtcat(out_buf, "StopStageIn=%s",
			   burst_buffer_ptr->stop_stage_in);
	}

	/****** Line (optional) ******/
	if (burst_buffer_ptr->stop_stage_out) {
		xstrcat(out_buf, line_end);
		xstrfmtcat(out_buf, "StopStageIn=%s",
			   burst_buffer_ptr->stop_stage_out);
	}

	xstrcat(out_buf, "\n");
	fprintf(out, "%s", out_buf);
	xfree(out_buf);

	/****** Lines (optional) ******/
	if (burst_buffer_ptr->buffer_count)
		fprintf(out, "  Allocated Buffers:\n");
	for (i = 0, bb_resv_ptr = burst_buffer_ptr->burst_buffer_resv_ptr;
	     i < burst_buffer_ptr->buffer_count; i++, bb_resv_ptr++) {
		 _print_burst_buffer_resv(out, bb_resv_ptr, one_liner, verbose);
	}

	/****** Lines (optional) ******/
	if (burst_buffer_ptr->use_count)
		fprintf(out, "  Per User Buffer Use:\n");
	for (i = 0, bb_use_ptr = burst_buffer_ptr->burst_buffer_use_ptr;
	     i < burst_buffer_ptr->use_count; i++, bb_use_ptr++) {
		 _print_burst_buffer_use(out, bb_use_ptr, one_liner);
	}
}
