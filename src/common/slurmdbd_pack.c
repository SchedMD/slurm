/****************************************************************************\
 *  slurmdbd_pack.c - functions to pack SlurmDBD RPCs
 *****************************************************************************
 *  Copyright (C) 2011-2018 SchedMD LLC.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurmdb_pack.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/slurmdbd_pack.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * Define slurm-specific aliases for use by plugins, see slurm_xlator.h
 * for details.
 */
strong_alias(pack_slurmdbd_msg, slurm_pack_slurmdbd_msg);
strong_alias(unpack_slurmdbd_msg, slurm_unpack_slurmdbd_msg);
strong_alias(slurmdbd_pack_fini_msg, slurm_slurmdbd_pack_fini_msg);


static int _unpack_config_name(char **object, uint16_t rpc_version,
			       buf_t *buffer)
{
	char *config_name;
	uint32_t uint32_tmp;

	safe_unpackstr_xmalloc(&config_name, &uint32_tmp, buffer);
	*object = config_name;
	return SLURM_SUCCESS;

unpack_error:
	*object = NULL;
	return SLURM_ERROR;
}

static void _slurmdbd_packstr(void *str, uint16_t rpc_version, buf_t *buffer)
{
	packstr((char *)str, buffer);
}

static int _slurmdbd_unpackstr(void **str, uint16_t rpc_version, buf_t *buffer)
{
	uint32_t uint32_tmp;
	safe_unpackstr_xmalloc((char **)str, &uint32_tmp, buffer);
	return SLURM_SUCCESS;
unpack_error:
	return SLURM_ERROR;
}

/****************************************************************************\
 * Pack and unpack data structures
\****************************************************************************/
static void _pack_acct_coord_msg(dbd_acct_coord_msg_t *msg,
				 uint16_t rpc_version, buf_t *buffer)
{
	char *acct = NULL;
	ListIterator itr = NULL;
	uint32_t count = 0;

	if (msg->acct_list)
		count = list_count(msg->acct_list);

	pack32(count, buffer);
	if (count) {
		itr = list_iterator_create(msg->acct_list);
		while ((acct = list_next(itr))) {
			packstr(acct, buffer);
		}
		list_iterator_destroy(itr);
	}

	slurmdb_pack_user_cond(msg->cond, rpc_version, buffer);
}

static int _unpack_acct_coord_msg(dbd_acct_coord_msg_t **msg,
				  uint16_t rpc_version, buf_t *buffer)
{
	uint32_t uint32_tmp;
	int i;
	char *acct = NULL;
	uint32_t count = 0;
	dbd_acct_coord_msg_t *msg_ptr = xmalloc(sizeof(dbd_acct_coord_msg_t));
	*msg = msg_ptr;

	safe_unpack32(&count, buffer);
	if (count) {
		msg_ptr->acct_list = list_create(xfree_ptr);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&acct, &uint32_tmp, buffer);
			list_append(msg_ptr->acct_list, acct);
		}
	}

	if (slurmdb_unpack_user_cond((void *)&msg_ptr->cond,
				     rpc_version, buffer)
	    == SLURM_ERROR)
		goto unpack_error;
	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_acct_coord_msg(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void _pack_cluster_tres_msg(dbd_cluster_tres_msg_t *msg,
				   uint16_t rpc_version, buf_t *buffer)
{
	if (rpc_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->cluster_nodes, buffer);
		pack_time(msg->event_time, buffer);
		packstr(msg->tres_str, buffer);
	}
}

static int _unpack_cluster_tres_msg(dbd_cluster_tres_msg_t **msg,
				    uint16_t rpc_version, buf_t *buffer)
{
	dbd_cluster_tres_msg_t *msg_ptr;
	uint32_t uint32_tmp;

	msg_ptr = xmalloc(sizeof(dbd_cluster_tres_msg_t));
	*msg = msg_ptr;

	if (rpc_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&msg_ptr->cluster_nodes,
				       &uint32_tmp, buffer);
		safe_unpack_time(&msg_ptr->event_time, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->tres_str,
				       &uint32_tmp, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_cluster_tres_msg(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void _pack_rec_msg(dbd_rec_msg_t *msg, uint16_t rpc_version,
			  slurmdbd_msg_type_t type, buf_t *buffer)
{
	void (*my_function) (void *object, uint16_t rpc_version, buf_t *buffer);

	switch (type) {
	case DBD_ADD_RESV:
	case DBD_REMOVE_RESV:
	case DBD_MODIFY_RESV:
		my_function = slurmdb_pack_reservation_rec;
		break;
	default:
		fatal("Unknown pack type");
		return;
	}

	(*(my_function))(msg->rec, rpc_version, buffer);
}

static int _unpack_rec_msg(dbd_rec_msg_t **msg, uint16_t rpc_version,
			   slurmdbd_msg_type_t type, buf_t *buffer)
{
	dbd_rec_msg_t *msg_ptr = NULL;
	int (*my_function) (void **object, uint16_t rpc_version, buf_t *buffer);

	switch (type) {
	case DBD_ADD_RESV:
	case DBD_REMOVE_RESV:
	case DBD_MODIFY_RESV:
		my_function = slurmdb_unpack_reservation_rec;
		break;
	default:
		fatal("%s: Unknown unpack type", __func__);
		return SLURM_ERROR;
	}

	msg_ptr = xmalloc(sizeof(dbd_rec_msg_t));
	*msg = msg_ptr;

	if ((*(my_function))(&msg_ptr->rec, rpc_version, buffer) == SLURM_ERROR)
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_rec_msg(msg_ptr, type);
	*msg = NULL;
	return SLURM_ERROR;
}

static void _pack_cond_msg(dbd_cond_msg_t *msg, uint16_t rpc_version,
			   slurmdbd_msg_type_t type, buf_t *buffer)
{
	void (*my_function) (void *object, uint16_t rpc_version, buf_t *buffer);

	switch (type) {
	case DBD_GET_ACCOUNTS:
	case DBD_REMOVE_ACCOUNTS:
		my_function = slurmdb_pack_account_cond;
		break;
	case DBD_GET_TRES:
		my_function = slurmdb_pack_tres_cond;
		break;
	case DBD_GET_ASSOCS:
	case DBD_GET_PROBS:
	case DBD_REMOVE_ASSOCS:
		my_function = slurmdb_pack_assoc_cond;
		break;
	case DBD_GET_CLUSTERS:
	case DBD_REMOVE_CLUSTERS:
		my_function = slurmdb_pack_cluster_cond;
		break;
	case DBD_GET_FEDERATIONS:
	case DBD_REMOVE_FEDERATIONS:
		my_function = slurmdb_pack_federation_cond;
		break;
	case DBD_GET_JOBS_COND:
		my_function = slurmdb_pack_job_cond;
		break;
	case DBD_GET_QOS:
	case DBD_REMOVE_QOS:
		my_function = slurmdb_pack_qos_cond;
		break;
	case DBD_GET_RES:
	case DBD_REMOVE_RES:
		my_function = slurmdb_pack_res_cond;
		break;
	case DBD_GET_WCKEYS:
	case DBD_REMOVE_WCKEYS:
		my_function = slurmdb_pack_wckey_cond;
		break;
	case DBD_GET_USERS:
	case DBD_REMOVE_USERS:
		my_function = slurmdb_pack_user_cond;
		break;
	case DBD_GET_TXN:
		my_function = slurmdb_pack_txn_cond;
		break;
	case DBD_ARCHIVE_DUMP:
		my_function = slurmdb_pack_archive_cond;
		break;
	case DBD_GET_RESVS:
		my_function = slurmdb_pack_reservation_cond;
		break;
	case DBD_GET_EVENTS:
		my_function = slurmdb_pack_event_cond;
		break;
	default:
		fatal("Unknown pack type");
		return;
	}

	(*(my_function))(msg->cond, rpc_version, buffer);
}

static int _unpack_cond_msg(dbd_cond_msg_t **msg, uint16_t rpc_version,
			    slurmdbd_msg_type_t type, buf_t *buffer)
{
	dbd_cond_msg_t *msg_ptr = NULL;
	int (*my_function) (void **object, uint16_t rpc_version, buf_t *buffer);

	switch (type) {
	case DBD_GET_ACCOUNTS:
	case DBD_REMOVE_ACCOUNTS:
		my_function = slurmdb_unpack_account_cond;
		break;
	case DBD_GET_TRES:
		my_function = slurmdb_unpack_tres_cond;
		break;
	case DBD_GET_ASSOCS:
	case DBD_GET_PROBS:
	case DBD_REMOVE_ASSOCS:
		my_function = slurmdb_unpack_assoc_cond;
		break;
	case DBD_GET_CLUSTERS:
	case DBD_REMOVE_CLUSTERS:
		my_function = slurmdb_unpack_cluster_cond;
		break;
	case DBD_GET_FEDERATIONS:
	case DBD_REMOVE_FEDERATIONS:
		my_function = slurmdb_unpack_federation_cond;
		break;
	case DBD_GET_JOBS_COND:
		my_function = slurmdb_unpack_job_cond;
		break;
	case DBD_GET_QOS:
	case DBD_REMOVE_QOS:
		my_function = slurmdb_unpack_qos_cond;
		break;
	case DBD_GET_RES:
	case DBD_REMOVE_RES:
		my_function = slurmdb_unpack_res_cond;
		break;
	case DBD_GET_WCKEYS:
	case DBD_REMOVE_WCKEYS:
		my_function = slurmdb_unpack_wckey_cond;
		break;
	case DBD_GET_USERS:
	case DBD_REMOVE_USERS:
		my_function = slurmdb_unpack_user_cond;
		break;
	case DBD_GET_TXN:
		my_function = slurmdb_unpack_txn_cond;
		break;
	case DBD_ARCHIVE_DUMP:
		my_function = slurmdb_unpack_archive_cond;
		break;
	case DBD_GET_RESVS:
		my_function = slurmdb_unpack_reservation_cond;
		break;
	case DBD_GET_EVENTS:
		my_function = slurmdb_unpack_event_cond;
		break;
	default:
		fatal("%s: Unknown unpack type", __func__);
		return SLURM_ERROR;
	}

	msg_ptr = xmalloc(sizeof(dbd_cond_msg_t));
	*msg = msg_ptr;

	if ((*(my_function))(&msg_ptr->cond, rpc_version, buffer) ==
	    SLURM_ERROR)
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_cond_msg(msg_ptr, type);
	*msg = NULL;
	return SLURM_ERROR;
}

static void _pack_job_complete_msg(dbd_job_comp_msg_t *msg,
				   uint16_t rpc_version, buf_t *buffer)
{
	if (rpc_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->admin_comment, buffer);
		pack32(msg->assoc_id, buffer);
		packstr(msg->comment, buffer);
		pack64(msg->db_index, buffer);
		pack32(msg->derived_ec, buffer);
		pack_time(msg->end_time, buffer);
		pack32(msg->exit_code, buffer);
		pack32(msg->job_id, buffer);
		pack32(msg->job_state, buffer);
		packstr(msg->nodes, buffer);
		pack32(msg->req_uid, buffer);
		pack_time(msg->start_time, buffer);
		pack_time(msg->submit_time, buffer);
		packstr(msg->system_comment, buffer);
		packstr(msg->tres_alloc_str, buffer);
	}
}

static int _unpack_job_complete_msg(dbd_job_comp_msg_t **msg,
				    uint16_t rpc_version, buf_t *buffer)
{
	uint32_t uint32_tmp;
	dbd_job_comp_msg_t *msg_ptr = xmalloc(sizeof(dbd_job_comp_msg_t));
	*msg = msg_ptr;

	if (rpc_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&msg_ptr->admin_comment,
				       &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->assoc_id, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->comment, &uint32_tmp, buffer);
		safe_unpack64(&msg_ptr->db_index, buffer);
		safe_unpack32(&msg_ptr->derived_ec, buffer);
		safe_unpack_time(&msg_ptr->end_time, buffer);
		safe_unpack32(&msg_ptr->exit_code, buffer);
		safe_unpack32(&msg_ptr->job_id, buffer);
		safe_unpack32(&msg_ptr->job_state, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->nodes, &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->req_uid, buffer);
		safe_unpack_time(&msg_ptr->start_time, buffer);
		safe_unpack_time(&msg_ptr->submit_time, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->system_comment,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->tres_alloc_str,
				       &uint32_tmp, buffer);
	} else
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_job_complete_msg(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void _pack_job_start_msg(void *in, uint16_t rpc_version, buf_t *buffer)
{
	dbd_job_start_msg_t *msg = (dbd_job_start_msg_t *)in;

	if (msg->script_buf)
		msg->script = msg->script_buf->head;

	if (rpc_version >= SLURM_21_08_PROTOCOL_VERSION) {
		packstr(msg->account, buffer);
		pack32(msg->alloc_nodes, buffer);
		pack32(msg->array_job_id, buffer);
		pack32(msg->array_max_tasks, buffer);
		pack32(msg->array_task_id, buffer);
		packstr(msg->array_task_str, buffer);
		pack32(msg->array_task_pending, buffer);
		pack32(msg->assoc_id, buffer);
		packstr(msg->constraints, buffer);
		packstr(msg->container, buffer);
		pack32(msg->db_flags, buffer);
		pack64(msg->db_index, buffer);
		pack_time(msg->eligible_time, buffer);
		packstr(msg->env, buffer);
		pack32(msg->gid, buffer);
		packstr(msg->gres_used, buffer);
		pack32(msg->job_id, buffer);
		pack32(msg->job_state, buffer);
		pack32(msg->state_reason_prev, buffer);
		packstr(msg->mcs_label, buffer);
		packstr(msg->name, buffer);
		packstr(msg->nodes, buffer);
		packstr(msg->node_inx, buffer);
		pack32(msg->het_job_id, buffer);
		pack32(msg->het_job_offset, buffer);
		packstr(msg->partition, buffer);
		pack32(msg->priority, buffer);
		pack32(msg->qos_id, buffer);
		pack32(msg->req_cpus, buffer);
		pack64(msg->req_mem, buffer);
		pack32(msg->resv_id, buffer);
		packstr(msg->script, buffer);
		pack_time(msg->start_time, buffer);
		packstr(msg->submit_line, buffer);
		pack_time(msg->submit_time, buffer);
		pack32(msg->timelimit, buffer);
		packstr(msg->tres_alloc_str, buffer);
		packstr(msg->tres_req_str, buffer);
		pack32(msg->uid, buffer);
		packstr(msg->wckey, buffer);
		packstr(msg->work_dir, buffer);
	} else if (rpc_version >= SLURM_20_11_PROTOCOL_VERSION) {
		packstr(msg->account, buffer);
		pack32(msg->alloc_nodes, buffer);
		pack32(msg->array_job_id, buffer);
		pack32(msg->array_max_tasks, buffer);
		pack32(msg->array_task_id, buffer);
		packstr(msg->array_task_str, buffer);
		pack32(msg->array_task_pending, buffer);
		pack32(msg->assoc_id, buffer);
		packstr(msg->constraints, buffer);
		pack32(msg->db_flags, buffer);
		pack64(msg->db_index, buffer);
		pack_time(msg->eligible_time, buffer);
		pack32(msg->gid, buffer);
		packstr(msg->gres_used, buffer);
		pack32(msg->job_id, buffer);
		pack32(msg->job_state, buffer);
		pack32(msg->state_reason_prev, buffer);
		packstr(msg->mcs_label, buffer);
		packstr(msg->name, buffer);
		packstr(msg->nodes, buffer);
		packstr(msg->node_inx, buffer);
		pack32(msg->het_job_id, buffer);
		pack32(msg->het_job_offset, buffer);
		packstr(msg->partition, buffer);
		pack32(msg->priority, buffer);
		pack32(msg->qos_id, buffer);
		pack32(msg->req_cpus, buffer);
		pack64(msg->req_mem, buffer);
		pack32(msg->resv_id, buffer);
		pack_time(msg->start_time, buffer);
		pack_time(msg->submit_time, buffer);
		pack32(msg->timelimit, buffer);
		packstr(msg->tres_alloc_str, buffer);
		packstr(msg->tres_req_str, buffer);
		pack32(msg->uid, buffer);
		packstr(msg->wckey, buffer);
		packstr(msg->work_dir, buffer);
	} else if (rpc_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->account, buffer);
		pack32(msg->alloc_nodes, buffer);
		pack32(msg->array_job_id, buffer);
		pack32(msg->array_max_tasks, buffer);
		pack32(msg->array_task_id, buffer);
		packstr(msg->array_task_str, buffer);
		pack32(msg->array_task_pending, buffer);
		pack32(msg->assoc_id, buffer);
		packstr(msg->constraints, buffer);
		pack32(msg->db_flags, buffer);
		pack64(msg->db_index, buffer);
		pack_time(msg->eligible_time, buffer);
		pack32(msg->gid, buffer);
		packnull(buffer);
		packnull(buffer);
		packstr(msg->gres_used, buffer);
		pack32(msg->job_id, buffer);
		pack32(msg->job_state, buffer);
		pack32(msg->state_reason_prev, buffer);
		packstr(msg->mcs_label, buffer);
		packstr(msg->name, buffer);
		packstr(msg->nodes, buffer);
		packstr(msg->node_inx, buffer);
		pack32(msg->het_job_id, buffer);
		pack32(msg->het_job_offset, buffer);
		packstr(msg->partition, buffer);
		pack32(msg->priority, buffer);
		pack32(msg->qos_id, buffer);
		pack32(msg->req_cpus, buffer);
		pack64(msg->req_mem, buffer);
		pack32(msg->resv_id, buffer);
		pack_time(msg->start_time, buffer);
		pack_time(msg->submit_time, buffer);
		pack32(msg->timelimit, buffer);
		packstr(msg->tres_alloc_str, buffer);
		packstr(msg->tres_req_str, buffer);
		pack32(msg->uid, buffer);
		packstr(msg->wckey, buffer);
		packstr(msg->work_dir, buffer);
	}

	if (msg->script_buf)
		msg->script = NULL;
}

static int _unpack_job_start_msg(void **msg, uint16_t rpc_version,
				 buf_t *buffer)
{
	uint32_t uint32_tmp;
	dbd_job_start_msg_t *msg_ptr = xmalloc(sizeof(dbd_job_start_msg_t));
	*msg = msg_ptr;

	msg_ptr->array_job_id = 0;
	msg_ptr->array_task_id = NO_VAL;

	if (rpc_version >= SLURM_21_08_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&msg_ptr->account, &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->alloc_nodes, buffer);
		safe_unpack32(&msg_ptr->array_job_id, buffer);
		safe_unpack32(&msg_ptr->array_max_tasks, buffer);
		safe_unpack32(&msg_ptr->array_task_id, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->array_task_str,
				       &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->array_task_pending, buffer);
		safe_unpack32(&msg_ptr->assoc_id, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->constraints,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->container,
				       &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->db_flags, buffer);
		safe_unpack64(&msg_ptr->db_index, buffer);
		safe_unpack_time(&msg_ptr->eligible_time, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->env, &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->gid, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->gres_used, &uint32_tmp,
				       buffer);
		safe_unpack32(&msg_ptr->job_id, buffer);
		safe_unpack32(&msg_ptr->job_state, buffer);
		safe_unpack32(&msg_ptr->state_reason_prev, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->mcs_label,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->node_inx, &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->het_job_id, buffer);
		safe_unpack32(&msg_ptr->het_job_offset, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->partition,
				       &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->priority, buffer);
		safe_unpack32(&msg_ptr->qos_id, buffer);
		safe_unpack32(&msg_ptr->req_cpus, buffer);
		safe_unpack64(&msg_ptr->req_mem, buffer);
		safe_unpack32(&msg_ptr->resv_id, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->script, &uint32_tmp, buffer);
		safe_unpack_time(&msg_ptr->start_time, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->submit_line,
				       &uint32_tmp, buffer);
		safe_unpack_time(&msg_ptr->submit_time, buffer);
		safe_unpack32(&msg_ptr->timelimit, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->tres_alloc_str,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->tres_req_str,
				       &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->uid, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->wckey, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->work_dir, &uint32_tmp, buffer);
	} else if (rpc_version >= SLURM_20_11_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&msg_ptr->account, &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->alloc_nodes, buffer);
		safe_unpack32(&msg_ptr->array_job_id, buffer);
		safe_unpack32(&msg_ptr->array_max_tasks, buffer);
		safe_unpack32(&msg_ptr->array_task_id, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->array_task_str,
				       &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->array_task_pending, buffer);
		safe_unpack32(&msg_ptr->assoc_id, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->constraints,
				       &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->db_flags, buffer);
		safe_unpack64(&msg_ptr->db_index, buffer);
		safe_unpack_time(&msg_ptr->eligible_time, buffer);
		safe_unpack32(&msg_ptr->gid, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->gres_used, &uint32_tmp,
				       buffer);
		safe_unpack32(&msg_ptr->job_id, buffer);
		safe_unpack32(&msg_ptr->job_state, buffer);
		safe_unpack32(&msg_ptr->state_reason_prev, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->mcs_label,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->node_inx, &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->het_job_id, buffer);
		safe_unpack32(&msg_ptr->het_job_offset, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->partition,
				       &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->priority, buffer);
		safe_unpack32(&msg_ptr->qos_id, buffer);
		safe_unpack32(&msg_ptr->req_cpus, buffer);
		safe_unpack64(&msg_ptr->req_mem, buffer);
		safe_unpack32(&msg_ptr->resv_id, buffer);
		safe_unpack_time(&msg_ptr->start_time, buffer);
		safe_unpack_time(&msg_ptr->submit_time, buffer);
		safe_unpack32(&msg_ptr->timelimit, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->tres_alloc_str,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->tres_req_str,
				       &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->uid, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->wckey, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->work_dir, &uint32_tmp, buffer);
	} else if (rpc_version >= SLURM_MIN_PROTOCOL_VERSION) {
		char *tmp_ptr = NULL;
		safe_unpackstr_xmalloc(&msg_ptr->account, &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->alloc_nodes, buffer);
		safe_unpack32(&msg_ptr->array_job_id, buffer);
		safe_unpack32(&msg_ptr->array_max_tasks, buffer);
		safe_unpack32(&msg_ptr->array_task_id, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->array_task_str,
				       &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->array_task_pending, buffer);
		safe_unpack32(&msg_ptr->assoc_id, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->constraints,
				       &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->db_flags, buffer);
		safe_unpack64(&msg_ptr->db_index, buffer);
		safe_unpack_time(&msg_ptr->eligible_time, buffer);
		safe_unpack32(&msg_ptr->gid, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr, &uint32_tmp, buffer);
		xfree(tmp_ptr);
		safe_unpackstr_xmalloc(&tmp_ptr, &uint32_tmp, buffer);
		xfree(tmp_ptr);
		safe_unpackstr_xmalloc(&msg_ptr->gres_used, &uint32_tmp,
				       buffer);
		safe_unpack32(&msg_ptr->job_id, buffer);
		safe_unpack32(&msg_ptr->job_state, buffer);
		safe_unpack32(&msg_ptr->state_reason_prev, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->mcs_label,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->node_inx, &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->het_job_id, buffer);
		safe_unpack32(&msg_ptr->het_job_offset, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->partition,
				       &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->priority, buffer);
		safe_unpack32(&msg_ptr->qos_id, buffer);
		safe_unpack32(&msg_ptr->req_cpus, buffer);
		safe_unpack64(&msg_ptr->req_mem, buffer);
		safe_unpack32(&msg_ptr->resv_id, buffer);
		safe_unpack_time(&msg_ptr->start_time, buffer);
		safe_unpack_time(&msg_ptr->submit_time, buffer);
		safe_unpack32(&msg_ptr->timelimit, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->tres_alloc_str,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->tres_req_str,
				       &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->uid, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->wckey, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->work_dir, &uint32_tmp, buffer);
	} else
		  goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_job_start_msg(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void _pack_job_suspend_msg(dbd_job_suspend_msg_t *msg,
				  uint16_t rpc_version, buf_t *buffer)
{
	if (rpc_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->assoc_id, buffer);
		pack64(msg->db_index, buffer);
		pack32(msg->job_id, buffer);
		pack32(msg->job_state, buffer);
		pack_time(msg->submit_time, buffer);
		pack_time(msg->suspend_time, buffer);
	}
}

static int _unpack_job_suspend_msg(dbd_job_suspend_msg_t **msg,
				   uint16_t rpc_version, buf_t *buffer)
{
	dbd_job_suspend_msg_t *msg_ptr = xmalloc(sizeof(dbd_job_suspend_msg_t));
	*msg = msg_ptr;

	if (rpc_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg_ptr->assoc_id, buffer);
		safe_unpack64(&msg_ptr->db_index, buffer);
		safe_unpack32(&msg_ptr->job_id, buffer);
		safe_unpack32(&msg_ptr->job_state, buffer);
		safe_unpack_time(&msg_ptr->submit_time, buffer);
		safe_unpack_time(&msg_ptr->suspend_time, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_job_suspend_msg(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void _pack_modify_msg(dbd_modify_msg_t *msg, uint16_t rpc_version,
			     slurmdbd_msg_type_t type, buf_t *buffer)
{
	void (*my_cond) (void *object, uint16_t rpc_version, buf_t *buffer);
	void (*my_rec) (void *object, uint16_t rpc_version, buf_t *buffer);

	switch (type) {
	case DBD_MODIFY_ACCOUNTS:
		my_cond = slurmdb_pack_account_cond;
		my_rec = slurmdb_pack_account_rec;
		break;
	case DBD_MODIFY_ASSOCS:
		my_cond = slurmdb_pack_assoc_cond;
		my_rec = slurmdb_pack_assoc_rec;
		break;
	case DBD_MODIFY_CLUSTERS:
		my_cond = slurmdb_pack_cluster_cond;
		my_rec = slurmdb_pack_cluster_rec;
		break;
	case DBD_MODIFY_FEDERATIONS:
		my_cond = slurmdb_pack_federation_cond;
		my_rec = slurmdb_pack_federation_rec;
		break;
	case DBD_MODIFY_JOB:
		if (rpc_version >= SLURM_20_02_PROTOCOL_VERSION)
			my_cond = slurmdb_pack_job_cond;
		else
			my_cond = slurmdb_pack_job_modify_cond;
		my_rec = slurmdb_pack_job_rec;
		break;
	case DBD_MODIFY_QOS:
		my_cond = slurmdb_pack_qos_cond;
		my_rec = slurmdb_pack_qos_rec;
		break;
	case DBD_MODIFY_RES:
		my_cond = slurmdb_pack_res_cond;
		my_rec = slurmdb_pack_res_rec;
		break;
	case DBD_MODIFY_USERS:
		my_cond = slurmdb_pack_user_cond;
		my_rec = slurmdb_pack_user_rec;
		break;
	default:
		fatal("Unknown pack type");
		return;
	}
	(*(my_cond))(msg->cond, rpc_version, buffer);
	(*(my_rec))(msg->rec, rpc_version, buffer);
}

static int _unpack_modify_msg(dbd_modify_msg_t **msg, uint16_t rpc_version,
			      slurmdbd_msg_type_t type, buf_t *buffer)
{
	dbd_modify_msg_t *msg_ptr = NULL;
	int (*my_cond) (void **object, uint16_t rpc_version, buf_t *buffer);
	int (*my_rec) (void **object, uint16_t rpc_version, buf_t *buffer);

	msg_ptr = xmalloc(sizeof(dbd_modify_msg_t));
	*msg = msg_ptr;

	switch (type) {
	case DBD_MODIFY_ACCOUNTS:
		my_cond = slurmdb_unpack_account_cond;
		my_rec = slurmdb_unpack_account_rec;
		break;
	case DBD_MODIFY_ASSOCS:
		my_cond = slurmdb_unpack_assoc_cond;
		my_rec = slurmdb_unpack_assoc_rec;
		break;
	case DBD_MODIFY_CLUSTERS:
		my_cond = slurmdb_unpack_cluster_cond;
		my_rec = slurmdb_unpack_cluster_rec;
		break;
	case DBD_MODIFY_FEDERATIONS:
		my_cond = slurmdb_unpack_federation_cond;
		my_rec = slurmdb_unpack_federation_rec;
		break;
	case DBD_MODIFY_JOB:
		if (rpc_version >= SLURM_20_02_PROTOCOL_VERSION)
			my_cond = slurmdb_unpack_job_cond;
		else
			my_cond = slurmdb_unpack_job_modify_cond;
		my_rec = slurmdb_unpack_job_rec;
		break;
	case DBD_MODIFY_QOS:
		my_cond = slurmdb_unpack_qos_cond;
		my_rec = slurmdb_unpack_qos_rec;
		break;
	case DBD_MODIFY_RES:
		my_cond = slurmdb_unpack_res_cond;
		my_rec = slurmdb_unpack_res_rec;
		break;
	case DBD_MODIFY_USERS:
		my_cond = slurmdb_unpack_user_cond;
		my_rec = slurmdb_unpack_user_rec;
		break;
	default:
		fatal("%s: Unknown unpack type", __func__);
		return SLURM_ERROR;
	}

	if ((*(my_cond))(&msg_ptr->cond, rpc_version, buffer) == SLURM_ERROR)
		goto unpack_error;
	if ((*(my_rec))(&msg_ptr->rec, rpc_version, buffer) == SLURM_ERROR)
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_modify_msg(msg_ptr, type);
	*msg = NULL;
	return SLURM_ERROR;
}

static void _pack_node_state_msg(dbd_node_state_msg_t *msg,
				 uint16_t rpc_version, buf_t *buffer)
{
	if (rpc_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->hostlist, buffer);
		packstr(msg->reason, buffer);
		pack32(msg->reason_uid, buffer);
		pack16(msg->new_state, buffer);
		pack_time(msg->event_time, buffer);
		pack32(msg->state, buffer);
		packstr(msg->tres_str, buffer);
	}
}

static int _unpack_node_state_msg(dbd_node_state_msg_t **msg,
				  uint16_t rpc_version, buf_t *buffer)
{
	dbd_node_state_msg_t *msg_ptr;
	uint32_t uint32_tmp;

	msg_ptr = xmalloc(sizeof(dbd_node_state_msg_t));
	*msg = msg_ptr;

	msg_ptr->reason_uid = NO_VAL;

	if (rpc_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&msg_ptr->hostlist, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->reason,   &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->reason_uid, buffer);
		safe_unpack16(&msg_ptr->new_state, buffer);
		safe_unpack_time(&msg_ptr->event_time, buffer);
		safe_unpack32(&msg_ptr->state, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->tres_str,
				       &uint32_tmp, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_node_state_msg(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void _pack_register_ctld_msg(dbd_register_ctld_msg_t *msg,
				    uint16_t rpc_version, buf_t *buffer)
{
	if (rpc_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack16(msg->dimensions, buffer);
		pack32(msg->flags, buffer);
		pack32(msg->plugin_id_select, buffer);
		pack16(msg->port, buffer);
	}
}

static int _unpack_register_ctld_msg(dbd_register_ctld_msg_t **msg,
				     uint16_t rpc_version, buf_t *buffer)
{
	dbd_register_ctld_msg_t *msg_ptr = xmalloc(
		sizeof(dbd_register_ctld_msg_t));
	*msg = msg_ptr;
	if (rpc_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&msg_ptr->dimensions, buffer);
		safe_unpack32(&msg_ptr->flags, buffer);
		safe_unpack32(&msg_ptr->plugin_id_select, buffer);
		safe_unpack16(&msg_ptr->port, buffer);
	}
	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_register_ctld_msg(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void _pack_roll_usage_msg(dbd_roll_usage_msg_t *msg,
				 uint16_t rpc_version, buf_t *buffer)
{
	if (rpc_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack16(msg->archive_data, buffer);
		pack_time(msg->end, buffer);
		pack_time(msg->start, buffer);
	}
}

static int _unpack_roll_usage_msg(dbd_roll_usage_msg_t **msg,
				  uint16_t rpc_version, buf_t *buffer)
{
	dbd_roll_usage_msg_t *msg_ptr = xmalloc(sizeof(dbd_roll_usage_msg_t));

	*msg = msg_ptr;

	if (rpc_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&msg_ptr->archive_data, buffer);
		safe_unpack_time(&msg_ptr->end, buffer);
		safe_unpack_time(&msg_ptr->start, buffer);
	}
	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_roll_usage_msg(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void _pack_step_complete_msg(dbd_step_comp_msg_t *msg,
				    uint16_t rpc_version, buf_t *buffer)
{
	if (rpc_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack32(msg->assoc_id, buffer);
		pack64(msg->db_index, buffer);
		pack_time(msg->end_time, buffer);
		pack32(msg->exit_code, buffer);
		jobacctinfo_pack((struct jobacctinfo *)msg->jobacct,
				 rpc_version, PROTOCOL_TYPE_DBD, buffer);
		pack_time(msg->job_submit_time, buffer);
		packstr(msg->job_tres_alloc_str, buffer);
		pack32(msg->req_uid, buffer);
		pack_time(msg->start_time, buffer);
		pack16(msg->state, buffer);
		pack_step_id(&msg->step_id, buffer, rpc_version);
		pack32(msg->total_tasks, buffer);
	} else if (rpc_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->assoc_id, buffer);
		pack64(msg->db_index, buffer);
		pack_time(msg->end_time, buffer);
		pack32(msg->exit_code, buffer);
		jobacctinfo_pack((struct jobacctinfo *)msg->jobacct,
				 rpc_version, PROTOCOL_TYPE_DBD, buffer);
		pack32(msg->step_id.job_id, buffer);
		pack_time(msg->job_submit_time, buffer);
		packstr(msg->job_tres_alloc_str, buffer);
		pack32(msg->req_uid, buffer);
		pack_time(msg->start_time, buffer);
		pack16(msg->state, buffer);
		pack_old_step_id(msg->step_id.step_id, buffer);
		pack32(msg->total_tasks, buffer);
	}
}

static int _unpack_step_complete_msg(dbd_step_comp_msg_t **msg,
				     uint16_t rpc_version, buf_t *buffer)
{
	uint32_t uint32_tmp;
	dbd_step_comp_msg_t *msg_ptr = xmalloc(sizeof(dbd_step_comp_msg_t));
	*msg = msg_ptr;

	if (rpc_version >= SLURM_20_11_PROTOCOL_VERSION) {
		safe_unpack32(&msg_ptr->assoc_id, buffer);
		safe_unpack64(&msg_ptr->db_index, buffer);
		safe_unpack_time(&msg_ptr->end_time, buffer);
		safe_unpack32(&msg_ptr->exit_code, buffer);
		jobacctinfo_unpack((struct jobacctinfo **)&msg_ptr->jobacct,
				   rpc_version, PROTOCOL_TYPE_DBD, buffer, 1);
		safe_unpack_time(&msg_ptr->job_submit_time, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->job_tres_alloc_str,
				       &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->req_uid, buffer);
		safe_unpack_time(&msg_ptr->start_time, buffer);
		safe_unpack16(&msg_ptr->state, buffer);
		if (unpack_step_id_members(&msg_ptr->step_id, buffer,
					   rpc_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&msg_ptr->total_tasks, buffer);
	} else if (rpc_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg_ptr->assoc_id, buffer);
		safe_unpack64(&msg_ptr->db_index, buffer);
		safe_unpack_time(&msg_ptr->end_time, buffer);
		safe_unpack32(&msg_ptr->exit_code, buffer);
		jobacctinfo_unpack((struct jobacctinfo **)&msg_ptr->jobacct,
				   rpc_version, PROTOCOL_TYPE_DBD, buffer, 1);
		safe_unpack32(&msg_ptr->step_id.job_id, buffer);
		safe_unpack_time(&msg_ptr->job_submit_time, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->job_tres_alloc_str,
				       &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->req_uid, buffer);
		safe_unpack_time(&msg_ptr->start_time, buffer);
		safe_unpack16(&msg_ptr->state, buffer);
		safe_unpack32(&msg_ptr->step_id.step_id, buffer);
		convert_old_step_id(&msg_ptr->step_id.step_id);
		msg_ptr->step_id.step_het_comp = NO_VAL;
		safe_unpack32(&msg_ptr->total_tasks, buffer);
	} else
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	debug2("slurmdbd_unpack_step_complete_msg:"
	       "unpack_error: size_buf(buffer) %u",
	       size_buf(buffer));
	slurmdbd_free_step_complete_msg(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void _pack_step_start_msg(dbd_step_start_msg_t *msg,
				 uint16_t rpc_version,
				 buf_t *buffer)
{
	if (rpc_version >= SLURM_21_08_PROTOCOL_VERSION) {
		pack32(msg->assoc_id, buffer);
		pack64(msg->db_index, buffer);
		packstr(msg->container, buffer);
		packstr(msg->name, buffer);
		packstr(msg->nodes, buffer);
		packstr(msg->node_inx, buffer);
		pack32(msg->node_cnt, buffer);
		pack_time(msg->start_time, buffer);
		pack_time(msg->job_submit_time, buffer);
		pack32(msg->req_cpufreq_min, buffer);
		pack32(msg->req_cpufreq_max, buffer);
		pack32(msg->req_cpufreq_gov, buffer);
		pack_step_id(&msg->step_id, buffer, rpc_version);
		packstr(msg->submit_line, buffer);
		pack32(msg->task_dist, buffer);
		pack32(msg->total_tasks, buffer);
		packstr(msg->tres_alloc_str, buffer);
	} else if (rpc_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack32(msg->assoc_id, buffer);
		pack64(msg->db_index, buffer);
		packstr(msg->name, buffer);
		packstr(msg->nodes, buffer);
		packstr(msg->node_inx, buffer);
		pack32(msg->node_cnt, buffer);
		pack_time(msg->start_time, buffer);
		pack_time(msg->job_submit_time, buffer);
		pack32(msg->req_cpufreq_min, buffer);
		pack32(msg->req_cpufreq_max, buffer);
		pack32(msg->req_cpufreq_gov, buffer);
		pack_step_id(&msg->step_id, buffer, rpc_version);
		pack32(msg->task_dist, buffer);
		pack32(msg->total_tasks, buffer);
		packstr(msg->tres_alloc_str, buffer);
	} else if (rpc_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->assoc_id, buffer);
		pack64(msg->db_index, buffer);
		pack32(msg->step_id.job_id, buffer);
		packstr(msg->name, buffer);
		packstr(msg->nodes, buffer);
		packstr(msg->node_inx, buffer);
		pack32(msg->node_cnt, buffer);
		pack_time(msg->start_time, buffer);
		pack_time(msg->job_submit_time, buffer);
		pack32(msg->req_cpufreq_min, buffer);
		pack32(msg->req_cpufreq_max, buffer);
		pack32(msg->req_cpufreq_gov, buffer);
		pack_old_step_id(msg->step_id.step_id, buffer);
		pack32(msg->task_dist, buffer);
		pack32(msg->total_tasks, buffer);
		packstr(msg->tres_alloc_str, buffer);
	}
}

static int _unpack_step_start_msg(dbd_step_start_msg_t **msg,
				  uint16_t rpc_version, buf_t *buffer)
{
	uint32_t uint32_tmp = 0;
	dbd_step_start_msg_t *msg_ptr = xmalloc(sizeof(dbd_step_start_msg_t));
	*msg = msg_ptr;

	if (rpc_version >= SLURM_21_08_PROTOCOL_VERSION) {
		safe_unpack32(&msg_ptr->assoc_id, buffer);
		safe_unpack64(&msg_ptr->db_index, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->container, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->node_inx, &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->node_cnt, buffer);
		safe_unpack_time(&msg_ptr->start_time, buffer);
		safe_unpack_time(&msg_ptr->job_submit_time, buffer);
		safe_unpack32(&msg_ptr->req_cpufreq_min, buffer);
		safe_unpack32(&msg_ptr->req_cpufreq_max, buffer);
		safe_unpack32(&msg_ptr->req_cpufreq_gov, buffer);
		if (unpack_step_id_members(&msg_ptr->step_id, buffer,
					   rpc_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr_xmalloc(&msg_ptr->submit_line,
				       &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->task_dist, buffer);
		safe_unpack32(&msg_ptr->total_tasks, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->tres_alloc_str,
				       &uint32_tmp, buffer);
	} else if (rpc_version >= SLURM_20_11_PROTOCOL_VERSION) {
		safe_unpack32(&msg_ptr->assoc_id, buffer);
		safe_unpack64(&msg_ptr->db_index, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->node_inx, &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->node_cnt, buffer);
		safe_unpack_time(&msg_ptr->start_time, buffer);
		safe_unpack_time(&msg_ptr->job_submit_time, buffer);
		safe_unpack32(&msg_ptr->req_cpufreq_min, buffer);
		safe_unpack32(&msg_ptr->req_cpufreq_max, buffer);
		safe_unpack32(&msg_ptr->req_cpufreq_gov, buffer);
		if (unpack_step_id_members(&msg_ptr->step_id, buffer,
					   rpc_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&msg_ptr->task_dist, buffer);
		safe_unpack32(&msg_ptr->total_tasks, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->tres_alloc_str,
				       &uint32_tmp, buffer);
	} else if (rpc_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg_ptr->assoc_id, buffer);
		safe_unpack64(&msg_ptr->db_index, buffer);
		safe_unpack32(&msg_ptr->step_id.job_id, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->node_inx, &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->node_cnt, buffer);
		safe_unpack_time(&msg_ptr->start_time, buffer);
		safe_unpack_time(&msg_ptr->job_submit_time, buffer);
		safe_unpack32(&msg_ptr->req_cpufreq_min, buffer);
		safe_unpack32(&msg_ptr->req_cpufreq_max, buffer);
		safe_unpack32(&msg_ptr->req_cpufreq_gov, buffer);
		safe_unpack32(&msg_ptr->step_id.step_id, buffer);
		convert_old_step_id(&msg_ptr->step_id.step_id);
		msg_ptr->step_id.step_het_comp = NO_VAL;
		safe_unpack32(&msg_ptr->task_dist, buffer);
		safe_unpack32(&msg_ptr->total_tasks, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->tres_alloc_str,
				       &uint32_tmp, buffer);
	} else
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	debug2("slurmdbd_unpack_step_start_msg:"
	       "unpack_error: size_buf(buffer) %u",
	       size_buf(buffer));
	slurmdbd_free_step_start_msg(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void _pack_buffer(void *in, uint16_t rpc_version, buf_t *buffer)
{
	buf_t *object = (buf_t *) in;

	packmem(get_buf_data(object), get_buf_offset(object), buffer);
}

static int _unpack_buffer(void **out, uint16_t rpc_version, buf_t *buffer)
{
	buf_t *out_ptr = NULL;
	char *msg = NULL;
	uint32_t uint32_tmp;

	safe_unpackmem_xmalloc(&msg, &uint32_tmp, buffer);
	if (!(out_ptr = create_buf(msg, uint32_tmp)))
		goto unpack_error;
	*out = out_ptr;

	return SLURM_SUCCESS;

unpack_error:
	xfree(msg);
	slurmdbd_free_buffer(out_ptr);
	*out = NULL;
	return SLURM_ERROR;

}

extern void slurmdbd_pack_id_rc_msg(void *in, uint16_t rpc_version,
				    buf_t *buffer)
{
	dbd_id_rc_msg_t *msg = (dbd_id_rc_msg_t *)in;

	if (rpc_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->job_id, buffer);
		pack64(msg->db_index, buffer);
		pack32(msg->return_code, buffer);
	}
}

extern int slurmdbd_unpack_id_rc_msg(void **msg, uint16_t rpc_version,
				     buf_t *buffer)
{
	dbd_id_rc_msg_t *msg_ptr = xmalloc(sizeof(dbd_id_rc_msg_t));

	*msg = msg_ptr;

	if (rpc_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg_ptr->job_id, buffer);
		safe_unpack64(&msg_ptr->db_index, buffer);
		safe_unpack32(&msg_ptr->return_code, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_id_rc_msg(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

extern void slurmdbd_pack_usage_msg(dbd_usage_msg_t *msg, uint16_t rpc_version,
				    slurmdbd_msg_type_t type, buf_t *buffer)
{
	void (*my_rec) (void *object, uint16_t rpc_version, buf_t *buffer);

	switch (type) {
	case DBD_GET_ASSOC_USAGE:
	case DBD_GOT_ASSOC_USAGE:
		my_rec = slurmdb_pack_assoc_rec;
		break;
	case DBD_GET_CLUSTER_USAGE:
	case DBD_GOT_CLUSTER_USAGE:
		my_rec = slurmdb_pack_cluster_rec;
		break;
	case DBD_GET_WCKEY_USAGE:
	case DBD_GOT_WCKEY_USAGE:
		my_rec = slurmdb_pack_wckey_rec;
		break;
	default:
		fatal("Unknown pack type");
		return;
	}

	(*(my_rec))(msg->rec, rpc_version, buffer);
	pack_time(msg->start, buffer);
	pack_time(msg->end, buffer);
}

extern int slurmdbd_unpack_usage_msg(dbd_usage_msg_t **msg,
				     uint16_t rpc_version,
				     slurmdbd_msg_type_t type,
				     buf_t *buffer)
{
	dbd_usage_msg_t *msg_ptr = NULL;
	int (*my_rec) (void **object, uint16_t rpc_version, buf_t *buffer);

	msg_ptr = xmalloc(sizeof(dbd_usage_msg_t));
	*msg = msg_ptr;

	switch (type) {
	case DBD_GET_ASSOC_USAGE:
	case DBD_GOT_ASSOC_USAGE:
		my_rec = slurmdb_unpack_assoc_rec;
		break;
	case DBD_GET_CLUSTER_USAGE:
	case DBD_GOT_CLUSTER_USAGE:
		my_rec = slurmdb_unpack_cluster_rec;
		break;
	case DBD_GET_WCKEY_USAGE:
	case DBD_GOT_WCKEY_USAGE:
		my_rec = slurmdb_unpack_wckey_rec;
		break;
	default:
		fatal("Unknown pack type");
		return SLURM_ERROR;
	}

	if ((*(my_rec))(&msg_ptr->rec, rpc_version, buffer) == SLURM_ERROR)
		goto unpack_error;

	safe_unpack_time(&msg_ptr->start, buffer);
	safe_unpack_time(&msg_ptr->end, buffer);


	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_usage_msg(msg_ptr, type);
	*msg = NULL;
	return SLURM_ERROR;
}

extern void slurmdbd_pack_fini_msg(dbd_fini_msg_t *msg, uint16_t rpc_version,
				   buf_t *buffer)
{
	pack16(msg->close_conn, buffer);
	pack16(msg->commit, buffer);
}

extern int slurmdbd_unpack_fini_msg(dbd_fini_msg_t **msg, uint16_t rpc_version,
				    buf_t *buffer)
{
	dbd_fini_msg_t *msg_ptr = xmalloc(sizeof(dbd_fini_msg_t));
	*msg = msg_ptr;

	safe_unpack16(&msg_ptr->close_conn, buffer);
	safe_unpack16(&msg_ptr->commit, buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_fini_msg(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

extern void slurmdbd_pack_list_msg(dbd_list_msg_t *msg, uint16_t rpc_version,
				   slurmdbd_msg_type_t type, buf_t *buffer)
{
	int rc;
	void (*my_function) (void *object, uint16_t rpc_version, buf_t *buffer);

	switch (type) {
	case DBD_ADD_ACCOUNTS:
	case DBD_GOT_ACCOUNTS:
		my_function = slurmdb_pack_account_rec;
		break;
	case DBD_ADD_TRES:
	case DBD_GOT_TRES:
		my_function = slurmdb_pack_tres_rec;
		break;
	case DBD_ADD_ASSOCS:
	case DBD_GOT_ASSOCS:
	case DBD_GOT_PROBS:
		my_function = slurmdb_pack_assoc_rec;
		break;
	case DBD_ADD_CLUSTERS:
	case DBD_GOT_CLUSTERS:
		my_function = slurmdb_pack_cluster_rec;
		break;
	case DBD_ADD_FEDERATIONS:
	case DBD_GOT_FEDERATIONS:
		my_function = slurmdb_pack_federation_rec;
		break;
	case DBD_GOT_CONFIG:
		my_function = pack_config_key_pair;
		break;
	case DBD_GOT_JOBS:
	case DBD_FIX_RUNAWAY_JOB:
		my_function = slurmdb_pack_job_rec;
		break;
	case DBD_GOT_LIST:
		my_function = _slurmdbd_packstr;
		break;
	case DBD_ADD_QOS:
	case DBD_GOT_QOS:
		my_function = slurmdb_pack_qos_rec;
		break;
	case DBD_GOT_RESVS:
		my_function = slurmdb_pack_reservation_rec;
		break;
	case DBD_ADD_RES:
	case DBD_GOT_RES:
		my_function = slurmdb_pack_res_rec;
		break;
	case DBD_ADD_WCKEYS:
	case DBD_GOT_WCKEYS:
		my_function = slurmdb_pack_wckey_rec;
		break;
	case DBD_ADD_USERS:
	case DBD_GOT_USERS:
		my_function = slurmdb_pack_user_rec;
		break;
	case DBD_GOT_TXN:
		my_function = slurmdb_pack_txn_rec;
		break;
	case DBD_GOT_EVENTS:
		my_function = slurmdb_pack_event_rec;
		break;
	case DBD_SEND_MULT_JOB_START:
		slurm_pack_list_until(msg->my_list, _pack_job_start_msg,
				      buffer, MAX_MSG_SIZE, rpc_version);
		pack32(msg->return_code, buffer);
		return;
	case DBD_GOT_MULT_JOB_START:
		my_function = slurmdbd_pack_id_rc_msg;
		break;
	case DBD_SEND_MULT_MSG:
	case DBD_GOT_MULT_MSG:
		my_function = _pack_buffer;
		break;
	default:
		fatal("Unknown pack type");
		return;
	}

	if ((rc = slurm_pack_list(msg->my_list, my_function,
				  buffer, rpc_version)) != SLURM_SUCCESS)
		msg->return_code = rc;

	pack32(msg->return_code, buffer);
}

extern int slurmdbd_unpack_list_msg(dbd_list_msg_t **msg, uint16_t rpc_version,
				    slurmdbd_msg_type_t type, buf_t *buffer)
{
	dbd_list_msg_t *msg_ptr = NULL;
	int (*my_function) (void **object, uint16_t rpc_version, buf_t *buffer);
	void (*my_destroy) (void *object);

	switch (type) {
	case DBD_ADD_ACCOUNTS:
	case DBD_GOT_ACCOUNTS:
		my_function = slurmdb_unpack_account_rec;
		my_destroy = slurmdb_destroy_account_rec;
		break;
	case DBD_ADD_TRES:
	case DBD_GOT_TRES:
		my_function = slurmdb_unpack_tres_rec;
		my_destroy = slurmdb_destroy_tres_rec;
		break;
	case DBD_ADD_ASSOCS:
	case DBD_GOT_ASSOCS:
	case DBD_GOT_PROBS:
		my_function = slurmdb_unpack_assoc_rec;
		my_destroy = slurmdb_destroy_assoc_rec;
		break;
	case DBD_ADD_CLUSTERS:
	case DBD_GOT_CLUSTERS:
		my_function = slurmdb_unpack_cluster_rec;
		my_destroy = slurmdb_destroy_cluster_rec;
		break;
	case DBD_ADD_FEDERATIONS:
	case DBD_GOT_FEDERATIONS:
		my_function = slurmdb_unpack_federation_rec;
		my_destroy = slurmdb_destroy_federation_rec;
		break;
	case DBD_GOT_CONFIG:
		my_function = unpack_config_key_pair;
		my_destroy = destroy_config_key_pair;
		break;
	case DBD_GOT_JOBS:
	case DBD_FIX_RUNAWAY_JOB:
		my_function = slurmdb_unpack_job_rec;
		my_destroy = slurmdb_destroy_job_rec;
		break;
	case DBD_GOT_LIST:
		my_function = _slurmdbd_unpackstr;
		my_destroy = xfree_ptr;
		break;
	case DBD_ADD_QOS:
	case DBD_GOT_QOS:
		my_function = slurmdb_unpack_qos_rec;
		my_destroy = slurmdb_destroy_qos_rec;
		break;
	case DBD_GOT_RESVS:
		my_function = slurmdb_unpack_reservation_rec;
		my_destroy = slurmdb_destroy_reservation_rec;
		break;
	case DBD_ADD_RES:
	case DBD_GOT_RES:
		my_function = slurmdb_unpack_res_rec;
		my_destroy = slurmdb_destroy_res_rec;
		break;
	case DBD_ADD_WCKEYS:
	case DBD_GOT_WCKEYS:
		my_function = slurmdb_unpack_wckey_rec;
		my_destroy = slurmdb_destroy_wckey_rec;
		break;
	case DBD_ADD_USERS:
	case DBD_GOT_USERS:
		my_function = slurmdb_unpack_user_rec;
		my_destroy = slurmdb_destroy_user_rec;
		break;
	case DBD_GOT_TXN:
		my_function = slurmdb_unpack_txn_rec;
		my_destroy = slurmdb_destroy_txn_rec;
		break;
	case DBD_GOT_EVENTS:
		my_function = slurmdb_unpack_event_rec;
		my_destroy = slurmdb_destroy_event_rec;
		break;
	case DBD_SEND_MULT_JOB_START:
		my_function = _unpack_job_start_msg;
		my_destroy = slurmdbd_free_job_start_msg;
		break;
	case DBD_GOT_MULT_JOB_START:
		my_function = slurmdbd_unpack_id_rc_msg;
		my_destroy = slurmdbd_free_id_rc_msg;
		break;
	case DBD_SEND_MULT_MSG:
	case DBD_GOT_MULT_MSG:
		my_function = _unpack_buffer;
		my_destroy = slurmdbd_free_buffer;
		break;
	default:
		fatal("%s: Unknown unpack type", __func__);
		return SLURM_ERROR;
	}

	msg_ptr = xmalloc(sizeof(dbd_list_msg_t));
	*msg = msg_ptr;

	if (slurm_unpack_list(&msg_ptr->my_list, my_function, my_destroy,
			      buffer, rpc_version) != SLURM_SUCCESS)
		goto unpack_error;

	safe_unpack32(&msg_ptr->return_code, buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_list_msg(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

extern buf_t *pack_slurmdbd_msg(persist_msg_t *req, uint16_t rpc_version)
{
	buf_t *buffer;

	if (rpc_version < SLURM_MIN_PROTOCOL_VERSION) {
		error("slurmdbd: Invalid message version=%hu, type:%hu",
		      rpc_version, req->msg_type);
		return NULL;
	}

	buffer = init_buf(MAX_DBD_MSG_LEN);
	pack16(req->msg_type, buffer);

	switch (req->msg_type) {
	case REQUEST_PERSIST_INIT:
		slurm_persist_pack_init_req_msg(req->data, buffer);
		break;
	case PERSIST_RC:
		slurm_persist_pack_rc_msg(req->data, buffer, rpc_version);
		break;
	case DBD_ADD_ACCOUNTS:
	case DBD_ADD_TRES:
	case DBD_ADD_ASSOCS:
	case DBD_ADD_CLUSTERS:
	case DBD_ADD_FEDERATIONS:
	case DBD_ADD_RES:
	case DBD_ADD_USERS:
	case DBD_GOT_ACCOUNTS:
	case DBD_GOT_TRES:
	case DBD_GOT_ASSOCS:
	case DBD_GOT_CLUSTERS:
	case DBD_GOT_EVENTS:
	case DBD_GOT_FEDERATIONS:
	case DBD_GOT_JOBS:
	case DBD_GOT_LIST:
	case DBD_GOT_PROBS:
	case DBD_GOT_RES:
	case DBD_ADD_QOS:
	case DBD_GOT_QOS:
	case DBD_GOT_RESVS:
	case DBD_ADD_WCKEYS:
	case DBD_GOT_WCKEYS:
	case DBD_GOT_TXN:
	case DBD_GOT_USERS:
	case DBD_GOT_CONFIG:
	case DBD_SEND_MULT_JOB_START:
	case DBD_GOT_MULT_JOB_START:
	case DBD_SEND_MULT_MSG:
	case DBD_GOT_MULT_MSG:
	case DBD_FIX_RUNAWAY_JOB:
		slurmdbd_pack_list_msg(
			(dbd_list_msg_t *)req->data, rpc_version,
			req->msg_type, buffer);
		break;
	case DBD_ADD_ACCOUNT_COORDS:
	case DBD_REMOVE_ACCOUNT_COORDS:
		_pack_acct_coord_msg(
			(dbd_acct_coord_msg_t *)req->data, rpc_version,
			buffer);
		break;
	case DBD_ARCHIVE_LOAD:
		slurmdb_pack_archive_rec(req->data, rpc_version, buffer);
		break;
	case DBD_CLUSTER_TRES:
	case DBD_FLUSH_JOBS:
		_pack_cluster_tres_msg(
			(dbd_cluster_tres_msg_t *)req->data, rpc_version,
			buffer);
		break;
	case DBD_GET_ACCOUNTS:
	case DBD_GET_TRES:
	case DBD_GET_ASSOCS:
	case DBD_GET_CLUSTERS:
	case DBD_GET_EVENTS:
	case DBD_GET_FEDERATIONS:
	case DBD_GET_JOBS_COND:
	case DBD_GET_PROBS:
	case DBD_GET_QOS:
	case DBD_GET_RESVS:
	case DBD_GET_RES:
	case DBD_GET_TXN:
	case DBD_GET_USERS:
	case DBD_GET_WCKEYS:
	case DBD_REMOVE_ACCOUNTS:
	case DBD_REMOVE_ASSOCS:
	case DBD_REMOVE_CLUSTERS:
	case DBD_REMOVE_FEDERATIONS:
	case DBD_REMOVE_QOS:
	case DBD_REMOVE_RES:
	case DBD_REMOVE_WCKEYS:
	case DBD_REMOVE_USERS:
	case DBD_ARCHIVE_DUMP:
		_pack_cond_msg(
			(dbd_cond_msg_t *)req->data, rpc_version, req->msg_type,
			buffer);
		break;
	case DBD_GET_ASSOC_USAGE:
	case DBD_GOT_ASSOC_USAGE:
	case DBD_GET_CLUSTER_USAGE:
	case DBD_GOT_CLUSTER_USAGE:
	case DBD_GET_WCKEY_USAGE:
	case DBD_GOT_WCKEY_USAGE:
		slurmdbd_pack_usage_msg(
			(dbd_usage_msg_t *)req->data, rpc_version,
			req->msg_type, buffer);
		break;
	case DBD_FINI:
		slurmdbd_pack_fini_msg((dbd_fini_msg_t *)req->data,
				       rpc_version, buffer);
		break;
	case DBD_JOB_COMPLETE:
		_pack_job_complete_msg((dbd_job_comp_msg_t *)req->data,
				       rpc_version,
				       buffer);
		break;
	case DBD_JOB_START:
		_pack_job_start_msg(req->data, rpc_version, buffer);
		break;
	case DBD_ID_RC:
		slurmdbd_pack_id_rc_msg(req->data, rpc_version, buffer);
		break;
	case DBD_JOB_SUSPEND:
		_pack_job_suspend_msg(
			(dbd_job_suspend_msg_t *)req->data, rpc_version,
			buffer);
		break;
	case DBD_MODIFY_ACCOUNTS:
	case DBD_MODIFY_ASSOCS:
	case DBD_MODIFY_CLUSTERS:
	case DBD_MODIFY_FEDERATIONS:
	case DBD_MODIFY_JOB:
	case DBD_MODIFY_QOS:
	case DBD_MODIFY_RES:
	case DBD_MODIFY_USERS:
		_pack_modify_msg(
			(dbd_modify_msg_t *)req->data, rpc_version,
			req->msg_type, buffer);
		break;
	case DBD_NODE_STATE:
		_pack_node_state_msg(
			(dbd_node_state_msg_t *)req->data, rpc_version,
			buffer);
		break;
	case DBD_STEP_COMPLETE:
		_pack_step_complete_msg(
			(dbd_step_comp_msg_t *)req->data, rpc_version,
			buffer);
		break;
	case DBD_STEP_START:
		_pack_step_start_msg((dbd_step_start_msg_t *)req->data,
				     rpc_version,
				     buffer);
		break;
	case DBD_REGISTER_CTLD:
		_pack_register_ctld_msg(
			(dbd_register_ctld_msg_t *)req->data, rpc_version,
			buffer);
		break;
	case DBD_ROLL_USAGE:
		_pack_roll_usage_msg((dbd_roll_usage_msg_t *)req->data,
				     rpc_version,
				     buffer);
		break;
	case DBD_ADD_RESV:
	case DBD_REMOVE_RESV:
	case DBD_MODIFY_RESV:
		_pack_rec_msg(
			(dbd_rec_msg_t *)req->data, rpc_version, req->msg_type,
			buffer);
		break;
	case DBD_GET_CONFIG:
		packstr((char *)req->data, buffer);
		break;
	case DBD_RECONFIG:
	case DBD_GET_STATS:
	case DBD_CLEAR_STATS:
	case DBD_SHUTDOWN:
		break;
	default:
		error("slurmdbd: Invalid message type pack %u(%s:%u)",
		      req->msg_type,
		      slurmdbd_msg_type_2_str(req->msg_type, 1),
		      req->msg_type);
		free_buf(buffer);
		return NULL;
	}
	return buffer;
}

extern int unpack_slurmdbd_msg(persist_msg_t *resp, uint16_t rpc_version,
			       buf_t *buffer)
{
	int rc = SLURM_SUCCESS;
	slurm_msg_t msg;

	safe_unpack16(&resp->msg_type, buffer);

	if (rpc_version < SLURM_MIN_PROTOCOL_VERSION) {
		error("slurmdbd: Invalid message version=%hu, type:%hu",
		      rpc_version, resp->msg_type);
		return SLURM_ERROR;
	}

	switch (resp->msg_type) {
	case PERSIST_RC:
		slurm_msg_t_init(&msg);

		msg.protocol_version = rpc_version;
		msg.msg_type = resp->msg_type;

		rc = unpack_msg(&msg, buffer);

		resp->data = msg.data;
		break;
	case REQUEST_PERSIST_INIT:
		resp->data = xmalloc(sizeof(slurm_msg_t));
		slurm_msg_t_init(resp->data);
		rc = slurm_unpack_received_msg(
			(slurm_msg_t *)resp->data, 0, buffer);
		break;
	case DBD_ADD_ACCOUNTS:
	case DBD_ADD_TRES:
	case DBD_ADD_ASSOCS:
	case DBD_ADD_CLUSTERS:
	case DBD_ADD_FEDERATIONS:
	case DBD_ADD_RES:
	case DBD_ADD_USERS:
	case DBD_GOT_ACCOUNTS:
	case DBD_GOT_TRES:
	case DBD_GOT_ASSOCS:
	case DBD_GOT_CLUSTERS:
	case DBD_GOT_EVENTS:
	case DBD_GOT_FEDERATIONS:
	case DBD_GOT_JOBS:
	case DBD_GOT_LIST:
	case DBD_GOT_PROBS:
	case DBD_ADD_QOS:
	case DBD_GOT_QOS:
	case DBD_GOT_RESVS:
	case DBD_GOT_RES:
	case DBD_ADD_WCKEYS:
	case DBD_GOT_WCKEYS:
	case DBD_GOT_TXN:
	case DBD_GOT_USERS:
	case DBD_GOT_CONFIG:
	case DBD_SEND_MULT_JOB_START:
	case DBD_GOT_MULT_JOB_START:
	case DBD_SEND_MULT_MSG:
	case DBD_GOT_MULT_MSG:
	case DBD_FIX_RUNAWAY_JOB:
		rc = slurmdbd_unpack_list_msg(
			(dbd_list_msg_t **)&resp->data, rpc_version,
			resp->msg_type, buffer);
		break;
	case DBD_ADD_ACCOUNT_COORDS:
	case DBD_REMOVE_ACCOUNT_COORDS:
		rc = _unpack_acct_coord_msg(
			(dbd_acct_coord_msg_t **)&resp->data,
			rpc_version, buffer);
		break;
	case DBD_ARCHIVE_LOAD:
		rc = slurmdb_unpack_archive_rec(
			&resp->data, rpc_version, buffer);
		break;
	case DBD_CLUSTER_TRES:
	case DBD_FLUSH_JOBS:
		rc = _unpack_cluster_tres_msg(
			(dbd_cluster_tres_msg_t **)&resp->data,
			rpc_version, buffer);
		break;
	case DBD_GET_ACCOUNTS:
	case DBD_GET_TRES:
	case DBD_GET_ASSOCS:
	case DBD_GET_CLUSTERS:
	case DBD_GET_EVENTS:
	case DBD_GET_FEDERATIONS:
	case DBD_GET_JOBS_COND:
	case DBD_GET_PROBS:
	case DBD_GET_QOS:
	case DBD_GET_RESVS:
	case DBD_GET_RES:
	case DBD_GET_TXN:
	case DBD_GET_USERS:
	case DBD_GET_WCKEYS:
	case DBD_REMOVE_ACCOUNTS:
	case DBD_REMOVE_ASSOCS:
	case DBD_REMOVE_CLUSTERS:
	case DBD_REMOVE_FEDERATIONS:
	case DBD_REMOVE_QOS:
	case DBD_REMOVE_RES:
	case DBD_REMOVE_WCKEYS:
	case DBD_REMOVE_USERS:
	case DBD_ARCHIVE_DUMP:
		rc = _unpack_cond_msg(
			(dbd_cond_msg_t **)&resp->data, rpc_version,
			resp->msg_type, buffer);
		break;
	case DBD_GET_ASSOC_USAGE:
	case DBD_GOT_ASSOC_USAGE:
	case DBD_GET_CLUSTER_USAGE:
	case DBD_GOT_CLUSTER_USAGE:
	case DBD_GET_WCKEY_USAGE:
	case DBD_GOT_WCKEY_USAGE:
		rc = slurmdbd_unpack_usage_msg(
			(dbd_usage_msg_t **)&resp->data, rpc_version,
			resp->msg_type, buffer);
		break;
	case DBD_FINI:
		rc = slurmdbd_unpack_fini_msg((dbd_fini_msg_t **)&resp->data,
					      rpc_version,
					      buffer);
		break;
	case DBD_JOB_COMPLETE:
		rc = _unpack_job_complete_msg(
			(dbd_job_comp_msg_t **)&resp->data,
			rpc_version, buffer);
		break;
	case DBD_JOB_START:
		rc = _unpack_job_start_msg(
			&resp->data, rpc_version, buffer);
		break;
	case DBD_ID_RC:
		rc = slurmdbd_unpack_id_rc_msg(
			&resp->data, rpc_version, buffer);
		break;
	case DBD_JOB_SUSPEND:
		rc = _unpack_job_suspend_msg(
			(dbd_job_suspend_msg_t **)&resp->data, rpc_version,
			buffer);
		break;
	case DBD_MODIFY_ACCOUNTS:
	case DBD_MODIFY_ASSOCS:
	case DBD_MODIFY_CLUSTERS:
	case DBD_MODIFY_FEDERATIONS:
	case DBD_MODIFY_JOB:
	case DBD_MODIFY_QOS:
	case DBD_MODIFY_RES:
	case DBD_MODIFY_USERS:
		rc = _unpack_modify_msg(
			(dbd_modify_msg_t **)&resp->data,
			rpc_version,
			resp->msg_type,
			buffer);
		break;
	case DBD_NODE_STATE:
		rc = _unpack_node_state_msg(
			(dbd_node_state_msg_t **)&resp->data, rpc_version,
			buffer);
		break;
	case DBD_STEP_COMPLETE:
		rc = _unpack_step_complete_msg(
			(dbd_step_comp_msg_t **)&resp->data,
			rpc_version, buffer);
		break;
	case DBD_STEP_START:
		rc = _unpack_step_start_msg(
			(dbd_step_start_msg_t **)&resp->data,
			rpc_version, buffer);
		break;
	case DBD_REGISTER_CTLD:
		rc = _unpack_register_ctld_msg(
			(dbd_register_ctld_msg_t **)&resp->data,
			rpc_version, buffer);
		break;
	case DBD_ROLL_USAGE:
		rc = _unpack_roll_usage_msg(
			(dbd_roll_usage_msg_t **)&resp->data, rpc_version,
			buffer);
		break;
	case DBD_ADD_RESV:
	case DBD_REMOVE_RESV:
	case DBD_MODIFY_RESV:
		rc = _unpack_rec_msg(
			(dbd_rec_msg_t **)&resp->data, rpc_version,
			resp->msg_type, buffer);
		break;
	case DBD_GET_CONFIG:
		rc = _unpack_config_name(
			(char **)&resp->data, rpc_version, buffer);
		break;
	case DBD_RECONFIG:
	case DBD_GET_STATS:
	case DBD_CLEAR_STATS:
	case DBD_SHUTDOWN:
		/* No message to unpack */
		break;
	case DBD_GOT_STATS:
		rc = slurmdb_unpack_stats_msg(
			(void **)&resp->data, rpc_version, buffer);
		break;
	default:
		error("slurmdbd: Invalid message type unpack %u(%s)",
		      resp->msg_type,
		      slurmdbd_msg_type_2_str(resp->msg_type, 1));
		return SLURM_ERROR;
	}
	return rc;

unpack_error:
	return SLURM_ERROR;
}

