/****************************************************************************\
 *  slurmscriptd_protocol_pack.c - functions to pack and unpack structures
 *	for RPCs for slurmscriptd.
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

#include "slurm/slurm_errno.h"

#include "src/common/env.h"
#include "src/common/log.h"
#include "src/common/pack.h"
#include "src/common/xmalloc.h"
#include "src/slurmctld/slurmscriptd_protocol_pack.h"

static void _pack_run_script(run_script_msg_t *script_msg, buf_t *buffer)
{
	packstr_array(script_msg->argv, script_msg->argc, buffer);
	packstr_array(script_msg->env, envcount(script_msg->env), buffer);
	/* Use packmem for extra_buf - treat it as data, not as a string */
	pack32(script_msg->extra_buf_size, buffer);
	packmem(script_msg->extra_buf, script_msg->extra_buf_size, buffer);
	pack32(script_msg->job_id, buffer);
	packstr(script_msg->script_name, buffer);
	packstr(script_msg->script_path, buffer);
	pack32(script_msg->script_type, buffer);
	pack32(script_msg->timeout, buffer);
	packstr(script_msg->tmp_file_env_name, buffer);
	packstr(script_msg->tmp_file_str, buffer);
}

static int _unpack_run_script(run_script_msg_t **msg, buf_t *buffer)
{
	int rc = SLURM_SUCCESS;
	uint32_t tmp32;
	run_script_msg_t *script_msg = xmalloc(sizeof(*script_msg));

	*msg = script_msg;

	safe_unpackstr_array(&script_msg->argv, &script_msg->argc, buffer);
	safe_unpackstr_array(&script_msg->env, &tmp32, buffer);
	safe_unpack32(&script_msg->extra_buf_size, buffer);
	safe_unpackmem_xmalloc(&script_msg->extra_buf,
			       &script_msg->extra_buf_size, buffer);
	safe_unpack32(&script_msg->job_id, buffer);
	safe_unpackstr(&script_msg->script_name, buffer);
	safe_unpackstr(&script_msg->script_path, buffer);
	safe_unpack32(&script_msg->script_type, buffer);
	safe_unpack32(&script_msg->timeout, buffer);
	safe_unpackstr(&script_msg->tmp_file_env_name, buffer);
	safe_unpackstr(&script_msg->tmp_file_str, buffer);

	return rc;

unpack_error:
	error("%s: Failed to unpack message", __func__);
	slurmscriptd_free_run_script_msg(script_msg);
	*msg = NULL;
	return SLURM_ERROR;
}

static void _pack_bb_script_info(bb_script_info_msg_t *msg, buf_t *buffer)
{
	packstr(msg->authalttypes, buffer);
	packstr(msg->authinfo, buffer);
	packstr(msg->authalt_params, buffer);
	packstr(msg->authtype, buffer);
	packstr(msg->cluster_name, buffer);
	/* Use packmem for extra_buf - treat it as data, not as a string */
	pack32(msg->extra_buf_size, buffer);
	packmem(msg->extra_buf, msg->extra_buf_size, buffer);
	packstr(msg->function, buffer);
	pack32(msg->job_id, buffer);
	pack16(msg->slurmctld_debug, buffer);
	packstr(msg->slurmctld_logfile, buffer);
	pack16(msg->log_fmt, buffer);
	packstr(msg->plugindir, buffer);
	packstr(msg->slurm_user_name, buffer);
	pack32(msg->slurm_user_id, buffer);
}

static int _unpack_bb_script_info(bb_script_info_msg_t **msg, buf_t *buffer)
{
	bb_script_info_msg_t *bb_msg = xmalloc(sizeof(*bb_msg));

	*msg = bb_msg;

	safe_unpackstr(&bb_msg->authalttypes, buffer);
	safe_unpackstr(&bb_msg->authinfo, buffer);
	safe_unpackstr(&bb_msg->authalt_params, buffer);
	safe_unpackstr(&bb_msg->authtype, buffer);
	safe_unpackstr(&bb_msg->cluster_name, buffer);
	safe_unpack32(&bb_msg->extra_buf_size, buffer);
	safe_unpackmem_xmalloc(&bb_msg->extra_buf,
			       &bb_msg->extra_buf_size, buffer);
	safe_unpackstr(&bb_msg->function, buffer);
	safe_unpack32(&bb_msg->job_id, buffer);
	safe_unpack16(&bb_msg->slurmctld_debug, buffer);
	safe_unpackstr(&bb_msg->slurmctld_logfile, buffer);
	safe_unpack16(&bb_msg->log_fmt, buffer);
	safe_unpackstr(&bb_msg->plugindir, buffer);
	safe_unpackstr(&bb_msg->slurm_user_name, buffer);
	safe_unpack32(&bb_msg->slurm_user_id, buffer);

	return SLURM_SUCCESS;

unpack_error:
	error("%s: Failed to unpack message", __func__);
	slurmscriptd_free_bb_script_info_msg(bb_msg);

	return SLURM_ERROR;
}

static void _pack_script_complete(script_complete_t *resp_msg, buf_t *buffer)
{
	pack32(resp_msg->job_id, buffer);
	packstr(resp_msg->resp_msg, buffer);
	packstr(resp_msg->script_name, buffer);
	pack32(resp_msg->script_type, buffer);
	packbool(resp_msg->signalled, buffer);
	pack32(resp_msg->status, buffer);
	packbool(resp_msg->timed_out, buffer);
}

static int _unpack_script_complete(script_complete_t **resp_msg,
				   buf_t *buffer)
{
	uint32_t tmp32;
	script_complete_t *data = xmalloc(sizeof *data);
	*resp_msg = data;

	safe_unpack32(&data->job_id, buffer);
	safe_unpackstr(&data->resp_msg, buffer);
	safe_unpackstr(&data->script_name, buffer);
	safe_unpack32(&data->script_type, buffer);
	safe_unpackbool(&data->signalled, buffer);
	safe_unpack32(&tmp32, buffer);
	data->status = (int)tmp32;
	safe_unpackbool(&data->timed_out, buffer);

	return SLURM_SUCCESS;

unpack_error:
	error("%s: Failed to unpack message", __func__);
	slurmscriptd_free_script_complete(data);
	*resp_msg = NULL;
	return SLURM_ERROR;
}

static void _pack_flush_job(flush_job_msg_t *msg, buf_t *buffer)
{
	pack32(msg->job_id, buffer);
}

static int _unpack_flush_job(flush_job_msg_t **resp_msg, buf_t *buffer)
{
	flush_job_msg_t *data = xmalloc(sizeof *data);
	*resp_msg = data;

	safe_unpack32(&data->job_id, buffer);

	return SLURM_SUCCESS;

unpack_error:
	error("%s: Failed to unpack message", __func__);
	xfree(data);
	*resp_msg = NULL;
	return SLURM_ERROR;
}

static void _pack_debug_flags(debug_flags_msg_t *msg, buf_t *buffer)
{
	pack64(msg->debug_flags, buffer);
}

static int _unpack_debug_flags(debug_flags_msg_t **msg, buf_t *buffer)
{
	debug_flags_msg_t *data = xmalloc(sizeof *data);
	*msg = data;

	safe_unpack64(&data->debug_flags, buffer);

	return SLURM_SUCCESS;

unpack_error:
	error("%s: Failed to unpack message", __func__);
	xfree(data);
	*msg = NULL;
	return SLURM_ERROR;
}

static void _pack_log_msg(log_msg_t *msg, buf_t *buffer)
{
	pack32(msg->debug_level, buffer);
	packbool(msg->log_rotate, buffer);
}

static int _unpack_log_msg(log_msg_t **msg, buf_t *buffer)
{
	log_msg_t *data = xmalloc(sizeof *data);
	*msg = data;

	safe_unpack32(&data->debug_level, buffer);
	safe_unpackbool(&data->log_rotate, buffer);

	return SLURM_SUCCESS;

unpack_error:
	error("%s: Failed to unpack message", __func__);
	xfree(data);
	*msg = NULL;
	return SLURM_ERROR;
}

extern int slurmscriptd_pack_msg(slurmscriptd_msg_t *msg, buf_t *buffer)
{
	int rc = SLURM_SUCCESS;

	packstr(msg->key, buffer); /* Can be NULL */

	switch (msg->msg_type) {
	case SLURMSCRIPTD_REQUEST_BB_SCRIPT_INFO:
		_pack_bb_script_info(msg->msg_data, buffer);
		break;
	case SLURMSCRIPTD_REQUEST_FLUSH:
		/* Nothing to pack */
		break;
	case SLURMSCRIPTD_REQUEST_FLUSH_JOB:
		_pack_flush_job(msg->msg_data, buffer);
		break;
	case SLURMSCRIPTD_REQUEST_RUN_SCRIPT:
		_pack_run_script(msg->msg_data, buffer);
		break;
	case SLURMSCRIPTD_REQUEST_SCRIPT_COMPLETE:
		_pack_script_complete(msg->msg_data, buffer);
		break;
	case SLURMSCRIPTD_REQUEST_UPDATE_DEBUG_FLAGS:
		_pack_debug_flags(msg->msg_data, buffer);
		break;
	case SLURMSCRIPTD_REQUEST_UPDATE_LOG:
		_pack_log_msg(msg->msg_data, buffer);
		break;
	case SLURMSCRIPTD_SHUTDOWN:
		/* Nothing to pack */
		break;
	default:
		error("Unrecognized slurmscriptd msg type=%d",
		      msg->msg_type);
		rc = SLURM_ERROR;
		break;
	}

	return rc;
}

extern int slurmscriptd_unpack_msg(slurmscriptd_msg_t *msg, buf_t *buffer)
{
	int rc = SLURM_SUCCESS;

	safe_unpackstr(&msg->key, buffer);
	switch (msg->msg_type) {
	case SLURMSCRIPTD_REQUEST_BB_SCRIPT_INFO:
		rc = _unpack_bb_script_info(
				(bb_script_info_msg_t **)(&msg->msg_data),
				buffer);
		break;
	case SLURMSCRIPTD_REQUEST_FLUSH:
		/* Nothing to unpack */
		break;
	case SLURMSCRIPTD_REQUEST_FLUSH_JOB:
		rc = _unpack_flush_job((flush_job_msg_t **)(&msg->msg_data),
				       buffer);
		break;
	case SLURMSCRIPTD_REQUEST_SCRIPT_COMPLETE:
		rc = _unpack_script_complete(
				(script_complete_t **)(&msg->msg_data),
				buffer);
		break;
	case SLURMSCRIPTD_REQUEST_RUN_SCRIPT:
		rc = _unpack_run_script(
				(run_script_msg_t **)(&msg->msg_data),
				buffer);
		break;
	case SLURMSCRIPTD_REQUEST_UPDATE_DEBUG_FLAGS:
		rc = _unpack_debug_flags(
				(debug_flags_msg_t **)(&msg->msg_data),
				 buffer);
		break;
	case SLURMSCRIPTD_REQUEST_UPDATE_LOG:
		rc = _unpack_log_msg((log_msg_t **) (&msg->msg_data), buffer);
		break;
	case SLURMSCRIPTD_SHUTDOWN:
		/* Nothing to unpack */
		break;
	default:
		error("Unrecognized slurmscriptd msg type=%d",
		      msg->msg_type);
		rc = SLURM_ERROR;
		break;
	}

	return rc;

unpack_error:
	error("%s: Read-write fail unpacking message=%d",
	      __func__, msg->msg_type);
	return SLURM_ERROR;
}
