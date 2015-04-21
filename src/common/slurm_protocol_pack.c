/****************************************************************************\
 *  slurm_protocol_pack.c - functions to pack and unpack structures for RPCs
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010 SchedMD <http://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/api/slurm_pmi.h"
#include "src/common/bitstring.h"
#include "src/common/forward.h"
#include "src/common/job_options.h"
#include "src/common/log.h"
#include "src/common/node_select.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_acct_gather_energy.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_cred.h"
#include "src/common/slurm_ext_sensors.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/switch.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xassert.h"


#define _pack_job_info_msg(msg,buf)		_pack_buffer_msg(msg,buf)
#define _pack_job_step_info_msg(msg,buf)	_pack_buffer_msg(msg,buf)
#define _pack_block_info_resp_msg(msg,buf)	_pack_buffer_msg(msg,buf)
#define _pack_front_end_info_msg(msg,buf)	_pack_buffer_msg(msg,buf)
#define _pack_node_info_msg(msg,buf)		_pack_buffer_msg(msg,buf)
#define _pack_partition_info_msg(msg,buf)	_pack_buffer_msg(msg,buf)
#define _pack_stats_response_msg(msg,buf)	_pack_buffer_msg(msg,buf)
#define _pack_reserve_info_msg(msg,buf)		_pack_buffer_msg(msg,buf)

static void _pack_assoc_shares_object(void *in, Buf buffer,
				      uint16_t protocol_version);
static int _unpack_assoc_shares_object(void **object, Buf buffer,
				       uint16_t protocol_version);
static void _pack_shares_request_msg(shares_request_msg_t * msg, Buf buffer,
				     uint16_t protocol_version);
static int _unpack_shares_request_msg(shares_request_msg_t ** msg, Buf buffer,
				      uint16_t protocol_version);
static void _pack_shares_response_msg(shares_response_msg_t * msg, Buf buffer,
				      uint16_t protocol_version);
static int _unpack_shares_response_msg(shares_response_msg_t ** msg,
				       Buf buffer,
				       uint16_t protocol_version);
static void _pack_priority_factors_object(void *in, Buf buffer,
					  uint16_t protocol_version);
static int _unpack_priority_factors_object(void **object, Buf buffer,
					   uint16_t protocol_version);
static void _pack_priority_factors_request_msg(
	priority_factors_request_msg_t * msg, Buf buffer,
	uint16_t protocol_version);
static int _unpack_priority_factors_request_msg(
	priority_factors_request_msg_t ** msg, Buf buffer,
	uint16_t protocol_version);
static void _pack_priority_factors_response_msg(
	priority_factors_response_msg_t * msg, Buf buffer,
	uint16_t protocol_version);
static int _unpack_priority_factors_response_msg(
	priority_factors_response_msg_t ** msg, Buf buffer,
	uint16_t protocol_version);

static void _pack_update_node_msg(update_node_msg_t * msg, Buf buffer,
				  uint16_t protocol_version);
static int _unpack_update_node_msg(update_node_msg_t ** msg, Buf buffer,
				   uint16_t protocol_version);

static void
_pack_node_registration_status_msg(slurm_node_registration_status_msg_t *
				   msg, Buf buffer,
				   uint16_t protocol_version);
static int
_unpack_node_registration_status_msg(slurm_node_registration_status_msg_t
				     ** msg, Buf buffer,
				     uint16_t protocol_version);

static void _pack_job_ready_msg(job_id_msg_t * msg, Buf buffer,
				uint16_t protocol_version);
static int _unpack_job_ready_msg(job_id_msg_t ** msg_ptr, Buf buffer,
				 uint16_t protocol_version);

static void _pack_job_user_msg(job_user_id_msg_t * msg, Buf buffer,
			       uint16_t protocol_version);
static int _unpack_job_user_msg(job_user_id_msg_t ** msg_ptr, Buf buffer,
				uint16_t protocol_version);

static void
_pack_resource_allocation_response_msg(resource_allocation_response_msg_t *
				       msg, Buf buffer,
				       uint16_t protocol_version);
static int
_unpack_resource_allocation_response_msg(resource_allocation_response_msg_t
					 ** msg, Buf buffer,
					 uint16_t protocol_version);

static void
_pack_job_alloc_info_response_msg(job_alloc_info_response_msg_t * msg,
				  Buf buffer,
				  uint16_t protocol_version);
static int
_unpack_job_alloc_info_response_msg(job_alloc_info_response_msg_t ** msg,
				    Buf buffer,
				    uint16_t protocol_version);

static void _pack_submit_response_msg(submit_response_msg_t * msg,
				      Buf buffer,
				      uint16_t protocol_version);
static int _unpack_submit_response_msg(submit_response_msg_t ** msg,
				       Buf buffer,
				       uint16_t protocol_version);

static void _pack_node_info_request_msg(
	node_info_request_msg_t * msg, Buf buffer,
	uint16_t protocol_version);

static int _unpack_node_info_request_msg(
	node_info_request_msg_t ** msg, Buf bufer,
	uint16_t protocol_version);

static void _pack_node_info_single_msg(node_info_single_msg_t * msg,
				       Buf buffer, uint16_t protocol_version);

static int _unpack_node_info_single_msg(node_info_single_msg_t ** msg,
					Buf buffer, uint16_t protocol_version);

static int _unpack_node_info_msg(node_info_msg_t ** msg, Buf buffer,
				 uint16_t protocol_version);
static int _unpack_node_info_members(node_info_t * node, Buf buffer,
				     uint16_t protocol_version);

static void _pack_front_end_info_request_msg(
	front_end_info_request_msg_t * msg,
	Buf buffer, uint16_t protocol_version);
static int _unpack_front_end_info_request_msg(
	front_end_info_request_msg_t ** msg,
	Buf buffer, uint16_t protocol_version);
static int _unpack_front_end_info_msg(front_end_info_msg_t ** msg, Buf buffer,
				      uint16_t protocol_version);
static int _unpack_front_end_info_members(front_end_info_t *front_end,
					  Buf buffer,
					  uint16_t protocol_version);
static void _pack_update_front_end_msg(update_front_end_msg_t * msg,
				       Buf buffer, uint16_t protocol_version);
static int _unpack_update_front_end_msg(update_front_end_msg_t ** msg,
					Buf buffer, uint16_t protocol_version);

static void _pack_update_partition_msg(update_part_msg_t * msg, Buf buffer,
				       uint16_t protocol_version);
static int _unpack_update_partition_msg(update_part_msg_t ** msg, Buf buffer,
					uint16_t protocol_version);

static void _pack_delete_partition_msg(delete_part_msg_t * msg, Buf buffer,
				       uint16_t protocol_version);
static int _unpack_delete_partition_msg(delete_part_msg_t ** msg, Buf buffer,
					uint16_t protocol_version);

static void _pack_kill_job_msg(kill_job_msg_t * msg, Buf buffer,
			       uint16_t protocol_version);
static int _unpack_kill_job_msg(kill_job_msg_t ** msg, Buf buffer,
				uint16_t protocol_version);

static void _pack_signal_job_msg(signal_job_msg_t * msg, Buf buffer,
				 uint16_t protocol_version);
static int _unpack_signal_job_msg(signal_job_msg_t ** msg, Buf buffer,
				  uint16_t protocol_version);

static void _pack_epilog_comp_msg(epilog_complete_msg_t * msg, Buf buffer,
				  uint16_t protocol_version);
static int  _unpack_epilog_comp_msg(epilog_complete_msg_t ** msg, Buf buffer,
				    uint16_t protocol_version);

static void _pack_update_job_time_msg(job_time_msg_t * msg, Buf buffer,
				      uint16_t protocol_version);
static int _unpack_update_job_time_msg(job_time_msg_t ** msg, Buf buffer,
				       uint16_t protocol_version);

static void _pack_acct_gather_node_resp_msg(acct_gather_node_resp_msg_t * msg,
					    Buf buffer,
					    uint16_t protocol_version);
static int _unpack_acct_gather_node_resp_msg(acct_gather_node_resp_msg_t ** msg,
					     Buf buffer,
					     uint16_t protocol_version);

static void _pack_acct_gather_energy_req(acct_gather_energy_req_msg_t *msg,
					 Buf buffer, uint16_t protocol_version);
static int _unpack_acct_gather_energy_req(acct_gather_energy_req_msg_t **msg,
					  Buf buffer,
					  uint16_t protocol_version);

static void _pack_part_info_request_msg(part_info_request_msg_t * msg,
					Buf buffer, uint16_t protocol_version);
static int _unpack_part_info_request_msg(part_info_request_msg_t ** msg,
					 Buf buffer, uint16_t protocol_version);

static void _pack_resv_info_request_msg(resv_info_request_msg_t * msg,
					Buf buffer, uint16_t protocol_version);
static int _unpack_resv_info_request_msg(resv_info_request_msg_t **msg,
					 Buf buffer, uint16_t protocol_version);

static int _unpack_partition_info_msg(partition_info_msg_t ** msg,
				      Buf buffer, uint16_t protocol_version);
static int _unpack_partition_info_members(partition_info_t * part,
					  Buf buffer,
					  uint16_t protocol_version);

static int _unpack_reserve_info_msg(reserve_info_msg_t ** msg,
				    Buf buffer, uint16_t protocol_version);
static int _unpack_reserve_info_members(reserve_info_t * resv,
					Buf buffer, uint16_t protocol_version);

static void _pack_launch_tasks_request_msg(launch_tasks_request_msg_t *msg,
					   Buf buffer,
					   uint16_t protocol_version);
static int _unpack_launch_tasks_request_msg(
	launch_tasks_request_msg_t **msg_ptr, Buf buffer,
	uint16_t protocol_version);


static void _pack_task_user_managed_io_stream_msg(task_user_managed_io_msg_t *
						  msg, Buf buffer,
						  uint16_t protocol_version);
static int _unpack_task_user_managed_io_stream_msg(task_user_managed_io_msg_t **
						   msg_ptr, Buf buffer,
						   uint16_t protocol_version);

static void _pack_cancel_tasks_msg(kill_tasks_msg_t * msg, Buf buffer,
				   uint16_t protocol_version);
static int _unpack_cancel_tasks_msg(kill_tasks_msg_t ** msg_ptr, Buf buffer,
				    uint16_t protocol_version);

static void _pack_checkpoint_tasks_msg(checkpoint_tasks_msg_t * msg, Buf buffer,
				       uint16_t protocol_version);
static int _unpack_checkpoint_tasks_msg(checkpoint_tasks_msg_t ** msg_ptr,
					Buf buffer, uint16_t protocol_version);

static void _pack_launch_tasks_response_msg(launch_tasks_response_msg_t *msg,
					    Buf buffer,
					    uint16_t protocol_version);
static int _unpack_launch_tasks_response_msg(
	launch_tasks_response_msg_t **msg_ptr, Buf buffer,
	uint16_t protocol_version);

static void _pack_reboot_msg(reboot_msg_t * msg, Buf buffer,
			     uint16_t protocol_version);
static int _unpack_reboot_msg(reboot_msg_t ** msg_ptr, Buf buffer,
			      uint16_t protocol_version);

static void _pack_shutdown_msg(shutdown_msg_t * msg, Buf buffer,
			       uint16_t protocol_version);
static int _unpack_shutdown_msg(shutdown_msg_t ** msg_ptr, Buf buffer,
				uint16_t protocol_version);

static void _pack_reattach_tasks_request_msg(reattach_tasks_request_msg_t *,
					     Buf, uint16_t);
static int _unpack_reattach_tasks_request_msg(reattach_tasks_request_msg_t **,
					      Buf, uint16_t);

static void
_pack_reattach_tasks_response_msg(reattach_tasks_response_msg_t *,
				  Buf, uint16_t);
static int
_unpack_reattach_tasks_response_msg(reattach_tasks_response_msg_t **,
				    Buf, uint16_t);

static void _pack_task_exit_msg(task_exit_msg_t * msg, Buf buffer,
				uint16_t protocol_version);
static int _unpack_task_exit_msg(task_exit_msg_t ** msg_ptr, Buf buffer,
				 uint16_t protocol_version);

static void _pack_job_alloc_info_msg(job_alloc_info_msg_t * job_desc_ptr,
				     Buf buffer,
				     uint16_t protocol_version);
static int
_unpack_job_alloc_info_msg(job_alloc_info_msg_t **job_desc_buffer_ptr,
			   Buf buffer,
			   uint16_t protocol_version);

static void _pack_return_code_msg(return_code_msg_t * msg, Buf buffer,
				  uint16_t protocol_version);
static int _unpack_return_code_msg(return_code_msg_t ** msg, Buf buffer,
				   uint16_t protocol_version);
static void _pack_return_code2_msg(return_code2_msg_t * msg, Buf buffer,
				   uint16_t protocol_version);
static int _unpack_return_code2_msg(return_code_msg_t ** msg, Buf buffer,
				    uint16_t protocol_version);

static void _pack_slurm_ctl_conf_msg(slurm_ctl_conf_info_msg_t * build_ptr,
				     Buf buffer, uint16_t protocol_version);
static int _unpack_slurm_ctl_conf_msg(slurm_ctl_conf_info_msg_t **
				      build_buffer_ptr, Buf buffer,
				      uint16_t protocol_version);

static void _pack_job_info_request_msg(job_info_request_msg_t *
				       msg, Buf buffer,
				       uint16_t protocol_version);
static int _unpack_job_info_request_msg(job_info_request_msg_t**
					msg, Buf buffer,
					uint16_t protocol_version);

static void _pack_block_info_req_msg(block_info_request_msg_t *
				     msg, Buf buffer,
				     uint16_t protocol_version);
static int _unpack_block_info_req_msg(block_info_request_msg_t **
				      msg, Buf buffer,
				      uint16_t protocol_version);
static void _pack_block_info_msg(block_info_t *block_info, Buf buffer,
				 uint16_t protocol_version);
static int _unpack_block_info(block_info_t **block_info, Buf buffer,
			      uint16_t protocol_version);

static void _pack_job_step_info_req_msg(job_step_info_request_msg_t * msg,
					Buf buffer,
					uint16_t protocol_version);
static int _unpack_job_step_info_req_msg(job_step_info_request_msg_t **
					 msg, Buf buffer,
					 uint16_t protocol_version);
static int _unpack_job_step_info_response_msg(job_step_info_response_msg_t
					      ** msg, Buf buffer,
					      uint16_t protocol_version);
static int _unpack_job_step_info_members(job_step_info_t * step, Buf buffer,
					 uint16_t protocol_version);

static void _pack_complete_job_allocation_msg(
	complete_job_allocation_msg_t * msg, Buf buffer,
	uint16_t protocol_version);
static int _unpack_complete_job_allocation_msg(
	complete_job_allocation_msg_t ** msg_ptr, Buf buffer,
	uint16_t protocol_version);

static void _pack_complete_prolog_msg(
	complete_prolog_msg_t * msg, Buf buffer,
	uint16_t protocol_version);
static int _unpack_complete_prolog_msg(
	complete_prolog_msg_t ** msg_ptr, Buf buffer,
	uint16_t protocol_version);

static void _pack_complete_batch_script_msg(
	complete_batch_script_msg_t * msg, Buf buffer,
	uint16_t protocol_version);
static int _unpack_complete_batch_script_msg(
	complete_batch_script_msg_t ** msg_ptr, Buf buffer,
	uint16_t protocol_version);

static void _pack_job_step_stat(job_step_stat_t * msg, Buf buffer,
				uint16_t protocol_version);
static int _unpack_job_step_stat(job_step_stat_t ** msg_ptr, Buf buffer,
				 uint16_t protocol_version);

static void _pack_job_step_id_msg(job_step_id_msg_t * msg, Buf buffer,
				  uint16_t protocol_version);
static int _unpack_job_step_id_msg(job_step_id_msg_t ** msg_ptr, Buf buffer,
				   uint16_t protocol_version);

static void _pack_job_step_pids(job_step_pids_t *msg, Buf buffer,
				uint16_t protocol_version);
static int _unpack_job_step_pids(job_step_pids_t **msg, Buf buffer,
				 uint16_t protocol_version);

static void _pack_step_complete_msg(step_complete_msg_t * msg,
				    Buf buffer,
				    uint16_t protocol_version);
static int _unpack_step_complete_msg(step_complete_msg_t **
				     msg_ptr, Buf buffer,
				     uint16_t protocol_version);
static int _unpack_job_info_members(job_info_t * job, Buf buffer,
				    uint16_t protocol_version);

static void _pack_batch_job_launch_msg(batch_job_launch_msg_t * msg,
				       Buf buffer,
				       uint16_t protocol_version);
static int _unpack_batch_job_launch_msg(batch_job_launch_msg_t ** msg,
					Buf buffer,
					uint16_t protocol_version);

static void _pack_prolog_launch_msg(prolog_launch_msg_t * msg,
				Buf buffer, uint16_t protocol_version);
static int _unpack_prolog_launch_msg(prolog_launch_msg_t ** msg,
				Buf buffer, uint16_t protocol_version);

static void _pack_job_desc_msg(job_desc_msg_t * job_desc_ptr, Buf buffer,
			       uint16_t protocol_version);
static int _unpack_job_desc_msg(job_desc_msg_t ** job_desc_buffer_ptr,
				Buf buffer,
				uint16_t protocol_version);
static int _unpack_job_info_msg(job_info_msg_t ** msg, Buf buffer,
				uint16_t protocol_version);

static void _pack_last_update_msg(last_update_msg_t * msg, Buf buffer,
				  uint16_t protocol_version);
static int _unpack_last_update_msg(last_update_msg_t ** msg, Buf buffer,
				   uint16_t protocol_version);

static void _pack_slurm_addr_array(slurm_addr_t * slurm_address,
				   uint32_t size_val, Buf buffer,
				   uint16_t protocol_version);
static int _unpack_slurm_addr_array(slurm_addr_t ** slurm_address,
				    uint32_t * size_val, Buf buffer,
				    uint16_t protocol_version);

static void _pack_ret_list(List ret_list, uint16_t size_val, Buf buffer,
			   uint16_t protocol_version);
static int _unpack_ret_list(List *ret_list, uint16_t size_val, Buf buffer,
			    uint16_t protocol_version);

static void _pack_job_id_request_msg(job_id_request_msg_t * msg, Buf buffer,
				     uint16_t protocol_version);
static int
_unpack_job_id_request_msg(job_id_request_msg_t ** msg, Buf buffer,
			   uint16_t protocol_version);

static void _pack_job_id_response_msg(job_id_response_msg_t * msg, Buf buffer,
				      uint16_t protocol_version);
static int  _unpack_job_id_response_msg(job_id_response_msg_t ** msg,
					Buf buffer,
					uint16_t protocol_version);

static void _pack_job_step_kill_msg(job_step_kill_msg_t * msg, Buf buffer,
				    uint16_t protocol_version);
static int  _unpack_job_step_kill_msg(job_step_kill_msg_t ** msg_ptr,
				      Buf buffer,
				      uint16_t protocol_version);

static void _pack_srun_exec_msg(srun_exec_msg_t * msg, Buf buffer,
				uint16_t protocol_version);
static int  _unpack_srun_exec_msg(srun_exec_msg_t ** msg_ptr, Buf buffer,
				  uint16_t protocol_version);

static void _pack_srun_ping_msg(srun_ping_msg_t * msg, Buf buffer,
				uint16_t protocol_version);
static int  _unpack_srun_ping_msg(srun_ping_msg_t ** msg_ptr, Buf buffer,
				  uint16_t protocol_version);

static void _pack_srun_node_fail_msg(srun_node_fail_msg_t * msg, Buf buffer,
				     uint16_t protocol_version);
static int  _unpack_srun_node_fail_msg(srun_node_fail_msg_t ** msg_ptr,
				       Buf buffer,
				       uint16_t protocol_version);

static void _pack_srun_step_missing_msg(srun_step_missing_msg_t * msg,
					Buf buffer,
					uint16_t protocol_version);
static int  _unpack_srun_step_missing_msg(srun_step_missing_msg_t ** msg_ptr,
					  Buf buffer,
					  uint16_t protocol_version);

static void _pack_srun_timeout_msg(srun_timeout_msg_t * msg, Buf buffer,
				   uint16_t protocol_version);
static int  _unpack_srun_timeout_msg(srun_timeout_msg_t ** msg_ptr,
				     Buf buffer,
				     uint16_t protocol_version);

static void _pack_srun_user_msg(srun_user_msg_t * msg, Buf buffer,
				uint16_t protocol_version);
static int  _unpack_srun_user_msg(srun_user_msg_t ** msg_ptr, Buf buffer,
				  uint16_t protocol_version);

static void _pack_checkpoint_msg(checkpoint_msg_t *msg, Buf buffer,
				 uint16_t protocol_version);
static int  _unpack_checkpoint_msg(checkpoint_msg_t **msg_ptr, Buf buffer,
				   uint16_t protocol_version);

static void _pack_checkpoint_resp_msg(checkpoint_resp_msg_t *msg, Buf buffer,
				      uint16_t protocol_version);
static int  _unpack_checkpoint_resp_msg(checkpoint_resp_msg_t **msg_ptr,
					Buf buffer,
					uint16_t protocol_version);

static void _pack_checkpoint_comp(checkpoint_comp_msg_t *msg, Buf buffer,
				  uint16_t protocol_version);
static int  _unpack_checkpoint_comp(checkpoint_comp_msg_t **msg_ptr,
				    Buf buffer,
				    uint16_t protocol_version);

static void _pack_checkpoint_task_comp(checkpoint_task_comp_msg_t *msg,
				       Buf buffer,
				       uint16_t protocol_version);
static int  _unpack_checkpoint_task_comp(checkpoint_task_comp_msg_t **msg_ptr,
					 Buf buffer,
					 uint16_t protocol_version);

static void _pack_suspend_msg(suspend_msg_t *msg, Buf buffer,
			      uint16_t protocol_version);
static int  _unpack_suspend_msg(suspend_msg_t **msg_ptr, Buf buffer,
				uint16_t protocol_version);

static void _pack_suspend_int_msg(suspend_int_msg_t *msg, Buf buffer,
				  uint16_t protocol_version);
static int  _unpack_suspend_int_msg(suspend_int_msg_t **msg_ptr, Buf buffer,
				    uint16_t protocol_version);

static void _pack_buffer_msg(slurm_msg_t * msg, Buf buffer);

static void _pack_kvs_host_rec(struct kvs_hosts *msg_ptr, Buf buffer,
			       uint16_t protocol_version);
static int  _unpack_kvs_host_rec(struct kvs_hosts *msg_ptr, Buf buffer,
				 uint16_t protocol_version);

static void _pack_kvs_rec(struct kvs_comm *msg_ptr, Buf buffer,
			  uint16_t protocol_version);
static int  _unpack_kvs_rec(struct kvs_comm **msg_ptr, Buf buffer,
			    uint16_t protocol_version);

static void _pack_kvs_data(struct kvs_comm_set *msg_ptr, Buf buffer,
			   uint16_t protocol_version);
static int  _unpack_kvs_data(struct kvs_comm_set **msg_ptr, Buf buffer,
			     uint16_t protocol_version);

static void _pack_kvs_get(kvs_get_msg_t *msg_ptr, Buf buffer,
			  uint16_t protocol_version);
static int  _unpack_kvs_get(kvs_get_msg_t **msg_ptr, Buf buffer,
			    uint16_t protocol_version);

static void _pack_file_bcast(file_bcast_msg_t * msg , Buf buffer,
			     uint16_t protocol_version);
static int _unpack_file_bcast(file_bcast_msg_t ** msg_ptr , Buf buffer,
			      uint16_t protocol_version);

static void _pack_trigger_msg(trigger_info_msg_t *msg , Buf buffer,
			      uint16_t protocol_version);
static int  _unpack_trigger_msg(trigger_info_msg_t ** msg_ptr , Buf buffer,
				uint16_t protocol_version);

static void _pack_slurmd_status(slurmd_status_t *msg, Buf buffer,
				uint16_t protocol_version);
static int  _unpack_slurmd_status(slurmd_status_t **msg_ptr, Buf buffer,
				  uint16_t protocol_version);

static void _pack_job_notify(job_notify_msg_t *msg, Buf buffer,
			     uint16_t protocol_version);
static int  _unpack_job_notify(job_notify_msg_t **msg_ptr, Buf buffer,
			       uint16_t protocol_version);

static void _pack_set_debug_flags_msg(set_debug_flags_msg_t * msg, Buf buffer,
				      uint16_t protocol_version);
static int _unpack_set_debug_flags_msg(set_debug_flags_msg_t ** msg_ptr,
				       Buf buffer,
				       uint16_t protocol_version);

static void _pack_set_debug_level_msg(set_debug_level_msg_t * msg, Buf buffer,
				      uint16_t protocol_version);
static int _unpack_set_debug_level_msg(set_debug_level_msg_t ** msg_ptr,
				       Buf buffer,
				       uint16_t protocol_version);

static void _pack_will_run_response_msg(will_run_response_msg_t *msg, Buf buffer,
					uint16_t protocol_version);
static int  _unpack_will_run_response_msg(will_run_response_msg_t ** msg_ptr,
					  Buf buffer,
					  uint16_t protocol_version);

static void _pack_accounting_update_msg(accounting_update_msg_t *msg,
					Buf buffer,
					uint16_t protocol_version);
static int _unpack_accounting_update_msg(accounting_update_msg_t **msg,
					 Buf buffer,
					 uint16_t protocol_version);

static void _pack_update_resv_msg(resv_desc_msg_t * msg, Buf buffer,
				  uint16_t protocol_version);
static int  _unpack_update_resv_msg(resv_desc_msg_t ** msg, Buf buffer,
				    uint16_t protocol_version);
static void _pack_resv_name_msg(reservation_name_msg_t * msg, Buf buffer,
				uint16_t protocol_version);
static int  _unpack_resv_name_msg(reservation_name_msg_t ** msg, Buf buffer,
				  uint16_t protocol_version);

static void _pack_topo_info_msg(topo_info_response_msg_t *msg, Buf buffer,
				uint16_t protocol_version);
static int  _unpack_topo_info_msg(topo_info_response_msg_t **msg,
				  Buf buffer,
				  uint16_t protocol_version);

static void _pack_job_sbcast_cred_msg(job_sbcast_cred_msg_t *msg, Buf buffer,
				      uint16_t protocol_version);
static int  _unpack_job_sbcast_cred_msg(job_sbcast_cred_msg_t **msg,
					Buf buffer,
					uint16_t protocol_version);

static void _pack_update_job_step_msg(step_update_request_msg_t * msg,
				      Buf buffer, uint16_t protocol_version);
static int _unpack_update_job_step_msg(step_update_request_msg_t ** msg_ptr,
				       Buf buffer, uint16_t protocol_version);

static void _pack_spank_env_request_msg(spank_env_request_msg_t * msg,
					Buf buffer, uint16_t protocol_version);
static int _unpack_spank_env_request_msg(spank_env_request_msg_t ** msg_ptr,
					 Buf buffer, uint16_t protocol_version);

static void _pack_spank_env_responce_msg(spank_env_responce_msg_t * msg,
					 Buf buffer, uint16_t protocol_version);
static int _unpack_spank_env_responce_msg(spank_env_responce_msg_t ** msg_ptr,
					  Buf buffer, uint16_t protocol_version);


static void _pack_stats_request_msg(stats_info_request_msg_t *msg, Buf buffer,
				    uint16_t protocol_version);
static int  _unpack_stats_request_msg(stats_info_request_msg_t **msg_ptr,
				      Buf buffer, uint16_t protocol_version);
static int  _unpack_stats_response_msg(stats_info_response_msg_t **msg_ptr,
				       Buf buffer, uint16_t protocol_version);

static void _pack_forward_data_msg(forward_data_msg_t *msg,
				   Buf buffer, uint16_t protocol_version);
static int _unpack_forward_data_msg(forward_data_msg_t **msg_ptr,
				    Buf buffer, uint16_t protocol_version);

static void _pack_ping_slurmd_resp(ping_slurmd_resp_msg_t *msg,
				   Buf buffer, uint16_t protocol_version);
static int _unpack_ping_slurmd_resp(ping_slurmd_resp_msg_t **msg_ptr,
				    Buf buffer, uint16_t protocol_version);

static void _pack_license_info_request_msg(license_info_request_msg_t *msg,
                                           Buf buffer,
                                           uint16_t protocol_version);
static int _unpack_license_info_request_msg(license_info_request_msg_t **msg,
                                            Buf buffer,
                                            uint16_t protocol_version);
static inline void _pack_license_info_msg(slurm_msg_t *msg, Buf buffer);
static int _unpack_license_info_msg(license_info_msg_t **msg,
                                    Buf buffer,
                                    uint16_t protocol_version);

static void _pack_job_requeue_msg(requeue_msg_t *msg, Buf buf,
				  uint16_t protocol_version);
static int  _unpack_job_requeue_msg(requeue_msg_t **msg, Buf buf,
				    uint16_t protocol_version);

static void _pack_job_array_resp_msg(job_array_resp_msg_t *msg, Buf buffer,
				     uint16_t protocol_version);
static int  _unpack_job_array_resp_msg(job_array_resp_msg_t **msg, Buf buffer,
				       uint16_t protocol_version);

/* pack_header
 * packs a slurm protocol header that precedes every slurm message
 * IN header - the header structure to pack
 * IN/OUT buffer - destination of the pack, contains pointers that are
 *			automatically updated
 */
void
pack_header(header_t * header, Buf buffer)
{
	pack16((uint16_t)header->version, buffer);
	pack16((uint16_t)header->flags, buffer);
	pack16((uint16_t)header->msg_type, buffer);
	pack32((uint32_t)header->body_length, buffer);
	pack16((uint16_t)header->forward.cnt, buffer);
	if (header->forward.cnt > 0) {
		packstr(header->forward.nodelist, buffer);
		pack32((uint32_t)header->forward.timeout, buffer);
	}
	pack16((uint16_t)header->ret_cnt, buffer);
	if (header->ret_cnt > 0) {
		_pack_ret_list(header->ret_list,
			       header->ret_cnt, buffer, header->version);
	}
	slurm_pack_slurm_addr(&header->orig_addr, buffer);
}

/* unpack_header
 * unpacks a slurm protocol header that precedes every slurm message
 * OUT header - the header structure to unpack
 * IN/OUT buffer - source of the unpack data, contains pointers that are
 *			automatically updated
 * RET 0 or error code
 */
int
unpack_header(header_t * header, Buf buffer)
{
	uint32_t uint32_tmp = 0;

	memset(header, 0, sizeof(header_t));
	forward_init(&header->forward, NULL);
	header->ret_list = NULL;
	safe_unpack16(&header->version, buffer);
	safe_unpack16(&header->flags, buffer);
	safe_unpack16(&header->msg_type, buffer);
	safe_unpack32(&header->body_length, buffer);
	safe_unpack16(&header->forward.cnt, buffer);
	if (header->forward.cnt > 0) {
		safe_unpackstr_xmalloc(&header->forward.nodelist,
				       &uint32_tmp, buffer);
		safe_unpack32(&header->forward.timeout, buffer);
	}

	safe_unpack16(&header->ret_cnt, buffer);
	if (header->ret_cnt > 0) {
		if (_unpack_ret_list(&(header->ret_list),
				     header->ret_cnt, buffer, header->version))
			goto unpack_error;
	} else {
		header->ret_list = NULL;
	}
	slurm_unpack_slurm_addr_no_alloc(&header->orig_addr, buffer);

	return SLURM_SUCCESS;

unpack_error:
	error("unpacking header");
	destroy_forward(&header->forward);
	if (header->ret_list)
		list_destroy(header->ret_list);
	return SLURM_ERROR;
}


/* pack_msg
 * packs a generic slurm protocol message body
 * IN msg - the body structure to pack (note: includes message type)
 * IN/OUT buffer - destination of the pack, contains pointers that are
 *			automatically updated
 * RET 0 or error code
 */
int
pack_msg(slurm_msg_t const *msg, Buf buffer)
{
	switch (msg->msg_type) {
	case REQUEST_NODE_INFO:
		_pack_node_info_request_msg((node_info_request_msg_t *)
					    msg->data, buffer,
					    msg->protocol_version);
		break;
	case REQUEST_NODE_INFO_SINGLE:
		_pack_node_info_single_msg((node_info_single_msg_t *)
					   msg->data, buffer,
					   msg->protocol_version);
		break;
	case REQUEST_PARTITION_INFO:
		_pack_part_info_request_msg((part_info_request_msg_t *)
					    msg->data, buffer,
					    msg->protocol_version);
		break;
	case REQUEST_RESERVATION_INFO:
		_pack_resv_info_request_msg((resv_info_request_msg_t *)
					    msg->data, buffer,
					    msg->protocol_version);
		break;
	case REQUEST_BUILD_INFO:
	case REQUEST_ACCTING_INFO:
		_pack_last_update_msg((last_update_msg_t *)
				      msg->data, buffer,
				      msg->protocol_version);
		break;
	case RESPONSE_BUILD_INFO:
		_pack_slurm_ctl_conf_msg((slurm_ctl_conf_info_msg_t *)
					 msg->data, buffer,
					 msg->protocol_version);
		break;
	case RESPONSE_JOB_INFO:
		_pack_job_info_msg((slurm_msg_t *) msg, buffer);
		break;
	case RESPONSE_PARTITION_INFO:
		_pack_partition_info_msg((slurm_msg_t *) msg, buffer);
		break;
	case RESPONSE_NODE_INFO:
		_pack_node_info_msg((slurm_msg_t *) msg, buffer);
		break;
	case MESSAGE_NODE_REGISTRATION_STATUS:
		_pack_node_registration_status_msg(
			(slurm_node_registration_status_msg_t *) msg->data,
			buffer,
			msg->protocol_version);
		break;
	case RESPONSE_ACCT_GATHER_UPDATE:
	case RESPONSE_ACCT_GATHER_ENERGY:
		_pack_acct_gather_node_resp_msg(
			(acct_gather_node_resp_msg_t *) msg->data,
			buffer, msg->protocol_version);
		break;
	case REQUEST_RESOURCE_ALLOCATION:
	case REQUEST_SUBMIT_BATCH_JOB:
	case REQUEST_JOB_WILL_RUN:
	case REQUEST_UPDATE_JOB:
		_pack_job_desc_msg((job_desc_msg_t *)
				   msg->data, buffer,
				   msg->protocol_version);
		break;
	case REQUEST_UPDATE_JOB_STEP:
		_pack_update_job_step_msg((step_update_request_msg_t *)
					  msg->data, buffer,
					  msg->protocol_version);
	case REQUEST_JOB_END_TIME:
	case REQUEST_JOB_ALLOCATION_INFO:
	case REQUEST_JOB_ALLOCATION_INFO_LITE:
	case REQUEST_JOB_SBCAST_CRED:
		_pack_job_alloc_info_msg((job_alloc_info_msg_t *) msg->data,
					 buffer,
					 msg->protocol_version);
		break;
	case REQUEST_NODE_REGISTRATION_STATUS:
	case REQUEST_RECONFIGURE:
	case REQUEST_SHUTDOWN_IMMEDIATE:
	case REQUEST_PING:
	case REQUEST_CONTROL:
	case REQUEST_TAKEOVER:
	case REQUEST_DAEMON_STATUS:
	case REQUEST_HEALTH_CHECK:
	case REQUEST_ACCT_GATHER_UPDATE:
	case ACCOUNTING_FIRST_REG:
	case ACCOUNTING_REGISTER_CTLD:
	case REQUEST_TOPO_INFO:
		/* Message contains no body/information */
		break;
	case REQUEST_ACCT_GATHER_ENERGY:
		_pack_acct_gather_energy_req(
			(acct_gather_energy_req_msg_t *)msg->data,
			buffer, msg->protocol_version);
		break;
	case REQUEST_REBOOT_NODES:
		_pack_reboot_msg((reboot_msg_t *)msg->data, buffer,
				 msg->protocol_version);
		break;
	case REQUEST_SHUTDOWN:
		_pack_shutdown_msg((shutdown_msg_t *) msg->data, buffer,
				   msg->protocol_version);
		break;
	case RESPONSE_SUBMIT_BATCH_JOB:
		_pack_submit_response_msg((submit_response_msg_t *)
					  msg->data, buffer,
					  msg->protocol_version);
		break;
	case RESPONSE_JOB_ALLOCATION_INFO_LITE:
	case RESPONSE_RESOURCE_ALLOCATION:
		_pack_resource_allocation_response_msg
			((resource_allocation_response_msg_t *) msg->data,
			 buffer,
			 msg->protocol_version);
		break;
	case RESPONSE_JOB_WILL_RUN:
		_pack_will_run_response_msg((will_run_response_msg_t *)
					    msg->data, buffer,
					    msg->protocol_version);
		break;
	case RESPONSE_JOB_ALLOCATION_INFO:
		_pack_job_alloc_info_response_msg(
			(job_alloc_info_response_msg_t *)
			msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_UPDATE_FRONT_END:
		_pack_update_front_end_msg((update_front_end_msg_t *) msg->data,
					   buffer, msg->protocol_version);
		break;
	case REQUEST_UPDATE_NODE:
		_pack_update_node_msg((update_node_msg_t *) msg->data,
				      buffer,
				      msg->protocol_version);
		break;
	case REQUEST_CREATE_PARTITION:
	case REQUEST_UPDATE_PARTITION:
		_pack_update_partition_msg((update_part_msg_t *) msg->
					   data, buffer,
					   msg->protocol_version);
		break;
	case REQUEST_DELETE_PARTITION:
		_pack_delete_partition_msg((delete_part_msg_t *) msg->
					   data, buffer,
					   msg->protocol_version);
		break;
	case REQUEST_CREATE_RESERVATION:
	case REQUEST_UPDATE_RESERVATION:
		_pack_update_resv_msg((resv_desc_msg_t *) msg->
				      data, buffer,
				      msg->protocol_version);
		break;
	case RESPONSE_RESERVATION_INFO:
		_pack_reserve_info_msg((slurm_msg_t *) msg, buffer);
		break;
	case REQUEST_DELETE_RESERVATION:
	case RESPONSE_CREATE_RESERVATION:
		_pack_resv_name_msg((reservation_name_msg_t *) msg->
				    data, buffer,
				    msg->protocol_version);
		break;
	case REQUEST_UPDATE_BLOCK:
		_pack_block_info_msg((block_info_t *)msg->data, buffer,
				     msg->protocol_version);
		break;
	case REQUEST_REATTACH_TASKS:
		_pack_reattach_tasks_request_msg(
			(reattach_tasks_request_msg_t *) msg->data, buffer,
			msg->protocol_version);
		break;
	case RESPONSE_REATTACH_TASKS:
		_pack_reattach_tasks_response_msg(
			(reattach_tasks_response_msg_t *) msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_LAUNCH_TASKS:
		_pack_launch_tasks_request_msg(
			(launch_tasks_request_msg_t *) msg->data,
			buffer,
			msg->protocol_version);
		break;
	case RESPONSE_LAUNCH_TASKS:
		_pack_launch_tasks_response_msg((launch_tasks_response_msg_t
						 *) msg->data, buffer,
						msg->protocol_version);
		break;
	case TASK_USER_MANAGED_IO_STREAM:
		_pack_task_user_managed_io_stream_msg(
			(task_user_managed_io_msg_t *) msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_SIGNAL_TASKS:
	case REQUEST_TERMINATE_TASKS:
		_pack_cancel_tasks_msg((kill_tasks_msg_t *) msg->data,
				       buffer,
				       msg->protocol_version);
		break;
	case REQUEST_CHECKPOINT_TASKS:
		_pack_checkpoint_tasks_msg((checkpoint_tasks_msg_t *) msg->data,
					   buffer,
					   msg->protocol_version);
		break;
	case REQUEST_JOB_STEP_INFO:
		_pack_job_step_info_req_msg((job_step_info_request_msg_t
					     *) msg->data, buffer,
					    msg->protocol_version);
		break;
	case REQUEST_JOB_INFO:
		_pack_job_info_request_msg((job_info_request_msg_t *)
					   msg->data, buffer,
					   msg->protocol_version);
		break;
	case REQUEST_CANCEL_JOB_STEP:
	case REQUEST_KILL_JOB:
	case SRUN_STEP_SIGNAL:
		_pack_job_step_kill_msg((job_step_kill_msg_t *)
					msg->data, buffer,
					msg->protocol_version);
		break;
	case REQUEST_COMPLETE_JOB_ALLOCATION:
		_pack_complete_job_allocation_msg(
			(complete_job_allocation_msg_t *)msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_COMPLETE_PROLOG:
		_pack_complete_prolog_msg(
			(complete_prolog_msg_t *)msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_COMPLETE_BATCH_JOB:
	case REQUEST_COMPLETE_BATCH_SCRIPT:
		_pack_complete_batch_script_msg(
			(complete_batch_script_msg_t *)msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_STEP_COMPLETE:
		_pack_step_complete_msg((step_complete_msg_t *)msg->data,
					buffer,
					msg->protocol_version);
		break;
	case RESPONSE_JOB_STEP_STAT:
		_pack_job_step_stat((job_step_stat_t *) msg->data,
				    buffer,
				    msg->protocol_version);
		break;
	case REQUEST_STEP_LAYOUT:
	case REQUEST_JOB_STEP_STAT:
	case REQUEST_JOB_STEP_PIDS:
		_pack_job_step_id_msg((job_step_id_msg_t *)msg->data, buffer,
				      msg->protocol_version);
		break;
	case RESPONSE_STEP_LAYOUT:
		pack_slurm_step_layout((slurm_step_layout_t *)msg->data,
				       buffer,
				       msg->protocol_version);
		break;
	case RESPONSE_JOB_STEP_PIDS:
		_pack_job_step_pids((job_step_pids_t *)msg->data,
				    buffer,
				    msg->protocol_version);
		break;
	case REQUEST_SIGNAL_JOB:
		_pack_signal_job_msg((signal_job_msg_t *) msg->data, buffer,
				     msg->protocol_version);
		break;
	case REQUEST_ABORT_JOB:
	case REQUEST_KILL_PREEMPTED:
	case REQUEST_KILL_TIMELIMIT:
	case REQUEST_TERMINATE_JOB:
		_pack_kill_job_msg((kill_job_msg_t *) msg->data, buffer,
				   msg->protocol_version);
		break;
	case MESSAGE_EPILOG_COMPLETE:
		_pack_epilog_comp_msg((epilog_complete_msg_t *) msg->data,
				      buffer,
				      msg->protocol_version);
		break;
	case REQUEST_UPDATE_JOB_TIME:
		_pack_update_job_time_msg((job_time_msg_t *)
					  msg->data, buffer,
					  msg->protocol_version);
		break;
	case RESPONSE_RECONFIGURE:
	case RESPONSE_SHUTDOWN:
	case RESPONSE_CANCEL_JOB_STEP:
		break;
	case REQUEST_JOB_ATTACH:
		break;
	case RESPONSE_JOB_ATTACH:
		break;
	case RESPONSE_JOB_STEP_INFO:
		_pack_job_step_info_msg((slurm_msg_t *) msg, buffer);
		break;
	case REQUEST_JOB_RESOURCE:
		break;
	case RESPONSE_JOB_RESOURCE:
		break;
	case REQUEST_RUN_JOB_STEP:
		break;
	case RESPONSE_RUN_JOB_STEP:
		break;
	case MESSAGE_TASK_EXIT:
		_pack_task_exit_msg((task_exit_msg_t *) msg->data, buffer,
				    msg->protocol_version);
		break;
	case REQUEST_BATCH_JOB_LAUNCH:
		_pack_batch_job_launch_msg((batch_job_launch_msg_t *)
					   msg->data, buffer,
					   msg->protocol_version);
		break;
	case REQUEST_LAUNCH_PROLOG:
		_pack_prolog_launch_msg((prolog_launch_msg_t *)
					   msg->data, buffer, msg->protocol_version);
		break;
	case RESPONSE_PROLOG_EXECUTING:
	case RESPONSE_JOB_READY:
	case RESPONSE_SLURM_RC:
		_pack_return_code_msg((return_code_msg_t *) msg->data,
				      buffer,
				      msg->protocol_version);
		break;
	case RESPONSE_SLURM_RC_MSG:
		_pack_return_code2_msg((return_code2_msg_t *) msg->data,
				       buffer,
				       msg->protocol_version);
		break;
	case RESPONSE_JOB_STEP_CREATE:
		pack_job_step_create_response_msg(
			(job_step_create_response_msg_t *)
			msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_JOB_STEP_CREATE:
		pack_job_step_create_request_msg(
			(job_step_create_request_msg_t *)
			msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_JOB_ID:
		_pack_job_id_request_msg(
			(job_id_request_msg_t *)msg->data,
			buffer,
			msg->protocol_version);
		break;
	case RESPONSE_JOB_ID:
		_pack_job_id_response_msg(
			(job_id_response_msg_t *)msg->data,
			buffer,
			msg->protocol_version);
		break;
	case SRUN_EXEC:
		_pack_srun_exec_msg((srun_exec_msg_t *)msg->data, buffer,
				    msg->protocol_version);
		break;
	case SRUN_JOB_COMPLETE:
	case SRUN_PING:
		_pack_srun_ping_msg((srun_ping_msg_t *)msg->data, buffer,
				    msg->protocol_version);
		break;
	case SRUN_NODE_FAIL:
		_pack_srun_node_fail_msg((srun_node_fail_msg_t *)msg->data,
					 buffer,
					 msg->protocol_version);
		break;
	case SRUN_STEP_MISSING:
		_pack_srun_step_missing_msg((srun_step_missing_msg_t *)
					    msg->data, buffer,
					    msg->protocol_version);
		break;
	case SRUN_TIMEOUT:
		_pack_srun_timeout_msg((srun_timeout_msg_t *)msg->data, buffer,
				       msg->protocol_version);
		break;
	case SRUN_USER_MSG:
		_pack_srun_user_msg((srun_user_msg_t *)msg->data, buffer,
				    msg->protocol_version);
		break;
	case REQUEST_CHECKPOINT:
		_pack_checkpoint_msg((checkpoint_msg_t *)msg->data, buffer,
				     msg->protocol_version);
		break;
	case REQUEST_CHECKPOINT_COMP:
		_pack_checkpoint_comp((checkpoint_comp_msg_t *)msg->data,
				      buffer,
				      msg->protocol_version);
		break;
	case REQUEST_CHECKPOINT_TASK_COMP:
		_pack_checkpoint_task_comp(
			(checkpoint_task_comp_msg_t *)msg->data,
			buffer,
			msg->protocol_version);
		break;
	case RESPONSE_CHECKPOINT:
	case RESPONSE_CHECKPOINT_COMP:
		_pack_checkpoint_resp_msg((checkpoint_resp_msg_t *)msg->data,
					  buffer,
					  msg->protocol_version);
		break;
	case REQUEST_SUSPEND:
	case SRUN_REQUEST_SUSPEND:
		_pack_suspend_msg((suspend_msg_t *)msg->data, buffer,
				  msg->protocol_version);
		break;
	case REQUEST_SUSPEND_INT:
		_pack_suspend_int_msg((suspend_int_msg_t *)msg->data, buffer,
				      msg->protocol_version);
		break;

	case REQUEST_JOB_READY:
	case REQUEST_JOB_INFO_SINGLE:
		_pack_job_ready_msg((job_id_msg_t *)msg->data, buffer,
				    msg->protocol_version);
		break;

	case REQUEST_JOB_REQUEUE:
		_pack_job_requeue_msg((requeue_msg_t *)msg->data,
		                      buffer,
		                      msg->protocol_version);
		break;

	case REQUEST_JOB_USER_INFO:
		_pack_job_user_msg((job_user_id_msg_t *)msg->data, buffer,
				   msg->protocol_version);
		break;

	case REQUEST_SHARE_INFO:
		_pack_shares_request_msg((shares_request_msg_t *)msg->data,
					 buffer,
					 msg->protocol_version);
		break;
	case RESPONSE_SHARE_INFO:
		_pack_shares_response_msg((shares_response_msg_t *)msg->data,
					  buffer,
					  msg->protocol_version);
		break;
	case REQUEST_PRIORITY_FACTORS:
		_pack_priority_factors_request_msg(
			(priority_factors_request_msg_t*)msg->data,
			buffer,
			msg->protocol_version);
		break;
	case RESPONSE_PRIORITY_FACTORS:
		_pack_priority_factors_response_msg(
			(priority_factors_response_msg_t*)msg->data,
			buffer,
			msg->protocol_version);
		break;
	case REQUEST_BLOCK_INFO:
		_pack_block_info_req_msg(
			(block_info_request_msg_t *) msg->data, buffer,
			msg->protocol_version);
		break;
	case RESPONSE_BLOCK_INFO:
		_pack_block_info_resp_msg((slurm_msg_t *) msg, buffer);
		break;
	case REQUEST_FILE_BCAST:
		_pack_file_bcast((file_bcast_msg_t *) msg->data, buffer,
				 msg->protocol_version);
		break;
	case PMI_KVS_PUT_REQ:
	case PMI_KVS_GET_RESP:
		_pack_kvs_data((struct kvs_comm_set *) msg->data, buffer,
			       msg->protocol_version);
		break;
	case PMI_KVS_GET_REQ:
		_pack_kvs_get((kvs_get_msg_t *) msg->data, buffer,
			      msg->protocol_version);
		break;
	case PMI_KVS_PUT_RESP:
		break;	/* no data in message */
	case RESPONSE_FORWARD_FAILED:
		break;
	case REQUEST_TRIGGER_GET:
	case RESPONSE_TRIGGER_GET:
	case REQUEST_TRIGGER_SET:
	case REQUEST_TRIGGER_CLEAR:
	case REQUEST_TRIGGER_PULL:
		_pack_trigger_msg((trigger_info_msg_t *) msg->data, buffer,
				  msg->protocol_version);
		break;
	case RESPONSE_SLURMD_STATUS:
		_pack_slurmd_status((slurmd_status_t *) msg->data, buffer,
				    msg->protocol_version);
		break;
	case REQUEST_JOB_NOTIFY:
		_pack_job_notify((job_notify_msg_t *) msg->data, buffer,
				 msg->protocol_version);
		break;
	case REQUEST_SET_DEBUG_FLAGS:
		_pack_set_debug_flags_msg(
			(set_debug_flags_msg_t *)msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_SET_DEBUG_LEVEL:
	case REQUEST_SET_SCHEDLOG_LEVEL:
		_pack_set_debug_level_msg(
			(set_debug_level_msg_t *)msg->data, buffer,
			msg->protocol_version);
		break;
	case ACCOUNTING_UPDATE_MSG:
		_pack_accounting_update_msg(
			(accounting_update_msg_t *)msg->data,
			buffer,
			msg->protocol_version);
		break;
	case RESPONSE_TOPO_INFO:
		_pack_topo_info_msg(
			(topo_info_response_msg_t *)msg->data, buffer,
			msg->protocol_version);
		break;
	case RESPONSE_JOB_SBCAST_CRED:
		_pack_job_sbcast_cred_msg(
			(job_sbcast_cred_msg_t *)msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_FRONT_END_INFO:
		_pack_front_end_info_request_msg(
			(front_end_info_request_msg_t *)msg->data, buffer,
			msg->protocol_version);
		break;
	case RESPONSE_FRONT_END_INFO:
		_pack_front_end_info_msg((slurm_msg_t *) msg, buffer);
		break;
	case REQUEST_SPANK_ENVIRONMENT:
		_pack_spank_env_request_msg(
			(spank_env_request_msg_t *)msg->data, buffer,
			msg->protocol_version);
		break;
	case RESPONCE_SPANK_ENVIRONMENT:
		_pack_spank_env_responce_msg(
			(spank_env_responce_msg_t *)msg->data, buffer,
			msg->protocol_version);
		break;

	case REQUEST_STATS_INFO:
		_pack_stats_request_msg((stats_info_request_msg_t *)msg->data,
					buffer, msg->protocol_version);
		break;

	case RESPONSE_STATS_INFO:
		_pack_stats_response_msg((slurm_msg_t *)msg, buffer);
		break;

	case REQUEST_FORWARD_DATA:
		_pack_forward_data_msg((forward_data_msg_t *)msg->data,
				       buffer, msg->protocol_version);
		break;

	case RESPONSE_PING_SLURMD:
		_pack_ping_slurmd_resp((ping_slurmd_resp_msg_t *)msg->data,
				       buffer, msg->protocol_version);
		break;
	case REQUEST_LICENSE_INFO:
		 _pack_license_info_request_msg((license_info_request_msg_t *)
		                                msg->data,
		                                buffer,
		                                msg->protocol_version);
			break;
	case RESPONSE_LICENSE_INFO:
		_pack_license_info_msg((slurm_msg_t *) msg, buffer);
		break;
	case RESPONSE_JOB_ARRAY_ERRORS:
		_pack_job_array_resp_msg((job_array_resp_msg_t *) msg->data,
					 buffer, msg->protocol_version);
		break;
	default:
		debug("No pack method for msg type %u", msg->msg_type);
		return EINVAL;
		break;

	}
	return SLURM_SUCCESS;
}

/* unpack_msg
 * unpacks a generic slurm protocol message body
 * OUT msg - the body structure to unpack (note: includes message type)
 * IN/OUT buffer - source of the unpack, contains pointers that are
 *			automatically updated
 * RET 0 or error code
 */
int
unpack_msg(slurm_msg_t * msg, Buf buffer)
{
	int rc = SLURM_SUCCESS;
	msg->data = NULL;	/* Initialize to no data for now */

	switch (msg->msg_type) {
	case REQUEST_NODE_INFO:
		rc = _unpack_node_info_request_msg((node_info_request_msg_t **)
						   & (msg->data), buffer,
						   msg->protocol_version);
		break;
	case REQUEST_NODE_INFO_SINGLE:
		rc = _unpack_node_info_single_msg((node_info_single_msg_t **)
						  & (msg->data), buffer,
						  msg->protocol_version);
		break;
	case REQUEST_PARTITION_INFO:
		rc = _unpack_part_info_request_msg((part_info_request_msg_t **)
						   & (msg->data), buffer,
						   msg->protocol_version);
		break;
	case REQUEST_RESERVATION_INFO:
		rc = _unpack_resv_info_request_msg((resv_info_request_msg_t **)
						   & (msg->data), buffer,
						   msg->protocol_version);
		break;
	case REQUEST_BUILD_INFO:
	case REQUEST_ACCTING_INFO:
		rc = _unpack_last_update_msg((last_update_msg_t **) &
					     (msg->data), buffer,
					     msg->protocol_version);
		break;
	case RESPONSE_BUILD_INFO:
		rc = _unpack_slurm_ctl_conf_msg((slurm_ctl_conf_info_msg_t
						 **)
						& (msg->data), buffer,
						msg->protocol_version);
		break;
	case RESPONSE_JOB_INFO:
		rc = _unpack_job_info_msg((job_info_msg_t **) & (msg->data),
					  buffer,
					  msg->protocol_version);
		break;
	case RESPONSE_PARTITION_INFO:
		rc = _unpack_partition_info_msg((partition_info_msg_t **) &
						(msg->data), buffer,
						msg->protocol_version);
		break;
	case RESPONSE_NODE_INFO:
		rc = _unpack_node_info_msg((node_info_msg_t **) &
					   (msg->data), buffer,
					   msg->protocol_version);
		break;
	case MESSAGE_NODE_REGISTRATION_STATUS:
		rc = _unpack_node_registration_status_msg(
			(slurm_node_registration_status_msg_t **)
			& (msg->data), buffer,
			msg->protocol_version);
		break;
	case RESPONSE_ACCT_GATHER_UPDATE:
	case RESPONSE_ACCT_GATHER_ENERGY:
		rc = _unpack_acct_gather_node_resp_msg(
			(acct_gather_node_resp_msg_t **)&(msg->data),
			buffer, msg->protocol_version);
		break;
	case REQUEST_RESOURCE_ALLOCATION:
	case REQUEST_SUBMIT_BATCH_JOB:
	case REQUEST_JOB_WILL_RUN:
	case REQUEST_UPDATE_JOB:
		rc = _unpack_job_desc_msg((job_desc_msg_t **) & (msg->data),
					  buffer,
					  msg->protocol_version);
		break;
	case REQUEST_UPDATE_JOB_STEP:
		rc = _unpack_update_job_step_msg(
			(step_update_request_msg_t **) & (msg->data),
			buffer, msg->protocol_version);
		break;
	case REQUEST_JOB_END_TIME:
	case REQUEST_JOB_ALLOCATION_INFO:
	case REQUEST_JOB_ALLOCATION_INFO_LITE:
	case REQUEST_JOB_SBCAST_CRED:
		rc = _unpack_job_alloc_info_msg((job_alloc_info_msg_t **) &
						(msg->data), buffer,
						msg->protocol_version);
		break;
	case REQUEST_NODE_REGISTRATION_STATUS:
	case REQUEST_RECONFIGURE:
	case REQUEST_SHUTDOWN_IMMEDIATE:
	case REQUEST_PING:
	case REQUEST_CONTROL:
	case REQUEST_TAKEOVER:
	case REQUEST_DAEMON_STATUS:
	case REQUEST_HEALTH_CHECK:
	case REQUEST_ACCT_GATHER_UPDATE:
	case ACCOUNTING_FIRST_REG:
	case ACCOUNTING_REGISTER_CTLD:
	case REQUEST_TOPO_INFO:
		/* Message contains no body/information */
		break;
	case REQUEST_ACCT_GATHER_ENERGY:
		rc = _unpack_acct_gather_energy_req(
			(acct_gather_energy_req_msg_t **) & (msg->data),
			buffer, msg->protocol_version);
		break;
	case REQUEST_REBOOT_NODES:
		rc = _unpack_reboot_msg((reboot_msg_t **) & (msg->data),
					buffer, msg->protocol_version);
		break;
	case REQUEST_SHUTDOWN:
		rc = _unpack_shutdown_msg((shutdown_msg_t **) & (msg->data),
					  buffer,
					  msg->protocol_version);
		break;
	case RESPONSE_SUBMIT_BATCH_JOB:
		rc = _unpack_submit_response_msg((submit_response_msg_t **)
						 & (msg->data), buffer,
						 msg->protocol_version);
		break;
	case RESPONSE_JOB_ALLOCATION_INFO_LITE:
	case RESPONSE_RESOURCE_ALLOCATION:
		rc = _unpack_resource_allocation_response_msg(
			(resource_allocation_response_msg_t **)
			& (msg->data), buffer,
			msg->protocol_version);
		break;
	case RESPONSE_JOB_WILL_RUN:
		rc = _unpack_will_run_response_msg((will_run_response_msg_t **)
						   &(msg->data), buffer,
						   msg->protocol_version);
		break;
	case RESPONSE_JOB_ALLOCATION_INFO:
		rc = _unpack_job_alloc_info_response_msg(
			(job_alloc_info_response_msg_t **)
			& (msg->data), buffer,
			msg->protocol_version);
		break;
	case REQUEST_UPDATE_FRONT_END:
		rc = _unpack_update_front_end_msg((update_front_end_msg_t **) &
						  (msg->data), buffer,
						  msg->protocol_version);
		break;
	case REQUEST_UPDATE_NODE:
		rc = _unpack_update_node_msg((update_node_msg_t **) &
					     (msg->data), buffer,
					     msg->protocol_version);
		break;
	case REQUEST_CREATE_PARTITION:
	case REQUEST_UPDATE_PARTITION:
		rc = _unpack_update_partition_msg((update_part_msg_t **) &
						  (msg->data), buffer,
						  msg->protocol_version);
		break;
	case REQUEST_DELETE_PARTITION:
		rc = _unpack_delete_partition_msg((delete_part_msg_t **) &
						  (msg->data), buffer,
						  msg->protocol_version);
		break;
	case REQUEST_CREATE_RESERVATION:
	case REQUEST_UPDATE_RESERVATION:
		rc = _unpack_update_resv_msg((resv_desc_msg_t **)
					     &(msg->data), buffer,
					     msg->protocol_version);
		break;
	case REQUEST_DELETE_RESERVATION:
	case RESPONSE_CREATE_RESERVATION:
		rc = _unpack_resv_name_msg((reservation_name_msg_t **)
					   &(msg->data), buffer,
					   msg->protocol_version);
		break;
	case REQUEST_UPDATE_BLOCK:
		rc = _unpack_block_info(
			(block_info_t **)&(msg->data), buffer,
			msg->protocol_version);
		break;
	case RESPONSE_RESERVATION_INFO:
		rc = _unpack_reserve_info_msg((reserve_info_msg_t **)
					      &(msg->data), buffer,
					      msg->protocol_version);
		break;
	case REQUEST_LAUNCH_TASKS:
		rc = _unpack_launch_tasks_request_msg(
			(launch_tasks_request_msg_t **)
			& (msg->data), buffer,
			msg->protocol_version);
		break;
	case RESPONSE_LAUNCH_TASKS:
		rc = _unpack_launch_tasks_response_msg(
			(launch_tasks_response_msg_t **)
			& (msg->data), buffer,
			msg->protocol_version);
		break;
	case TASK_USER_MANAGED_IO_STREAM:
		_unpack_task_user_managed_io_stream_msg(
			(task_user_managed_io_msg_t **) &msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_REATTACH_TASKS:
		rc = _unpack_reattach_tasks_request_msg(
			(reattach_tasks_request_msg_t **) & msg->data,
			buffer,
			msg->protocol_version);
		break;
	case RESPONSE_REATTACH_TASKS:
		rc = _unpack_reattach_tasks_response_msg(
			(reattach_tasks_response_msg_t **)
			& msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_SIGNAL_TASKS:
	case REQUEST_TERMINATE_TASKS:
		rc = _unpack_cancel_tasks_msg((kill_tasks_msg_t **) &
					      (msg->data), buffer,
					      msg->protocol_version);
		break;
	case REQUEST_CHECKPOINT_TASKS:
		rc = _unpack_checkpoint_tasks_msg((checkpoint_tasks_msg_t **) &
						  (msg->data), buffer,
						  msg->protocol_version);
		break;
	case REQUEST_JOB_STEP_INFO:
		rc = _unpack_job_step_info_req_msg(
			(job_step_info_request_msg_t **)
			& (msg->data), buffer,
			msg->protocol_version);
		break;
		/********  job_step_id_t Messages  ********/
	case REQUEST_JOB_INFO:
		rc = _unpack_job_info_request_msg((job_info_request_msg_t**)
						  & (msg->data), buffer,
						  msg->protocol_version);
		break;
	case REQUEST_CANCEL_JOB_STEP:
	case REQUEST_KILL_JOB:
	case SRUN_STEP_SIGNAL:
		rc = _unpack_job_step_kill_msg((job_step_kill_msg_t **)
					       & (msg->data), buffer,
					       msg->protocol_version);
		break;
	case REQUEST_COMPLETE_JOB_ALLOCATION:
		rc = _unpack_complete_job_allocation_msg(
			(complete_job_allocation_msg_t **)&msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_COMPLETE_PROLOG:
		rc = _unpack_complete_prolog_msg(
			(complete_prolog_msg_t **)&msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_COMPLETE_BATCH_JOB:
	case REQUEST_COMPLETE_BATCH_SCRIPT:
		rc = _unpack_complete_batch_script_msg(
			(complete_batch_script_msg_t **)&msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_STEP_COMPLETE:
		rc = _unpack_step_complete_msg((step_complete_msg_t
						**) & (msg->data),
					       buffer,
					       msg->protocol_version);
		break;
	case RESPONSE_JOB_STEP_STAT:
		rc = _unpack_job_step_stat(
			(job_step_stat_t **) &(msg->data), buffer,
			msg->protocol_version);
		break;
	case REQUEST_STEP_LAYOUT:
	case REQUEST_JOB_STEP_STAT:
	case REQUEST_JOB_STEP_PIDS:
		_unpack_job_step_id_msg((job_step_id_msg_t **)&msg->data,
					buffer,
					msg->protocol_version);
		break;
	case RESPONSE_STEP_LAYOUT:
		unpack_slurm_step_layout((slurm_step_layout_t **)&msg->data,
					 buffer,
					 msg->protocol_version);
		break;
	case RESPONSE_JOB_STEP_PIDS:
		_unpack_job_step_pids(
			(job_step_pids_t **)&msg->data,
			buffer,	msg->protocol_version);
		break;
	case REQUEST_SIGNAL_JOB:
		rc = _unpack_signal_job_msg((signal_job_msg_t **)&(msg->data),
					    buffer,
					    msg->protocol_version);
		break;
	case REQUEST_ABORT_JOB:
	case REQUEST_KILL_PREEMPTED:
	case REQUEST_KILL_TIMELIMIT:
	case REQUEST_TERMINATE_JOB:
		rc = _unpack_kill_job_msg((kill_job_msg_t **) & (msg->data),
					  buffer,
					  msg->protocol_version);
		break;
	case MESSAGE_EPILOG_COMPLETE:
		rc = _unpack_epilog_comp_msg((epilog_complete_msg_t **)
					     & (msg->data), buffer,
					     msg->protocol_version);
		break;
	case REQUEST_UPDATE_JOB_TIME:
		rc = _unpack_update_job_time_msg(
			(job_time_msg_t **)
			& (msg->data), buffer,
			msg->protocol_version);
		break;
	case RESPONSE_RECONFIGURE:
	case RESPONSE_SHUTDOWN:
	case RESPONSE_CANCEL_JOB_STEP:
		break;
	case REQUEST_JOB_ATTACH:
		break;
	case RESPONSE_JOB_ATTACH:
		break;
	case RESPONSE_JOB_STEP_INFO:
		rc = _unpack_job_step_info_response_msg(
			(job_step_info_response_msg_t **)
			& (msg->data), buffer,
			msg->protocol_version);
		break;
	case REQUEST_JOB_RESOURCE:
		break;
	case RESPONSE_JOB_RESOURCE:
		break;
	case REQUEST_RUN_JOB_STEP:
		break;
	case RESPONSE_RUN_JOB_STEP:
		break;
	case MESSAGE_TASK_EXIT:
		rc = _unpack_task_exit_msg((task_exit_msg_t **)
					   & (msg->data), buffer,
					   msg->protocol_version);
		break;
	case REQUEST_BATCH_JOB_LAUNCH:
		rc = _unpack_batch_job_launch_msg((batch_job_launch_msg_t **)
						  & (msg->data), buffer,
						  msg->protocol_version);
		break;
	case REQUEST_LAUNCH_PROLOG:
		rc = _unpack_prolog_launch_msg((prolog_launch_msg_t **)
						  & (msg->data), buffer, msg->protocol_version);
		break;
	case RESPONSE_PROLOG_EXECUTING:
	case RESPONSE_JOB_READY:
	case RESPONSE_SLURM_RC:
		rc = _unpack_return_code_msg((return_code_msg_t **)
					     & (msg->data), buffer,
					     msg->protocol_version);
		break;
	case RESPONSE_SLURM_RC_MSG:
		/* Log error message, otherwise replicate RESPONSE_SLURM_RC */
		msg->msg_type = RESPONSE_SLURM_RC;
		rc = _unpack_return_code2_msg((return_code_msg_t **)
					      & (msg->data), buffer,
					      msg->protocol_version);
		break;
	case RESPONSE_JOB_STEP_CREATE:
		rc = unpack_job_step_create_response_msg(
			(job_step_create_response_msg_t **)
			& msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_JOB_STEP_CREATE:
		rc = unpack_job_step_create_request_msg(
			(job_step_create_request_msg_t **) & msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_JOB_ID:
		rc = _unpack_job_id_request_msg(
			(job_id_request_msg_t **) & msg->data,
			buffer,
			msg->protocol_version);
		break;
	case RESPONSE_JOB_ID:
		rc = _unpack_job_id_response_msg(
			(job_id_response_msg_t **) & msg->data,
			buffer,
			msg->protocol_version);
		break;
	case SRUN_EXEC:
		rc = _unpack_srun_exec_msg((srun_exec_msg_t **) & msg->data,
					   buffer,
					   msg->protocol_version);
		break;
	case SRUN_JOB_COMPLETE:
	case SRUN_PING:
		rc = _unpack_srun_ping_msg((srun_ping_msg_t **) & msg->data,
					   buffer,
					   msg->protocol_version);
		break;
	case SRUN_NODE_FAIL:
		rc = _unpack_srun_node_fail_msg((srun_node_fail_msg_t **)
						& msg->data, buffer,
						msg->protocol_version);
		break;
	case SRUN_STEP_MISSING:
		rc = _unpack_srun_step_missing_msg((srun_step_missing_msg_t **)
						   & msg->data, buffer,
						   msg->protocol_version);
		break;
	case SRUN_TIMEOUT:
		rc = _unpack_srun_timeout_msg((srun_timeout_msg_t **)
					      & msg->data, buffer,
					      msg->protocol_version);
		break;
	case SRUN_USER_MSG:
		rc = _unpack_srun_user_msg((srun_user_msg_t **)
					   & msg->data, buffer,
					   msg->protocol_version);
		break;
	case REQUEST_CHECKPOINT:
		rc = _unpack_checkpoint_msg((checkpoint_msg_t **)
					    & msg->data, buffer,
					    msg->protocol_version);
		break;
	case REQUEST_CHECKPOINT_COMP:
		rc = _unpack_checkpoint_comp((checkpoint_comp_msg_t **)
					     & msg->data, buffer,
					     msg->protocol_version);
		break;
	case REQUEST_CHECKPOINT_TASK_COMP:
		rc = _unpack_checkpoint_task_comp(
			(checkpoint_task_comp_msg_t **)
			& msg->data, buffer,
			msg->protocol_version);
		break;
	case RESPONSE_CHECKPOINT:
	case RESPONSE_CHECKPOINT_COMP:
		rc = _unpack_checkpoint_resp_msg((checkpoint_resp_msg_t **)
						 & msg->data, buffer,
						 msg->protocol_version);
		break;
	case REQUEST_SUSPEND:
	case SRUN_REQUEST_SUSPEND:
		rc = _unpack_suspend_msg((suspend_msg_t **) &msg->data,
					 buffer,
					 msg->protocol_version);
		break;
	case REQUEST_SUSPEND_INT:
		rc = _unpack_suspend_int_msg((suspend_int_msg_t **) &msg->data,
					     buffer, msg->protocol_version);
		break;

	case REQUEST_JOB_READY:
	case REQUEST_JOB_INFO_SINGLE:
		rc = _unpack_job_ready_msg((job_id_msg_t **)
		                           & msg->data, buffer,
		                           msg->protocol_version);
		break;

	case REQUEST_JOB_REQUEUE:
		rc = _unpack_job_requeue_msg((requeue_msg_t **)&msg->data,
		                             buffer,
		                             msg->protocol_version);
		break;

	case REQUEST_JOB_USER_INFO:
		rc = _unpack_job_user_msg((job_user_id_msg_t **)
					  &msg->data, buffer,
					  msg->protocol_version);
		break;

	case REQUEST_SHARE_INFO:
		rc = _unpack_shares_request_msg(
			(shares_request_msg_t **)&msg->data,
			buffer,
			msg->protocol_version);
		break;
	case RESPONSE_SHARE_INFO:
		rc = _unpack_shares_response_msg(
			(shares_response_msg_t **)&msg->data,
			buffer,
			msg->protocol_version);
		break;
	case REQUEST_PRIORITY_FACTORS:
		_unpack_priority_factors_request_msg(
			(priority_factors_request_msg_t**)&msg->data,
			buffer,
			msg->protocol_version);
		break;
	case RESPONSE_PRIORITY_FACTORS:
		_unpack_priority_factors_response_msg(
			(priority_factors_response_msg_t**)&msg->data,
			buffer,
			msg->protocol_version);
		break;
	case REQUEST_BLOCK_INFO:
		rc = _unpack_block_info_req_msg(
			(block_info_request_msg_t **) &msg->data,
			buffer,
			msg->protocol_version);
		break;
	case RESPONSE_BLOCK_INFO:
		rc = slurm_unpack_block_info_msg(
			(block_info_msg_t **) &(msg->data), buffer,
			msg->protocol_version);
		break;
	case REQUEST_FILE_BCAST:
		rc = _unpack_file_bcast( (file_bcast_msg_t **)
					 & msg->data, buffer,
					 msg->protocol_version);
		break;
	case PMI_KVS_PUT_REQ:
	case PMI_KVS_GET_RESP:
		rc = _unpack_kvs_data((struct kvs_comm_set **) &msg->data,
				      buffer,
				      msg->protocol_version);
		break;
	case PMI_KVS_GET_REQ:
		rc = _unpack_kvs_get((kvs_get_msg_t **) &msg->data, buffer,
				     msg->protocol_version);
		break;
	case PMI_KVS_PUT_RESP:
		break;	/* no data */
	case RESPONSE_FORWARD_FAILED:
		break;
	case REQUEST_TRIGGER_GET:
	case RESPONSE_TRIGGER_GET:
	case REQUEST_TRIGGER_SET:
	case REQUEST_TRIGGER_CLEAR:
	case REQUEST_TRIGGER_PULL:
		rc = _unpack_trigger_msg((trigger_info_msg_t **)
					 &msg->data, buffer,
					 msg->protocol_version);
		break;
	case RESPONSE_SLURMD_STATUS:
		rc = _unpack_slurmd_status((slurmd_status_t **)
					   &msg->data, buffer,
					   msg->protocol_version);
		break;
	case REQUEST_JOB_NOTIFY:
		rc =  _unpack_job_notify((job_notify_msg_t **)
					 &msg->data, buffer,
					 msg->protocol_version);
		break;
	case REQUEST_SET_DEBUG_FLAGS:
		rc = _unpack_set_debug_flags_msg(
			(set_debug_flags_msg_t **)&(msg->data), buffer,
			msg->protocol_version);
		break;
	case REQUEST_SET_DEBUG_LEVEL:
	case REQUEST_SET_SCHEDLOG_LEVEL:
		rc = _unpack_set_debug_level_msg(
			(set_debug_level_msg_t **)&(msg->data), buffer,
			msg->protocol_version);
		break;
	case ACCOUNTING_UPDATE_MSG:
		rc = _unpack_accounting_update_msg(
			(accounting_update_msg_t **)&msg->data,
			buffer,
			msg->protocol_version);
		break;
	case RESPONSE_TOPO_INFO:
		rc = _unpack_topo_info_msg(
			(topo_info_response_msg_t **)&msg->data, buffer,
			msg->protocol_version);
		break;
	case RESPONSE_JOB_SBCAST_CRED:
		rc = _unpack_job_sbcast_cred_msg(
			(job_sbcast_cred_msg_t **)&msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_FRONT_END_INFO:
		rc = _unpack_front_end_info_request_msg(
			(front_end_info_request_msg_t **)&msg->data, buffer,
			msg->protocol_version);
		break;
	case RESPONSE_FRONT_END_INFO:
		rc = _unpack_front_end_info_msg(
			(front_end_info_msg_t **)&msg->data, buffer,
			msg->protocol_version);
		break;
	case REQUEST_SPANK_ENVIRONMENT:
		rc = _unpack_spank_env_request_msg(
			(spank_env_request_msg_t **)&msg->data, buffer,
			msg->protocol_version);
		break;
	case RESPONCE_SPANK_ENVIRONMENT:
		rc = _unpack_spank_env_responce_msg(
			(spank_env_responce_msg_t **)&msg->data, buffer,
			msg->protocol_version);
		break;

	case REQUEST_STATS_INFO:
		_unpack_stats_request_msg((stats_info_request_msg_t **)
					  &msg->data, buffer,
					  msg->protocol_version);
		break;

	case RESPONSE_STATS_INFO:
		_unpack_stats_response_msg((stats_info_response_msg_t **)
					   &msg->data, buffer,
					   msg->protocol_version);
		break;

	case REQUEST_FORWARD_DATA:
		rc = _unpack_forward_data_msg((forward_data_msg_t **)&msg->data,
					      buffer, msg->protocol_version);
		break;

	case RESPONSE_PING_SLURMD:
		rc = _unpack_ping_slurmd_resp((ping_slurmd_resp_msg_t **)
					      &msg->data, buffer,
					      msg->protocol_version);
		break;
	case RESPONSE_LICENSE_INFO:
		rc = _unpack_license_info_msg((license_info_msg_t **)&(msg->data),
		                              buffer,
		                              msg->protocol_version);
		break;
	case REQUEST_LICENSE_INFO:
		rc = _unpack_license_info_request_msg((license_info_request_msg_t **)
		                                      &(msg->data),
		                                      buffer,
		                                      msg->protocol_version);
		break;
	case RESPONSE_JOB_ARRAY_ERRORS:
		rc = _unpack_job_array_resp_msg((job_array_resp_msg_t **)
						&(msg->data), buffer,
						msg->protocol_version);
		break;
	default:
		debug("No unpack method for msg type %u", msg->msg_type);
		return EINVAL;
		break;
	}

	if (rc) {
		error("Malformed RPC of type %s(%u) received",
		      rpc_num2string(msg->msg_type), msg->msg_type);
	}
	return rc;
}

static void _pack_assoc_shares_object(void *in, Buf buffer,
				      uint16_t protocol_version)
{
	association_shares_object_t *object = (association_shares_object_t *)in;

	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		if (!object) {
			pack32(0, buffer);

			packnull(buffer);
			packnull(buffer);
			packnull(buffer);

			packdouble(0, buffer);
			pack32(0, buffer);

			packdouble(0, buffer);
			packdouble(0, buffer);
			pack64(0, buffer);

			pack64(0, buffer);
			pack64(0, buffer);
			packdouble(0, buffer);
			packdouble(0, buffer);

			pack16(0, buffer);

			return;
		}

		pack32(object->assoc_id, buffer);

		packstr(object->cluster, buffer);
		packstr(object->name, buffer);
		packstr(object->parent, buffer);

		packdouble(object->shares_norm, buffer);
		pack32(object->shares_raw, buffer);

		packdouble(object->usage_efctv, buffer);
		packdouble(object->usage_norm, buffer);
		pack64(object->usage_raw, buffer);

		pack64(object->grp_cpu_mins, buffer);
		pack64(object->cpu_run_mins, buffer);
		packdouble(object->fs_factor, buffer);
		packdouble(object->level_fs, buffer);

		pack16(object->user, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!object) {
			pack32(0, buffer);

			packnull(buffer);
			packnull(buffer);
			packnull(buffer);

			packdouble(0, buffer);
			pack32(0, buffer);

			packdouble(0, buffer);
			packdouble(0, buffer);
			pack64(0, buffer);

			pack64(0, buffer);
			pack64(0, buffer);

			pack16(0, buffer);

			return;
		}

		pack32(object->assoc_id, buffer);

		packstr(object->cluster, buffer);
		packstr(object->name, buffer);
		packstr(object->parent, buffer);

		packdouble(object->shares_norm, buffer);
		pack32(object->shares_raw, buffer);

		packdouble(object->usage_efctv, buffer);
		packdouble(object->usage_norm, buffer);
		pack64(object->usage_raw, buffer);

		pack64(object->grp_cpu_mins, buffer);
		pack64(object->cpu_run_mins, buffer);

		pack16(object->user, buffer);
	} else {
		error("_pack_assoc_shares_object: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

static int _unpack_assoc_shares_object(void **object, Buf buffer,
				       uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	association_shares_object_t *object_ptr =
		xmalloc(sizeof(association_shares_object_t));

	*object = (void *) object_ptr;

	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		safe_unpack32(&object_ptr->assoc_id, buffer);

		safe_unpackstr_xmalloc(&object_ptr->cluster,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->parent,
				       &uint32_tmp, buffer);

		safe_unpackdouble(&object_ptr->shares_norm, buffer);
		safe_unpack32(&object_ptr->shares_raw, buffer);

		safe_unpackdouble(&object_ptr->usage_efctv, buffer);
		safe_unpackdouble(&object_ptr->usage_norm, buffer);
		safe_unpack64(&object_ptr->usage_raw, buffer);

		safe_unpack64(&object_ptr->grp_cpu_mins, buffer);
		safe_unpack64(&object_ptr->cpu_run_mins, buffer);
		safe_unpackdouble(&object_ptr->fs_factor, buffer);
		safe_unpackdouble(&object_ptr->level_fs, buffer);

		safe_unpack16(&object_ptr->user, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&object_ptr->assoc_id, buffer);

		safe_unpackstr_xmalloc(&object_ptr->cluster,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->parent,
				       &uint32_tmp, buffer);

		safe_unpackdouble(&object_ptr->shares_norm, buffer);
		safe_unpack32(&object_ptr->shares_raw, buffer);

		safe_unpackdouble(&object_ptr->usage_efctv, buffer);
		safe_unpackdouble(&object_ptr->usage_norm, buffer);
		safe_unpack64(&object_ptr->usage_raw, buffer);

		safe_unpack64(&object_ptr->grp_cpu_mins, buffer);
		safe_unpack64(&object_ptr->cpu_run_mins, buffer);

		safe_unpack16(&object_ptr->user, buffer);
	} else {
		error("_unpack_assoc_shares_object: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_destroy_association_shares_object(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

static void _pack_shares_request_msg(shares_request_msg_t * msg, Buf buffer,
				     uint16_t protocol_version)
{
	uint32_t count = NO_VAL;
	char *tmp_info = NULL;
	ListIterator itr = NULL;

	xassert(msg != NULL);

	if (msg->acct_list)
		count = list_count(msg->acct_list);
	pack32(count, buffer);
	if (count && count != NO_VAL) {
		itr = list_iterator_create(msg->acct_list);
		while ((tmp_info = list_next(itr))) {
			packstr(tmp_info, buffer);
		}
		list_iterator_destroy(itr);
	}
	count = NO_VAL;


	if (msg->user_list)
		count = list_count(msg->user_list);
	pack32(count, buffer);
	if (count && count != NO_VAL) {
		itr = list_iterator_create(msg->user_list);
		while ((tmp_info = list_next(itr))) {
			packstr(tmp_info, buffer);
		}
		list_iterator_destroy(itr);
	}
}

static int _unpack_shares_request_msg(shares_request_msg_t ** msg, Buf buffer,
				      uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	uint32_t count = NO_VAL;
	int i;
	char *tmp_info = NULL;
	shares_request_msg_t *object_ptr = NULL;

	xassert(msg != NULL);

	object_ptr = xmalloc(sizeof(shares_request_msg_t));
	*msg = object_ptr;

	safe_unpack32(&count, buffer);
	if (count != NO_VAL) {
		object_ptr->acct_list = list_create(slurm_destroy_char);
		for (i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info,
					       &uint32_tmp, buffer);
			list_append(object_ptr->acct_list, tmp_info);
		}
	}

	safe_unpack32(&count, buffer);
	if (count != NO_VAL) {
		object_ptr->user_list = list_create(slurm_destroy_char);
		for (i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info,
					       &uint32_tmp, buffer);
			list_append(object_ptr->user_list, tmp_info);
		}
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_shares_request_msg(object_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void _pack_shares_response_msg(shares_response_msg_t * msg, Buf buffer,
				      uint16_t protocol_version)
{
	ListIterator itr = NULL;
	association_shares_object_t *share = NULL;
	uint32_t count = NO_VAL;

	xassert(msg != NULL);
	if (msg->assoc_shares_list)
		count = list_count(msg->assoc_shares_list);
	pack32(count, buffer);
	if (count && count != NO_VAL) {
		itr = list_iterator_create(msg->assoc_shares_list);
		while ((share = list_next(itr)))
			_pack_assoc_shares_object(share, buffer,
						  protocol_version);
		list_iterator_destroy(itr);
	}
	pack64(msg->tot_shares, buffer);
}

static int _unpack_shares_response_msg(shares_response_msg_t ** msg,
				       Buf buffer,
				       uint16_t protocol_version)
{
	uint32_t count = NO_VAL;
	int i = 0;
	void *tmp_info = NULL;
	shares_response_msg_t *object_ptr = NULL;
	xassert(msg != NULL);

	object_ptr = xmalloc(sizeof(shares_response_msg_t));
	*msg = object_ptr;

	safe_unpack32(&count, buffer);
	if (count != NO_VAL) {
		object_ptr->assoc_shares_list =
			list_create(slurm_destroy_association_shares_object);
		for (i=0; i<count; i++) {
			if (_unpack_assoc_shares_object(&tmp_info, buffer,
						       protocol_version)
			   != SLURM_SUCCESS)
				goto unpack_error;
			list_append(object_ptr->assoc_shares_list, tmp_info);
		}
	}

	safe_unpack64(&object_ptr->tot_shares, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_shares_response_msg(object_ptr);
	*msg = NULL;
	return SLURM_ERROR;

}

static void _pack_priority_factors_object(void *in, Buf buffer,
					  uint16_t protocol_version)
{
	priority_factors_object_t *object = (priority_factors_object_t *)in;

	if (!object) {
		pack32(0, buffer);
		pack32(0, buffer);

		packdouble(0, buffer);
		packdouble(0, buffer);
		packdouble(0, buffer);
		packdouble(0, buffer);
		packdouble(0, buffer);

		pack16(0, buffer);

		return;
	}

	pack32(object->job_id, buffer);
	pack32(object->user_id, buffer);

	packdouble(object->priority_age, buffer);
	packdouble(object->priority_fs, buffer);
	packdouble(object->priority_js, buffer);
	packdouble(object->priority_part, buffer);
	packdouble(object->priority_qos, buffer);

	pack16(object->nice, buffer);
}

static int _unpack_priority_factors_object(void **object, Buf buffer,
					   uint16_t protocol_version)
{
	priority_factors_object_t *object_ptr =
		xmalloc(sizeof(priority_factors_object_t));

	*object = (void *) object_ptr;
	safe_unpack32(&object_ptr->job_id, buffer);
	safe_unpack32(&object_ptr->user_id, buffer);

	safe_unpackdouble(&object_ptr->priority_age, buffer);
	safe_unpackdouble(&object_ptr->priority_fs, buffer);
	safe_unpackdouble(&object_ptr->priority_js, buffer);
	safe_unpackdouble(&object_ptr->priority_part, buffer);
	safe_unpackdouble(&object_ptr->priority_qos, buffer);

	safe_unpack16(&object_ptr->nice, buffer);

	return SLURM_SUCCESS;

unpack_error:
	xfree(object);
	*object = NULL;
	return SLURM_ERROR;
}

static void
_pack_priority_factors_request_msg(priority_factors_request_msg_t * msg,
				   Buf buffer,
				   uint16_t protocol_version)
{
	uint32_t count = NO_VAL;
	uint32_t* tmp = NULL;
	ListIterator itr = NULL;

	xassert(msg != NULL);

	if (msg->job_id_list)
		count = list_count(msg->job_id_list);
	pack32(count, buffer);
	if (count && count != NO_VAL) {
		itr = list_iterator_create(msg->job_id_list);
		while ((tmp = list_next(itr))) {
			pack32(*tmp, buffer);
		}
		list_iterator_destroy(itr);
	}

	count = NO_VAL;
	if (msg->uid_list)
		count = list_count(msg->uid_list);
	pack32(count, buffer);
	if (count && count != NO_VAL) {
		itr = list_iterator_create(msg->uid_list);
		while ((tmp = list_next(itr))) {
			pack32(*tmp, buffer);
		}
		list_iterator_destroy(itr);
	}

}

static int
_unpack_priority_factors_request_msg(priority_factors_request_msg_t ** msg,
				     Buf buffer,
				     uint16_t protocol_version)
{
	uint32_t* uint32_tmp;
	uint32_t count = NO_VAL;
	int i;
	priority_factors_request_msg_t *object_ptr = NULL;

	xassert(msg != NULL);

	object_ptr = xmalloc(sizeof(priority_factors_request_msg_t));
	*msg = object_ptr;

	safe_unpack32(&count, buffer);
	if (count != NO_VAL) {
		object_ptr->job_id_list = list_create(slurm_destroy_uint32_ptr);
		for (i=0; i<count; i++) {
			uint32_tmp = xmalloc(sizeof(uint32_t));
			safe_unpack32(uint32_tmp, buffer);
			list_append(object_ptr->job_id_list, uint32_tmp);
		}
	}

	safe_unpack32(&count, buffer);
	if (count != NO_VAL) {
		object_ptr->uid_list = list_create(slurm_destroy_uint32_ptr);
		for (i=0; i<count; i++) {
			uint32_tmp = xmalloc(sizeof(uint32_t));
			safe_unpack32(uint32_tmp, buffer);
			list_append(object_ptr->uid_list, uint32_tmp);
		}
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_priority_factors_request_msg(object_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_priority_factors_response_msg(priority_factors_response_msg_t * msg,
				    Buf buffer,
				    uint16_t protocol_version)
{
	ListIterator itr = NULL;
	priority_factors_object_t *factors = NULL;
	uint32_t count = NO_VAL;

	xassert(msg != NULL);
	if (msg->priority_factors_list)
		count = list_count(msg->priority_factors_list);
	pack32(count, buffer);
	if (count && count != NO_VAL) {
		itr = list_iterator_create(msg->priority_factors_list);
		while ((factors = list_next(itr)))
			_pack_priority_factors_object(factors, buffer,
						      protocol_version);
		list_iterator_destroy(itr);
	}
}

static void _priority_factors_resp_list_del(void *x)
{
	xfree(x);
}

static int
_unpack_priority_factors_response_msg(priority_factors_response_msg_t ** msg,
				      Buf buffer,
				      uint16_t protocol_version)
{
	uint32_t count = NO_VAL;
	int i = 0;
	void *tmp_info = NULL;
	priority_factors_response_msg_t *object_ptr = NULL;
	xassert(msg != NULL);

	object_ptr = xmalloc(sizeof(priority_factors_response_msg_t));
	*msg = object_ptr;

	safe_unpack32(&count, buffer);
	if (count != NO_VAL) {
		object_ptr->priority_factors_list =
			list_create(_priority_factors_resp_list_del);
		for (i=0; i<count; i++) {
			if (_unpack_priority_factors_object(&tmp_info, buffer,
							    protocol_version)
			    != SLURM_SUCCESS)
				goto unpack_error;
			list_append(object_ptr->priority_factors_list,
				    tmp_info);
		}
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_priority_factors_response_msg(object_ptr);
	*msg = NULL;
	return SLURM_ERROR;

}

static void
_pack_update_front_end_msg(update_front_end_msg_t * msg, Buf buffer,
			   uint16_t protocol_version)
{
	xassert(msg != NULL);

	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		packstr(msg->name, buffer);
		pack32(msg->node_state, buffer);
		packstr(msg->reason, buffer);
		pack32(msg->reason_uid, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->name, buffer);
		pack16(msg->node_state, buffer);
		packstr(msg->reason, buffer);
		pack32(msg->reason_uid, buffer);
	} else {
		error("_pack_update_front_end_msg: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

static int
_unpack_update_front_end_msg(update_front_end_msg_t ** msg, Buf buffer,
			     uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	update_front_end_msg_t *tmp_ptr;
	uint16_t tmp_state;

	/* alloc memory for structure */
	xassert(msg != NULL);
	tmp_ptr = xmalloc(sizeof(update_front_end_msg_t));
	*msg = tmp_ptr;

	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&tmp_ptr->name,
				       &uint32_tmp, buffer);
		safe_unpack32(&tmp_ptr->node_state, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->reason, &uint32_tmp, buffer);
		safe_unpack32(&tmp_ptr->reason_uid, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&tmp_ptr->name,
				       &uint32_tmp, buffer);
		safe_unpack16(&tmp_state, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->reason, &uint32_tmp, buffer);
		safe_unpack32(&tmp_ptr->reason_uid, buffer);
		tmp_ptr->node_state = tmp_state;
	} else {
		error("_unpack_update_front_end_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_update_front_end_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_update_node_msg(update_node_msg_t * msg, Buf buffer,
		      uint16_t protocol_version)
{
	xassert(msg != NULL);

	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		packstr(msg->node_addr, buffer);
		packstr(msg->node_hostname, buffer);
		packstr(msg->node_names, buffer);
		pack32(msg->node_state, buffer);
		packstr(msg->features, buffer);
		packstr(msg->gres, buffer);
		packstr(msg->reason, buffer);
		pack32(msg->weight, buffer);
		pack32(msg->reason_uid, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->node_addr, buffer);
		packstr(msg->node_hostname, buffer);
		packstr(msg->node_names, buffer);
		pack16(msg->node_state, buffer);
		packstr(msg->features, buffer);
		packstr(msg->gres, buffer);
		packstr(msg->reason, buffer);
		pack32(msg->weight, buffer);
		pack32(msg->reason_uid, buffer);
	} else {
		error("_pack_update_node_msg: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

static int
_unpack_update_node_msg(update_node_msg_t ** msg, Buf buffer,
			uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	update_node_msg_t *tmp_ptr;
	uint16_t tmp_state;

	/* alloc memory for structure */
	xassert(msg != NULL);
	tmp_ptr = xmalloc(sizeof(update_node_msg_t));
	*msg = tmp_ptr;

	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&tmp_ptr->node_addr,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->node_hostname,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->node_names,
				       &uint32_tmp, buffer);
		safe_unpack32(&tmp_ptr->node_state, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->features, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->gres, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->reason, &uint32_tmp, buffer);
		safe_unpack32(&tmp_ptr->weight, buffer);
		safe_unpack32(&tmp_ptr->reason_uid, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&tmp_ptr->node_addr,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->node_hostname,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->node_names,
				       &uint32_tmp, buffer);
		safe_unpack16(&tmp_state, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->features, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->gres, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->reason, &uint32_tmp, buffer);
		safe_unpack32(&tmp_ptr->weight, buffer);
		safe_unpack32(&tmp_ptr->reason_uid, buffer);
		tmp_ptr->node_state = tmp_state;
	} else {
		error("_unpack_update_node_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_update_node_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_acct_gather_node_resp_msg(acct_gather_node_resp_msg_t *msg,
				Buf buffer, uint16_t protocol_version)
{
	xassert(msg != NULL);

	packstr(msg->node_name, buffer);
	acct_gather_energy_pack(msg->energy, buffer, protocol_version);
}
static int
_unpack_acct_gather_node_resp_msg(acct_gather_node_resp_msg_t **msg,
				  Buf buffer, uint16_t protocol_version)
{
	acct_gather_node_resp_msg_t *node_data_ptr;
	uint32_t uint32_tmp;
	/* alloc memory for structure */
	xassert(msg != NULL);
	node_data_ptr = xmalloc(sizeof(acct_gather_node_resp_msg_t));
	*msg = node_data_ptr;

	safe_unpackstr_xmalloc(&node_data_ptr->node_name,
			       &uint32_tmp, buffer);
	if (acct_gather_energy_unpack(&node_data_ptr->energy, buffer,
				      protocol_version) != SLURM_SUCCESS)
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_acct_gather_node_resp_msg(node_data_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_acct_gather_energy_req(acct_gather_energy_req_msg_t *msg,
			     Buf buffer, uint16_t protocol_version)
{
	xassert(msg != NULL);
	pack16(msg->delta, buffer);
}

static int
_unpack_acct_gather_energy_req(acct_gather_energy_req_msg_t **msg,
			       Buf buffer, uint16_t protocol_version)
{
	acct_gather_energy_req_msg_t *msg_ptr;

	xassert(msg != NULL);

	msg_ptr = xmalloc(sizeof(acct_gather_energy_req_msg_t));
	*msg = msg_ptr;

	safe_unpack16(&msg_ptr->delta, buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_acct_gather_energy_req_msg(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;

}

static void
_pack_node_registration_status_msg(slurm_node_registration_status_msg_t *
				   msg, Buf buffer,
				   uint16_t protocol_version)
{
	int i;
	uint32_t gres_info_size = 0;
	xassert(msg != NULL);

	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		pack_time(msg->timestamp, buffer);
		pack_time(msg->slurmd_start_time, buffer);
		pack32(msg->status, buffer);
		packstr(msg->node_name, buffer);
		packstr(msg->arch, buffer);
		packstr(msg->cpu_spec_list, buffer);
		packstr(msg->os, buffer);
		pack16(msg->cpus, buffer);
		pack16(msg->boards, buffer);
		pack16(msg->sockets, buffer);
		pack16(msg->cores, buffer);
		pack16(msg->threads, buffer);
		pack32(msg->real_memory, buffer);
		pack32(msg->tmp_disk, buffer);
		pack32(msg->up_time, buffer);
		pack32(msg->hash_val, buffer);
		pack32(msg->cpu_load, buffer);

		pack32(msg->job_count, buffer);
		for (i = 0; i < msg->job_count; i++) {
			pack32(msg->job_id[i], buffer);
		}
		for (i = 0; i < msg->job_count; i++) {
			pack32(msg->step_id[i], buffer);
		}
		pack16(msg->startup, buffer);
		if (msg->startup)
			switch_g_pack_node_info(msg->switch_nodeinfo, buffer,
						protocol_version);
		if (msg->gres_info)
			gres_info_size = get_buf_offset(msg->gres_info);
		pack32(gres_info_size, buffer);
		if (gres_info_size) {
			packmem(get_buf_data(msg->gres_info), gres_info_size,
				buffer);
		}
		acct_gather_energy_pack(msg->energy, buffer, protocol_version);
		packstr(msg->version, buffer);
	} else if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		pack_time(msg->timestamp, buffer);
		pack_time(msg->slurmd_start_time, buffer);
		pack32(msg->status, buffer);
		packstr(msg->node_name, buffer);
		packstr(msg->arch, buffer);
		packstr(msg->os, buffer);
		pack16(msg->cpus, buffer);
		pack16(msg->boards, buffer);
		pack16(msg->sockets, buffer);
		pack16(msg->cores, buffer);
		pack16(msg->threads, buffer);
		pack32(msg->real_memory, buffer);
		pack32(msg->tmp_disk, buffer);
		pack32(msg->up_time, buffer);
		pack32(msg->hash_val, buffer);
		pack32(msg->cpu_load, buffer);

		pack32(msg->job_count, buffer);
		for (i = 0; i < msg->job_count; i++) {
			pack32(msg->job_id[i], buffer);
		}
		for (i = 0; i < msg->job_count; i++) {
			pack32(msg->step_id[i], buffer);
		}
		pack16(msg->startup, buffer);
		if (msg->startup)
			switch_g_pack_node_info(msg->switch_nodeinfo, buffer,
						protocol_version);
		if (msg->gres_info)
			gres_info_size = get_buf_offset(msg->gres_info);
		pack32(gres_info_size, buffer);
		if (gres_info_size) {
			packmem(get_buf_data(msg->gres_info), gres_info_size,
				buffer);
		}
		acct_gather_energy_pack(msg->energy, buffer, protocol_version);
		packstr(msg->version, buffer);
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		pack_time(msg->timestamp, buffer);
		pack_time(msg->slurmd_start_time, buffer);
		pack32(msg->status, buffer);
		packstr(msg->node_name, buffer);
		packstr(msg->arch, buffer);
		packstr(msg->os, buffer);
		pack16(msg->cpus, buffer);
		pack16(msg->boards, buffer);
		pack16(msg->sockets, buffer);
		pack16(msg->cores, buffer);
		pack16(msg->threads, buffer);
		pack32(msg->real_memory, buffer);
		pack32(msg->tmp_disk, buffer);
		pack32(msg->up_time, buffer);
		pack32(msg->hash_val, buffer);
		pack32(msg->cpu_load, buffer);

		pack32(msg->job_count, buffer);
		for (i = 0; i < msg->job_count; i++) {
			pack32(msg->job_id[i], buffer);
		}
		for (i = 0; i < msg->job_count; i++) {
			pack32(msg->step_id[i], buffer);
		}
		pack16(msg->startup, buffer);
		if (msg->startup)
			switch_g_pack_node_info(msg->switch_nodeinfo, buffer,
						protocol_version);
		if (msg->gres_info)
			gres_info_size = get_buf_offset(msg->gres_info);
		pack32(gres_info_size, buffer);
		if (gres_info_size) {
			packmem(get_buf_data(msg->gres_info), gres_info_size,
				buffer);
		}
		acct_gather_energy_pack(msg->energy, buffer, protocol_version);
	} else {
		error("_pack_node_registration_status_msg: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

static int
_unpack_node_registration_status_msg(slurm_node_registration_status_msg_t
				     ** msg, Buf buffer,
				     uint16_t protocol_version)
{
	char *gres_info = NULL;
	uint32_t gres_info_size, uint32_tmp;
	int i;
	slurm_node_registration_status_msg_t *node_reg_ptr;

	/* alloc memory for structure */
	xassert(msg != NULL);
	node_reg_ptr = xmalloc(sizeof(slurm_node_registration_status_msg_t));
	*msg = node_reg_ptr;

	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		/* unpack timestamp of snapshot */
		safe_unpack_time(&node_reg_ptr->timestamp, buffer);
		safe_unpack_time(&node_reg_ptr->slurmd_start_time, buffer);
		/* load the data values */
		safe_unpack32(&node_reg_ptr->status, buffer);
		safe_unpackstr_xmalloc(&node_reg_ptr->node_name,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node_reg_ptr->arch,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node_reg_ptr->cpu_spec_list,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node_reg_ptr->os, &uint32_tmp, buffer);
		safe_unpack16(&node_reg_ptr->cpus, buffer);
		safe_unpack16(&node_reg_ptr->boards, buffer);
		safe_unpack16(&node_reg_ptr->sockets, buffer);
		safe_unpack16(&node_reg_ptr->cores, buffer);
		safe_unpack16(&node_reg_ptr->threads, buffer);
		safe_unpack32(&node_reg_ptr->real_memory, buffer);
		safe_unpack32(&node_reg_ptr->tmp_disk, buffer);
		safe_unpack32(&node_reg_ptr->up_time, buffer);
		safe_unpack32(&node_reg_ptr->hash_val, buffer);
		safe_unpack32(&node_reg_ptr->cpu_load, buffer);

		safe_unpack32(&node_reg_ptr->job_count, buffer);
		node_reg_ptr->job_id =
			xmalloc(sizeof(uint32_t) * node_reg_ptr->job_count);
		for (i = 0; i < node_reg_ptr->job_count; i++) {
			safe_unpack32(&node_reg_ptr->job_id[i], buffer);
		}
		node_reg_ptr->step_id =
			xmalloc(sizeof(uint32_t) * node_reg_ptr->job_count);
		for (i = 0; i < node_reg_ptr->job_count; i++) {
			safe_unpack32(&node_reg_ptr->step_id[i], buffer);
		}

		safe_unpack16(&node_reg_ptr->startup, buffer);
		if (node_reg_ptr->startup
		    &&  (switch_g_alloc_node_info(
				 &node_reg_ptr->switch_nodeinfo)
			 ||   switch_g_unpack_node_info(
				 node_reg_ptr->switch_nodeinfo, buffer,
				 protocol_version)))
			goto unpack_error;

		safe_unpack32(&gres_info_size, buffer);
		if (gres_info_size) {
			safe_unpackmem_xmalloc(&gres_info, &uint32_tmp, buffer);
			if (gres_info_size != uint32_tmp)
				goto unpack_error;
			node_reg_ptr->gres_info = create_buf(gres_info,
							     gres_info_size);
		}
		if (acct_gather_energy_unpack(&node_reg_ptr->energy, buffer,
					      protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr_xmalloc(&node_reg_ptr->version,
				       &uint32_tmp, buffer);
	} else if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		/* unpack timestamp of snapshot */
		safe_unpack_time(&node_reg_ptr->timestamp, buffer);
		safe_unpack_time(&node_reg_ptr->slurmd_start_time, buffer);
		/* load the data values */
		safe_unpack32(&node_reg_ptr->status, buffer);
		safe_unpackstr_xmalloc(&node_reg_ptr->node_name,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node_reg_ptr->arch,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node_reg_ptr->os, &uint32_tmp, buffer);
		safe_unpack16(&node_reg_ptr->cpus, buffer);
		safe_unpack16(&node_reg_ptr->boards, buffer);
		safe_unpack16(&node_reg_ptr->sockets, buffer);
		safe_unpack16(&node_reg_ptr->cores, buffer);
		safe_unpack16(&node_reg_ptr->threads, buffer);
		safe_unpack32(&node_reg_ptr->real_memory, buffer);
		safe_unpack32(&node_reg_ptr->tmp_disk, buffer);
		safe_unpack32(&node_reg_ptr->up_time, buffer);
		safe_unpack32(&node_reg_ptr->hash_val, buffer);
		safe_unpack32(&node_reg_ptr->cpu_load, buffer);

		safe_unpack32(&node_reg_ptr->job_count, buffer);
		node_reg_ptr->job_id =
			xmalloc(sizeof(uint32_t) * node_reg_ptr->job_count);
		for (i = 0; i < node_reg_ptr->job_count; i++) {
			safe_unpack32(&node_reg_ptr->job_id[i], buffer);
		}
		node_reg_ptr->step_id =
			xmalloc(sizeof(uint32_t) * node_reg_ptr->job_count);
		for (i = 0; i < node_reg_ptr->job_count; i++) {
			safe_unpack32(&node_reg_ptr->step_id[i], buffer);
		}

		safe_unpack16(&node_reg_ptr->startup, buffer);
		if (node_reg_ptr->startup
		    &&  (switch_g_alloc_node_info(
				 &node_reg_ptr->switch_nodeinfo)
			 ||   switch_g_unpack_node_info(
				 node_reg_ptr->switch_nodeinfo, buffer,
				 protocol_version)))
			goto unpack_error;

		safe_unpack32(&gres_info_size, buffer);
		if (gres_info_size) {
			safe_unpackmem_xmalloc(&gres_info, &uint32_tmp, buffer);
			if (gres_info_size != uint32_tmp)
				goto unpack_error;
			node_reg_ptr->gres_info = create_buf(gres_info,
							     gres_info_size);
		}
		if (acct_gather_energy_unpack(&node_reg_ptr->energy, buffer,
					      protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpackstr_xmalloc(&node_reg_ptr->version,
				       &uint32_tmp, buffer);
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		/* unpack timestamp of snapshot */
		safe_unpack_time(&node_reg_ptr->timestamp, buffer);
		safe_unpack_time(&node_reg_ptr->slurmd_start_time, buffer);
		/* load the data values */
		safe_unpack32(&node_reg_ptr->status, buffer);
		safe_unpackstr_xmalloc(&node_reg_ptr->node_name,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node_reg_ptr->arch,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node_reg_ptr->os, &uint32_tmp, buffer);
		safe_unpack16(&node_reg_ptr->cpus, buffer);
		safe_unpack16(&node_reg_ptr->boards, buffer);
		safe_unpack16(&node_reg_ptr->sockets, buffer);
		safe_unpack16(&node_reg_ptr->cores, buffer);
		safe_unpack16(&node_reg_ptr->threads, buffer);
		safe_unpack32(&node_reg_ptr->real_memory, buffer);
		safe_unpack32(&node_reg_ptr->tmp_disk, buffer);
		safe_unpack32(&node_reg_ptr->up_time, buffer);
		safe_unpack32(&node_reg_ptr->hash_val, buffer);
		safe_unpack32(&node_reg_ptr->cpu_load, buffer);

		safe_unpack32(&node_reg_ptr->job_count, buffer);
		node_reg_ptr->job_id =
			xmalloc(sizeof(uint32_t) * node_reg_ptr->job_count);
		for (i = 0; i < node_reg_ptr->job_count; i++) {
			safe_unpack32(&node_reg_ptr->job_id[i], buffer);
		}
		node_reg_ptr->step_id =
			xmalloc(sizeof(uint32_t) * node_reg_ptr->job_count);
		for (i = 0; i < node_reg_ptr->job_count; i++) {
			safe_unpack32(&node_reg_ptr->step_id[i], buffer);
		}

		safe_unpack16(&node_reg_ptr->startup, buffer);
		if (node_reg_ptr->startup
		    &&  (switch_g_alloc_node_info(
				 &node_reg_ptr->switch_nodeinfo)
			 ||   switch_g_unpack_node_info(
				 node_reg_ptr->switch_nodeinfo, buffer,
				 protocol_version)))
			goto unpack_error;

		safe_unpack32(&gres_info_size, buffer);
		if (gres_info_size) {
			safe_unpackmem_xmalloc(&gres_info, &uint32_tmp, buffer);
			if (gres_info_size != uint32_tmp)
				goto unpack_error;
			node_reg_ptr->gres_info = create_buf(gres_info,
							     gres_info_size);
		}
		if (acct_gather_energy_unpack(&node_reg_ptr->energy, buffer,
					      protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;
	} else {
		error("_unpack_node_registration_status_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_node_registration_status_msg(node_reg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_resource_allocation_response_msg(resource_allocation_response_msg_t *msg,
				       Buf buffer,
				       uint16_t protocol_version)
{
	xassert(msg != NULL);

	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		pack32(msg->error_code, buffer);
		pack32(msg->job_id, buffer);
		pack32(msg->pn_min_memory, buffer);
		packstr(msg->alias_list, buffer);
		packstr(msg->node_list, buffer);
		packstr(msg->partition, buffer);

		pack32(msg->num_cpu_groups, buffer);
		if (msg->num_cpu_groups) {
			pack16_array(msg->cpus_per_node, msg->num_cpu_groups, buffer);
			pack32_array(msg->cpu_count_reps, msg->num_cpu_groups, buffer);
		}

		pack32(msg->node_cnt, buffer);

		select_g_select_jobinfo_pack(msg->select_jobinfo, buffer,
					     protocol_version);
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		pack32(msg->error_code, buffer);
		pack32(msg->job_id, buffer);
		pack32(msg->pn_min_memory, buffer);
		packstr(msg->alias_list, buffer);
		packstr(msg->node_list, buffer);

		pack32(msg->num_cpu_groups, buffer);
		if (msg->num_cpu_groups) {
			pack16_array(msg->cpus_per_node, msg->num_cpu_groups, buffer);
			pack32_array(msg->cpu_count_reps, msg->num_cpu_groups, buffer);
		}

		pack32(msg->node_cnt, buffer);

		select_g_select_jobinfo_pack(msg->select_jobinfo, buffer,
					     protocol_version);
	} else {
		error("_pack_resource_allocation_response_msg: "
		      "protocol_version %hu not supported", protocol_version);
	}
}

static int
_unpack_resource_allocation_response_msg(
	resource_allocation_response_msg_t** msg, Buf buffer,
	uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	resource_allocation_response_msg_t *tmp_ptr;

	/* alloc memory for structure */
	xassert(msg != NULL);
	tmp_ptr = xmalloc(sizeof(resource_allocation_response_msg_t));
	*msg = tmp_ptr;

	/* load the data values */
	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		safe_unpack32(&tmp_ptr->error_code, buffer);
		safe_unpack32(&tmp_ptr->job_id, buffer);
		safe_unpack32(&tmp_ptr->pn_min_memory, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->alias_list, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->node_list, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->partition, &uint32_tmp,
				       buffer);

		safe_unpack32(&tmp_ptr->num_cpu_groups, buffer);
		if (tmp_ptr->num_cpu_groups > 0) {
			safe_unpack16_array(&tmp_ptr->cpus_per_node,
					    &uint32_tmp, buffer);
			if (tmp_ptr->num_cpu_groups != uint32_tmp)
				goto unpack_error;
			safe_unpack32_array(&tmp_ptr->cpu_count_reps,
					    &uint32_tmp, buffer);
			if (tmp_ptr->num_cpu_groups != uint32_tmp)
				goto unpack_error;
		} else {
			tmp_ptr->cpus_per_node = NULL;
			tmp_ptr->cpu_count_reps = NULL;
		}

		safe_unpack32(&tmp_ptr->node_cnt, buffer);
		if (select_g_select_jobinfo_unpack(&tmp_ptr->select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		safe_unpack32(&tmp_ptr->error_code, buffer);
		safe_unpack32(&tmp_ptr->job_id, buffer);
		safe_unpack32(&tmp_ptr->pn_min_memory, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->alias_list, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->node_list, &uint32_tmp,
				       buffer);

		safe_unpack32(&tmp_ptr->num_cpu_groups, buffer);
		if (tmp_ptr->num_cpu_groups > 0) {
			safe_unpack16_array(&tmp_ptr->cpus_per_node,
					    &uint32_tmp, buffer);
			if (tmp_ptr->num_cpu_groups != uint32_tmp)
				goto unpack_error;
			safe_unpack32_array(&tmp_ptr->cpu_count_reps,
					    &uint32_tmp, buffer);
			if (tmp_ptr->num_cpu_groups != uint32_tmp)
				goto unpack_error;
		} else {
			tmp_ptr->cpus_per_node = NULL;
			tmp_ptr->cpu_count_reps = NULL;
		}

		safe_unpack32(&tmp_ptr->node_cnt, buffer);
		if (select_g_select_jobinfo_unpack(&tmp_ptr->select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;
	} else {
		error("_unpack_resource_allocation_response_msg: "
		      "protocol_version %hu not supported", protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_resource_allocation_response_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_job_alloc_info_response_msg(job_alloc_info_response_msg_t * msg,
				  Buf buffer, uint16_t protocol_version)
{
	xassert(msg != NULL);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->error_code, buffer);
		pack32(msg->job_id, buffer);
		packstr(msg->node_list, buffer);

		pack32(msg->num_cpu_groups, buffer);
		if (msg->num_cpu_groups) {
			pack16_array(msg->cpus_per_node, msg->num_cpu_groups,
				     buffer);
			pack32_array(msg->cpu_count_reps, msg->num_cpu_groups,
				     buffer);
		}

		pack32(msg->node_cnt, buffer);
		if (msg->node_cnt > 0)
			_pack_slurm_addr_array(msg->node_addr, msg->node_cnt,
					       buffer, protocol_version);

		select_g_select_jobinfo_pack(msg->select_jobinfo, buffer,
					     protocol_version);
	} else {
		error("_pack_job_alloc_info_response_msg: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

static int
_unpack_job_alloc_info_response_msg(job_alloc_info_response_msg_t ** msg,
				    Buf buffer, uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	job_alloc_info_response_msg_t *tmp_ptr;

	/* alloc memory for structure */
	xassert(msg != NULL);
	tmp_ptr = xmalloc(sizeof(job_alloc_info_response_msg_t));
	*msg = tmp_ptr;

	/* load the data values */
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&tmp_ptr->error_code, buffer);
		safe_unpack32(&tmp_ptr->job_id, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->node_list, &uint32_tmp,
				       buffer);

		safe_unpack32(&tmp_ptr->num_cpu_groups, buffer);
		if (tmp_ptr->num_cpu_groups > 0) {
			safe_unpack16_array(&tmp_ptr->cpus_per_node,
					    &uint32_tmp, buffer);
			if (tmp_ptr->num_cpu_groups != uint32_tmp)
				goto unpack_error;
			safe_unpack32_array(&tmp_ptr->cpu_count_reps,
					    &uint32_tmp, buffer);
			if (tmp_ptr->num_cpu_groups != uint32_tmp)
				goto unpack_error;
		}

		safe_unpack32(&tmp_ptr->node_cnt, buffer);
		if (tmp_ptr->node_cnt > 0) {
			if (_unpack_slurm_addr_array(&(tmp_ptr->node_addr),
						     &uint32_tmp, buffer,
						     protocol_version))
				goto unpack_error;
			if (uint32_tmp != tmp_ptr->node_cnt)
				goto unpack_error;
		} else
			tmp_ptr->node_addr = NULL;

		if (select_g_select_jobinfo_unpack(&tmp_ptr->select_jobinfo,
						   buffer,
						   protocol_version))
			goto unpack_error;
	} else {
		error("_unpack_job_alloc_info_response_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_alloc_info_response_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_job_sbcast_cred_msg(job_sbcast_cred_msg_t * msg, Buf buffer,
			  uint16_t protocol_version)
{
	xassert(msg != NULL);

	pack32(msg->job_id, buffer);
	packstr(msg->node_list, buffer);

	pack32(msg->node_cnt, buffer);
	if (msg->node_cnt > 0)
		_pack_slurm_addr_array(msg->node_addr, msg->node_cnt, buffer,
				       protocol_version);
	pack_sbcast_cred(msg->sbcast_cred, buffer);
}

static int
_unpack_job_sbcast_cred_msg(job_sbcast_cred_msg_t ** msg, Buf buffer,
			    uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	job_sbcast_cred_msg_t *tmp_ptr;

	/* alloc memory for structure */
	xassert(msg != NULL);
	tmp_ptr = xmalloc(sizeof(job_sbcast_cred_msg_t));
	*msg = tmp_ptr;

	/* load the data values */
	safe_unpack32(&tmp_ptr->job_id, buffer);
	safe_unpackstr_xmalloc(&tmp_ptr->node_list, &uint32_tmp, buffer);

	safe_unpack32(&tmp_ptr->node_cnt, buffer);
	if (tmp_ptr->node_cnt > 0) {
		if (_unpack_slurm_addr_array(&(tmp_ptr->node_addr),
					     &uint32_tmp, buffer,
					     protocol_version))
			goto unpack_error;
		if (uint32_tmp != tmp_ptr->node_cnt)
			goto unpack_error;
	} else
		tmp_ptr->node_addr = NULL;

	tmp_ptr->sbcast_cred = unpack_sbcast_cred(buffer);
	if (tmp_ptr->sbcast_cred == NULL)
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_sbcast_cred_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_submit_response_msg(submit_response_msg_t * msg, Buf buffer,
			  uint16_t protocol_version)
{
	xassert(msg != NULL);

	pack32((uint32_t)msg->job_id, buffer);
	pack32((uint32_t)msg->step_id, buffer);
	pack32((uint32_t)msg->error_code, buffer);
}

static int
_unpack_submit_response_msg(submit_response_msg_t ** msg, Buf buffer,
			    uint16_t protocol_version)
{
	submit_response_msg_t *tmp_ptr;

	/* alloc memory for structure */
	xassert(msg != NULL);
	tmp_ptr = xmalloc(sizeof(submit_response_msg_t));
	*msg = tmp_ptr;

	/* load the data values */
	safe_unpack32(&tmp_ptr->job_id, buffer);
	safe_unpack32(&tmp_ptr->step_id, buffer);
	safe_unpack32(&tmp_ptr->error_code, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_submit_response_response_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static int
_unpack_node_info_msg(node_info_msg_t ** msg, Buf buffer,
		      uint16_t protocol_version)
{
	int i;
	node_info_t *node = NULL;

	xassert(msg != NULL);
	*msg = xmalloc(sizeof(node_info_msg_t));

	/* load buffer's header (data structure version and time) */
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&((*msg)->record_count), buffer);
		safe_unpack32(&((*msg)->node_scaling), buffer);
		safe_unpack_time(&((*msg)->last_update), buffer);

		node = (*msg)->node_array =
			xmalloc(sizeof(node_info_t) * (*msg)->record_count);

		/* load individual job info */
		for (i = 0; i < (*msg)->record_count; i++) {
			if (_unpack_node_info_members(&node[i], buffer,
						      protocol_version))
				goto unpack_error;
		}
	} else {
		error("_unpack_node_info_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_node_info_msg(*msg);
	*msg = NULL;
	return SLURM_ERROR;
}

static int
_unpack_node_info_members(node_info_t * node, Buf buffer,
			  uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	uint16_t tmp_state;

	xassert(node != NULL);

	tmp_state = node->node_state;

	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&node->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->node_hostname, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&node->node_addr, &uint32_tmp, buffer);
		safe_unpack32(&node->node_state, buffer);
		safe_unpackstr_xmalloc(&node->version, &uint32_tmp, buffer);

		safe_unpack16(&node->cpus, buffer);
		safe_unpack16(&node->boards, buffer);
		safe_unpack16(&node->sockets, buffer);
		safe_unpack16(&node->cores, buffer);
		safe_unpack16(&node->threads, buffer);

		safe_unpack32(&node->real_memory, buffer);
		safe_unpack32(&node->tmp_disk, buffer);

		safe_unpack16(&node->core_spec_cnt, buffer);
		safe_unpack32(&node->mem_spec_limit, buffer);
		safe_unpackstr_xmalloc(&node->cpu_spec_list, &uint32_tmp,
				       buffer);

		safe_unpack32(&node->cpu_load, buffer);
		safe_unpack32(&node->weight, buffer);
		safe_unpack32(&node->reason_uid, buffer);

		safe_unpack_time(&node->boot_time, buffer);
		safe_unpack_time(&node->reason_time, buffer);
		safe_unpack_time(&node->slurmd_start_time, buffer);

		select_g_select_nodeinfo_unpack(&node->select_nodeinfo, buffer,
						protocol_version);

		safe_unpackstr_xmalloc(&node->arch, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->features, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->gres, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->gres_drain, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->gres_used, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->os, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->reason, &uint32_tmp, buffer);
		if (acct_gather_energy_unpack(&node->energy, buffer,
					      protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;
		if (ext_sensors_data_unpack(&node->ext_sensors, buffer,
					      protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;
	} else if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&node->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->node_hostname, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&node->node_addr, &uint32_tmp, buffer);
		safe_unpack16(&tmp_state, buffer);
		safe_unpackstr_xmalloc(&node->version, &uint32_tmp, buffer);

		safe_unpack16(&node->cpus, buffer);
		safe_unpack16(&node->boards, buffer);
		safe_unpack16(&node->sockets, buffer);
		safe_unpack16(&node->cores, buffer);
		safe_unpack16(&node->threads, buffer);

		safe_unpack32(&node->real_memory, buffer);
		safe_unpack32(&node->tmp_disk, buffer);
		safe_unpack32(&node->cpu_load, buffer);
		safe_unpack32(&node->weight, buffer);
		safe_unpack32(&node->reason_uid, buffer);

		safe_unpack_time(&node->boot_time, buffer);
		safe_unpack_time(&node->reason_time, buffer);
		safe_unpack_time(&node->slurmd_start_time, buffer);

		select_g_select_nodeinfo_unpack(&node->select_nodeinfo, buffer,
						protocol_version);

		safe_unpackstr_xmalloc(&node->arch, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->features, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->gres, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->os, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->reason, &uint32_tmp, buffer);
		if (acct_gather_energy_unpack(&node->energy, buffer,
					      protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;
		if (ext_sensors_data_unpack(&node->ext_sensors, buffer,
					      protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&node->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->node_hostname, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&node->node_addr, &uint32_tmp, buffer);
		safe_unpack16(&tmp_state, buffer);
		safe_unpack16(&node->cpus, buffer);
		safe_unpack16(&node->boards, buffer);
		safe_unpack16(&node->sockets, buffer);
		safe_unpack16(&node->cores, buffer);
		safe_unpack16(&node->threads, buffer);

		safe_unpack32(&node->real_memory, buffer);
		safe_unpack32(&node->tmp_disk, buffer);
		safe_unpack32(&node->cpu_load, buffer);
		safe_unpack32(&node->weight, buffer);
		safe_unpack32(&node->reason_uid, buffer);

		safe_unpack_time(&node->boot_time, buffer);
		safe_unpack_time(&node->reason_time, buffer);
		safe_unpack_time(&node->slurmd_start_time, buffer);

		select_g_select_nodeinfo_unpack(&node->select_nodeinfo, buffer,
						protocol_version);

		safe_unpackstr_xmalloc(&node->arch, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->features, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->gres, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->os, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node->reason, &uint32_tmp, buffer);
		if (acct_gather_energy_unpack(&node->energy, buffer,
					      protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;
		if (ext_sensors_data_unpack(&node->ext_sensors, buffer,
					      protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;
	} else {
		error("_unpack_node_info_members: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_node_info_members(node);
	return SLURM_ERROR;
}

static void
_pack_update_partition_msg(update_part_msg_t * msg, Buf buffer,
			   uint16_t protocol_version)
{
	xassert(msg != NULL);

	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		packstr(msg->allow_accounts, buffer);
		packstr(msg->allow_alloc_nodes, buffer);
		packstr(msg->allow_groups, buffer);
		packstr(msg->allow_qos,    buffer);
		packstr(msg->alternate,    buffer);
		packstr(msg->deny_accounts, buffer);
		packstr(msg->deny_qos,     buffer);
		packstr(msg->name,         buffer);
		packstr(msg->nodes,        buffer);

		pack32(msg-> grace_time,   buffer);
		pack32(msg-> max_time,     buffer);
		pack32(msg-> default_time, buffer);
		pack32(msg-> max_nodes,    buffer);
		pack32(msg-> min_nodes,    buffer);
		pack32(msg-> max_cpus_per_node, buffer);
		pack32(msg-> def_mem_per_cpu, buffer);
		pack32(msg-> max_mem_per_cpu, buffer);

		pack16(msg-> flags,        buffer);
		pack16(msg-> max_share,    buffer);
		pack16(msg-> preempt_mode, buffer);
		pack16(msg-> priority,     buffer);
		pack16(msg-> state_up,     buffer);
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		packstr(msg->allow_groups, buffer);
		packstr(msg->alternate,    buffer);
		pack32(msg-> grace_time,   buffer);
		pack32(msg-> max_time,     buffer);
		pack32(msg-> default_time, buffer);
		pack32(msg-> max_nodes,    buffer);
		pack32(msg-> min_nodes,    buffer);
		pack32(msg-> max_cpus_per_node, buffer);
		pack32(msg-> def_mem_per_cpu, buffer);
		pack32(msg-> max_mem_per_cpu, buffer);
		packstr(msg->name,         buffer);
		packstr(msg->nodes,        buffer);
		pack16(msg-> flags,        buffer);
		pack16(msg-> max_share,    buffer);
		pack16(msg-> preempt_mode, buffer);
		pack16(msg-> priority,     buffer);
		pack16(msg-> state_up,     buffer);

		packstr(msg->allow_alloc_nodes, buffer);
	} else {
		error("_pack_update_partition_msg: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

static int
_unpack_update_partition_msg(update_part_msg_t ** msg, Buf buffer,
			     uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	update_part_msg_t *tmp_ptr;

	xassert(msg != NULL);

	/* alloc memory for structure */
	tmp_ptr = xmalloc(sizeof(update_part_msg_t));
	*msg = tmp_ptr;

	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&tmp_ptr->allow_accounts,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->allow_alloc_nodes,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->allow_groups,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->allow_qos,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->alternate, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->deny_accounts,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->deny_qos,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->nodes, &uint32_tmp, buffer);

		safe_unpack32(&tmp_ptr->grace_time, buffer);
		safe_unpack32(&tmp_ptr->max_time, buffer);
		safe_unpack32(&tmp_ptr->default_time, buffer);
		safe_unpack32(&tmp_ptr->max_nodes, buffer);
		safe_unpack32(&tmp_ptr->min_nodes, buffer);
		safe_unpack32(&tmp_ptr->max_cpus_per_node, buffer);
		safe_unpack32(&tmp_ptr->def_mem_per_cpu, buffer);
		safe_unpack32(&tmp_ptr->max_mem_per_cpu, buffer);

		safe_unpack16(&tmp_ptr->flags,     buffer);
		safe_unpack16(&tmp_ptr->max_share, buffer);
		safe_unpack16(&tmp_ptr->preempt_mode, buffer);
		safe_unpack16(&tmp_ptr->priority,  buffer);
		safe_unpack16(&tmp_ptr->state_up,  buffer);
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&tmp_ptr->allow_groups,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->alternate,
				       &uint32_tmp, buffer);
		safe_unpack32(&tmp_ptr->grace_time, buffer);
		safe_unpack32(&tmp_ptr->max_time, buffer);
		safe_unpack32(&tmp_ptr->default_time, buffer);
		safe_unpack32(&tmp_ptr->max_nodes, buffer);
		safe_unpack32(&tmp_ptr->min_nodes, buffer);
		safe_unpack32(&tmp_ptr->max_cpus_per_node, buffer);
		safe_unpack32(&tmp_ptr->def_mem_per_cpu, buffer);
		safe_unpack32(&tmp_ptr->max_mem_per_cpu, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->nodes, &uint32_tmp, buffer);

		safe_unpack16(&tmp_ptr->flags,     buffer);
		safe_unpack16(&tmp_ptr->max_share, buffer);
		safe_unpack16(&tmp_ptr->preempt_mode, buffer);
		safe_unpack16(&tmp_ptr->priority,  buffer);
		safe_unpack16(&tmp_ptr->state_up,  buffer);

		safe_unpackstr_xmalloc(&tmp_ptr->allow_alloc_nodes,
				       &uint32_tmp, buffer);
	} else {
		error("_unpack_update_partition_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_update_part_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_update_resv_msg(resv_desc_msg_t * msg, Buf buffer,
		      uint16_t protocol_version)
{
	uint32_t array_len;
	xassert(msg != NULL);

	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		packstr(msg->name,         buffer);
		pack_time(msg->start_time, buffer);
		pack_time(msg->end_time,   buffer);
		pack32(msg->duration,      buffer);
		pack32(msg->flags,         buffer);
		if (msg->node_cnt) {
			for (array_len = 0; msg->node_cnt[array_len];
			     array_len++) {
				/* determine array length */
			}
			array_len++;	/* Include trailing zero */
		} else
			array_len = 0;
		pack32_array(msg->node_cnt, array_len, buffer);
		if (msg->core_cnt) {
			for (array_len = 0; msg->core_cnt[array_len];
			     array_len++) {
				/* determine array length */
			}
			array_len++;	/* Include trailing zero */
		} else
			array_len = 0;
		pack32_array(msg->core_cnt, array_len, buffer);
		packstr(msg->node_list,    buffer);
		packstr(msg->features,     buffer);
		packstr(msg->licenses,     buffer);
		packstr(msg->partition,    buffer);

		packstr(msg->users,        buffer);
		packstr(msg->accounts,     buffer);
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		uint16_t flags;
		packstr(msg->name,         buffer);
		pack_time(msg->start_time, buffer);
		pack_time(msg->end_time,   buffer);
		pack32(msg->duration,      buffer);
		flags = (uint16_t) msg->flags;
		pack16(flags,              buffer);
		if (msg->node_cnt) {
			for (array_len = 0; msg->node_cnt[array_len];
			     array_len++) {
				/* determine array length */
			}
			array_len++;	/* Include trailing zero */
		} else
			array_len = 0;
		pack32_array(msg->node_cnt, array_len, buffer);
		if (msg->core_cnt) {
			for (array_len = 0; msg->core_cnt[array_len];
			     array_len++) {
				/* determine array length */
			}
			array_len++;	/* Include trailing zero */
		} else
			array_len = 0;
		pack32_array(msg->core_cnt, array_len, buffer);
		packstr(msg->node_list,    buffer);
		packstr(msg->features,     buffer);
		packstr(msg->licenses,     buffer);
		packstr(msg->partition,    buffer);

		packstr(msg->users,        buffer);
		packstr(msg->accounts,     buffer);
	} else {
		error("_pack_update_resv_msg: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

static int
_unpack_update_resv_msg(resv_desc_msg_t ** msg, Buf buffer,
			uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	resv_desc_msg_t *tmp_ptr;

	xassert(msg != NULL);

	/* alloc memory for structure */
	tmp_ptr = xmalloc(sizeof(resv_desc_msg_t));
	*msg = tmp_ptr;

	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&tmp_ptr->name, &uint32_tmp, buffer);
		safe_unpack_time(&tmp_ptr->start_time, buffer);
		safe_unpack_time(&tmp_ptr->end_time,   buffer);
		safe_unpack32(&tmp_ptr->duration,      buffer);
		safe_unpack32(&tmp_ptr->flags,         buffer);
		safe_unpack32_array(&tmp_ptr->node_cnt, &uint32_tmp, buffer);
		if (uint32_tmp > 0) {
			/* Must be zero terminated */
			if (tmp_ptr->node_cnt[uint32_tmp-1] != 0)
				goto unpack_error;
		} else {
			/* This avoids a pointer to a zero length buffer */
			xfree(tmp_ptr->node_cnt);
		}
		safe_unpack32_array(&tmp_ptr->core_cnt, &uint32_tmp, buffer);
		if (uint32_tmp > 0) {
			/* Must be zero terminated */
			if (tmp_ptr->core_cnt[uint32_tmp-1] != 0)
				goto unpack_error;
		} else {
			/* This avoids a pointer to a zero length buffer */
			xfree(tmp_ptr->core_cnt);
		}
		safe_unpackstr_xmalloc(&tmp_ptr->node_list,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->features,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->licenses,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->partition,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&tmp_ptr->users,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->accounts,
				       &uint32_tmp, buffer);
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		uint16_t flags;
		safe_unpackstr_xmalloc(&tmp_ptr->name, &uint32_tmp, buffer);
		safe_unpack_time(&tmp_ptr->start_time, buffer);
		safe_unpack_time(&tmp_ptr->end_time,   buffer);
		safe_unpack32(&tmp_ptr->duration,      buffer);
		safe_unpack16(&flags,                  buffer);
		tmp_ptr->flags = flags;
		safe_unpack32_array(&tmp_ptr->node_cnt, &uint32_tmp, buffer);
		if (uint32_tmp > 0) {
			/* Must be zero terminated */
			if (tmp_ptr->node_cnt[uint32_tmp-1] != 0)
				goto unpack_error;
		} else {
			/* This avoids a pointer to a zero length buffer */
			xfree(tmp_ptr->node_cnt);
		}
		safe_unpack32_array(&tmp_ptr->core_cnt, &uint32_tmp, buffer);
		if (uint32_tmp > 0) {
			/* Must be zero terminated */
			if (tmp_ptr->core_cnt[uint32_tmp-1] != 0)
				goto unpack_error;
		} else {
			/* This avoids a pointer to a zero length buffer */
			xfree(tmp_ptr->core_cnt);
		}
		safe_unpackstr_xmalloc(&tmp_ptr->node_list,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->features,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->licenses,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->partition,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&tmp_ptr->users,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&tmp_ptr->accounts,
				       &uint32_tmp, buffer);
	} else {
		error("_unpack_update_resv_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_resv_desc_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_delete_partition_msg(delete_part_msg_t * msg, Buf buffer,
			   uint16_t protocol_version)
{
	xassert(msg != NULL);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->name, buffer);
	} else {
		error("_pack_delete_partition_msg: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

static int
_unpack_delete_partition_msg(delete_part_msg_t ** msg, Buf buffer,
			     uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	delete_part_msg_t *tmp_ptr;

	xassert(msg != NULL);

	/* alloc memory for structure */
	tmp_ptr = xmalloc(sizeof(delete_part_msg_t));
	*msg = tmp_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&tmp_ptr->name, &uint32_tmp, buffer);
	} else {
		error("_unpack_delete_partition_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_delete_part_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_resv_name_msg(reservation_name_msg_t * msg, Buf buffer,
		    uint16_t protocol_version)
{
	xassert(msg != NULL);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->name, buffer);
	} else {
		error("_pack_resv_name_msg: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

static int
_unpack_resv_name_msg(reservation_name_msg_t ** msg, Buf buffer,
		      uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	reservation_name_msg_t *tmp_ptr;

	xassert(msg != NULL);

	/* alloc memory for structure */
	tmp_ptr = xmalloc(sizeof(reservation_name_msg_t));
	*msg = tmp_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&tmp_ptr->name, &uint32_tmp, buffer);
	} else {
		error("_unpack_resv_name_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_resv_name_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

extern void
pack_job_step_create_request_msg(job_step_create_request_msg_t * msg,
				 Buf buffer, uint16_t protocol_version)
{
	xassert(msg != NULL);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->job_id, buffer);
		pack32(msg->user_id, buffer);
		pack32(msg->min_nodes, buffer);
		pack32(msg->max_nodes, buffer);
		pack32(msg->cpu_count, buffer);
		pack32(msg->cpu_freq, buffer);
		pack32(msg->num_tasks, buffer);
		pack32(msg->pn_min_memory, buffer);
		pack32(msg->time_limit, buffer);

		pack16(msg->relative, buffer);
		pack16(msg->task_dist, buffer);
		pack16(msg->plane_size, buffer);
		pack16(msg->port, buffer);
		pack16(msg->ckpt_interval, buffer);
		pack16(msg->exclusive, buffer);
		pack16(msg->immediate, buffer);
		pack16(msg->resv_port_cnt, buffer);

		packstr(msg->host, buffer);
		packstr(msg->name, buffer);
		packstr(msg->network, buffer);
		packstr(msg->node_list, buffer);
		packstr(msg->ckpt_dir, buffer);
		packstr(msg->features, buffer);
		packstr(msg->gres, buffer);

		pack8(msg->no_kill, buffer);
		pack8(msg->overcommit, buffer);
	} else {
		error("pack_job_step_create_request_msg: protocol_version "
		      "%hu not supported", protocol_version);
	}

}

extern int
unpack_job_step_create_request_msg(job_step_create_request_msg_t ** msg,
				   Buf buffer, uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	job_step_create_request_msg_t *tmp_ptr;

	/* alloc memory for structure */
	xassert(msg != NULL);
	tmp_ptr = xmalloc(sizeof(job_step_create_request_msg_t));
	*msg = tmp_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&(tmp_ptr->job_id), buffer);
		safe_unpack32(&(tmp_ptr->user_id), buffer);
		safe_unpack32(&(tmp_ptr->min_nodes), buffer);
		safe_unpack32(&(tmp_ptr->max_nodes), buffer);
		safe_unpack32(&(tmp_ptr->cpu_count), buffer);
		safe_unpack32(&(tmp_ptr->cpu_freq), buffer);
		safe_unpack32(&(tmp_ptr->num_tasks), buffer);
		safe_unpack32(&(tmp_ptr->pn_min_memory), buffer);
		safe_unpack32(&(tmp_ptr->time_limit), buffer);

		safe_unpack16(&(tmp_ptr->relative), buffer);
		safe_unpack16(&(tmp_ptr->task_dist), buffer);
		safe_unpack16(&(tmp_ptr->plane_size), buffer);
		safe_unpack16(&(tmp_ptr->port), buffer);
		safe_unpack16(&(tmp_ptr->ckpt_interval), buffer);
		safe_unpack16(&(tmp_ptr->exclusive), buffer);
		safe_unpack16(&(tmp_ptr->immediate), buffer);
		safe_unpack16(&(tmp_ptr->resv_port_cnt), buffer);

		safe_unpackstr_xmalloc(&(tmp_ptr->host), &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&(tmp_ptr->name), &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&(tmp_ptr->network), &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&(tmp_ptr->node_list), &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&(tmp_ptr->ckpt_dir), &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&(tmp_ptr->features), &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&(tmp_ptr->gres), &uint32_tmp, buffer);

		safe_unpack8(&(tmp_ptr->no_kill), buffer);
		safe_unpack8(&(tmp_ptr->overcommit), buffer);
	} else {
		error("unpack_job_step_create_request_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_step_create_request_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_kill_job_msg(kill_job_msg_t * msg, Buf buffer, uint16_t protocol_version)
{
	xassert(msg != NULL);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->job_id,  buffer);
		pack32(msg->step_id,  buffer);
		pack16(msg->job_state, buffer);
		pack32(msg->job_uid, buffer);
		pack_time(msg->time, buffer);
		pack_time(msg->start_time, buffer);
		packstr(msg->nodes, buffer);
		select_g_select_jobinfo_pack(msg->select_jobinfo, buffer,
					     protocol_version);
		packstr_array(msg->spank_job_env, msg->spank_job_env_size,
			      buffer);
	} else {
		error("_pack_kill_job_msg: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

static int
_unpack_kill_job_msg(kill_job_msg_t ** msg, Buf buffer,
		     uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	kill_job_msg_t *tmp_ptr;

	/* alloc memory for structure */
	xassert(msg);
	tmp_ptr = xmalloc(sizeof(kill_job_msg_t));
	*msg = tmp_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&(tmp_ptr->job_id),  buffer);
		safe_unpack32(&(tmp_ptr->step_id),  buffer);
		safe_unpack16(&(tmp_ptr->job_state),  buffer);
		safe_unpack32(&(tmp_ptr->job_uid), buffer);
		safe_unpack_time(&(tmp_ptr->time), buffer);
		safe_unpack_time(&(tmp_ptr->start_time), buffer);
		safe_unpackstr_xmalloc(&(tmp_ptr->nodes), &uint32_tmp, buffer);
		if (select_g_select_jobinfo_unpack(&tmp_ptr->select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;
		safe_unpackstr_array(&(tmp_ptr->spank_job_env),
				     &tmp_ptr->spank_job_env_size, buffer);
	} else {
		error("_unpack_kill_job_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_kill_job_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_signal_job_msg(signal_job_msg_t * msg, Buf buffer,
		     uint16_t protocol_version)
{
	xassert(msg != NULL);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32((uint32_t)msg->job_id,  buffer);
		pack32((uint32_t)msg->signal, buffer);
	} else {
		error("_pack_signal_job_msg: protocol_version "
		      "%hu not supported", protocol_version);
	}
	debug("_pack_signal_job_msg signal = %d", msg->signal);
}

static int
_unpack_signal_job_msg(signal_job_msg_t ** msg, Buf buffer,
		       uint16_t protocol_version)
{
	signal_job_msg_t *tmp_ptr;

	/* alloc memory for structure */
	xassert(msg);
	tmp_ptr = xmalloc(sizeof(signal_job_msg_t));
	*msg = tmp_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&(tmp_ptr->job_id), buffer);
		safe_unpack32(&(tmp_ptr->signal), buffer);
	} else {
		error("_unpack_signal_job_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	debug("_unpack_signal_job_msg signal = %d", tmp_ptr->signal);

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_signal_job_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_epilog_comp_msg(epilog_complete_msg_t * msg, Buf buffer,
		      uint16_t protocol_version)
{
	xassert(msg != NULL);

	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		pack32((uint32_t)msg->job_id, buffer);
		pack32((uint32_t)msg->return_code, buffer);
		packstr(msg->node_name, buffer);
	} else {
		switch_node_info_t *switch_nodeinfo = NULL;

		pack32((uint32_t)msg->job_id, buffer);
		pack32((uint32_t)msg->return_code, buffer);
		packstr(msg->node_name, buffer);
		switch_g_alloc_node_info(&switch_nodeinfo);
		switch_g_pack_node_info(switch_nodeinfo, buffer,
					protocol_version);
		switch_g_free_node_info(&switch_nodeinfo);
	}
}

static int
_unpack_epilog_comp_msg(epilog_complete_msg_t ** msg, Buf buffer,
			uint16_t protocol_version)
{
	epilog_complete_msg_t *tmp_ptr;
	uint32_t uint32_tmp;

	/* alloc memory for structure */
	xassert(msg);
	tmp_ptr = xmalloc(sizeof(epilog_complete_msg_t));
	*msg = tmp_ptr;

	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		safe_unpack32(&(tmp_ptr->job_id), buffer);
		safe_unpack32(&(tmp_ptr->return_code), buffer);
		safe_unpackstr_xmalloc(&(tmp_ptr->node_name),
				       &uint32_tmp, buffer);
	} else {
		switch_node_info_t *switch_nodeinfo = NULL;
		safe_unpack32(&(tmp_ptr->job_id), buffer);
		safe_unpack32(&(tmp_ptr->return_code), buffer);
		safe_unpackstr_xmalloc(&(tmp_ptr->node_name),
				       &uint32_tmp, buffer);
		if (switch_g_alloc_node_info(&switch_nodeinfo)
		    || switch_g_unpack_node_info(switch_nodeinfo, buffer,
						 protocol_version)) {
			switch_g_free_node_info(&switch_nodeinfo);
			goto unpack_error;
		}
		switch_g_free_node_info(&switch_nodeinfo);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_epilog_complete_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_update_job_time_msg(job_time_msg_t * msg, Buf buffer,
			  uint16_t protocol_version)
{
	xassert(msg != NULL);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32((uint32_t)msg->job_id, buffer);
		pack_time(msg->expiration_time, buffer);
	} else {
		error("_pack_update_job_time_msg: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

static int
_unpack_update_job_time_msg(job_time_msg_t ** msg, Buf buffer,
			    uint16_t protocol_version)
{
	job_time_msg_t *tmp_ptr;

	/* alloc memory for structure */
	xassert(msg);
	tmp_ptr = xmalloc(sizeof(job_time_msg_t));
	*msg = tmp_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&(tmp_ptr->job_id), buffer);
		safe_unpack_time(& (tmp_ptr->expiration_time), buffer);
	} else {
		error("_unpack_update_job_time_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_update_job_time_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

extern void
pack_job_step_create_response_msg(job_step_create_response_msg_t * msg,
				  Buf buffer, uint16_t protocol_version)
{
	xassert(msg != NULL);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->resv_ports, buffer);
		pack32(msg->job_step_id, buffer);
		pack_slurm_step_layout(
			msg->step_layout, buffer, protocol_version);
		slurm_cred_pack(msg->cred, buffer, protocol_version);
		select_g_select_jobinfo_pack(
			msg->select_jobinfo, buffer, protocol_version);
		switch_g_pack_jobinfo(msg->switch_job, buffer,
				      protocol_version);
	} else {
		error("pack_job_step_create_response_msg: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

extern int
unpack_job_step_create_response_msg(job_step_create_response_msg_t ** msg,
				    Buf buffer, uint16_t protocol_version)
{
	job_step_create_response_msg_t *tmp_ptr = NULL;
	uint32_t uint32_tmp;

	/* alloc memory for structure */
	xassert(msg != NULL);
	tmp_ptr = xmalloc(sizeof(job_step_create_response_msg_t));
	*msg = tmp_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(
			&tmp_ptr->resv_ports, &uint32_tmp, buffer);
		safe_unpack32(&tmp_ptr->job_step_id, buffer);
		if (unpack_slurm_step_layout(&tmp_ptr->step_layout, buffer,
					     protocol_version))
			goto unpack_error;

		if (!(tmp_ptr->cred = slurm_cred_unpack(
			      buffer, protocol_version)))
			goto unpack_error;

		if (select_g_select_jobinfo_unpack(
			    &tmp_ptr->select_jobinfo, buffer, protocol_version))
			goto unpack_error;
		switch_g_alloc_jobinfo(&tmp_ptr->switch_job, NO_VAL,
				       tmp_ptr->job_step_id);
		if (switch_g_unpack_jobinfo(tmp_ptr->switch_job, buffer,
					    protocol_version)) {
			error("switch_g_unpack_jobinfo: %m");
			switch_g_free_jobinfo(tmp_ptr->switch_job);
			goto unpack_error;
		}
	} else {
		error("unpack_job_step_create_response_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_step_create_response_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static int
_unpack_partition_info_msg(partition_info_msg_t ** msg, Buf buffer,
			   uint16_t protocol_version)
{
	int i;
	partition_info_t *partition = NULL;

	xassert(msg != NULL);
	*msg = xmalloc(sizeof(partition_info_msg_t));

	/* load buffer's header (data structure version and time) */
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&((*msg)->record_count), buffer);
		safe_unpack_time(&((*msg)->last_update), buffer);

		partition = (*msg)->partition_array =
			xmalloc(sizeof(partition_info_t)
				* (*msg)->record_count);

		/* load individual partition info */
		for (i = 0; i < (*msg)->record_count; i++) {
			if (_unpack_partition_info_members(&partition[i],
							   buffer,
							   protocol_version))
				goto unpack_error;
		}
	} else {
		error("_unpack_partition_info_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_partition_info_msg(*msg);
	*msg = NULL;
	return SLURM_ERROR;
}


static int
_unpack_partition_info_members(partition_info_t * part, Buf buffer,
			       uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	char *node_inx_str = NULL;

	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&part->name, &uint32_tmp, buffer);
		if (part->name == NULL)
			part->name = xmalloc(1);/* part->name = "" implicit */
		safe_unpack32(&part->grace_time,   buffer);
		safe_unpack32(&part->max_time,     buffer);
		safe_unpack32(&part->default_time, buffer);
		safe_unpack32(&part->max_nodes,    buffer);
		safe_unpack32(&part->min_nodes,    buffer);
		safe_unpack32(&part->total_nodes,  buffer);
		safe_unpack32(&part->total_cpus,   buffer);
		safe_unpack32(&part->def_mem_per_cpu, buffer);
		safe_unpack32(&part->max_cpus_per_node, buffer);
		safe_unpack32(&part->max_mem_per_cpu, buffer);
		safe_unpack16(&part->flags,        buffer);
		safe_unpack16(&part->max_share,    buffer);
		safe_unpack16(&part->preempt_mode, buffer);
		safe_unpack16(&part->priority,     buffer);
		safe_unpack16(&part->state_up,     buffer);
		safe_unpack16(&part->cr_type ,     buffer);

		safe_unpackstr_xmalloc(&part->allow_accounts, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&part->allow_groups, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&part->allow_alloc_nodes, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&part->allow_qos, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&part->alternate, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&part->deny_accounts, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&part->deny_qos, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&part->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node_inx_str, &uint32_tmp, buffer);
		if (node_inx_str == NULL)
			part->node_inx = bitfmt2int("");
		else {
			part->node_inx = bitfmt2int(node_inx_str);
			xfree(node_inx_str);
			node_inx_str = NULL;
		}

	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&part->name, &uint32_tmp, buffer);
		if (part->name == NULL)
			part->name = xmalloc(1);/* part->name = "" implicit */
		safe_unpack32(&part->grace_time,   buffer);
		safe_unpack32(&part->max_time,     buffer);
		safe_unpack32(&part->default_time, buffer);
		safe_unpack32(&part->max_nodes,    buffer);
		safe_unpack32(&part->min_nodes,    buffer);
		safe_unpack32(&part->total_nodes,  buffer);
		safe_unpack32(&part->total_cpus,   buffer);
		safe_unpack32(&part->def_mem_per_cpu, buffer);
		safe_unpack32(&part->max_cpus_per_node, buffer);
		safe_unpack32(&part->max_mem_per_cpu, buffer);
		safe_unpack16(&part->flags,        buffer);
		safe_unpack16(&part->max_share,    buffer);
		safe_unpack16(&part->preempt_mode, buffer);
		safe_unpack16(&part->priority,     buffer);
		safe_unpack16(&part->state_up,     buffer);
		safe_unpack16(&part->cr_type ,     buffer);

		safe_unpackstr_xmalloc(&part->allow_groups, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&part->allow_alloc_nodes, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&part->alternate, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&part->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node_inx_str, &uint32_tmp, buffer);
		if (node_inx_str == NULL)
			part->node_inx = bitfmt2int("");
		else {
			part->node_inx = bitfmt2int(node_inx_str);
			xfree(node_inx_str);
			node_inx_str = NULL;
		}
	} else {
		error("_unpack_partition_info_members: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_partition_info_members(part);
	return SLURM_ERROR;
}

static int
_unpack_reserve_info_msg(reserve_info_msg_t ** msg, Buf buffer,
			 uint16_t protocol_version)
{
	int i;
	reserve_info_t *reserve = NULL;

	xassert(msg != NULL);
	*msg = xmalloc(sizeof(reserve_info_msg_t));

	/* load buffer's header (data structure version and time) */
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&((*msg)->record_count), buffer);
		safe_unpack_time(&((*msg)->last_update), buffer);

		reserve = (*msg)->reservation_array =
			xmalloc(sizeof(reserve_info_t) * (*msg)->record_count);

		/* load individual reservation records */
		for (i = 0; i < (*msg)->record_count; i++) {
			if (_unpack_reserve_info_members(&reserve[i], buffer,
							 protocol_version))
				goto unpack_error;
		}
	} else {
		error("_unpack_reserve_info_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_reservation_info_msg(*msg);
	*msg = NULL;
	return SLURM_ERROR;
}


static int
_unpack_reserve_info_members(reserve_info_t * resv, Buf buffer,
			     uint16_t protocol_version)
{
	char *node_inx_str = NULL;
	uint32_t uint32_tmp;

	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&resv->accounts,	&uint32_tmp, buffer);
		safe_unpack32(&resv->core_cnt,		buffer);
		safe_unpack_time(&resv->end_time,	buffer);
		safe_unpackstr_xmalloc(&resv->features,	&uint32_tmp, buffer);
		safe_unpack32(&resv->flags,		buffer);
		safe_unpackstr_xmalloc(&resv->licenses, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&resv->name,	&uint32_tmp, buffer);
		safe_unpack32(&resv->node_cnt,		buffer);
		safe_unpackstr_xmalloc(&resv->node_list, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&resv->partition, &uint32_tmp, buffer);
		safe_unpack_time(&resv->start_time,	buffer);
		safe_unpackstr_xmalloc(&resv->users,	&uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node_inx_str,   &uint32_tmp, buffer);
		if (node_inx_str == NULL)
			resv->node_inx = bitfmt2int("");
		else {
			resv->node_inx = bitfmt2int(node_inx_str);
			xfree(node_inx_str);
			node_inx_str = NULL;
		}
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		uint16_t flags;
		safe_unpackstr_xmalloc(&resv->accounts,	&uint32_tmp, buffer);
		safe_unpack32(&resv->core_cnt,		buffer);
		safe_unpack_time(&resv->end_time,	buffer);
		safe_unpackstr_xmalloc(&resv->features,	&uint32_tmp, buffer);
		safe_unpack16(&flags,			buffer);
		resv->flags = flags;
		safe_unpackstr_xmalloc(&resv->licenses, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&resv->name,	&uint32_tmp, buffer);
		safe_unpack32(&resv->node_cnt,		buffer);
		safe_unpackstr_xmalloc(&resv->node_list, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&resv->partition, &uint32_tmp, buffer);
		safe_unpack_time(&resv->start_time,	buffer);
		safe_unpackstr_xmalloc(&resv->users,	&uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node_inx_str,   &uint32_tmp, buffer);
		if (node_inx_str == NULL)
			resv->node_inx = bitfmt2int("");
		else {
			resv->node_inx = bitfmt2int(node_inx_str);
			xfree(node_inx_str);
			node_inx_str = NULL;
		}
	} else {
		error("_unpack_reserve_info_members: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	xfree(node_inx_str);
	slurm_free_reserve_info_members(resv);
	return SLURM_ERROR;
}

/* _unpack_job_step_info_members
 * unpacks a set of slurm job step info for one job step
 * OUT step - pointer to the job step info buffer
 * IN/OUT buffer - source of the unpack, contains pointers that are
 *			automatically updated
 */
static int
_unpack_job_step_info_members(job_step_info_t * step, Buf buffer,
			      uint16_t protocol_version)
{
	uint32_t uint32_tmp = 0;
	uint16_t uint16_tmp = 0;
	char *node_inx_str;

	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		safe_unpack32(&step->array_job_id, buffer);
		safe_unpack32(&step->array_task_id, buffer);
		safe_unpack32(&step->job_id, buffer);
		safe_unpack32(&step->step_id, buffer);
		safe_unpack16(&step->ckpt_interval, buffer);
		safe_unpack32(&step->user_id, buffer);
		safe_unpack32(&step->num_cpus, buffer);
		safe_unpack32(&step->cpu_freq, buffer);
		safe_unpack32(&step->num_tasks, buffer);
		safe_unpack32(&step->time_limit, buffer);
		safe_unpack16(&step->state, buffer);

		safe_unpack_time(&step->start_time, buffer);
		safe_unpack_time(&step->run_time, buffer);

		safe_unpackstr_xmalloc(&step->partition, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->resv_ports, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->network, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node_inx_str, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->ckpt_dir, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->gres, &uint32_tmp, buffer);
		if (node_inx_str == NULL)
			step->node_inx = bitfmt2int("");
		else {
			step->node_inx = bitfmt2int(node_inx_str);
			xfree(node_inx_str);
		}
		if (select_g_select_jobinfo_unpack(&step->select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		safe_unpack32(&step->array_job_id, buffer);
		safe_unpack16(&uint16_tmp, buffer);
		if (uint16_tmp == (uint16_t) NO_VAL)
			step->array_task_id = NO_VAL;
		else
			step->array_task_id = (uint32_t) uint16_tmp;
		safe_unpack32(&step->job_id, buffer);
		safe_unpack32(&step->step_id, buffer);
		safe_unpack16(&step->ckpt_interval, buffer);
		safe_unpack32(&step->user_id, buffer);
		safe_unpack32(&step->num_cpus, buffer);
		safe_unpack32(&step->cpu_freq, buffer);
		safe_unpack32(&step->num_tasks, buffer);
		safe_unpack32(&step->time_limit, buffer);
		safe_unpack16(&step->state, buffer);

		safe_unpack_time(&step->start_time, buffer);
		safe_unpack_time(&step->run_time, buffer);

		safe_unpackstr_xmalloc(&step->partition, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->resv_ports, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->network, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node_inx_str, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->ckpt_dir, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step->gres, &uint32_tmp, buffer);
		if (node_inx_str == NULL)
			step->node_inx = bitfmt2int("");
		else {
			step->node_inx = bitfmt2int(node_inx_str);
			xfree(node_inx_str);
		}
		if (select_g_select_jobinfo_unpack(&step->select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;
	} else {
		error("_unpack_job_step_info_members: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	/* no need to free here.  (we will just be freeing it 2 times
	   since this is freed in _unpack_job_step_info_response_msg
	*/
	//slurm_free_job_step_info_members(step);
	return SLURM_ERROR;
}

static int
_unpack_job_step_info_response_msg(job_step_info_response_msg_t** msg,
				   Buf buffer,
				   uint16_t protocol_version)
{
	int i = 0;
	job_step_info_t *step;

	xassert(msg != NULL);
	*msg = xmalloc(sizeof(job_step_info_response_msg_t));

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack_time(&(*msg)->last_update, buffer);
		safe_unpack32(&(*msg)->job_step_count, buffer);

		step = (*msg)->job_steps = xmalloc(sizeof(job_step_info_t) *
						   (*msg)->job_step_count);

		for (i = 0; i < (*msg)->job_step_count; i++)
			if (_unpack_job_step_info_members(&step[i], buffer,
							  protocol_version))
				goto unpack_error;
	} else {
		error("_unpack_job_step_info_response_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_step_info_response_msg(*msg);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_buffer_msg(slurm_msg_t * msg, Buf buffer)
{
	xassert(msg != NULL);
	packmem_array(msg->data, msg->data_size, buffer);
}

static int
_unpack_job_info_msg(job_info_msg_t ** msg, Buf buffer,
		     uint16_t protocol_version)
{
	int i;
	job_info_t *job = NULL;

	xassert(msg != NULL);
	*msg = xmalloc(sizeof(job_info_msg_t));

	/* load buffer's header (data structure version and time) */
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&((*msg)->record_count), buffer);
		safe_unpack_time(&((*msg)->last_update), buffer);

		job = (*msg)->job_array = xmalloc(sizeof(job_info_t) *
						  (*msg)->record_count);
		/* load individual job info */
		for (i = 0; i < (*msg)->record_count; i++) {
			if (_unpack_job_info_members(&job[i], buffer,
						     protocol_version))
				goto unpack_error;
		}
	} else {
		error("_unpack_job_info_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_info_msg(*msg);
	*msg = NULL;
	return SLURM_ERROR;
}

/* Translate bitmap representation from hex to decimal format, replacing
 * array_task_str and store the bitmap in job->array_bitmap. */
static void _xlate_task_str(job_info_t *job_ptr)
{
	static int bitstr_len = -1;
	int buf_size, len;
	int i, i_first, i_last, i_prev, i_step = 0;
	bitstr_t *task_bitmap;
	char *in_buf = job_ptr->array_task_str;
	char *out_buf = NULL;

	if (!in_buf) {
		job_ptr->array_bitmap = NULL;
		return;
	}

	i = strlen(in_buf);
	task_bitmap = bit_alloc(i * 4);
	bit_unfmt_hexmask(task_bitmap, in_buf);
	job_ptr->array_bitmap = (void *) task_bitmap;

	/* Check first for a step function */
	i_first = bit_ffs(task_bitmap);
	i_last  = bit_fls(task_bitmap);
	if (((i_last - i_first) > 10) &&
	    !bit_test(task_bitmap, i_first + 1)) {
		bool is_step = true;
		i_prev = i_first;
		for (i = i_first + 1; i <= i_last; i++) {
			if (!bit_test(task_bitmap, i))
				continue;
			if (i_step == 0) {
				i_step = i - i_prev;
			} else if ((i - i_prev) != i_step) {
				is_step = false;
				break;
			}
			i_prev = i;
		}
		if (is_step) {
			xstrfmtcat(out_buf, "%d-%d:%d",
				   i_first, i_last, i_step);
		}
	}

	if (bitstr_len == -1) {
		char *bitstr_len_str = getenv("SLURM_BITSTR_LEN");
		if (bitstr_len_str)
			bitstr_len = atoi(bitstr_len_str);
		if (bitstr_len < 0)
			bitstr_len = 64;
	}

	if (bitstr_len > 0) {
		/* Print the first bitstr_len bytes of the bitmap string */
		buf_size = bitstr_len;
		out_buf = xmalloc(buf_size);
		bit_fmt(out_buf, buf_size, task_bitmap);
		len = strlen(out_buf);
		if (len > (buf_size - 3))
		for (i = 0; i < 3; i++)
			out_buf[buf_size - 2 - i] = '.';
	} else {
		/* Print the full bitmap's string representation.
		 * For huge bitmaps this can take roughly one minute,
		 * so let the client do the work */
		buf_size = bit_size(task_bitmap) * 8;
		while (1) {
			out_buf = xmalloc(buf_size);
			bit_fmt(out_buf, buf_size, task_bitmap);
			len = strlen(out_buf);
			if ((len > 0) && (len < (buf_size - 32)))
				break;
			xfree(out_buf);
			buf_size *= 2;
		}
	}

	if (job_ptr->array_max_tasks)
		xstrfmtcat(out_buf, "%c%u", '%', job_ptr->array_max_tasks);

	xfree(job_ptr->array_task_str);
	job_ptr->array_task_str = out_buf;
}

/* _unpack_job_info_members
 * unpacks a set of slurm job info for one job
 * OUT job - pointer to the job info buffer
 * IN/OUT buffer - source of the unpack, contains pointers that are
 *			automatically updated
 */
static int
_unpack_job_info_members(job_info_t * job, Buf buffer,
			 uint16_t protocol_version)
{
	uint32_t uint32_tmp = 0;
	uint16_t uint16_tmp = 0;
	char *node_inx_str;
	multi_core_data_t *mc_ptr;

	job->ntasks_per_node = (uint16_t)NO_VAL;

	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		safe_unpack32(&job->array_job_id, buffer);
		safe_unpack32(&job->array_task_id, buffer);
		/* The array_task_str value is stored in slurmctld and passed
		 * here in hex format for best scalability. Its format needs
		 * to be converted to human readable form by the client. */
		safe_unpackstr_xmalloc(&job->array_task_str, &uint32_tmp,
				       buffer);
		safe_unpack32(&job->array_max_tasks, buffer);
		_xlate_task_str(job);

		safe_unpack32(&job->assoc_id, buffer);
		safe_unpack32(&job->job_id,   buffer);
		safe_unpack32(&job->user_id,  buffer);
		safe_unpack32(&job->group_id, buffer);
		safe_unpack32(&job->profile,  buffer);

		safe_unpack16(&job->job_state,    buffer);
		safe_unpack16(&job->batch_flag,   buffer);
		safe_unpack16(&job->state_reason, buffer);
		safe_unpack8 (&job->reboot,       buffer);
		safe_unpack16(&job->restart_cnt,  buffer);
		safe_unpack16(&job->show_flags,   buffer);

		safe_unpack32(&job->alloc_sid,    buffer);
		safe_unpack32(&job->time_limit,   buffer);
		safe_unpack32(&job->time_min,     buffer);

		safe_unpack16(&job->nice, buffer);

		safe_unpack_time(&job->submit_time, buffer);
		safe_unpack_time(&job->eligible_time, buffer);
		safe_unpack_time(&job->start_time, buffer);
		safe_unpack_time(&job->end_time, buffer);
		safe_unpack_time(&job->suspend_time, buffer);
		safe_unpack_time(&job->pre_sus_time, buffer);
		safe_unpack_time(&job->resize_time, buffer);
		safe_unpack_time(&job->preempt_time, buffer);
		safe_unpack32(&job->priority, buffer);
		safe_unpackstr_xmalloc(&job->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->sched_nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->partition, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->account, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->network, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->comment, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->gres, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->batch_host, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->batch_script, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->qos, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->licenses, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->state_desc, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->resv_name,  &uint32_tmp, buffer);

		safe_unpack32(&job->exit_code, buffer);
		safe_unpack32(&job->derived_ec, buffer);
		unpack_job_resources(&job->job_resrcs, buffer,
				     protocol_version);

		safe_unpackstr_xmalloc(&job->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->wckey, &uint32_tmp, buffer);
		safe_unpack32(&job->req_switch, buffer);
		safe_unpack32(&job->wait4switch, buffer);

		safe_unpackstr_xmalloc(&job->alloc_node, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node_inx_str, &uint32_tmp, buffer);
		if (node_inx_str == NULL)
			job->node_inx = bitfmt2int("");
		else {
			job->node_inx = bitfmt2int(node_inx_str);
			xfree(node_inx_str);
		}

		if (select_g_select_jobinfo_unpack(&job->select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;

		/*** unpack default job details ***/
		safe_unpackstr_xmalloc(&job->features,   &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->work_dir,   &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->dependency, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->command,    &uint32_tmp, buffer);

		safe_unpack32(&job->num_cpus, buffer);
		safe_unpack32(&job->max_cpus, buffer);
		safe_unpack32(&job->num_nodes,   buffer);
		safe_unpack32(&job->max_nodes,   buffer);
		safe_unpack16(&job->requeue,     buffer);
		safe_unpack16(&job->ntasks_per_node, buffer);

		/*** unpack pending job details ***/
		safe_unpack16(&job->shared,        buffer);
		safe_unpack16(&job->contiguous,    buffer);
		safe_unpack16(&job->core_spec,     buffer);
		safe_unpack16(&job->cpus_per_task, buffer);
		safe_unpack16(&job->pn_min_cpus, buffer);

		safe_unpack32(&job->pn_min_memory, buffer);
		safe_unpack32(&job->pn_min_tmp_disk, buffer);

		safe_unpackstr_xmalloc(&job->req_nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node_inx_str, &uint32_tmp, buffer);
		if (node_inx_str == NULL)
			job->req_node_inx = bitfmt2int("");
		else {
			job->req_node_inx = bitfmt2int(node_inx_str);
			xfree(node_inx_str);
		}
		safe_unpackstr_xmalloc(&job->exc_nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node_inx_str, &uint32_tmp, buffer);
		if (node_inx_str == NULL)
			job->exc_node_inx = bitfmt2int("");
		else {
			job->exc_node_inx = bitfmt2int(node_inx_str);
			xfree(node_inx_str);
		}

		safe_unpackstr_xmalloc(&job->std_err, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->std_in,  &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->std_out, &uint32_tmp, buffer);

		if (unpack_multi_core_data(&mc_ptr, buffer, protocol_version))
			goto unpack_error;
		if (mc_ptr) {
			job->boards_per_node  = mc_ptr->boards_per_node;
			job->sockets_per_board  = mc_ptr->sockets_per_board;
			job->sockets_per_node  = mc_ptr->sockets_per_node;
			job->cores_per_socket  = mc_ptr->cores_per_socket;
			job->threads_per_core  = mc_ptr->threads_per_core;
			job->ntasks_per_board = mc_ptr->ntasks_per_board;
			job->ntasks_per_socket = mc_ptr->ntasks_per_socket;
			job->ntasks_per_core   = mc_ptr->ntasks_per_core;
			xfree(mc_ptr);
		}
	} else if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		safe_unpack32(&job->array_job_id, buffer);
		safe_unpack32(&job->array_task_id, buffer);
		safe_unpack32(&job->assoc_id, buffer);
		safe_unpack32(&job->job_id, buffer);
		safe_unpack32(&job->user_id, buffer);
		safe_unpack32(&job->group_id, buffer);
		safe_unpack32(&job->profile, buffer);

		safe_unpack16(&job->job_state,    buffer);
		safe_unpack16(&job->batch_flag,   buffer);
		safe_unpack16(&job->state_reason, buffer);
		safe_unpack16(&job->restart_cnt, buffer);
		safe_unpack16(&job->show_flags, buffer);

		safe_unpack32(&job->alloc_sid,    buffer);
		safe_unpack32(&job->time_limit,   buffer);
		safe_unpack32(&job->time_min,   buffer);

		safe_unpack16(&job->nice, buffer);

		safe_unpack_time(&job->submit_time, buffer);
		safe_unpack_time(&job->eligible_time, buffer);
		safe_unpack_time(&job->start_time, buffer);
		safe_unpack_time(&job->end_time, buffer);
		safe_unpack_time(&job->suspend_time, buffer);
		safe_unpack_time(&job->pre_sus_time, buffer);
		safe_unpack_time(&job->resize_time, buffer);
		safe_unpack_time(&job->preempt_time, buffer);
		safe_unpack32(&job->priority, buffer);
		safe_unpackstr_xmalloc(&job->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->partition, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->account, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->network, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->comment, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->gres, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->batch_host, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->batch_script, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->qos, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->licenses, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->state_desc, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->resv_name,  &uint32_tmp, buffer);

		safe_unpack32(&job->exit_code, buffer);
		safe_unpack32(&job->derived_ec, buffer);
		unpack_job_resources(&job->job_resrcs, buffer,
				     protocol_version);

		safe_unpackstr_xmalloc(&job->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->wckey, &uint32_tmp, buffer);
		safe_unpack32(&job->req_switch, buffer);
		safe_unpack32(&job->wait4switch, buffer);

		safe_unpackstr_xmalloc(&job->alloc_node, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node_inx_str, &uint32_tmp, buffer);
		if (node_inx_str == NULL)
			job->node_inx = bitfmt2int("");
		else {
			job->node_inx = bitfmt2int(node_inx_str);
			xfree(node_inx_str);
		}

		if (select_g_select_jobinfo_unpack(&job->select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;

		/*** unpack default job details ***/
		safe_unpackstr_xmalloc(&job->features,   &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->work_dir,   &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->dependency, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->command,    &uint32_tmp, buffer);

		safe_unpack32(&job->num_cpus, buffer);
		safe_unpack32(&job->max_cpus, buffer);
		safe_unpack32(&job->num_nodes,   buffer);
		safe_unpack32(&job->max_nodes,   buffer);
		safe_unpack16(&job->requeue,     buffer);
		safe_unpack16(&job->ntasks_per_node, buffer);

		/*** unpack pending job details ***/
		safe_unpack16(&job->shared,        buffer);
		safe_unpack16(&job->contiguous,    buffer);
		safe_unpack16(&job->core_spec,     buffer);
		safe_unpack16(&job->cpus_per_task, buffer);
		safe_unpack16(&job->pn_min_cpus, buffer);

		safe_unpack32(&job->pn_min_memory, buffer);
		safe_unpack32(&job->pn_min_tmp_disk, buffer);

		safe_unpackstr_xmalloc(&job->req_nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node_inx_str, &uint32_tmp, buffer);
		if (node_inx_str == NULL)
			job->req_node_inx = bitfmt2int("");
		else {
			job->req_node_inx = bitfmt2int(node_inx_str);
			xfree(node_inx_str);
		}
		safe_unpackstr_xmalloc(&job->exc_nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node_inx_str, &uint32_tmp, buffer);
		if (node_inx_str == NULL)
			job->exc_node_inx = bitfmt2int("");
		else {
			job->exc_node_inx = bitfmt2int(node_inx_str);
			xfree(node_inx_str);
		}

		safe_unpackstr_xmalloc(&job->std_err, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->std_in,  &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->std_out, &uint32_tmp, buffer);

		if (unpack_multi_core_data(&mc_ptr, buffer, protocol_version))
			goto unpack_error;
		if (mc_ptr) {
			job->boards_per_node  = mc_ptr->boards_per_node;
			job->sockets_per_board  = mc_ptr->sockets_per_board;
			job->sockets_per_node  = mc_ptr->sockets_per_node;
			job->cores_per_socket  = mc_ptr->cores_per_socket;
			job->threads_per_core  = mc_ptr->threads_per_core;
			job->ntasks_per_board = mc_ptr->ntasks_per_board;
			job->ntasks_per_socket = mc_ptr->ntasks_per_socket;
			job->ntasks_per_core   = mc_ptr->ntasks_per_core;
			xfree(mc_ptr);
		}
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		safe_unpack32(&job->array_job_id, buffer);
		safe_unpack16(&uint16_tmp, buffer);
		if (uint16_tmp == (uint16_t) NO_VAL)
			job->array_task_id = NO_VAL;
		else
			job->array_task_id = (uint32_t) uint16_tmp;
		safe_unpack32(&job->assoc_id, buffer);
		safe_unpack32(&job->job_id, buffer);
		safe_unpack32(&job->user_id, buffer);
		safe_unpack32(&job->group_id, buffer);
		safe_unpack32(&job->profile, buffer);

		safe_unpack16(&job->job_state,    buffer);
		safe_unpack16(&job->batch_flag,   buffer);
		safe_unpack16(&job->state_reason, buffer);
		safe_unpack16(&job->restart_cnt, buffer);
		safe_unpack16(&job->show_flags, buffer);

		safe_unpack32(&job->alloc_sid,    buffer);
		safe_unpack32(&job->time_limit,   buffer);
		safe_unpack32(&job->time_min,   buffer);

		safe_unpack16(&job->nice, buffer);

		safe_unpack_time(&job->submit_time, buffer);
		safe_unpack_time(&job->eligible_time, buffer);
		safe_unpack_time(&job->start_time, buffer);
		safe_unpack_time(&job->end_time, buffer);
		safe_unpack_time(&job->suspend_time, buffer);
		safe_unpack_time(&job->pre_sus_time, buffer);
		safe_unpack_time(&job->resize_time, buffer);
		safe_unpack_time(&job->preempt_time, buffer);
		safe_unpack32(&job->priority, buffer);
		safe_unpackstr_xmalloc(&job->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->partition, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->account, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->network, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->comment, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->gres, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->batch_host, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->batch_script, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->qos, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->licenses, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->state_desc, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->resv_name,  &uint32_tmp, buffer);

		safe_unpack32(&job->exit_code, buffer);
		safe_unpack32(&job->derived_ec, buffer);
		unpack_job_resources(&job->job_resrcs, buffer,
				     protocol_version);

		safe_unpackstr_xmalloc(&job->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->wckey, &uint32_tmp, buffer);
		safe_unpack32(&job->req_switch, buffer);
		safe_unpack32(&job->wait4switch, buffer);

		safe_unpackstr_xmalloc(&job->alloc_node, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node_inx_str, &uint32_tmp, buffer);
		if (node_inx_str == NULL)
			job->node_inx = bitfmt2int("");
		else {
			job->node_inx = bitfmt2int(node_inx_str);
			xfree(node_inx_str);
		}

		if (select_g_select_jobinfo_unpack(&job->select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;

		/*** unpack default job details ***/
		safe_unpackstr_xmalloc(&job->features,   &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->work_dir,   &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->dependency, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job->command,    &uint32_tmp, buffer);

		safe_unpack32(&job->num_cpus, buffer);
		safe_unpack32(&job->max_cpus, buffer);
		safe_unpack32(&job->num_nodes,   buffer);
		safe_unpack32(&job->max_nodes,   buffer);
		safe_unpack16(&job->requeue,     buffer);
		safe_unpack16(&job->ntasks_per_node, buffer);

		/*** unpack pending job details ***/
		safe_unpack16(&job->shared,        buffer);
		safe_unpack16(&job->contiguous,    buffer);
		safe_unpack16(&job->cpus_per_task, buffer);
		safe_unpack16(&job->pn_min_cpus, buffer);

		safe_unpack32(&job->pn_min_memory, buffer);
		safe_unpack32(&job->pn_min_tmp_disk, buffer);

		safe_unpackstr_xmalloc(&job->req_nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node_inx_str, &uint32_tmp, buffer);
		if (node_inx_str == NULL)
			job->req_node_inx = bitfmt2int("");
		else {
			job->req_node_inx = bitfmt2int(node_inx_str);
			xfree(node_inx_str);
		}
		safe_unpackstr_xmalloc(&job->exc_nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&node_inx_str, &uint32_tmp, buffer);
		if (node_inx_str == NULL)
			job->exc_node_inx = bitfmt2int("");
		else {
			job->exc_node_inx = bitfmt2int(node_inx_str);
			xfree(node_inx_str);
		}

		if (unpack_multi_core_data(&mc_ptr, buffer, protocol_version))
			goto unpack_error;
		if (mc_ptr) {
			job->boards_per_node  = mc_ptr->boards_per_node;
			job->sockets_per_board  = mc_ptr->sockets_per_board;
			job->sockets_per_node  = mc_ptr->sockets_per_node;
			job->cores_per_socket  = mc_ptr->cores_per_socket;
			job->threads_per_core  = mc_ptr->threads_per_core;
			job->ntasks_per_board = mc_ptr->ntasks_per_board;
			job->ntasks_per_socket = mc_ptr->ntasks_per_socket;
			job->ntasks_per_core   = mc_ptr->ntasks_per_core;
			xfree(mc_ptr);
		}
	} else {
		error("_unpack_job_info_members: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_info_members(job);
	return SLURM_ERROR;
}

static void
_pack_slurm_ctl_conf_msg(slurm_ctl_conf_info_msg_t * build_ptr, Buf buffer,
			 uint16_t protocol_version)
{
	uint32_t count = NO_VAL;
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();

	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		pack_time(build_ptr->last_update, buffer);

		pack16(build_ptr->accounting_storage_enforce, buffer);
		packstr(build_ptr->accounting_storage_backup_host, buffer);
		packstr(build_ptr->accounting_storage_host, buffer);
		packstr(build_ptr->accounting_storage_loc, buffer);
		pack32(build_ptr->accounting_storage_port, buffer);
		packstr(build_ptr->accounting_storage_type, buffer);
		packstr(build_ptr->accounting_storage_user, buffer);
		pack16(build_ptr->acctng_store_job_comment, buffer);

		if (build_ptr->acct_gather_conf)
			count = list_count(build_ptr->acct_gather_conf);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			ListIterator itr = list_iterator_create(
				(List)build_ptr->acct_gather_conf);
			config_key_pair_t *key_pair = NULL;
			while ((key_pair = list_next(itr))) {
				pack_config_key_pair(key_pair,
						     protocol_version, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		packstr(build_ptr->acct_gather_energy_type, buffer);
		packstr(build_ptr->acct_gather_filesystem_type, buffer);
		packstr(build_ptr->acct_gather_infiniband_type, buffer);
		pack16(build_ptr->acct_gather_node_freq, buffer);
		packstr(build_ptr->acct_gather_profile_type, buffer);

		packstr(build_ptr->authinfo, buffer);
		packstr(build_ptr->authtype, buffer);

		packstr(build_ptr->backup_addr, buffer);
		packstr(build_ptr->backup_controller, buffer);
		pack16(build_ptr->batch_start_timeout, buffer);
		pack_time(build_ptr->boot_time, buffer);

		packstr(build_ptr->checkpoint_type, buffer);
		packstr(build_ptr->chos_loc, buffer);
		packstr(build_ptr->cluster_name, buffer);
		pack16(build_ptr->complete_wait, buffer);
		packstr(build_ptr->control_addr, buffer);
		packstr(build_ptr->control_machine, buffer);
		packstr(build_ptr->core_spec_plugin, buffer);
		pack32(build_ptr->cpu_freq_def, buffer);
		packstr(build_ptr->crypto_type, buffer);

		pack32(build_ptr->def_mem_per_cpu, buffer);
		pack64(build_ptr->debug_flags, buffer);
		pack16(build_ptr->disable_root_jobs, buffer);
		pack16(build_ptr->dynalloc_port, buffer);

		pack16(build_ptr->enforce_part_limits, buffer);
		packstr(build_ptr->epilog, buffer);
		pack32(build_ptr->epilog_msg_time, buffer);
		packstr(build_ptr->epilog_slurmctld, buffer);

		if (build_ptr->ext_sensors_conf)
			count = list_count(build_ptr->ext_sensors_conf);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			ListIterator itr = list_iterator_create(
				(List)build_ptr->ext_sensors_conf);
			config_key_pair_t *key_pair = NULL;
			while ((key_pair = list_next(itr))) {
				pack_config_key_pair(key_pair,
						     protocol_version, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		packstr(build_ptr->ext_sensors_type, buffer);
		pack16(build_ptr->ext_sensors_freq, buffer);

		pack16(build_ptr->fast_schedule, buffer);
		pack32(build_ptr->first_job_id, buffer);
		pack16(build_ptr->fs_dampening_factor, buffer);

		pack16(build_ptr->get_env_timeout, buffer);
		packstr(build_ptr->gres_plugins, buffer);
		pack16(build_ptr->group_info, buffer);

		pack32(build_ptr->hash_val, buffer);

		pack16(build_ptr->health_check_interval, buffer);
		pack16(build_ptr->health_check_node_state, buffer);
		packstr(build_ptr->health_check_program, buffer);

		pack16(build_ptr->inactive_limit, buffer);

		packstr(build_ptr->job_acct_gather_freq, buffer);
		packstr(build_ptr->job_acct_gather_type, buffer);
		packstr(build_ptr->job_acct_gather_params, buffer);

		packstr(build_ptr->job_ckpt_dir, buffer);

		packstr(build_ptr->job_comp_host, buffer);
		packstr(build_ptr->job_comp_loc, buffer);
		pack32((uint32_t)build_ptr->job_comp_port, buffer);
		packstr(build_ptr->job_comp_type, buffer);
		packstr(build_ptr->job_comp_user, buffer);
		packstr(build_ptr->job_container_plugin, buffer);

		packstr(build_ptr->job_credential_private_key, buffer);
		packstr(build_ptr->job_credential_public_certificate, buffer);
		pack16(build_ptr->job_file_append, buffer);
		pack16(build_ptr->job_requeue, buffer);
		packstr(build_ptr->job_submit_plugins, buffer);

		pack16(build_ptr->keep_alive_time, buffer);
		pack16(build_ptr->kill_on_bad_exit, buffer);
		pack16(build_ptr->kill_wait, buffer);

		packstr(build_ptr->launch_type, buffer);
		packstr(build_ptr->layouts, buffer);
		packstr(build_ptr->licenses, buffer);
		packstr(build_ptr->licenses_used, buffer);

		pack32(build_ptr->max_array_sz, buffer);
		packstr(build_ptr->mail_prog, buffer);
		pack32(build_ptr->max_job_cnt, buffer);
		pack32(build_ptr->max_job_id, buffer);
		pack32(build_ptr->max_mem_per_cpu, buffer);
		pack32(build_ptr->max_step_cnt, buffer);
		pack16(build_ptr->max_tasks_per_node, buffer);
		pack16(build_ptr->mem_limit_enforce, buffer);
		pack16(build_ptr->min_job_age, buffer);
		packstr(build_ptr->mpi_default, buffer);
		packstr(build_ptr->mpi_params, buffer);
		pack16(build_ptr->msg_timeout, buffer);

		pack32(build_ptr->next_job_id, buffer);
		packstr(build_ptr->node_prefix, buffer);

		pack16(build_ptr->over_time_limit, buffer);

		packstr(build_ptr->plugindir, buffer);
		packstr(build_ptr->plugstack, buffer);
		pack16(build_ptr->preempt_mode, buffer);
		packstr(build_ptr->preempt_type, buffer);

		pack32(build_ptr->priority_decay_hl, buffer);
		pack32(build_ptr->priority_calc_period, buffer);
		pack16(build_ptr->priority_favor_small, buffer);
		pack16(build_ptr->priority_flags, buffer);
		pack32(build_ptr->priority_max_age, buffer);
		packstr(build_ptr->priority_params, buffer);
		pack16(build_ptr->priority_reset_period, buffer);
		packstr(build_ptr->priority_type, buffer);
		pack32(build_ptr->priority_weight_age, buffer);
		pack32(build_ptr->priority_weight_fs, buffer);
		pack32(build_ptr->priority_weight_js, buffer);
		pack32(build_ptr->priority_weight_part, buffer);
		pack32(build_ptr->priority_weight_qos, buffer);

		pack16(build_ptr->private_data, buffer);
		packstr(build_ptr->proctrack_type, buffer);
		packstr(build_ptr->prolog, buffer);
		packstr(build_ptr->prolog_slurmctld, buffer);
		pack16(build_ptr->prolog_flags, buffer);
		pack16(build_ptr->propagate_prio_process, buffer);
		packstr(build_ptr->propagate_rlimits, buffer);
		packstr(build_ptr->propagate_rlimits_except, buffer);

		packstr(build_ptr->reboot_program, buffer);
		pack16(build_ptr->reconfig_flags, buffer);
		packstr(build_ptr->requeue_exit, buffer);
		packstr(build_ptr->requeue_exit_hold, buffer);
		packstr(build_ptr->resume_program, buffer);
		pack16(build_ptr->resume_rate, buffer);
		pack16(build_ptr->resume_timeout, buffer);
		packstr(build_ptr->resv_epilog, buffer);
		pack16(build_ptr->resv_over_run, buffer);
		packstr(build_ptr->resv_prolog, buffer);
		pack16(build_ptr->ret2service, buffer);

		packstr(build_ptr->route_plugin, buffer);
		packstr(build_ptr->salloc_default_command, buffer);
		packstr(build_ptr->sched_params, buffer);
		pack16(build_ptr->schedport, buffer);
		pack16(build_ptr->schedrootfltr, buffer);
		packstr(build_ptr->sched_logfile, buffer);
		pack16(build_ptr->sched_log_level, buffer);
		pack16(build_ptr->sched_time_slice, buffer);
		packstr(build_ptr->schedtype, buffer);
		packstr(build_ptr->select_type, buffer);
		if (build_ptr->select_conf_key_pairs)
			count = list_count(build_ptr->select_conf_key_pairs);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			ListIterator itr = list_iterator_create(
				(List)build_ptr->select_conf_key_pairs);
			config_key_pair_t *key_pair = NULL;
			while ((key_pair = list_next(itr))) {
				pack_config_key_pair(key_pair,
						     protocol_version, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(build_ptr->select_type_param, buffer);

		packstr(build_ptr->slurm_conf, buffer);
		pack32(build_ptr->slurm_user_id, buffer);
		packstr(build_ptr->slurm_user_name, buffer);
		pack32(build_ptr->slurmd_user_id, buffer);
		packstr(build_ptr->slurmd_user_name, buffer);

		pack16(build_ptr->slurmctld_debug, buffer);
		packstr(build_ptr->slurmctld_logfile, buffer);
		packstr(build_ptr->slurmctld_pidfile, buffer);
		packstr(build_ptr->slurmctld_plugstack, buffer);
		pack32(build_ptr->slurmctld_port, buffer);
		pack16(build_ptr->slurmctld_port_count, buffer);
		pack16(build_ptr->slurmctld_timeout, buffer);

		pack16(build_ptr->slurmd_debug, buffer);
		packstr(build_ptr->slurmd_logfile, buffer);
		packstr(build_ptr->slurmd_pidfile, buffer);
		packstr(build_ptr->slurmd_plugstack, buffer);
		if (!(cluster_flags & CLUSTER_FLAG_MULTSD))
			pack32(build_ptr->slurmd_port, buffer);

		packstr(build_ptr->slurmd_spooldir, buffer);
		pack16(build_ptr->slurmd_timeout, buffer);
		packstr(build_ptr->srun_epilog, buffer);
		pack16(build_ptr->srun_port_range[0], buffer);
		pack16(build_ptr->srun_port_range[1], buffer);
		packstr(build_ptr->srun_prolog, buffer);
		packstr(build_ptr->state_save_location, buffer);
		packstr(build_ptr->suspend_exc_nodes, buffer);
		packstr(build_ptr->suspend_exc_parts, buffer);
		packstr(build_ptr->suspend_program, buffer);
		pack16(build_ptr->suspend_rate, buffer);
		pack32(build_ptr->suspend_time, buffer);
		pack16(build_ptr->suspend_timeout, buffer);
		packstr(build_ptr->switch_type, buffer);

		packstr(build_ptr->task_epilog, buffer);
		packstr(build_ptr->task_prolog, buffer);
		packstr(build_ptr->task_plugin, buffer);
		pack16(build_ptr->task_plugin_param, buffer);
		packstr(build_ptr->tmp_fs, buffer);
		packstr(build_ptr->topology_plugin, buffer);
		pack16(build_ptr->track_wckey, buffer);
		pack16(build_ptr->tree_width, buffer);

		pack16(build_ptr->use_pam, buffer);
		pack16(build_ptr->use_spec_resources, buffer);
		packstr(build_ptr->unkillable_program, buffer);
		pack16(build_ptr->unkillable_timeout, buffer);
		packstr(build_ptr->version, buffer);
		pack16(build_ptr->vsize_factor, buffer);

		pack16(build_ptr->wait_time, buffer);
		pack16(build_ptr->z_16, buffer);
		pack32(build_ptr->z_32, buffer);
		packstr(build_ptr->z_char, buffer);
	} else if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		pack_time(build_ptr->last_update, buffer);

		pack16(build_ptr->accounting_storage_enforce, buffer);
		packstr(build_ptr->accounting_storage_backup_host, buffer);
		packstr(build_ptr->accounting_storage_host, buffer);
		packstr(build_ptr->accounting_storage_loc, buffer);
		pack32(build_ptr->accounting_storage_port, buffer);
		packstr(build_ptr->accounting_storage_type, buffer);
		packstr(build_ptr->accounting_storage_user, buffer);
		pack16(build_ptr->acctng_store_job_comment, buffer);

		if (build_ptr->acct_gather_conf)
			count = list_count(build_ptr->acct_gather_conf);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			ListIterator itr = list_iterator_create(
				(List)build_ptr->acct_gather_conf);
			config_key_pair_t *key_pair = NULL;
			while ((key_pair = list_next(itr))) {
				pack_config_key_pair(key_pair,
						     protocol_version, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		packstr(build_ptr->acct_gather_energy_type, buffer);
		packstr(build_ptr->acct_gather_filesystem_type, buffer);
		packstr(build_ptr->acct_gather_infiniband_type, buffer);
		pack16(build_ptr->acct_gather_node_freq, buffer);
		packstr(build_ptr->acct_gather_profile_type, buffer);

		packstr(build_ptr->authinfo, buffer);
		packstr(build_ptr->authtype, buffer);

		packstr(build_ptr->backup_addr, buffer);
		packstr(build_ptr->backup_controller, buffer);
		pack16(build_ptr->batch_start_timeout, buffer);
		pack_time(build_ptr->boot_time, buffer);

		packstr(build_ptr->checkpoint_type, buffer);
		packstr(build_ptr->cluster_name, buffer);
		pack16(build_ptr->complete_wait, buffer);
		packstr(build_ptr->control_addr, buffer);
		packstr(build_ptr->control_machine, buffer);
		packstr(build_ptr->core_spec_plugin, buffer);
		packstr(build_ptr->crypto_type, buffer);

		pack32(build_ptr->def_mem_per_cpu, buffer);
		pack32((uint32_t)build_ptr->debug_flags, buffer);
		pack16(build_ptr->disable_root_jobs, buffer);
		pack16(build_ptr->dynalloc_port, buffer);

		pack16(build_ptr->enforce_part_limits, buffer);
		packstr(build_ptr->epilog, buffer);
		pack32(build_ptr->epilog_msg_time, buffer);
		packstr(build_ptr->epilog_slurmctld, buffer);

		if (build_ptr->ext_sensors_conf)
			count = list_count(build_ptr->ext_sensors_conf);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			ListIterator itr = list_iterator_create(
				(List)build_ptr->ext_sensors_conf);
			config_key_pair_t *key_pair = NULL;
			while ((key_pair = list_next(itr))) {
				pack_config_key_pair(key_pair,
						     protocol_version, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		packstr(build_ptr->ext_sensors_type, buffer);
		pack16(build_ptr->ext_sensors_freq, buffer);

		pack16(build_ptr->fast_schedule, buffer);
		pack32(build_ptr->first_job_id, buffer);
		pack16(build_ptr->fs_dampening_factor, buffer);

		pack16(build_ptr->get_env_timeout, buffer);
		packstr(build_ptr->gres_plugins, buffer);
		pack16(build_ptr->group_info, buffer);

		pack32(build_ptr->hash_val, buffer);

		pack16(build_ptr->health_check_interval, buffer);
		pack16(build_ptr->health_check_node_state, buffer);
		packstr(build_ptr->health_check_program, buffer);

		pack16(build_ptr->inactive_limit, buffer);

		packstr(build_ptr->job_acct_gather_freq, buffer);
		packstr(build_ptr->job_acct_gather_type, buffer);
		packstr(build_ptr->job_acct_gather_params, buffer);

		packstr(build_ptr->job_ckpt_dir, buffer);

		packstr(build_ptr->job_comp_host, buffer);
		packstr(build_ptr->job_comp_loc, buffer);
		pack32((uint32_t)build_ptr->job_comp_port, buffer);
		packstr(build_ptr->job_comp_type, buffer);
		packstr(build_ptr->job_comp_user, buffer);
		packstr(build_ptr->job_container_plugin, buffer);

		packstr(build_ptr->job_credential_private_key, buffer);
		packstr(build_ptr->job_credential_public_certificate, buffer);
		pack16(build_ptr->job_file_append, buffer);
		pack16(build_ptr->job_requeue, buffer);
		packstr(build_ptr->job_submit_plugins, buffer);

		pack16(build_ptr->keep_alive_time, buffer);
		pack16(build_ptr->kill_on_bad_exit, buffer);
		pack16(build_ptr->kill_wait, buffer);

		packstr(build_ptr->launch_type, buffer);
		packstr(build_ptr->licenses, buffer);
		packstr(build_ptr->licenses_used, buffer);

		pack32(build_ptr->max_array_sz, buffer);
		packstr(build_ptr->mail_prog, buffer);
		pack32(build_ptr->max_job_cnt, buffer);
		pack32(build_ptr->max_job_id, buffer);
		pack32(build_ptr->max_mem_per_cpu, buffer);
		pack32(build_ptr->max_step_cnt, buffer);
		pack16(build_ptr->max_tasks_per_node, buffer);
		pack16(build_ptr->min_job_age, buffer);
		packstr(build_ptr->mpi_default, buffer);
		packstr(build_ptr->mpi_params, buffer);
		pack16(build_ptr->msg_timeout, buffer);

		pack32(build_ptr->next_job_id, buffer);
		packstr(build_ptr->node_prefix, buffer);

		pack16(build_ptr->over_time_limit, buffer);

		packstr(build_ptr->plugindir, buffer);
		packstr(build_ptr->plugstack, buffer);
		pack16(build_ptr->preempt_mode, buffer);
		packstr(build_ptr->preempt_type, buffer);

		pack32(build_ptr->priority_decay_hl, buffer);
		pack32(build_ptr->priority_calc_period, buffer);
		pack16(build_ptr->priority_favor_small, buffer);
		pack16(build_ptr->priority_flags, buffer);
		pack32(build_ptr->priority_max_age, buffer);
		pack16(build_ptr->priority_reset_period, buffer);
		packstr(build_ptr->priority_type, buffer);
		pack32(build_ptr->priority_weight_age, buffer);
		pack32(build_ptr->priority_weight_fs, buffer);
		pack32(build_ptr->priority_weight_js, buffer);
		pack32(build_ptr->priority_weight_part, buffer);
		pack32(build_ptr->priority_weight_qos, buffer);

		pack16(build_ptr->private_data, buffer);
		packstr(build_ptr->proctrack_type, buffer);
		packstr(build_ptr->prolog, buffer);
		packstr(build_ptr->prolog_slurmctld, buffer);
		pack16(build_ptr->prolog_flags, buffer);
		pack16(build_ptr->propagate_prio_process, buffer);
		packstr(build_ptr->propagate_rlimits, buffer);
		packstr(build_ptr->propagate_rlimits_except, buffer);

		packstr(build_ptr->reboot_program, buffer);
		pack16(build_ptr->reconfig_flags, buffer);
		packstr(build_ptr->resume_program, buffer);
		pack16(build_ptr->resume_rate, buffer);
		pack16(build_ptr->resume_timeout, buffer);
		packstr(build_ptr->resv_epilog, buffer);
		pack16(build_ptr->resv_over_run, buffer);
		packstr(build_ptr->resv_prolog, buffer);
		pack16(build_ptr->ret2service, buffer);

		packstr(build_ptr->salloc_default_command, buffer);
		packstr(build_ptr->sched_params, buffer);
		pack16(build_ptr->schedport, buffer);
		pack16(build_ptr->schedrootfltr, buffer);
		packstr(build_ptr->sched_logfile, buffer);
		pack16(build_ptr->sched_log_level, buffer);
		pack16(build_ptr->sched_time_slice, buffer);
		packstr(build_ptr->schedtype, buffer);
		packstr(build_ptr->select_type, buffer);
		if (build_ptr->select_conf_key_pairs)
			count = list_count(build_ptr->select_conf_key_pairs);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			ListIterator itr = list_iterator_create(
				(List)build_ptr->select_conf_key_pairs);
			config_key_pair_t *key_pair = NULL;
			while ((key_pair = list_next(itr))) {
				pack_config_key_pair(key_pair,
						     protocol_version, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(build_ptr->select_type_param, buffer);

		packstr(build_ptr->slurm_conf, buffer);
		pack32(build_ptr->slurm_user_id, buffer);
		packstr(build_ptr->slurm_user_name, buffer);
		pack32(build_ptr->slurmd_user_id, buffer);
		packstr(build_ptr->slurmd_user_name, buffer);

		pack16(build_ptr->slurmctld_debug, buffer);
		packstr(build_ptr->slurmctld_logfile, buffer);
		packstr(build_ptr->slurmctld_pidfile, buffer);
		packstr(build_ptr->slurmctld_plugstack, buffer);
		pack32(build_ptr->slurmctld_port, buffer);
		pack16(build_ptr->slurmctld_port_count, buffer);
		pack16(build_ptr->slurmctld_timeout, buffer);

		pack16(build_ptr->slurmd_debug, buffer);
		packstr(build_ptr->slurmd_logfile, buffer);
		packstr(build_ptr->slurmd_pidfile, buffer);
		packstr(build_ptr->slurmd_plugstack, buffer);
		if (!(cluster_flags & CLUSTER_FLAG_MULTSD))
			pack32(build_ptr->slurmd_port, buffer);

		packstr(build_ptr->slurmd_spooldir, buffer);
		pack16(build_ptr->slurmd_timeout, buffer);
		packstr(build_ptr->srun_epilog, buffer);
		packstr(build_ptr->srun_prolog, buffer);
		packstr(build_ptr->state_save_location, buffer);
		packstr(build_ptr->suspend_exc_nodes, buffer);
		packstr(build_ptr->suspend_exc_parts, buffer);
		packstr(build_ptr->suspend_program, buffer);
		pack16(build_ptr->suspend_rate, buffer);
		pack32(build_ptr->suspend_time, buffer);
		pack16(build_ptr->suspend_timeout, buffer);
		packstr(build_ptr->switch_type, buffer);

		packstr(build_ptr->task_epilog, buffer);
		packstr(build_ptr->task_prolog, buffer);
		packstr(build_ptr->task_plugin, buffer);
		pack16(build_ptr->task_plugin_param, buffer);
		packstr(build_ptr->tmp_fs, buffer);
		packstr(build_ptr->topology_plugin, buffer);
		pack16(build_ptr->track_wckey, buffer);
		pack16(build_ptr->tree_width, buffer);

		pack16(build_ptr->use_pam, buffer);
		packstr(build_ptr->unkillable_program, buffer);
		pack16(build_ptr->unkillable_timeout, buffer);
		packstr(build_ptr->version, buffer);
		pack16(build_ptr->vsize_factor, buffer);

		pack16(build_ptr->wait_time, buffer);
		pack16(build_ptr->z_16, buffer);
		pack32(build_ptr->z_32, buffer);
		packstr(build_ptr->z_char, buffer);
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		pack_time(build_ptr->last_update, buffer);

		pack16(build_ptr->accounting_storage_enforce, buffer);
		packstr(build_ptr->accounting_storage_backup_host, buffer);
		packstr(build_ptr->accounting_storage_host, buffer);
		packstr(build_ptr->accounting_storage_loc, buffer);
		pack32(build_ptr->accounting_storage_port, buffer);
		packstr(build_ptr->accounting_storage_type, buffer);
		packstr(build_ptr->accounting_storage_user, buffer);
		pack16(build_ptr->acctng_store_job_comment, buffer);
		packstr(build_ptr->acct_gather_energy_type, buffer);
		packstr(build_ptr->acct_gather_filesystem_type, buffer);
		packstr(build_ptr->acct_gather_infiniband_type, buffer);
		pack16(build_ptr->acct_gather_node_freq, buffer);
		packstr(build_ptr->acct_gather_profile_type, buffer);

		packstr(build_ptr->authtype, buffer);

		packstr(build_ptr->backup_addr, buffer);
		packstr(build_ptr->backup_controller, buffer);
		pack16(build_ptr->batch_start_timeout, buffer);
		pack_time(build_ptr->boot_time, buffer);

		packstr(build_ptr->checkpoint_type, buffer);
		packstr(build_ptr->cluster_name, buffer);
		pack16(build_ptr->complete_wait, buffer);
		packstr(build_ptr->control_addr, buffer);
		packstr(build_ptr->control_machine, buffer);
		packstr(build_ptr->crypto_type, buffer);

		pack32(build_ptr->def_mem_per_cpu, buffer);
		pack32((uint32_t)build_ptr->debug_flags, buffer);
		pack16(build_ptr->disable_root_jobs, buffer);
		pack16(build_ptr->dynalloc_port, buffer);

		pack16(build_ptr->enforce_part_limits, buffer);
		packstr(build_ptr->epilog, buffer);
		pack32(build_ptr->epilog_msg_time, buffer);
		packstr(build_ptr->epilog_slurmctld, buffer);
		packstr(build_ptr->ext_sensors_type, buffer);
		pack16(build_ptr->ext_sensors_freq, buffer);

		pack16(build_ptr->fast_schedule, buffer);
		pack32(build_ptr->first_job_id, buffer);

		pack16(build_ptr->get_env_timeout, buffer);
		packstr(build_ptr->gres_plugins, buffer);
		pack16(build_ptr->group_info, buffer);

		pack32(build_ptr->hash_val, buffer);

		pack16(build_ptr->health_check_interval, buffer);
		pack16(build_ptr->health_check_node_state, buffer);
		packstr(build_ptr->health_check_program, buffer);

		pack16(build_ptr->inactive_limit, buffer);

		packstr(build_ptr->job_acct_gather_freq, buffer);
		packstr(build_ptr->job_acct_gather_type, buffer);

		packstr(build_ptr->job_ckpt_dir, buffer);

		packstr(build_ptr->job_comp_host, buffer);
		packstr(build_ptr->job_comp_loc, buffer);
		pack32((uint32_t)build_ptr->job_comp_port, buffer);
		packstr(build_ptr->job_comp_type, buffer);
		packstr(build_ptr->job_comp_user, buffer);

		packstr(build_ptr->job_credential_private_key, buffer);
		packstr(build_ptr->job_credential_public_certificate, buffer);
		pack16(build_ptr->job_file_append, buffer);
		pack16(build_ptr->job_requeue, buffer);
		packstr(build_ptr->job_submit_plugins, buffer);

		pack16(build_ptr->keep_alive_time, buffer);
		pack16(build_ptr->kill_on_bad_exit, buffer);
		pack16(build_ptr->kill_wait, buffer);

		packstr(build_ptr->launch_type, buffer);
		packstr(build_ptr->licenses, buffer);
		packstr(build_ptr->licenses_used, buffer);

		pack16((uint16_t) build_ptr->max_array_sz, buffer);
		packstr(build_ptr->mail_prog, buffer);
		pack32(build_ptr->max_job_cnt, buffer);
		pack32(build_ptr->max_job_id, buffer);
		pack32(build_ptr->max_mem_per_cpu, buffer);
		pack32(build_ptr->max_step_cnt, buffer);
		pack16(build_ptr->max_tasks_per_node, buffer);
		pack16(build_ptr->min_job_age, buffer);
		packstr(build_ptr->mpi_default, buffer);
		packstr(build_ptr->mpi_params, buffer);
		pack16(build_ptr->msg_timeout, buffer);

		pack32(build_ptr->next_job_id, buffer);
		packstr(build_ptr->node_prefix, buffer);

		pack16(build_ptr->over_time_limit, buffer);

		packstr(build_ptr->plugindir, buffer);
		packstr(build_ptr->plugstack, buffer);
		pack16(build_ptr->preempt_mode, buffer);
		packstr(build_ptr->preempt_type, buffer);

		pack32(build_ptr->priority_decay_hl, buffer);
		pack32(build_ptr->priority_calc_period, buffer);
		pack16(build_ptr->priority_favor_small, buffer);
		pack16(build_ptr->priority_flags, buffer);
		pack32(build_ptr->priority_max_age, buffer);
		pack16(build_ptr->priority_reset_period, buffer);
		packstr(build_ptr->priority_type, buffer);
		pack32(build_ptr->priority_weight_age, buffer);
		pack32(build_ptr->priority_weight_fs, buffer);
		pack32(build_ptr->priority_weight_js, buffer);
		pack32(build_ptr->priority_weight_part, buffer);
		pack32(build_ptr->priority_weight_qos, buffer);

		pack16(build_ptr->private_data, buffer);
		packstr(build_ptr->proctrack_type, buffer);
		packstr(build_ptr->prolog, buffer);
		packstr(build_ptr->prolog_slurmctld, buffer);
		pack16(build_ptr->propagate_prio_process, buffer);
		packstr(build_ptr->propagate_rlimits, buffer);
		packstr(build_ptr->propagate_rlimits_except, buffer);

		packstr(build_ptr->reboot_program, buffer);
		pack16(build_ptr->reconfig_flags, buffer);
		packstr(build_ptr->resume_program, buffer);
		pack16(build_ptr->resume_rate, buffer);
		pack16(build_ptr->resume_timeout, buffer);
		packstr(build_ptr->resv_epilog, buffer);
		pack16(build_ptr->resv_over_run, buffer);
		packstr(build_ptr->resv_prolog, buffer);
		pack16(build_ptr->ret2service, buffer);

		packstr(build_ptr->salloc_default_command, buffer);
		packstr(build_ptr->sched_params, buffer);
		pack16(build_ptr->schedport, buffer);
		pack16(build_ptr->schedrootfltr, buffer);
		packstr(build_ptr->sched_logfile, buffer);
		pack16(build_ptr->sched_log_level, buffer);
		pack16(build_ptr->sched_time_slice, buffer);
		packstr(build_ptr->schedtype, buffer);
		packstr(build_ptr->select_type, buffer);
		if (build_ptr->select_conf_key_pairs)
			count = list_count(build_ptr->select_conf_key_pairs);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			ListIterator itr = list_iterator_create(
				(List)build_ptr->select_conf_key_pairs);
			config_key_pair_t *key_pair = NULL;
			while ((key_pair = list_next(itr))) {
				pack_config_key_pair(key_pair,
						     protocol_version, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(build_ptr->select_type_param, buffer);

		packstr(build_ptr->slurm_conf, buffer);
		pack32(build_ptr->slurm_user_id, buffer);
		packstr(build_ptr->slurm_user_name, buffer);
		pack32(build_ptr->slurmd_user_id, buffer);
		packstr(build_ptr->slurmd_user_name, buffer);

		pack16(build_ptr->slurmctld_debug, buffer);
		packstr(build_ptr->slurmctld_logfile, buffer);
		packstr(build_ptr->slurmctld_pidfile, buffer);
		packstr(build_ptr->slurmctld_plugstack, buffer);
		pack32(build_ptr->slurmctld_port, buffer);
		pack16(build_ptr->slurmctld_port_count, buffer);
		pack16(build_ptr->slurmctld_timeout, buffer);

		pack16(build_ptr->slurmd_debug, buffer);
		packstr(build_ptr->slurmd_logfile, buffer);
		packstr(build_ptr->slurmd_pidfile, buffer);
		if (!(cluster_flags & CLUSTER_FLAG_MULTSD))
			pack32(build_ptr->slurmd_port, buffer);

		packstr(build_ptr->slurmd_spooldir, buffer);
		pack16(build_ptr->slurmd_timeout, buffer);
		packstr(build_ptr->srun_epilog, buffer);
		packstr(build_ptr->srun_prolog, buffer);
		packstr(build_ptr->state_save_location, buffer);
		packstr(build_ptr->suspend_exc_nodes, buffer);
		packstr(build_ptr->suspend_exc_parts, buffer);
		packstr(build_ptr->suspend_program, buffer);
		pack16(build_ptr->suspend_rate, buffer);
		pack32(build_ptr->suspend_time, buffer);
		pack16(build_ptr->suspend_timeout, buffer);
		packstr(build_ptr->switch_type, buffer);

		packstr(build_ptr->task_epilog, buffer);
		packstr(build_ptr->task_prolog, buffer);
		packstr(build_ptr->task_plugin, buffer);
		pack16(build_ptr->task_plugin_param, buffer);
		packstr(build_ptr->tmp_fs, buffer);
		packstr(build_ptr->topology_plugin, buffer);
		pack16(build_ptr->track_wckey, buffer);
		pack16(build_ptr->tree_width, buffer);

		pack16(build_ptr->use_pam, buffer);
		packstr(build_ptr->unkillable_program, buffer);
		pack16(build_ptr->unkillable_timeout, buffer);
		packstr(build_ptr->version, buffer);
		pack16(build_ptr->vsize_factor, buffer);

		pack16(build_ptr->wait_time, buffer);
		pack16(build_ptr->z_16, buffer);
		pack32(build_ptr->z_32, buffer);
		packstr(build_ptr->z_char, buffer);
	} else {
		error("_pack_slurm_ctl_conf_msg: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

static int
_unpack_slurm_ctl_conf_msg(slurm_ctl_conf_info_msg_t **build_buffer_ptr,
			   Buf buffer, uint16_t protocol_version)
{
	uint32_t count = NO_VAL;
	uint32_t uint32_tmp = 0;
	uint16_t uint16_tmp = 0;
	slurm_ctl_conf_info_msg_t *build_ptr;
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();

	/* alloc memory for structure */
	build_ptr = xmalloc(sizeof(slurm_ctl_conf_t));
	*build_buffer_ptr = build_ptr;

	/* initialize this so we don't check for those not sending it */
	build_ptr->hash_val = NO_VAL;

	/* load the data values */

	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		/* unpack timestamp of snapshot */
		safe_unpack_time(&build_ptr->last_update, buffer);

		safe_unpack16(&build_ptr->accounting_storage_enforce, buffer);
		safe_unpackstr_xmalloc(
			&build_ptr->accounting_storage_backup_host,
			&uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_host,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_loc,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->accounting_storage_port, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_type,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_user,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->acctng_store_job_comment, buffer);

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			List tmp_list = list_create(destroy_config_key_pair);
			config_key_pair_t *object = NULL;
			int i;
			for (i=0; i<count; i++) {
				if (unpack_config_key_pair(
					    (void *)&object, protocol_version,
					    buffer)
				    == SLURM_ERROR)
					goto unpack_error;
				list_append(tmp_list, object);
			}
			build_ptr->acct_gather_conf = (void *)tmp_list;
		}

		safe_unpackstr_xmalloc(&build_ptr->acct_gather_energy_type,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->acct_gather_filesystem_type,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->acct_gather_infiniband_type,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->acct_gather_node_freq, buffer);
		safe_unpackstr_xmalloc(&build_ptr->acct_gather_profile_type,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->authinfo,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->authtype,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->backup_addr,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->backup_controller,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->batch_start_timeout, buffer);
		safe_unpack_time(&build_ptr->boot_time, buffer);

		safe_unpackstr_xmalloc(&build_ptr->checkpoint_type,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->chos_loc,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->cluster_name,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->complete_wait, buffer);
		safe_unpackstr_xmalloc(&build_ptr->control_addr,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->control_machine,
				       &uint32_tmp,buffer);
		safe_unpackstr_xmalloc(&build_ptr->core_spec_plugin,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->cpu_freq_def, buffer);
		safe_unpackstr_xmalloc(&build_ptr->crypto_type, &uint32_tmp,
				       buffer);

		safe_unpack32(&build_ptr->def_mem_per_cpu, buffer);
		safe_unpack64(&build_ptr->debug_flags, buffer);
		safe_unpack16(&build_ptr->disable_root_jobs, buffer);
		safe_unpack16(&build_ptr->dynalloc_port, buffer);

		safe_unpack16(&build_ptr->enforce_part_limits, buffer);
		safe_unpackstr_xmalloc(&build_ptr->epilog, &uint32_tmp,
				       buffer);
		safe_unpack32(&build_ptr->epilog_msg_time, buffer);
		safe_unpackstr_xmalloc(&build_ptr->epilog_slurmctld,
				       &uint32_tmp, buffer);

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			List tmp_list = list_create(destroy_config_key_pair);
			config_key_pair_t *object = NULL;
			int i;
			for (i=0; i<count; i++) {
				if (unpack_config_key_pair(
					    (void *)&object, protocol_version,
					    buffer)
				    == SLURM_ERROR)
					goto unpack_error;
				list_append(tmp_list, object);
			}
			build_ptr->ext_sensors_conf = (void *)tmp_list;
		}

		safe_unpackstr_xmalloc(&build_ptr->ext_sensors_type,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->ext_sensors_freq, buffer);

		safe_unpack16(&build_ptr->fast_schedule, buffer);
		safe_unpack32(&build_ptr->first_job_id, buffer);
		safe_unpack16(&build_ptr->fs_dampening_factor, buffer);

		safe_unpack16(&build_ptr->get_env_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->gres_plugins,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->group_info, buffer);

		safe_unpack32(&build_ptr->hash_val, buffer);

		safe_unpack16(&build_ptr->health_check_interval, buffer);
		safe_unpack16(&build_ptr->health_check_node_state, buffer);
		safe_unpackstr_xmalloc(&build_ptr->health_check_program,
				       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->inactive_limit, buffer);

		safe_unpackstr_xmalloc(&build_ptr->job_acct_gather_freq,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_acct_gather_type,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_acct_gather_params,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->job_ckpt_dir,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->job_comp_host,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_comp_loc,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->job_comp_port, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_comp_type,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_comp_user,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_container_plugin,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->job_credential_private_key,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->
				       job_credential_public_certificate,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->job_file_append, buffer);
		safe_unpack16(&build_ptr->job_requeue, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_submit_plugins,
				       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->keep_alive_time, buffer);
		safe_unpack16(&build_ptr->kill_on_bad_exit, buffer);
		safe_unpack16(&build_ptr->kill_wait, buffer);

		safe_unpackstr_xmalloc(&build_ptr->launch_type,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->layouts,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->licenses,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->licenses_used,
				       &uint32_tmp, buffer);

		safe_unpack32(&build_ptr->max_array_sz, buffer);
		safe_unpackstr_xmalloc(&build_ptr->mail_prog,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->max_job_cnt, buffer);
		safe_unpack32(&build_ptr->max_job_id, buffer);
		safe_unpack32(&build_ptr->max_mem_per_cpu, buffer);
		safe_unpack32(&build_ptr->max_step_cnt, buffer);
		safe_unpack16(&build_ptr->max_tasks_per_node, buffer);
		safe_unpack16(&build_ptr->mem_limit_enforce, buffer);
		safe_unpack16(&build_ptr->min_job_age, buffer);
		safe_unpackstr_xmalloc(&build_ptr->mpi_default,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->mpi_params,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->msg_timeout, buffer);

		safe_unpack32(&build_ptr->next_job_id, buffer);
		safe_unpackstr_xmalloc(&build_ptr->node_prefix,
				       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->over_time_limit, buffer);

		safe_unpackstr_xmalloc(&build_ptr->plugindir,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->plugstack,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->preempt_mode, buffer);
		safe_unpackstr_xmalloc(&build_ptr->preempt_type,
				       &uint32_tmp, buffer);

		safe_unpack32(&build_ptr->priority_decay_hl, buffer);
		safe_unpack32(&build_ptr->priority_calc_period, buffer);
		safe_unpack16(&build_ptr->priority_favor_small, buffer);
		safe_unpack16(&build_ptr->priority_flags, buffer);
		safe_unpack32(&build_ptr->priority_max_age, buffer);
		safe_unpackstr_xmalloc(&build_ptr->priority_params, &uint32_tmp,
				       buffer);
		safe_unpack16(&build_ptr->priority_reset_period, buffer);
		safe_unpackstr_xmalloc(&build_ptr->priority_type, &uint32_tmp,
				       buffer);
		safe_unpack32(&build_ptr->priority_weight_age, buffer);
		safe_unpack32(&build_ptr->priority_weight_fs, buffer);
		safe_unpack32(&build_ptr->priority_weight_js, buffer);
		safe_unpack32(&build_ptr->priority_weight_part, buffer);
		safe_unpack32(&build_ptr->priority_weight_qos, buffer);

		safe_unpack16(&build_ptr->private_data, buffer);
		safe_unpackstr_xmalloc(&build_ptr->proctrack_type, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&build_ptr->prolog, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&build_ptr->prolog_slurmctld,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->prolog_flags, buffer);
		safe_unpack16(&build_ptr->propagate_prio_process, buffer);
		safe_unpackstr_xmalloc(&build_ptr->propagate_rlimits,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->propagate_rlimits_except,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->reboot_program, &uint32_tmp,
				       buffer);
		safe_unpack16(&build_ptr->reconfig_flags, buffer);

		safe_unpackstr_xmalloc(&build_ptr->requeue_exit,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->requeue_exit_hold,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->resume_program,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->resume_rate, buffer);
		safe_unpack16(&build_ptr->resume_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->resv_epilog, &uint32_tmp,
				       buffer);
		safe_unpack16(&build_ptr->resv_over_run, buffer);
		safe_unpackstr_xmalloc(&build_ptr->resv_prolog, &uint32_tmp,
				       buffer);
		safe_unpack16(&build_ptr->ret2service, buffer);

		safe_unpackstr_xmalloc(&build_ptr->route_plugin,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->salloc_default_command,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->sched_params,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->schedport, buffer);
		safe_unpack16(&build_ptr->schedrootfltr, buffer);
		safe_unpackstr_xmalloc(&build_ptr->sched_logfile,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->sched_log_level, buffer);
		safe_unpack16(&build_ptr->sched_time_slice, buffer);
		safe_unpackstr_xmalloc(&build_ptr->schedtype,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->select_type,
				       &uint32_tmp, buffer);
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			List tmp_list = list_create(destroy_config_key_pair);
			config_key_pair_t *object = NULL;
			int i;
			for (i=0; i<count; i++) {
				if (unpack_config_key_pair(
					    (void *)&object, protocol_version,
					    buffer)
				    == SLURM_ERROR)
					goto unpack_error;
				list_append(tmp_list, object);
			}
			build_ptr->select_conf_key_pairs = (void *)tmp_list;
		}

		safe_unpack16(&build_ptr->select_type_param, buffer);

		safe_unpackstr_xmalloc(&build_ptr->slurm_conf,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->slurm_user_id, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurm_user_name,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->slurmd_user_id, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmd_user_name,
				       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->slurmctld_debug, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_logfile,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_pidfile,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_plugstack,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->slurmctld_port, buffer);
		safe_unpack16(&build_ptr->slurmctld_port_count, buffer);
		safe_unpack16(&build_ptr->slurmctld_timeout, buffer);

		safe_unpack16(&build_ptr->slurmd_debug, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmd_logfile, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmd_pidfile, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmd_plugstack,
				       &uint32_tmp, buffer);
		if (!(cluster_flags & CLUSTER_FLAG_MULTSD))
			safe_unpack32(&build_ptr->slurmd_port, buffer);

		safe_unpackstr_xmalloc(&build_ptr->slurmd_spooldir,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->slurmd_timeout, buffer);

		safe_unpackstr_xmalloc(&build_ptr->srun_epilog,
				       &uint32_tmp, buffer);

		build_ptr->srun_port_range = xmalloc(2 * sizeof(uint16_t));
		safe_unpack16(&build_ptr->srun_port_range[0], buffer);
		safe_unpack16(&build_ptr->srun_port_range[1], buffer);

		safe_unpackstr_xmalloc(&build_ptr->srun_prolog,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->state_save_location,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->suspend_exc_nodes,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->suspend_exc_parts,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->suspend_program,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->suspend_rate, buffer);
		safe_unpack32(&build_ptr->suspend_time, buffer);
		safe_unpack16(&build_ptr->suspend_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->switch_type,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->task_epilog,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->task_prolog,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->task_plugin,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->task_plugin_param, buffer);
		safe_unpackstr_xmalloc(&build_ptr->tmp_fs, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&build_ptr->topology_plugin,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->track_wckey, buffer);
		safe_unpack16(&build_ptr->tree_width, buffer);

		safe_unpack16(&build_ptr->use_pam, buffer);
		safe_unpack16(&build_ptr->use_spec_resources, buffer);
		safe_unpackstr_xmalloc(&build_ptr->unkillable_program,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->unkillable_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->version,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->vsize_factor, buffer);

		safe_unpack16(&build_ptr->wait_time, buffer);

		safe_unpack16(&build_ptr->z_16, buffer);
		safe_unpack32(&build_ptr->z_32, buffer);
		safe_unpackstr_xmalloc(&build_ptr->z_char, &uint32_tmp,
				       buffer);
	} else if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		/* unpack timestamp of snapshot */
		safe_unpack_time(&build_ptr->last_update, buffer);

		safe_unpack16(&build_ptr->accounting_storage_enforce, buffer);
		safe_unpackstr_xmalloc(
			&build_ptr->accounting_storage_backup_host,
			&uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_host,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_loc,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->accounting_storage_port, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_type,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_user,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->acctng_store_job_comment, buffer);

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			List tmp_list = list_create(destroy_config_key_pair);
			config_key_pair_t *object = NULL;
			int i;
			for (i=0; i<count; i++) {
				if (unpack_config_key_pair(
					    (void *)&object, protocol_version,
					    buffer)
				    == SLURM_ERROR)
					goto unpack_error;
				list_append(tmp_list, object);
			}
			build_ptr->acct_gather_conf = (void *)tmp_list;
		}

		safe_unpackstr_xmalloc(&build_ptr->acct_gather_energy_type,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->acct_gather_filesystem_type,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->acct_gather_infiniband_type,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->acct_gather_node_freq, buffer);
		safe_unpackstr_xmalloc(&build_ptr->acct_gather_profile_type,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->authinfo,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->authtype,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->backup_addr,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->backup_controller,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->batch_start_timeout, buffer);
		safe_unpack_time(&build_ptr->boot_time, buffer);

		safe_unpackstr_xmalloc(&build_ptr->checkpoint_type,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->cluster_name,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->complete_wait, buffer);
		safe_unpackstr_xmalloc(&build_ptr->control_addr,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->control_machine,
				       &uint32_tmp,buffer);
		safe_unpackstr_xmalloc(&build_ptr->core_spec_plugin,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->crypto_type, &uint32_tmp,
				       buffer);

		safe_unpack32(&build_ptr->def_mem_per_cpu, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		build_ptr->debug_flags = (uint64_t)uint32_tmp;
		safe_unpack16(&build_ptr->disable_root_jobs, buffer);
		safe_unpack16(&build_ptr->dynalloc_port, buffer);

		safe_unpack16(&build_ptr->enforce_part_limits, buffer);
		safe_unpackstr_xmalloc(&build_ptr->epilog, &uint32_tmp,
				       buffer);
		safe_unpack32(&build_ptr->epilog_msg_time, buffer);
		safe_unpackstr_xmalloc(&build_ptr->epilog_slurmctld,
				       &uint32_tmp, buffer);

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			List tmp_list = list_create(destroy_config_key_pair);
			config_key_pair_t *object = NULL;
			int i;
			for (i=0; i<count; i++) {
				if (unpack_config_key_pair(
					    (void *)&object, protocol_version,
					    buffer)
				    == SLURM_ERROR)
					goto unpack_error;
				list_append(tmp_list, object);
			}
			build_ptr->ext_sensors_conf = (void *)tmp_list;
		}

		safe_unpackstr_xmalloc(&build_ptr->ext_sensors_type,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->ext_sensors_freq, buffer);

		safe_unpack16(&build_ptr->fast_schedule, buffer);
		safe_unpack32(&build_ptr->first_job_id, buffer);
		safe_unpack16(&build_ptr->fs_dampening_factor, buffer);

		safe_unpack16(&build_ptr->get_env_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->gres_plugins,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->group_info, buffer);

		safe_unpack32(&build_ptr->hash_val, buffer);

		safe_unpack16(&build_ptr->health_check_interval, buffer);
		safe_unpack16(&build_ptr->health_check_node_state, buffer);
		safe_unpackstr_xmalloc(&build_ptr->health_check_program,
				       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->inactive_limit, buffer);

		safe_unpackstr_xmalloc(&build_ptr->job_acct_gather_freq,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_acct_gather_type,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_acct_gather_params,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->job_ckpt_dir,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->job_comp_host,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_comp_loc,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->job_comp_port, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_comp_type,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_comp_user,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_container_plugin,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->job_credential_private_key,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->
				       job_credential_public_certificate,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->job_file_append, buffer);
		safe_unpack16(&build_ptr->job_requeue, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_submit_plugins,
				       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->keep_alive_time, buffer);
		safe_unpack16(&build_ptr->kill_on_bad_exit, buffer);
		safe_unpack16(&build_ptr->kill_wait, buffer);

		safe_unpackstr_xmalloc(&build_ptr->launch_type,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->licenses,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->licenses_used,
				       &uint32_tmp, buffer);

		safe_unpack32(&build_ptr->max_array_sz, buffer);
		safe_unpackstr_xmalloc(&build_ptr->mail_prog,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->max_job_cnt, buffer);
		safe_unpack32(&build_ptr->max_job_id, buffer);
		safe_unpack32(&build_ptr->max_mem_per_cpu, buffer);
		safe_unpack32(&build_ptr->max_step_cnt, buffer);
		safe_unpack16(&build_ptr->max_tasks_per_node, buffer);
		safe_unpack16(&build_ptr->min_job_age, buffer);
		safe_unpackstr_xmalloc(&build_ptr->mpi_default,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->mpi_params,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->msg_timeout, buffer);

		safe_unpack32(&build_ptr->next_job_id, buffer);
		safe_unpackstr_xmalloc(&build_ptr->node_prefix,
				       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->over_time_limit, buffer);

		safe_unpackstr_xmalloc(&build_ptr->plugindir,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->plugstack,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->preempt_mode, buffer);
		safe_unpackstr_xmalloc(&build_ptr->preempt_type,
				       &uint32_tmp, buffer);

		safe_unpack32(&build_ptr->priority_decay_hl, buffer);
		safe_unpack32(&build_ptr->priority_calc_period, buffer);
		safe_unpack16(&build_ptr->priority_favor_small, buffer);
		safe_unpack16(&build_ptr->priority_flags, buffer);
		safe_unpack32(&build_ptr->priority_max_age, buffer);
		safe_unpack16(&build_ptr->priority_reset_period, buffer);
		safe_unpackstr_xmalloc(&build_ptr->priority_type, &uint32_tmp,
				       buffer);
		safe_unpack32(&build_ptr->priority_weight_age, buffer);
		safe_unpack32(&build_ptr->priority_weight_fs, buffer);
		safe_unpack32(&build_ptr->priority_weight_js, buffer);
		safe_unpack32(&build_ptr->priority_weight_part, buffer);
		safe_unpack32(&build_ptr->priority_weight_qos, buffer);

		safe_unpack16(&build_ptr->private_data, buffer);
		safe_unpackstr_xmalloc(&build_ptr->proctrack_type, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&build_ptr->prolog, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&build_ptr->prolog_slurmctld,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->prolog_flags, buffer);
		safe_unpack16(&build_ptr->propagate_prio_process, buffer);
		safe_unpackstr_xmalloc(&build_ptr->propagate_rlimits,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->propagate_rlimits_except,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->reboot_program, &uint32_tmp,
				       buffer);
		safe_unpack16(&build_ptr->reconfig_flags, buffer);
		safe_unpackstr_xmalloc(&build_ptr->resume_program,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->resume_rate, buffer);
		safe_unpack16(&build_ptr->resume_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->resv_epilog, &uint32_tmp,
				       buffer);
		safe_unpack16(&build_ptr->resv_over_run, buffer);
		safe_unpackstr_xmalloc(&build_ptr->resv_prolog, &uint32_tmp,
				       buffer);
		safe_unpack16(&build_ptr->ret2service, buffer);

		safe_unpackstr_xmalloc(&build_ptr->salloc_default_command,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->sched_params,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->schedport, buffer);
		safe_unpack16(&build_ptr->schedrootfltr, buffer);
		safe_unpackstr_xmalloc(&build_ptr->sched_logfile,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->sched_log_level, buffer);
		safe_unpack16(&build_ptr->sched_time_slice, buffer);
		safe_unpackstr_xmalloc(&build_ptr->schedtype,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->select_type,
				       &uint32_tmp, buffer);
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			List tmp_list = list_create(destroy_config_key_pair);
			config_key_pair_t *object = NULL;
			int i;
			for (i=0; i<count; i++) {
				if (unpack_config_key_pair(
					    (void *)&object, protocol_version,
					    buffer)
				    == SLURM_ERROR)
					goto unpack_error;
				list_append(tmp_list, object);
			}
			build_ptr->select_conf_key_pairs = (void *)tmp_list;
		}

		safe_unpack16(&build_ptr->select_type_param, buffer);

		safe_unpackstr_xmalloc(&build_ptr->slurm_conf,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->slurm_user_id, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurm_user_name,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->slurmd_user_id, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmd_user_name,
				       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->slurmctld_debug, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_logfile,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_pidfile,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_plugstack,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->slurmctld_port, buffer);
		safe_unpack16(&build_ptr->slurmctld_port_count, buffer);
		safe_unpack16(&build_ptr->slurmctld_timeout, buffer);

		safe_unpack16(&build_ptr->slurmd_debug, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmd_logfile, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmd_pidfile, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmd_plugstack,
				       &uint32_tmp, buffer);
		if (!(cluster_flags & CLUSTER_FLAG_MULTSD))
			safe_unpack32(&build_ptr->slurmd_port, buffer);

		safe_unpackstr_xmalloc(&build_ptr->slurmd_spooldir,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->slurmd_timeout, buffer);

		safe_unpackstr_xmalloc(&build_ptr->srun_epilog,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->srun_prolog,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->state_save_location,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->suspend_exc_nodes,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->suspend_exc_parts,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->suspend_program,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->suspend_rate, buffer);
		safe_unpack32(&build_ptr->suspend_time, buffer);
		safe_unpack16(&build_ptr->suspend_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->switch_type,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->task_epilog,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->task_prolog,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->task_plugin,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->task_plugin_param, buffer);
		safe_unpackstr_xmalloc(&build_ptr->tmp_fs, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&build_ptr->topology_plugin,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->track_wckey, buffer);
		safe_unpack16(&build_ptr->tree_width, buffer);

		safe_unpack16(&build_ptr->use_pam, buffer);
		safe_unpackstr_xmalloc(&build_ptr->unkillable_program,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->unkillable_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->version,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->vsize_factor, buffer);

		safe_unpack16(&build_ptr->wait_time, buffer);

		safe_unpack16(&build_ptr->z_16, buffer);
		safe_unpack32(&build_ptr->z_32, buffer);
		safe_unpackstr_xmalloc(&build_ptr->z_char, &uint32_tmp,
				       buffer);
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		/* unpack timestamp of snapshot */
		safe_unpack_time(&build_ptr->last_update, buffer);

		safe_unpack16(&build_ptr->accounting_storage_enforce, buffer);
		safe_unpackstr_xmalloc(
			&build_ptr->accounting_storage_backup_host,
			&uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_host,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_loc,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->accounting_storage_port, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_type,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->accounting_storage_user,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->acctng_store_job_comment, buffer);
		safe_unpackstr_xmalloc(&build_ptr->acct_gather_energy_type,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->acct_gather_filesystem_type,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->acct_gather_infiniband_type,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->acct_gather_node_freq, buffer);
		safe_unpackstr_xmalloc(&build_ptr->acct_gather_profile_type,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->authtype,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->backup_addr,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->backup_controller,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->batch_start_timeout, buffer);
		safe_unpack_time(&build_ptr->boot_time, buffer);

		safe_unpackstr_xmalloc(&build_ptr->checkpoint_type,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->cluster_name,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->complete_wait, buffer);
		safe_unpackstr_xmalloc(&build_ptr->control_addr,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->control_machine,
				       &uint32_tmp,buffer);
		safe_unpackstr_xmalloc(&build_ptr->crypto_type, &uint32_tmp,
				       buffer);

		safe_unpack32(&build_ptr->def_mem_per_cpu, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		build_ptr->debug_flags = (uint64_t)uint32_tmp;
		safe_unpack16(&build_ptr->disable_root_jobs, buffer);
		safe_unpack16(&build_ptr->dynalloc_port, buffer);

		safe_unpack16(&build_ptr->enforce_part_limits, buffer);
		safe_unpackstr_xmalloc(&build_ptr->epilog, &uint32_tmp,
				       buffer);
		safe_unpack32(&build_ptr->epilog_msg_time, buffer);
		safe_unpackstr_xmalloc(&build_ptr->epilog_slurmctld,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->ext_sensors_type,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->ext_sensors_freq, buffer);

		safe_unpack16(&build_ptr->fast_schedule, buffer);
		safe_unpack32(&build_ptr->first_job_id, buffer);

		safe_unpack16(&build_ptr->get_env_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->gres_plugins,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->group_info, buffer);

		safe_unpack32(&build_ptr->hash_val, buffer);

		safe_unpack16(&build_ptr->health_check_interval, buffer);
		safe_unpack16(&build_ptr->health_check_node_state, buffer);
		safe_unpackstr_xmalloc(&build_ptr->health_check_program,
				       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->inactive_limit, buffer);

		safe_unpackstr_xmalloc(&build_ptr->job_acct_gather_freq,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_acct_gather_type,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->job_ckpt_dir,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->job_comp_host,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_comp_loc,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->job_comp_port, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_comp_type,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_comp_user,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->job_credential_private_key,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->
				       job_credential_public_certificate,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->job_file_append, buffer);
		safe_unpack16(&build_ptr->job_requeue, buffer);
		safe_unpackstr_xmalloc(&build_ptr->job_submit_plugins,
				       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->keep_alive_time, buffer);
		safe_unpack16(&build_ptr->kill_on_bad_exit, buffer);
		safe_unpack16(&build_ptr->kill_wait, buffer);

		safe_unpackstr_xmalloc(&build_ptr->launch_type,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->licenses,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->licenses_used,
				       &uint32_tmp, buffer);

		safe_unpack16(&uint16_tmp, buffer);
		if (uint16_tmp == (uint16_t) NO_VAL)
			build_ptr->max_array_sz = NO_VAL;
		else
			build_ptr->max_array_sz = (uint32_t) uint16_tmp;
		safe_unpackstr_xmalloc(&build_ptr->mail_prog,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->max_job_cnt, buffer);
		safe_unpack32(&build_ptr->max_job_id, buffer);
		safe_unpack32(&build_ptr->max_mem_per_cpu, buffer);
		safe_unpack32(&build_ptr->max_step_cnt, buffer);
		safe_unpack16(&build_ptr->max_tasks_per_node, buffer);
		safe_unpack16(&build_ptr->min_job_age, buffer);
		safe_unpackstr_xmalloc(&build_ptr->mpi_default,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->mpi_params,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->msg_timeout, buffer);

		safe_unpack32(&build_ptr->next_job_id, buffer);
		safe_unpackstr_xmalloc(&build_ptr->node_prefix,
				       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->over_time_limit, buffer);

		safe_unpackstr_xmalloc(&build_ptr->plugindir,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->plugstack,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->preempt_mode, buffer);
		safe_unpackstr_xmalloc(&build_ptr->preempt_type,
				       &uint32_tmp, buffer);

		safe_unpack32(&build_ptr->priority_decay_hl, buffer);
		safe_unpack32(&build_ptr->priority_calc_period, buffer);
		safe_unpack16(&build_ptr->priority_favor_small, buffer);
		safe_unpack16(&build_ptr->priority_flags, buffer);
		safe_unpack32(&build_ptr->priority_max_age, buffer);
		safe_unpack16(&build_ptr->priority_reset_period, buffer);
		safe_unpackstr_xmalloc(&build_ptr->priority_type, &uint32_tmp,
				       buffer);
		safe_unpack32(&build_ptr->priority_weight_age, buffer);
		safe_unpack32(&build_ptr->priority_weight_fs, buffer);
		safe_unpack32(&build_ptr->priority_weight_js, buffer);
		safe_unpack32(&build_ptr->priority_weight_part, buffer);
		safe_unpack32(&build_ptr->priority_weight_qos, buffer);

		safe_unpack16(&build_ptr->private_data, buffer);
		safe_unpackstr_xmalloc(&build_ptr->proctrack_type, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&build_ptr->prolog, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&build_ptr->prolog_slurmctld,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->propagate_prio_process, buffer);
		safe_unpackstr_xmalloc(&build_ptr->propagate_rlimits,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->propagate_rlimits_except,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->reboot_program, &uint32_tmp,
				       buffer);
		safe_unpack16(&build_ptr->reconfig_flags, buffer);
		safe_unpackstr_xmalloc(&build_ptr->resume_program,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->resume_rate, buffer);
		safe_unpack16(&build_ptr->resume_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->resv_epilog, &uint32_tmp,
				       buffer);
		safe_unpack16(&build_ptr->resv_over_run, buffer);
		safe_unpackstr_xmalloc(&build_ptr->resv_prolog, &uint32_tmp,
				       buffer);
		safe_unpack16(&build_ptr->ret2service, buffer);

		safe_unpackstr_xmalloc(&build_ptr->salloc_default_command,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->sched_params,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->schedport, buffer);
		safe_unpack16(&build_ptr->schedrootfltr, buffer);
		safe_unpackstr_xmalloc(&build_ptr->sched_logfile,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->sched_log_level, buffer);
		safe_unpack16(&build_ptr->sched_time_slice, buffer);
		safe_unpackstr_xmalloc(&build_ptr->schedtype,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->select_type,
				       &uint32_tmp, buffer);
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			List tmp_list = list_create(destroy_config_key_pair);
			config_key_pair_t *object = NULL;
			int i;
			for (i=0; i<count; i++) {
				if (unpack_config_key_pair(
					    (void *)&object, protocol_version,
					    buffer)
				    == SLURM_ERROR)
					goto unpack_error;
				list_append(tmp_list, object);
			}
			build_ptr->select_conf_key_pairs = (void *)tmp_list;
		}

		safe_unpack16(&build_ptr->select_type_param, buffer);

		safe_unpackstr_xmalloc(&build_ptr->slurm_conf,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->slurm_user_id, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurm_user_name,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->slurmd_user_id, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmd_user_name,
				       &uint32_tmp, buffer);

		safe_unpack16(&build_ptr->slurmctld_debug, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_logfile,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_pidfile,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmctld_plugstack,
				       &uint32_tmp, buffer);
		safe_unpack32(&build_ptr->slurmctld_port, buffer);
		safe_unpack16(&build_ptr->slurmctld_port_count, buffer);
		safe_unpack16(&build_ptr->slurmctld_timeout, buffer);

		safe_unpack16(&build_ptr->slurmd_debug, buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmd_logfile, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&build_ptr->slurmd_pidfile, &uint32_tmp,
				       buffer);
		if (!(cluster_flags & CLUSTER_FLAG_MULTSD))
			safe_unpack32(&build_ptr->slurmd_port, buffer);

		safe_unpackstr_xmalloc(&build_ptr->slurmd_spooldir,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->slurmd_timeout, buffer);

		safe_unpackstr_xmalloc(&build_ptr->srun_epilog,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->srun_prolog,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->state_save_location,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->suspend_exc_nodes,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->suspend_exc_parts,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->suspend_program,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->suspend_rate, buffer);
		safe_unpack32(&build_ptr->suspend_time, buffer);
		safe_unpack16(&build_ptr->suspend_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->switch_type,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&build_ptr->task_epilog,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->task_prolog,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&build_ptr->task_plugin,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->task_plugin_param, buffer);
		safe_unpackstr_xmalloc(&build_ptr->tmp_fs, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&build_ptr->topology_plugin,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->track_wckey, buffer);
		safe_unpack16(&build_ptr->tree_width, buffer);

		safe_unpack16(&build_ptr->use_pam, buffer);
		safe_unpackstr_xmalloc(&build_ptr->unkillable_program,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->unkillable_timeout, buffer);
		safe_unpackstr_xmalloc(&build_ptr->version,
				       &uint32_tmp, buffer);
		safe_unpack16(&build_ptr->vsize_factor, buffer);

		safe_unpack16(&build_ptr->wait_time, buffer);

		safe_unpack16(&build_ptr->z_16, buffer);
		safe_unpack32(&build_ptr->z_32, buffer);
		safe_unpackstr_xmalloc(&build_ptr->z_char, &uint32_tmp,
				       buffer);
	} else {
		error("_unpack_slurm_ctl_conf_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_ctl_conf(build_ptr);
	*build_buffer_ptr = NULL;
	return SLURM_ERROR;
}

/* _pack_job_desc_msg
 * packs a job_desc struct
 * IN job_desc_ptr - pointer to the job descriptor to pack
 * IN/OUT buffer - destination of the pack, contains pointers that are
 *			automatically updated
 */
static void
_pack_job_desc_msg(job_desc_msg_t * job_desc_ptr, Buf buffer,
		   uint16_t protocol_version)
{
	/* load the data values */
	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		pack16(job_desc_ptr->contiguous, buffer);
		pack16(job_desc_ptr->core_spec, buffer);
		pack16(job_desc_ptr->task_dist, buffer);
		pack16(job_desc_ptr->kill_on_node_fail, buffer);
		packstr(job_desc_ptr->features, buffer);
		packstr(job_desc_ptr->gres, buffer);
		pack32(job_desc_ptr->job_id, buffer);
		packstr(job_desc_ptr->job_id_str, buffer);
		packstr(job_desc_ptr->name, buffer);

		packstr(job_desc_ptr->alloc_node, buffer);
		pack32(job_desc_ptr->alloc_sid, buffer);
		packstr(job_desc_ptr->array_inx, buffer);
		pack16(job_desc_ptr->pn_min_cpus, buffer);
		pack32(job_desc_ptr->pn_min_memory, buffer);
		pack32(job_desc_ptr->pn_min_tmp_disk, buffer);

		packstr(job_desc_ptr->partition, buffer);
		pack32(job_desc_ptr->priority, buffer);
		packstr(job_desc_ptr->dependency, buffer);
		packstr(job_desc_ptr->account, buffer);
		packstr(job_desc_ptr->comment, buffer);
		pack16(job_desc_ptr->nice, buffer);
		pack32(job_desc_ptr->profile, buffer);
		packstr(job_desc_ptr->qos, buffer);

		pack8(job_desc_ptr->open_mode,   buffer);
		pack8(job_desc_ptr->overcommit,  buffer);
		packstr(job_desc_ptr->acctg_freq, buffer);
		pack32(job_desc_ptr->num_tasks,  buffer);
		pack16(job_desc_ptr->ckpt_interval, buffer);

		packstr(job_desc_ptr->req_nodes, buffer);
		packstr(job_desc_ptr->exc_nodes, buffer);
		packstr_array(job_desc_ptr->environment,
			      job_desc_ptr->env_size, buffer);
		packstr_array(job_desc_ptr->spank_job_env,
			      job_desc_ptr->spank_job_env_size, buffer);
		packstr(job_desc_ptr->script, buffer);
		packstr_array(job_desc_ptr->argv, job_desc_ptr->argc, buffer);

		packstr(job_desc_ptr->std_err, buffer);
		packstr(job_desc_ptr->std_in, buffer);
		packstr(job_desc_ptr->std_out, buffer);
		packstr(job_desc_ptr->work_dir, buffer);
		packstr(job_desc_ptr->ckpt_dir, buffer);

		pack16(job_desc_ptr->immediate, buffer);
		pack16(job_desc_ptr->reboot, buffer);
		pack16(job_desc_ptr->requeue, buffer);
		pack16(job_desc_ptr->shared, buffer);
		pack16(job_desc_ptr->cpus_per_task, buffer);
		pack16(job_desc_ptr->ntasks_per_node, buffer);
		pack16(job_desc_ptr->ntasks_per_board, buffer);
		pack16(job_desc_ptr->ntasks_per_socket, buffer);
		pack16(job_desc_ptr->ntasks_per_core, buffer);

		pack16(job_desc_ptr->plane_size, buffer);
		pack16(job_desc_ptr->cpu_bind_type, buffer);
		pack16(job_desc_ptr->mem_bind_type, buffer);
		packstr(job_desc_ptr->cpu_bind, buffer);
		packstr(job_desc_ptr->mem_bind, buffer);

		pack32(job_desc_ptr->time_limit, buffer);
		pack32(job_desc_ptr->time_min, buffer);
		pack32(job_desc_ptr->min_cpus, buffer);
		pack32(job_desc_ptr->max_cpus, buffer);
		pack32(job_desc_ptr->min_nodes, buffer);
		pack32(job_desc_ptr->max_nodes, buffer);
		pack16(job_desc_ptr->boards_per_node, buffer);
		pack16(job_desc_ptr->sockets_per_board, buffer);
		pack16(job_desc_ptr->sockets_per_node, buffer);
		pack16(job_desc_ptr->cores_per_socket, buffer);
		pack16(job_desc_ptr->threads_per_core, buffer);
		pack32(job_desc_ptr->user_id, buffer);
		pack32(job_desc_ptr->group_id, buffer);

		pack16(job_desc_ptr->alloc_resp_port, buffer);
		pack16(job_desc_ptr->other_port, buffer);
		packstr(job_desc_ptr->network, buffer);
		pack_time(job_desc_ptr->begin_time, buffer);
		pack_time(job_desc_ptr->end_time, buffer);

		packstr(job_desc_ptr->licenses, buffer);
		pack16(job_desc_ptr->mail_type, buffer);
		packstr(job_desc_ptr->mail_user, buffer);
		packstr(job_desc_ptr->reservation, buffer);
		pack16(job_desc_ptr->warn_flags, buffer);
		pack16(job_desc_ptr->warn_signal, buffer);
		pack16(job_desc_ptr->warn_time, buffer);
		packstr(job_desc_ptr->wckey, buffer);
		pack32(job_desc_ptr->req_switch, buffer);
		pack32(job_desc_ptr->wait4switch, buffer);

		if (job_desc_ptr->select_jobinfo) {
			select_g_select_jobinfo_pack(
				job_desc_ptr->select_jobinfo,
				buffer, protocol_version);
		} else {
			job_desc_ptr->select_jobinfo =
				select_g_select_jobinfo_alloc();
			if (job_desc_ptr->geometry[0] != (uint16_t) NO_VAL)
				select_g_select_jobinfo_set(
					job_desc_ptr->select_jobinfo,
					SELECT_JOBDATA_GEOMETRY,
					job_desc_ptr->geometry);

			if (job_desc_ptr->conn_type[0] != (uint16_t) NO_VAL)
				select_g_select_jobinfo_set(
					job_desc_ptr->select_jobinfo,
					SELECT_JOBDATA_CONN_TYPE,
					&(job_desc_ptr->conn_type));
			if (job_desc_ptr->reboot != (uint16_t) NO_VAL)
				select_g_select_jobinfo_set(
					job_desc_ptr->select_jobinfo,
					SELECT_JOBDATA_REBOOT,
					&(job_desc_ptr->reboot));
			if (job_desc_ptr->rotate != (uint16_t) NO_VAL)
				select_g_select_jobinfo_set(
					job_desc_ptr->select_jobinfo,
					SELECT_JOBDATA_ROTATE,
					&(job_desc_ptr->rotate));
			if (job_desc_ptr->blrtsimage) {
				select_g_select_jobinfo_set(
					job_desc_ptr->select_jobinfo,
					SELECT_JOBDATA_BLRTS_IMAGE,
					job_desc_ptr->blrtsimage);
			}
			if (job_desc_ptr->linuximage)
				select_g_select_jobinfo_set(
					job_desc_ptr->select_jobinfo,
					SELECT_JOBDATA_LINUX_IMAGE,
					job_desc_ptr->linuximage);
			if (job_desc_ptr->mloaderimage)
				select_g_select_jobinfo_set(
					job_desc_ptr->select_jobinfo,
					SELECT_JOBDATA_MLOADER_IMAGE,
					job_desc_ptr->mloaderimage);
			if (job_desc_ptr->ramdiskimage)
				select_g_select_jobinfo_set(
					job_desc_ptr->select_jobinfo,
					SELECT_JOBDATA_RAMDISK_IMAGE,
					job_desc_ptr->ramdiskimage);
			select_g_select_jobinfo_pack(
				job_desc_ptr->select_jobinfo,
				buffer, protocol_version);
			select_g_select_jobinfo_free(
				job_desc_ptr->select_jobinfo);
			job_desc_ptr->select_jobinfo = NULL;
		}
		pack16(job_desc_ptr->wait_all_nodes, buffer);
	} else if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		pack16(job_desc_ptr->contiguous, buffer);
		pack16(job_desc_ptr->core_spec, buffer);
		pack16(job_desc_ptr->task_dist, buffer);
		pack16(job_desc_ptr->kill_on_node_fail, buffer);
		packstr(job_desc_ptr->features, buffer);
		packstr(job_desc_ptr->gres, buffer);
		pack32(job_desc_ptr->job_id, buffer);
		packstr(job_desc_ptr->name, buffer);

		packstr(job_desc_ptr->alloc_node, buffer);
		pack32(job_desc_ptr->alloc_sid, buffer);
		packstr(job_desc_ptr->array_inx, buffer);
		pack16(job_desc_ptr->pn_min_cpus, buffer);
		pack32(job_desc_ptr->pn_min_memory, buffer);
		pack32(job_desc_ptr->pn_min_tmp_disk, buffer);

		packstr(job_desc_ptr->partition, buffer);
		pack32(job_desc_ptr->priority, buffer);
		packstr(job_desc_ptr->dependency, buffer);
		packstr(job_desc_ptr->account, buffer);
		packstr(job_desc_ptr->comment, buffer);
		pack16(job_desc_ptr->nice, buffer);
		pack32(job_desc_ptr->profile, buffer);
		packstr(job_desc_ptr->qos, buffer);

		pack8(job_desc_ptr->open_mode,   buffer);
		pack8(job_desc_ptr->overcommit,  buffer);
		packstr(job_desc_ptr->acctg_freq, buffer);
		pack32(job_desc_ptr->num_tasks,  buffer);
		pack16(job_desc_ptr->ckpt_interval, buffer);

		packstr(job_desc_ptr->req_nodes, buffer);
		packstr(job_desc_ptr->exc_nodes, buffer);
		packstr_array(job_desc_ptr->environment,
			      job_desc_ptr->env_size, buffer);
		packstr_array(job_desc_ptr->spank_job_env,
			      job_desc_ptr->spank_job_env_size, buffer);
		packstr(job_desc_ptr->script, buffer);
		packstr_array(job_desc_ptr->argv, job_desc_ptr->argc, buffer);

		packstr(job_desc_ptr->std_err, buffer);
		packstr(job_desc_ptr->std_in, buffer);
		packstr(job_desc_ptr->std_out, buffer);
		packstr(job_desc_ptr->work_dir, buffer);
		packstr(job_desc_ptr->ckpt_dir, buffer);

		pack16(job_desc_ptr->immediate, buffer);
		pack16(job_desc_ptr->requeue, buffer);
		pack16(job_desc_ptr->shared, buffer);
		pack16(job_desc_ptr->cpus_per_task, buffer);
		pack16(job_desc_ptr->ntasks_per_node, buffer);
		pack16(job_desc_ptr->ntasks_per_board, buffer);
		pack16(job_desc_ptr->ntasks_per_socket, buffer);
		pack16(job_desc_ptr->ntasks_per_core, buffer);

		pack16(job_desc_ptr->plane_size, buffer);
		pack16(job_desc_ptr->cpu_bind_type, buffer);
		pack16(job_desc_ptr->mem_bind_type, buffer);
		packstr(job_desc_ptr->cpu_bind, buffer);
		packstr(job_desc_ptr->mem_bind, buffer);

		pack32(job_desc_ptr->time_limit, buffer);
		pack32(job_desc_ptr->time_min, buffer);
		pack32(job_desc_ptr->min_cpus, buffer);
		pack32(job_desc_ptr->max_cpus, buffer);
		pack32(job_desc_ptr->min_nodes, buffer);
		pack32(job_desc_ptr->max_nodes, buffer);
		pack16(job_desc_ptr->boards_per_node, buffer);
		pack16(job_desc_ptr->sockets_per_board, buffer);
		pack16(job_desc_ptr->sockets_per_node, buffer);
		pack16(job_desc_ptr->cores_per_socket, buffer);
		pack16(job_desc_ptr->threads_per_core, buffer);
		pack32(job_desc_ptr->user_id, buffer);
		pack32(job_desc_ptr->group_id, buffer);

		pack16(job_desc_ptr->alloc_resp_port, buffer);
		pack16(job_desc_ptr->other_port, buffer);
		packstr(job_desc_ptr->network, buffer);
		pack_time(job_desc_ptr->begin_time, buffer);
		pack_time(job_desc_ptr->end_time, buffer);

		packstr(job_desc_ptr->licenses, buffer);
		pack16(job_desc_ptr->mail_type, buffer);
		packstr(job_desc_ptr->mail_user, buffer);
		packstr(job_desc_ptr->reservation, buffer);
		pack16(job_desc_ptr->warn_flags, buffer);
		pack16(job_desc_ptr->warn_signal, buffer);
		pack16(job_desc_ptr->warn_time, buffer);
		packstr(job_desc_ptr->wckey, buffer);
		pack32(job_desc_ptr->req_switch, buffer);
		pack32(job_desc_ptr->wait4switch, buffer);

		if (job_desc_ptr->select_jobinfo) {
			select_g_select_jobinfo_pack(
				job_desc_ptr->select_jobinfo,
				buffer, protocol_version);
		} else {
			job_desc_ptr->select_jobinfo =
				select_g_select_jobinfo_alloc();
			if (job_desc_ptr->geometry[0] != (uint16_t) NO_VAL)
				select_g_select_jobinfo_set(
					job_desc_ptr->select_jobinfo,
					SELECT_JOBDATA_GEOMETRY,
					job_desc_ptr->geometry);

			if (job_desc_ptr->conn_type[0] != (uint16_t) NO_VAL)
				select_g_select_jobinfo_set(
					job_desc_ptr->select_jobinfo,
					SELECT_JOBDATA_CONN_TYPE,
					&(job_desc_ptr->conn_type));
			if (job_desc_ptr->reboot != (uint16_t) NO_VAL)
				select_g_select_jobinfo_set(
					job_desc_ptr->select_jobinfo,
					SELECT_JOBDATA_REBOOT,
					&(job_desc_ptr->reboot));
			if (job_desc_ptr->rotate != (uint16_t) NO_VAL)
				select_g_select_jobinfo_set(
					job_desc_ptr->select_jobinfo,
					SELECT_JOBDATA_ROTATE,
					&(job_desc_ptr->rotate));
			if (job_desc_ptr->blrtsimage) {
				select_g_select_jobinfo_set(
					job_desc_ptr->select_jobinfo,
					SELECT_JOBDATA_BLRTS_IMAGE,
					job_desc_ptr->blrtsimage);
			}
			if (job_desc_ptr->linuximage)
				select_g_select_jobinfo_set(
					job_desc_ptr->select_jobinfo,
					SELECT_JOBDATA_LINUX_IMAGE,
					job_desc_ptr->linuximage);
			if (job_desc_ptr->mloaderimage)
				select_g_select_jobinfo_set(
					job_desc_ptr->select_jobinfo,
					SELECT_JOBDATA_MLOADER_IMAGE,
					job_desc_ptr->mloaderimage);
			if (job_desc_ptr->ramdiskimage)
				select_g_select_jobinfo_set(
					job_desc_ptr->select_jobinfo,
					SELECT_JOBDATA_RAMDISK_IMAGE,
					job_desc_ptr->ramdiskimage);
			select_g_select_jobinfo_pack(
				job_desc_ptr->select_jobinfo,
				buffer, protocol_version);
			select_g_select_jobinfo_free(
				job_desc_ptr->select_jobinfo);
			job_desc_ptr->select_jobinfo = NULL;
		}
		pack16(job_desc_ptr->wait_all_nodes, buffer);
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		pack16(job_desc_ptr->contiguous, buffer);
		pack16(job_desc_ptr->task_dist, buffer);
		pack16(job_desc_ptr->kill_on_node_fail, buffer);
		packstr(job_desc_ptr->features, buffer);
		packstr(job_desc_ptr->gres, buffer);
		pack32(job_desc_ptr->job_id, buffer);
		packstr(job_desc_ptr->name, buffer);

		packstr(job_desc_ptr->alloc_node, buffer);
		pack32(job_desc_ptr->alloc_sid, buffer);
		packstr(job_desc_ptr->array_inx, buffer);
		pack16(job_desc_ptr->pn_min_cpus, buffer);
		pack32(job_desc_ptr->pn_min_memory, buffer);
		pack32(job_desc_ptr->pn_min_tmp_disk, buffer);

		packstr(job_desc_ptr->partition, buffer);
		pack32(job_desc_ptr->priority, buffer);
		packstr(job_desc_ptr->dependency, buffer);
		packstr(job_desc_ptr->account, buffer);
		packstr(job_desc_ptr->comment, buffer);
		pack16(job_desc_ptr->nice, buffer);
		pack32(job_desc_ptr->profile, buffer);
		packstr(job_desc_ptr->qos, buffer);

		pack8(job_desc_ptr->open_mode,   buffer);
		pack8(job_desc_ptr->overcommit,  buffer);
		packstr(job_desc_ptr->acctg_freq, buffer);
		pack32(job_desc_ptr->num_tasks,  buffer);
		pack16(job_desc_ptr->ckpt_interval, buffer);

		packstr(job_desc_ptr->req_nodes, buffer);
		packstr(job_desc_ptr->exc_nodes, buffer);
		packstr_array(job_desc_ptr->environment,
			      job_desc_ptr->env_size, buffer);
		packstr_array(job_desc_ptr->spank_job_env,
			      job_desc_ptr->spank_job_env_size, buffer);
		packstr(job_desc_ptr->script, buffer);
		packstr_array(job_desc_ptr->argv, job_desc_ptr->argc, buffer);

		packstr(job_desc_ptr->std_err, buffer);
		packstr(job_desc_ptr->std_in, buffer);
		packstr(job_desc_ptr->std_out, buffer);
		packstr(job_desc_ptr->work_dir, buffer);
		packstr(job_desc_ptr->ckpt_dir, buffer);

		pack16(job_desc_ptr->immediate, buffer);
		pack16(job_desc_ptr->requeue, buffer);
		pack16(job_desc_ptr->shared, buffer);
		pack16(job_desc_ptr->cpus_per_task, buffer);
		pack16(job_desc_ptr->ntasks_per_node, buffer);
		pack16(job_desc_ptr->ntasks_per_board, buffer);
		pack16(job_desc_ptr->ntasks_per_socket, buffer);
		pack16(job_desc_ptr->ntasks_per_core, buffer);

		pack16(job_desc_ptr->plane_size, buffer);
		pack16(job_desc_ptr->cpu_bind_type, buffer);
		pack16(job_desc_ptr->mem_bind_type, buffer);
		packstr(job_desc_ptr->cpu_bind, buffer);
		packstr(job_desc_ptr->mem_bind, buffer);

		pack32(job_desc_ptr->time_limit, buffer);
		pack32(job_desc_ptr->time_min, buffer);
		pack32(job_desc_ptr->min_cpus, buffer);
		pack32(job_desc_ptr->max_cpus, buffer);
		pack32(job_desc_ptr->min_nodes, buffer);
		pack32(job_desc_ptr->max_nodes, buffer);
		pack16(job_desc_ptr->boards_per_node, buffer);
		pack16(job_desc_ptr->sockets_per_board, buffer);
		pack16(job_desc_ptr->sockets_per_node, buffer);
		pack16(job_desc_ptr->cores_per_socket, buffer);
		pack16(job_desc_ptr->threads_per_core, buffer);
		pack32(job_desc_ptr->user_id, buffer);
		pack32(job_desc_ptr->group_id, buffer);

		pack16(job_desc_ptr->alloc_resp_port, buffer);
		pack16(job_desc_ptr->other_port, buffer);
		packstr(job_desc_ptr->network, buffer);
		pack_time(job_desc_ptr->begin_time, buffer);
		pack_time(job_desc_ptr->end_time, buffer);

		packstr(job_desc_ptr->licenses, buffer);
		pack16(job_desc_ptr->mail_type, buffer);
		packstr(job_desc_ptr->mail_user, buffer);
		packstr(job_desc_ptr->reservation, buffer);
		pack16(job_desc_ptr->warn_signal, buffer);
		pack16(job_desc_ptr->warn_time, buffer);
		packstr(job_desc_ptr->wckey, buffer);
		pack32(job_desc_ptr->req_switch, buffer);
		pack32(job_desc_ptr->wait4switch, buffer);

		if (job_desc_ptr->select_jobinfo) {
			select_g_select_jobinfo_pack(
				job_desc_ptr->select_jobinfo,
				buffer, protocol_version);
		} else {
			job_desc_ptr->select_jobinfo =
				select_g_select_jobinfo_alloc();
			if (job_desc_ptr->geometry[0] != (uint16_t) NO_VAL)
				select_g_select_jobinfo_set(
					job_desc_ptr->select_jobinfo,
					SELECT_JOBDATA_GEOMETRY,
					job_desc_ptr->geometry);

			if (job_desc_ptr->conn_type[0] != (uint16_t) NO_VAL)
				select_g_select_jobinfo_set(
					job_desc_ptr->select_jobinfo,
					SELECT_JOBDATA_CONN_TYPE,
					&(job_desc_ptr->conn_type));
			if (job_desc_ptr->reboot != (uint16_t) NO_VAL)
				select_g_select_jobinfo_set(
					job_desc_ptr->select_jobinfo,
					SELECT_JOBDATA_REBOOT,
					&(job_desc_ptr->reboot));
			if (job_desc_ptr->rotate != (uint16_t) NO_VAL)
				select_g_select_jobinfo_set(
					job_desc_ptr->select_jobinfo,
					SELECT_JOBDATA_ROTATE,
					&(job_desc_ptr->rotate));
			if (job_desc_ptr->blrtsimage) {
				select_g_select_jobinfo_set(
					job_desc_ptr->select_jobinfo,
					SELECT_JOBDATA_BLRTS_IMAGE,
					job_desc_ptr->blrtsimage);
			}
			if (job_desc_ptr->linuximage)
				select_g_select_jobinfo_set(
					job_desc_ptr->select_jobinfo,
					SELECT_JOBDATA_LINUX_IMAGE,
					job_desc_ptr->linuximage);
			if (job_desc_ptr->mloaderimage)
				select_g_select_jobinfo_set(
					job_desc_ptr->select_jobinfo,
					SELECT_JOBDATA_MLOADER_IMAGE,
					job_desc_ptr->mloaderimage);
			if (job_desc_ptr->ramdiskimage)
				select_g_select_jobinfo_set(
					job_desc_ptr->select_jobinfo,
					SELECT_JOBDATA_RAMDISK_IMAGE,
					job_desc_ptr->ramdiskimage);
			select_g_select_jobinfo_pack(
				job_desc_ptr->select_jobinfo,
				buffer, protocol_version);
			select_g_select_jobinfo_free(
				job_desc_ptr->select_jobinfo);
			job_desc_ptr->select_jobinfo = NULL;
		}
		pack16(job_desc_ptr->wait_all_nodes, buffer);
	} else {
		error("_pack_job_desc_msg: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

/* _unpack_job_desc_msg
 * unpacks a job_desc struct
 * OUT job_desc_buffer_ptr - place to put pointer to allocated job desc struct
 * IN/OUT buffer - source of the unpack, contains pointers that are
 *			automatically updated
 */
static int
_unpack_job_desc_msg(job_desc_msg_t ** job_desc_buffer_ptr, Buf buffer,
		     uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	job_desc_msg_t *job_desc_ptr = NULL;

	/* alloc memory for structure */
	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		job_desc_ptr = xmalloc(sizeof(job_desc_msg_t));
		*job_desc_buffer_ptr = job_desc_ptr;

		/* load the data values */
		safe_unpack16(&job_desc_ptr->contiguous, buffer);
		safe_unpack16(&job_desc_ptr->core_spec, buffer);
		safe_unpack16(&job_desc_ptr->task_dist, buffer);
		safe_unpack16(&job_desc_ptr->kill_on_node_fail, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->features,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->gres, &uint32_tmp,buffer);
		safe_unpack32(&job_desc_ptr->job_id, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->job_id_str,
				       &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->name,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->alloc_node,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->alloc_sid, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->array_inx,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->pn_min_cpus, buffer);
		safe_unpack32(&job_desc_ptr->pn_min_memory, buffer);
		safe_unpack32(&job_desc_ptr->pn_min_tmp_disk, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->partition,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->priority, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->dependency,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->account,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->comment,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->nice, buffer);
		safe_unpack32(&job_desc_ptr->profile, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->qos, &uint32_tmp,
				       buffer);

		safe_unpack8(&job_desc_ptr->open_mode,   buffer);
		safe_unpack8(&job_desc_ptr->overcommit,  buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->acctg_freq,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->num_tasks,  buffer);
		safe_unpack16(&job_desc_ptr->ckpt_interval, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->req_nodes,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->exc_nodes,
				       &uint32_tmp, buffer);
		safe_unpackstr_array(&job_desc_ptr->environment,
				     &job_desc_ptr->env_size, buffer);
		safe_unpackstr_array(&job_desc_ptr->spank_job_env,
				     &job_desc_ptr->spank_job_env_size,
				     buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->script,
				       &uint32_tmp, buffer);
		safe_unpackstr_array(&job_desc_ptr->argv,
				     &job_desc_ptr->argc, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->std_err,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->std_in,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->std_out,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->work_dir,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->ckpt_dir,
				       &uint32_tmp, buffer);

		safe_unpack16(&job_desc_ptr->immediate, buffer);
		safe_unpack16(&job_desc_ptr->reboot, buffer);
		safe_unpack16(&job_desc_ptr->requeue, buffer);
		safe_unpack16(&job_desc_ptr->shared, buffer);
		safe_unpack16(&job_desc_ptr->cpus_per_task, buffer);
		safe_unpack16(&job_desc_ptr->ntasks_per_node, buffer);
		safe_unpack16(&job_desc_ptr->ntasks_per_board, buffer);
		safe_unpack16(&job_desc_ptr->ntasks_per_socket, buffer);
		safe_unpack16(&job_desc_ptr->ntasks_per_core, buffer);

		safe_unpack16(&job_desc_ptr->plane_size, buffer);
		safe_unpack16(&job_desc_ptr->cpu_bind_type, buffer);
		safe_unpack16(&job_desc_ptr->mem_bind_type, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->cpu_bind,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->mem_bind,
				       &uint32_tmp, buffer);

		safe_unpack32(&job_desc_ptr->time_limit, buffer);
		safe_unpack32(&job_desc_ptr->time_min, buffer);
		safe_unpack32(&job_desc_ptr->min_cpus, buffer);
		safe_unpack32(&job_desc_ptr->max_cpus, buffer);
		safe_unpack32(&job_desc_ptr->min_nodes, buffer);
		safe_unpack32(&job_desc_ptr->max_nodes, buffer);
		safe_unpack16(&job_desc_ptr->boards_per_node, buffer);
		safe_unpack16(&job_desc_ptr->sockets_per_board, buffer);
		safe_unpack16(&job_desc_ptr->sockets_per_node, buffer);
		safe_unpack16(&job_desc_ptr->cores_per_socket, buffer);
		safe_unpack16(&job_desc_ptr->threads_per_core, buffer);
		safe_unpack32(&job_desc_ptr->user_id, buffer);
		safe_unpack32(&job_desc_ptr->group_id, buffer);

		safe_unpack16(&job_desc_ptr->alloc_resp_port, buffer);
		safe_unpack16(&job_desc_ptr->other_port, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->network,
				       &uint32_tmp, buffer);
		safe_unpack_time(&job_desc_ptr->begin_time, buffer);
		safe_unpack_time(&job_desc_ptr->end_time, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->licenses,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->mail_type, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->mail_user,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->reservation,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->warn_flags, buffer);
		safe_unpack16(&job_desc_ptr->warn_signal, buffer);
		safe_unpack16(&job_desc_ptr->warn_time, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->wckey,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->req_switch, buffer);
		safe_unpack32(&job_desc_ptr->wait4switch, buffer);

		if (select_g_select_jobinfo_unpack(
			    &job_desc_ptr->select_jobinfo,
			    buffer, protocol_version))
			goto unpack_error;

		/* These are set so we don't confuse them later for what is
		 * set in the select_jobinfo structure.
		 */
		job_desc_ptr->geometry[0] = (uint16_t)NO_VAL;
		job_desc_ptr->conn_type[0] = (uint16_t)NO_VAL;
		job_desc_ptr->rotate = (uint16_t)NO_VAL;
		job_desc_ptr->blrtsimage = NULL;
		job_desc_ptr->linuximage = NULL;
		job_desc_ptr->mloaderimage = NULL;
		job_desc_ptr->ramdiskimage = NULL;
		safe_unpack16(&job_desc_ptr->wait_all_nodes, buffer);
	} else if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		job_desc_ptr = xmalloc(sizeof(job_desc_msg_t));
		*job_desc_buffer_ptr = job_desc_ptr;

		/* load the data values */
		safe_unpack16(&job_desc_ptr->contiguous, buffer);
		safe_unpack16(&job_desc_ptr->core_spec, buffer);
		safe_unpack16(&job_desc_ptr->task_dist, buffer);
		safe_unpack16(&job_desc_ptr->kill_on_node_fail, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->features,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->gres, &uint32_tmp,buffer);
		safe_unpack32(&job_desc_ptr->job_id, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->name,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->alloc_node,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->alloc_sid, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->array_inx,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->pn_min_cpus, buffer);
		safe_unpack32(&job_desc_ptr->pn_min_memory, buffer);
		safe_unpack32(&job_desc_ptr->pn_min_tmp_disk, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->partition,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->priority, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->dependency,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->account,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->comment,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->nice, buffer);
		safe_unpack32(&job_desc_ptr->profile, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->qos, &uint32_tmp,
				       buffer);

		safe_unpack8(&job_desc_ptr->open_mode,   buffer);
		safe_unpack8(&job_desc_ptr->overcommit,  buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->acctg_freq,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->num_tasks,  buffer);
		safe_unpack16(&job_desc_ptr->ckpt_interval, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->req_nodes,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->exc_nodes,
				       &uint32_tmp, buffer);
		safe_unpackstr_array(&job_desc_ptr->environment,
				     &job_desc_ptr->env_size, buffer);
		safe_unpackstr_array(&job_desc_ptr->spank_job_env,
				     &job_desc_ptr->spank_job_env_size,
				     buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->script,
				       &uint32_tmp, buffer);
		safe_unpackstr_array(&job_desc_ptr->argv,
				     &job_desc_ptr->argc, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->std_err,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->std_in,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->std_out,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->work_dir,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->ckpt_dir,
				       &uint32_tmp, buffer);

		safe_unpack16(&job_desc_ptr->immediate, buffer);
		safe_unpack16(&job_desc_ptr->requeue, buffer);
		safe_unpack16(&job_desc_ptr->shared, buffer);
		safe_unpack16(&job_desc_ptr->cpus_per_task, buffer);
		safe_unpack16(&job_desc_ptr->ntasks_per_node, buffer);
		safe_unpack16(&job_desc_ptr->ntasks_per_board, buffer);
		safe_unpack16(&job_desc_ptr->ntasks_per_socket, buffer);
		safe_unpack16(&job_desc_ptr->ntasks_per_core, buffer);

		safe_unpack16(&job_desc_ptr->plane_size, buffer);
		safe_unpack16(&job_desc_ptr->cpu_bind_type, buffer);
		safe_unpack16(&job_desc_ptr->mem_bind_type, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->cpu_bind,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->mem_bind,
				       &uint32_tmp, buffer);

		safe_unpack32(&job_desc_ptr->time_limit, buffer);
		safe_unpack32(&job_desc_ptr->time_min, buffer);
		safe_unpack32(&job_desc_ptr->min_cpus, buffer);
		safe_unpack32(&job_desc_ptr->max_cpus, buffer);
		safe_unpack32(&job_desc_ptr->min_nodes, buffer);
		safe_unpack32(&job_desc_ptr->max_nodes, buffer);
		safe_unpack16(&job_desc_ptr->boards_per_node, buffer);
		safe_unpack16(&job_desc_ptr->sockets_per_board, buffer);
		safe_unpack16(&job_desc_ptr->sockets_per_node, buffer);
		safe_unpack16(&job_desc_ptr->cores_per_socket, buffer);
		safe_unpack16(&job_desc_ptr->threads_per_core, buffer);
		safe_unpack32(&job_desc_ptr->user_id, buffer);
		safe_unpack32(&job_desc_ptr->group_id, buffer);

		safe_unpack16(&job_desc_ptr->alloc_resp_port, buffer);
		safe_unpack16(&job_desc_ptr->other_port, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->network,
				       &uint32_tmp, buffer);
		safe_unpack_time(&job_desc_ptr->begin_time, buffer);
		safe_unpack_time(&job_desc_ptr->end_time, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->licenses,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->mail_type, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->mail_user,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->reservation,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->warn_flags, buffer);
		safe_unpack16(&job_desc_ptr->warn_signal, buffer);
		safe_unpack16(&job_desc_ptr->warn_time, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->wckey,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->req_switch, buffer);
		safe_unpack32(&job_desc_ptr->wait4switch, buffer);

		if (select_g_select_jobinfo_unpack(
			    &job_desc_ptr->select_jobinfo,
			    buffer, protocol_version))
			goto unpack_error;

		/* These are set so we don't confuse them later for what is
		 * set in the select_jobinfo structure.
		 */
		job_desc_ptr->geometry[0] = (uint16_t)NO_VAL;
		job_desc_ptr->conn_type[0] = (uint16_t)NO_VAL;
		job_desc_ptr->reboot = (uint16_t)NO_VAL;
		job_desc_ptr->rotate = (uint16_t)NO_VAL;
		job_desc_ptr->blrtsimage = NULL;
		job_desc_ptr->linuximage = NULL;
		job_desc_ptr->mloaderimage = NULL;
		job_desc_ptr->ramdiskimage = NULL;
		safe_unpack16(&job_desc_ptr->wait_all_nodes, buffer);
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		job_desc_ptr = xmalloc(sizeof(job_desc_msg_t));
		*job_desc_buffer_ptr = job_desc_ptr;

		/* load the data values */
		safe_unpack16(&job_desc_ptr->contiguous, buffer);
		safe_unpack16(&job_desc_ptr->task_dist, buffer);
		safe_unpack16(&job_desc_ptr->kill_on_node_fail, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->features,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->gres, &uint32_tmp,buffer);
		safe_unpack32(&job_desc_ptr->job_id, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->name,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->alloc_node,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->alloc_sid, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->array_inx,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->pn_min_cpus, buffer);
		safe_unpack32(&job_desc_ptr->pn_min_memory, buffer);
		safe_unpack32(&job_desc_ptr->pn_min_tmp_disk, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->partition,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->priority, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->dependency,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->account,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->comment,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->nice, buffer);
		safe_unpack32(&job_desc_ptr->profile, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->qos, &uint32_tmp,
				       buffer);

		safe_unpack8(&job_desc_ptr->open_mode,   buffer);
		safe_unpack8(&job_desc_ptr->overcommit,  buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->acctg_freq,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->num_tasks,  buffer);
		safe_unpack16(&job_desc_ptr->ckpt_interval, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->req_nodes,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->exc_nodes,
				       &uint32_tmp, buffer);
		safe_unpackstr_array(&job_desc_ptr->environment,
				     &job_desc_ptr->env_size, buffer);
		safe_unpackstr_array(&job_desc_ptr->spank_job_env,
				     &job_desc_ptr->spank_job_env_size,
				     buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->script,
				       &uint32_tmp, buffer);
		safe_unpackstr_array(&job_desc_ptr->argv,
				     &job_desc_ptr->argc, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->std_err,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->std_in,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->std_out,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->work_dir,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->ckpt_dir,
				       &uint32_tmp, buffer);

		safe_unpack16(&job_desc_ptr->immediate, buffer);
		safe_unpack16(&job_desc_ptr->requeue, buffer);
		safe_unpack16(&job_desc_ptr->shared, buffer);
		safe_unpack16(&job_desc_ptr->cpus_per_task, buffer);
		safe_unpack16(&job_desc_ptr->ntasks_per_node, buffer);
		safe_unpack16(&job_desc_ptr->ntasks_per_board, buffer);
		safe_unpack16(&job_desc_ptr->ntasks_per_socket, buffer);
		safe_unpack16(&job_desc_ptr->ntasks_per_core, buffer);

		safe_unpack16(&job_desc_ptr->plane_size, buffer);
		safe_unpack16(&job_desc_ptr->cpu_bind_type, buffer);
		safe_unpack16(&job_desc_ptr->mem_bind_type, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->cpu_bind,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->mem_bind,
				       &uint32_tmp, buffer);

		safe_unpack32(&job_desc_ptr->time_limit, buffer);
		safe_unpack32(&job_desc_ptr->time_min, buffer);
		safe_unpack32(&job_desc_ptr->min_cpus, buffer);
		safe_unpack32(&job_desc_ptr->max_cpus, buffer);
		safe_unpack32(&job_desc_ptr->min_nodes, buffer);
		safe_unpack32(&job_desc_ptr->max_nodes, buffer);
		safe_unpack16(&job_desc_ptr->boards_per_node, buffer);
		safe_unpack16(&job_desc_ptr->sockets_per_board, buffer);
		safe_unpack16(&job_desc_ptr->sockets_per_node, buffer);
		safe_unpack16(&job_desc_ptr->cores_per_socket, buffer);
		safe_unpack16(&job_desc_ptr->threads_per_core, buffer);
		safe_unpack32(&job_desc_ptr->user_id, buffer);
		safe_unpack32(&job_desc_ptr->group_id, buffer);

		safe_unpack16(&job_desc_ptr->alloc_resp_port, buffer);
		safe_unpack16(&job_desc_ptr->other_port, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->network,
				       &uint32_tmp, buffer);
		safe_unpack_time(&job_desc_ptr->begin_time, buffer);
		safe_unpack_time(&job_desc_ptr->end_time, buffer);

		safe_unpackstr_xmalloc(&job_desc_ptr->licenses,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->mail_type, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->mail_user,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->reservation,
				       &uint32_tmp, buffer);
		safe_unpack16(&job_desc_ptr->warn_signal, buffer);
		safe_unpack16(&job_desc_ptr->warn_time, buffer);
		safe_unpackstr_xmalloc(&job_desc_ptr->wckey,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_desc_ptr->req_switch, buffer);
		safe_unpack32(&job_desc_ptr->wait4switch, buffer);

		if (select_g_select_jobinfo_unpack(
			    &job_desc_ptr->select_jobinfo,
			    buffer, protocol_version))
			goto unpack_error;

		/* These are set so we don't confuse them later for what is
		 * set in the select_jobinfo structure.
		 */
		job_desc_ptr->geometry[0] = (uint16_t)NO_VAL;
		job_desc_ptr->conn_type[0] = (uint16_t)NO_VAL;
		job_desc_ptr->reboot = (uint16_t)NO_VAL;
		job_desc_ptr->rotate = (uint16_t)NO_VAL;
		job_desc_ptr->blrtsimage = NULL;
		job_desc_ptr->linuximage = NULL;
		job_desc_ptr->mloaderimage = NULL;
		job_desc_ptr->ramdiskimage = NULL;
		safe_unpack16(&job_desc_ptr->wait_all_nodes, buffer);
	} else {
		error("_unpack_job_desc_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_desc_msg(job_desc_ptr);
	*job_desc_buffer_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_job_alloc_info_msg(job_alloc_info_msg_t * job_desc_ptr, Buf buffer,
			 uint16_t protocol_version)
{
	/* load the data values */
	pack32((uint32_t)job_desc_ptr->job_id, buffer);
}

static int
_unpack_job_alloc_info_msg(job_alloc_info_msg_t **
			   job_desc_buffer_ptr, Buf buffer,
			   uint16_t protocol_version)
{
	job_alloc_info_msg_t *job_desc_ptr;

	/* alloc memory for structure */
	assert(job_desc_buffer_ptr != NULL);
	job_desc_ptr = xmalloc(sizeof(job_alloc_info_msg_t));
	*job_desc_buffer_ptr = job_desc_ptr;

	/* load the data values */
	safe_unpack32(&job_desc_ptr->job_id, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_alloc_info_msg(job_desc_ptr);
	*job_desc_buffer_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_last_update_msg(last_update_msg_t * msg, Buf buffer,
		      uint16_t protocol_version)
{
	xassert(msg != NULL);
	pack_time(msg->last_update, buffer);
}

static int
_unpack_last_update_msg(last_update_msg_t ** msg, Buf buffer,
			uint16_t protocol_version)
{
	last_update_msg_t *last_update_msg;

	xassert(msg != NULL);
	last_update_msg = xmalloc(sizeof(last_update_msg_t));
	*msg = last_update_msg;

	safe_unpack_time(&last_update_msg->last_update, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_last_update_msg(last_update_msg);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_return_code_msg(return_code_msg_t * msg, Buf buffer,
		      uint16_t protocol_version)
{
	xassert(msg != NULL);
	pack32(msg->return_code, buffer);
}

static int
_unpack_return_code_msg(return_code_msg_t ** msg, Buf buffer,
			uint16_t protocol_version)
{
	return_code_msg_t *return_code_msg;

	xassert(msg != NULL);
	return_code_msg = xmalloc(sizeof(return_code_msg_t));
	*msg = return_code_msg;

	safe_unpack32(&return_code_msg->return_code, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_return_code_msg(return_code_msg);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_return_code2_msg(return_code2_msg_t * msg, Buf buffer,
		       uint16_t protocol_version)
{
	xassert(msg != NULL);
	pack32(msg->return_code, buffer);
	packstr(msg->err_msg,    buffer);
}

/* Log error message, otherwise replicate _unpack_return_code_msg() */
static int
_unpack_return_code2_msg(return_code_msg_t ** msg, Buf buffer,
			uint16_t protocol_version)
{
	return_code_msg_t *return_code_msg;
	uint32_t uint32_tmp = 0;
	char *err_msg = NULL;

	xassert(msg != NULL);
	return_code_msg = xmalloc(sizeof(return_code_msg_t));
	*msg = return_code_msg;

	safe_unpack32(&return_code_msg->return_code, buffer);
	safe_unpackstr_xmalloc(&err_msg, &uint32_tmp, buffer);
	if (err_msg) {
		error("%s", err_msg);
		xfree(err_msg);
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_return_code_msg(return_code_msg);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_reattach_tasks_request_msg(reattach_tasks_request_msg_t * msg,
				 Buf buffer,
				 uint16_t protocol_version)
{
	int i;

	xassert(msg != NULL);
	pack32((uint32_t)msg->job_id, buffer);
	pack32((uint32_t)msg->job_step_id, buffer);
	pack16((uint16_t)msg->num_resp_port, buffer);
	for (i = 0; i < msg->num_resp_port; i++)
		pack16((uint16_t)msg->resp_port[i], buffer);
	pack16((uint16_t)msg->num_io_port, buffer);
	for (i = 0; i < msg->num_io_port; i++)
		pack16((uint16_t)msg->io_port[i], buffer);

	slurm_cred_pack(msg->cred, buffer, protocol_version);
}

static int
_unpack_reattach_tasks_request_msg(reattach_tasks_request_msg_t ** msg_ptr,
				   Buf buffer,
				   uint16_t protocol_version)
{
	reattach_tasks_request_msg_t *msg;
	int i;

	xassert(msg_ptr != NULL);
	msg = xmalloc(sizeof(*msg));
	*msg_ptr = msg;

	safe_unpack32(&msg->job_id, buffer);
	safe_unpack32(&msg->job_step_id, buffer);
	safe_unpack16(&msg->num_resp_port, buffer);
	if (msg->num_resp_port > 0) {
		msg->resp_port = xmalloc(sizeof(uint16_t)*msg->num_resp_port);
		for (i = 0; i < msg->num_resp_port; i++)
			safe_unpack16(&msg->resp_port[i], buffer);
	}
	safe_unpack16(&msg->num_io_port, buffer);
	if (msg->num_io_port > 0) {
		msg->io_port = xmalloc(sizeof(uint16_t)*msg->num_io_port);
		for (i = 0; i < msg->num_io_port; i++)
			safe_unpack16(&msg->io_port[i], buffer);
	}

	if (!(msg->cred = slurm_cred_unpack(buffer, protocol_version)))
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_reattach_tasks_request_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_reattach_tasks_response_msg(reattach_tasks_response_msg_t * msg,
				  Buf buffer,
				  uint16_t protocol_version)
{
	int i;

	xassert(msg != NULL);
	packstr(msg->node_name,   buffer);
	pack32((uint32_t)msg->return_code,  buffer);
	pack32((uint32_t)msg->ntasks,       buffer);
	pack32_array(msg->gtids,      msg->ntasks, buffer);
	pack32_array(msg->local_pids, msg->ntasks, buffer);
	for (i = 0; i < msg->ntasks; i++) {
		packstr(msg->executable_names[i], buffer);
	}
}

static int
_unpack_reattach_tasks_response_msg(reattach_tasks_response_msg_t ** msg_ptr,
				    Buf buffer,
				    uint16_t protocol_version)
{
	uint32_t ntasks;
	uint32_t uint32_tmp;
	reattach_tasks_response_msg_t *msg = xmalloc(sizeof(*msg));
	int i;

	xassert(msg_ptr != NULL);
	*msg_ptr = msg;

	safe_unpackstr_xmalloc(&msg->node_name, &uint32_tmp, buffer);
	safe_unpack32(&msg->return_code,  buffer);
	safe_unpack32(&msg->ntasks,       buffer);
	safe_unpack32_array(&msg->gtids,      &ntasks, buffer);
	safe_unpack32_array(&msg->local_pids, &ntasks, buffer);
	if (msg->ntasks != ntasks)
		goto unpack_error;
	msg->executable_names = (char **)xmalloc(sizeof(char *) * msg->ntasks);
	for (i = 0; i < msg->ntasks; i++) {
		safe_unpackstr_xmalloc(&(msg->executable_names[i]), &uint32_tmp,
				       buffer);
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_reattach_tasks_response_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}


static void
_pack_task_exit_msg(task_exit_msg_t * msg, Buf buffer,
		    uint16_t protocol_version)
{
	xassert(msg != NULL);
	pack32(msg->return_code, buffer);
	pack32(msg->num_tasks, buffer);
	pack32_array(msg->task_id_list,
		     msg->num_tasks, buffer);
	pack32(msg->job_id, buffer);
	pack32(msg->step_id, buffer);
}

static int
_unpack_task_exit_msg(task_exit_msg_t ** msg_ptr, Buf buffer,
		      uint16_t protocol_version)
{
	task_exit_msg_t *msg;
	uint32_t uint32_tmp;

	xassert(msg_ptr != NULL);
	msg = xmalloc(sizeof(task_exit_msg_t));
	*msg_ptr = msg;

	safe_unpack32(&msg->return_code, buffer);
	safe_unpack32(&msg->num_tasks, buffer);
	safe_unpack32_array(&msg->task_id_list, &uint32_tmp, buffer);
	if (msg->num_tasks != uint32_tmp)
		goto unpack_error;
	safe_unpack32(&msg->job_id, buffer);
	safe_unpack32(&msg->step_id, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_task_exit_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}


static void
_pack_launch_tasks_response_msg(launch_tasks_response_msg_t * msg, Buf buffer,
				uint16_t protocol_version)
{
	xassert(msg != NULL);
	pack32((uint32_t)msg->return_code, buffer);
	packstr(msg->node_name, buffer);
	pack32((uint32_t)msg->count_of_pids, buffer);
	pack32_array(msg->local_pids, msg->count_of_pids, buffer);
	pack32_array(msg->task_ids, msg->count_of_pids, buffer);
}

static int
_unpack_launch_tasks_response_msg(launch_tasks_response_msg_t **
				  msg_ptr, Buf buffer,
				  uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	launch_tasks_response_msg_t *msg;

	xassert(msg_ptr != NULL);
	msg = xmalloc(sizeof(launch_tasks_response_msg_t));
	*msg_ptr = msg;

	safe_unpack32(&msg->return_code, buffer);
	safe_unpackstr_xmalloc(&msg->node_name, &uint32_tmp, buffer);
	safe_unpack32(&msg->count_of_pids, buffer);
	safe_unpack32_array(&msg->local_pids, &uint32_tmp, buffer);
	if (msg->count_of_pids != uint32_tmp)
		goto unpack_error;
	safe_unpack32_array(&msg->task_ids, &uint32_tmp, buffer);
	if (msg->count_of_pids != uint32_tmp)
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_launch_tasks_response_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_launch_tasks_request_msg(launch_tasks_request_msg_t * msg, Buf buffer,
			       uint16_t protocol_version)
{
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();
	int i = 0;

	xassert(msg != NULL);

	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		pack32(msg->job_id, buffer);
		pack32(msg->job_step_id, buffer);
		pack32(msg->ntasks, buffer);
		pack32(msg->uid, buffer);
		packstr(msg->partition, buffer);
		packstr(msg->user_name, buffer);
		pack32(msg->gid, buffer);
		pack32(msg->job_mem_lim, buffer);
		pack32(msg->step_mem_lim, buffer);

		pack32(msg->nnodes, buffer);
		pack16(msg->cpus_per_task, buffer);
		pack16(msg->task_dist, buffer);
		pack16(msg->node_cpus, buffer);
		pack16(msg->job_core_spec, buffer);

		slurm_cred_pack(msg->cred, buffer, protocol_version);
		for (i = 0; i < msg->nnodes; i++) {
			pack16(msg->tasks_to_launch[i], buffer);
			pack32_array(msg->global_task_ids[i],
				     (uint32_t) msg->tasks_to_launch[i],
				     buffer);
		}
		pack16(msg->num_resp_port, buffer);
		for (i = 0; i < msg->num_resp_port; i++)
			pack16(msg->resp_port[i], buffer);
		slurm_pack_slurm_addr(&msg->orig_addr, buffer);
		packstr_array(msg->env, msg->envc, buffer);
		packstr_array(msg->spank_job_env, msg->spank_job_env_size,
			      buffer);
		packstr(msg->cwd, buffer);
		pack16(msg->cpu_bind_type, buffer);
		packstr(msg->cpu_bind, buffer);
		pack16(msg->mem_bind_type, buffer);
		packstr(msg->mem_bind, buffer);
		packstr_array(msg->argv, msg->argc, buffer);
		pack16(msg->task_flags, buffer);
		pack16(msg->multi_prog, buffer);
		pack16(msg->user_managed_io, buffer);
		if (msg->user_managed_io == 0) {
			packstr(msg->ofname, buffer);
			packstr(msg->efname, buffer);
			packstr(msg->ifname, buffer);
			pack8(msg->buffered_stdio, buffer);
			pack8(msg->labelio, buffer);
			pack16(msg->num_io_port, buffer);
			for (i = 0; i < msg->num_io_port; i++)
				pack16(msg->io_port[i], buffer);
		}
		pack32(msg->profile, buffer);
		packstr(msg->task_prolog, buffer);
		packstr(msg->task_epilog, buffer);
		pack16(msg->slurmd_debug, buffer);
		switch_g_pack_jobinfo(msg->switch_job, buffer,
				      protocol_version);
		job_options_pack(msg->options, buffer);
		packstr(msg->alias_list, buffer);
		packstr(msg->complete_nodelist, buffer);

		pack8(msg->open_mode, buffer);
		pack8(msg->pty, buffer);
		packstr(msg->acctg_freq, buffer);
		pack32(msg->cpu_freq, buffer);
		packstr(msg->ckpt_dir, buffer);
		packstr(msg->restart_dir, buffer);
		if (!(cluster_flags & CLUSTER_FLAG_BG)) {
			/* If on a Blue Gene cluster do not send this to the
			 * slurmstepd, it will overwrite the environment that
			 * ia already set up correctly for both the job and the
			 * step. The slurmstep treats this select_jobinfo as if
			 * were for the job  instead of for the step.
			 */
			select_g_select_jobinfo_pack(msg->select_jobinfo,
						     buffer,
						     protocol_version);
		}
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		pack32(msg->job_id, buffer);
		pack32(msg->job_step_id, buffer);
		pack32(msg->ntasks, buffer);
		pack32(msg->uid, buffer);
		pack32(msg->gid, buffer);
		pack32(msg->job_mem_lim, buffer);
		pack32(msg->step_mem_lim, buffer);

		pack32(msg->nnodes, buffer);
		pack16(msg->cpus_per_task, buffer);
		pack16(msg->task_dist, buffer);

		slurm_cred_pack(msg->cred, buffer, protocol_version);
		for (i = 0; i < msg->nnodes; i++) {
			pack16(msg->tasks_to_launch[i], buffer);
			pack16(0, buffer);
			pack32_array(msg->global_task_ids[i],
				     (uint32_t) msg->tasks_to_launch[i],
				     buffer);
		}
		pack16(msg->num_resp_port, buffer);
		for (i = 0; i < msg->num_resp_port; i++)
			pack16(msg->resp_port[i], buffer);
		slurm_pack_slurm_addr(&msg->orig_addr, buffer);
		packstr_array(msg->env, msg->envc, buffer);
		packstr_array(msg->spank_job_env, msg->spank_job_env_size,
			      buffer);
		packstr(msg->cwd, buffer);
		pack16(msg->cpu_bind_type, buffer);
		packstr(msg->cpu_bind, buffer);
		pack16(msg->mem_bind_type, buffer);
		packstr(msg->mem_bind, buffer);
		packstr_array(msg->argv, msg->argc, buffer);
		pack16(msg->task_flags, buffer);
		pack16(msg->multi_prog, buffer);
		pack16(msg->user_managed_io, buffer);
		if (msg->user_managed_io == 0) {
			packstr(msg->ofname, buffer);
			packstr(msg->efname, buffer);
			packstr(msg->ifname, buffer);
			pack8(msg->buffered_stdio, buffer);
			pack8(msg->labelio, buffer);
			pack16(msg->num_io_port, buffer);
			for (i = 0; i < msg->num_io_port; i++)
				pack16(msg->io_port[i], buffer);
		}
		pack32(msg->profile, buffer);
		packstr(msg->task_prolog, buffer);
		packstr(msg->task_epilog, buffer);
		pack16(msg->slurmd_debug, buffer);
		switch_g_pack_jobinfo(msg->switch_job, buffer,
				      protocol_version);
		job_options_pack(msg->options, buffer);
		packstr(msg->alias_list, buffer);
		packstr(msg->complete_nodelist, buffer);

		pack8(msg->open_mode, buffer);
		pack8(msg->pty, buffer);
		packstr(msg->acctg_freq, buffer);
		pack32(msg->cpu_freq, buffer);
		packstr(msg->ckpt_dir, buffer);
		packstr(msg->restart_dir, buffer);
		if (!(cluster_flags & CLUSTER_FLAG_BG)) {
			/* If on a Blue Gene cluster do not send this to the
			 * slurmstepd, it will overwrite the environment that
			 * ia already set up correctly for both the job and the
			 * step. The slurmstep treats this select_jobinfo as if
			 * were for the job  instead of for the step.
			 */
			select_g_select_jobinfo_pack(msg->select_jobinfo,
						     buffer,
						     protocol_version);
		}
	} else {
		error("_pack_launch_tasks_request_msg: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

static int
_unpack_launch_tasks_request_msg(launch_tasks_request_msg_t **
				 msg_ptr, Buf buffer,
				 uint16_t protocol_version)
{
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();
	uint32_t uint32_tmp;
	uint16_t uint16 = 0;
	launch_tasks_request_msg_t *msg;
	int i = 0;

	xassert(msg_ptr != NULL);
	msg = xmalloc(sizeof(launch_tasks_request_msg_t));
	*msg_ptr = msg;

	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		safe_unpack32(&msg->job_id, buffer);
		safe_unpack32(&msg->job_step_id, buffer);
		safe_unpack32(&msg->ntasks, buffer);
		safe_unpack32(&msg->uid, buffer);
		safe_unpackstr_xmalloc(&msg->partition, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->user_name, &uint32_tmp, buffer);
		safe_unpack32(&msg->gid, buffer);
		safe_unpack32(&msg->job_mem_lim, buffer);
		safe_unpack32(&msg->step_mem_lim, buffer);

		safe_unpack32(&msg->nnodes, buffer);
		safe_unpack16(&msg->cpus_per_task, buffer);
		safe_unpack16(&msg->task_dist, buffer);
		safe_unpack16(&msg->node_cpus, buffer);
		safe_unpack16(&msg->job_core_spec, buffer);

		if (!(msg->cred = slurm_cred_unpack(buffer, protocol_version)))
			goto unpack_error;
		msg->tasks_to_launch = xmalloc(sizeof(uint16_t) * msg->nnodes);
		msg->global_task_ids = xmalloc(sizeof(uint32_t *) *
					       msg->nnodes);
		for (i = 0; i < msg->nnodes; i++) {
			safe_unpack16(&msg->tasks_to_launch[i], buffer);
			safe_unpack32_array(&msg->global_task_ids[i],
					    &uint32_tmp,
					    buffer);
			if (msg->tasks_to_launch[i] != (uint16_t) uint32_tmp)
				goto unpack_error;
		}
		safe_unpack16(&msg->num_resp_port, buffer);
		if (msg->num_resp_port > 0) {
			msg->resp_port = xmalloc(sizeof(uint16_t) *
						 msg->num_resp_port);
			for (i = 0; i < msg->num_resp_port; i++)
				safe_unpack16(&msg->resp_port[i], buffer);
		}
		slurm_unpack_slurm_addr_no_alloc(&msg->orig_addr, buffer);
		safe_unpackstr_array(&msg->env, &msg->envc, buffer);
		safe_unpackstr_array(&msg->spank_job_env,
				     &msg->spank_job_env_size, buffer);
		safe_unpackstr_xmalloc(&msg->cwd, &uint32_tmp, buffer);
		safe_unpack16(&msg->cpu_bind_type, buffer);
		safe_unpackstr_xmalloc(&msg->cpu_bind, &uint32_tmp, buffer);
		safe_unpack16(&msg->mem_bind_type, buffer);
		safe_unpackstr_xmalloc(&msg->mem_bind, &uint32_tmp, buffer);
		safe_unpackstr_array(&msg->argv, &msg->argc, buffer);
		safe_unpack16(&msg->task_flags, buffer);
		safe_unpack16(&msg->multi_prog, buffer);
		safe_unpack16(&msg->user_managed_io, buffer);
		if (msg->user_managed_io == 0) {
			safe_unpackstr_xmalloc(&msg->ofname, &uint32_tmp,
					       buffer);
			safe_unpackstr_xmalloc(&msg->efname, &uint32_tmp,
					       buffer);
			safe_unpackstr_xmalloc(&msg->ifname, &uint32_tmp,
					       buffer);
			safe_unpack8(&msg->buffered_stdio, buffer);
			safe_unpack8(&msg->labelio, buffer);
			safe_unpack16(&msg->num_io_port, buffer);
			if (msg->num_io_port > 0) {
				msg->io_port = xmalloc(sizeof(uint16_t) *
						       msg->num_io_port);
				for (i = 0; i < msg->num_io_port; i++)
					safe_unpack16(&msg->io_port[i],
						      buffer);
			}
		}
		safe_unpack32(&msg->profile, buffer);
		safe_unpackstr_xmalloc(&msg->task_prolog, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->task_epilog, &uint32_tmp, buffer);
		safe_unpack16(&msg->slurmd_debug, buffer);

		switch_g_alloc_jobinfo(&msg->switch_job,
				       msg->job_id, msg->job_step_id);
		if (switch_g_unpack_jobinfo(msg->switch_job, buffer,
					    protocol_version) < 0) {
			error("switch_g_unpack_jobinfo: %m");
			switch_g_free_jobinfo(msg->switch_job);
			goto unpack_error;
		}
		msg->options = job_options_create();
		if (job_options_unpack(msg->options, buffer) < 0) {
			error("Unable to unpack extra job options: %m");
			goto unpack_error;
		}
		safe_unpackstr_xmalloc(&msg->alias_list, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->complete_nodelist, &uint32_tmp,
				       buffer);

		safe_unpack8(&msg->open_mode, buffer);
		safe_unpack8(&msg->pty, buffer);
		safe_unpackstr_xmalloc(&msg->acctg_freq, &uint32_tmp, buffer);
		safe_unpack32(&msg->cpu_freq, buffer);
		safe_unpackstr_xmalloc(&msg->ckpt_dir, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->restart_dir, &uint32_tmp, buffer);
		if (!(cluster_flags & CLUSTER_FLAG_BG)) {
			select_g_select_jobinfo_unpack(&msg->select_jobinfo,
						       buffer,
						       protocol_version);
		}
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		safe_unpack32(&msg->job_id, buffer);
		safe_unpack32(&msg->job_step_id, buffer);
		safe_unpack32(&msg->ntasks, buffer);
		safe_unpack32(&msg->uid, buffer);
		safe_unpack32(&msg->gid, buffer);
		safe_unpack32(&msg->job_mem_lim, buffer);
		safe_unpack32(&msg->step_mem_lim, buffer);

		safe_unpack32(&msg->nnodes, buffer);
		safe_unpack16(&msg->cpus_per_task, buffer);
		safe_unpack16(&msg->task_dist, buffer);

		if (!(msg->cred = slurm_cred_unpack(buffer, protocol_version)))
			goto unpack_error;
		msg->tasks_to_launch = xmalloc(sizeof(uint16_t) * msg->nnodes);
		msg->global_task_ids = xmalloc(sizeof(uint32_t *) *
					       msg->nnodes);
		for (i = 0; i < msg->nnodes; i++) {
			safe_unpack16(&msg->tasks_to_launch[i], buffer);
			safe_unpack16(&uint16, buffer); /* not needed */
			safe_unpack32_array(&msg->global_task_ids[i],
					    &uint32_tmp,
					    buffer);
			if (msg->tasks_to_launch[i] != (uint16_t) uint32_tmp)
				goto unpack_error;
		}
		safe_unpack16(&msg->num_resp_port, buffer);
		if (msg->num_resp_port > 0) {
			msg->resp_port = xmalloc(sizeof(uint16_t) *
						 msg->num_resp_port);
			for (i = 0; i < msg->num_resp_port; i++)
				safe_unpack16(&msg->resp_port[i], buffer);
		}
		slurm_unpack_slurm_addr_no_alloc(&msg->orig_addr, buffer);
		safe_unpackstr_array(&msg->env, &msg->envc, buffer);
		safe_unpackstr_array(&msg->spank_job_env,
				     &msg->spank_job_env_size, buffer);
		safe_unpackstr_xmalloc(&msg->cwd, &uint32_tmp, buffer);
		safe_unpack16(&msg->cpu_bind_type, buffer);
		safe_unpackstr_xmalloc(&msg->cpu_bind, &uint32_tmp, buffer);
		safe_unpack16(&msg->mem_bind_type, buffer);
		safe_unpackstr_xmalloc(&msg->mem_bind, &uint32_tmp, buffer);
		safe_unpackstr_array(&msg->argv, &msg->argc, buffer);
		safe_unpack16(&msg->task_flags, buffer);
		safe_unpack16(&msg->multi_prog, buffer);
		safe_unpack16(&msg->user_managed_io, buffer);
		if (msg->user_managed_io == 0) {
			safe_unpackstr_xmalloc(&msg->ofname, &uint32_tmp,
					       buffer);
			safe_unpackstr_xmalloc(&msg->efname, &uint32_tmp,
					       buffer);
			safe_unpackstr_xmalloc(&msg->ifname, &uint32_tmp,
					       buffer);
			safe_unpack8(&msg->buffered_stdio, buffer);
			safe_unpack8(&msg->labelio, buffer);
			safe_unpack16(&msg->num_io_port, buffer);
			if (msg->num_io_port > 0) {
				msg->io_port = xmalloc(sizeof(uint16_t) *
						       msg->num_io_port);
				for (i = 0; i < msg->num_io_port; i++)
					safe_unpack16(&msg->io_port[i],
						      buffer);
			}
		}
		safe_unpack32(&msg->profile, buffer);
		safe_unpackstr_xmalloc(&msg->task_prolog, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->task_epilog, &uint32_tmp, buffer);
		safe_unpack16(&msg->slurmd_debug, buffer);

		switch_g_alloc_jobinfo(&msg->switch_job,
				       msg->job_id, msg->job_step_id);
		if (switch_g_unpack_jobinfo(msg->switch_job, buffer,
					    protocol_version) < 0) {
			error("switch_g_unpack_jobinfo: %m");
			switch_g_free_jobinfo(msg->switch_job);
			goto unpack_error;
		}
		msg->options = job_options_create();
		if (job_options_unpack(msg->options, buffer) < 0) {
			error("Unable to unpack extra job options: %m");
			goto unpack_error;
		}
		safe_unpackstr_xmalloc(&msg->alias_list, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->complete_nodelist, &uint32_tmp,
				       buffer);

		safe_unpack8(&msg->open_mode, buffer);
		safe_unpack8(&msg->pty, buffer);
		safe_unpackstr_xmalloc(&msg->acctg_freq, &uint32_tmp, buffer);
		safe_unpack32(&msg->cpu_freq, buffer);
		safe_unpackstr_xmalloc(&msg->ckpt_dir, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->restart_dir, &uint32_tmp, buffer);
		if (!(cluster_flags & CLUSTER_FLAG_BG)) {
			select_g_select_jobinfo_unpack(&msg->select_jobinfo,
						       buffer,
						       protocol_version);
		}
	} else {
		error("_unpack_launch_tasks_request_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_launch_tasks_request_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_task_user_managed_io_stream_msg(task_user_managed_io_msg_t * msg,
				      Buf buffer,
				      uint16_t protocol_version)
{
	xassert(msg != NULL);
	pack32(msg->task_id, buffer);
}

static int
_unpack_task_user_managed_io_stream_msg(task_user_managed_io_msg_t **msg_ptr,
					Buf buffer,
					uint16_t protocol_version)
{
	task_user_managed_io_msg_t *msg;

	xassert(msg_ptr != NULL);
	msg = xmalloc(sizeof(task_user_managed_io_msg_t));
	*msg_ptr = msg;

	safe_unpack32(&msg->task_id, buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_task_user_managed_io_stream_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_cancel_tasks_msg(kill_tasks_msg_t * msg, Buf buffer,
		       uint16_t protocol_version)
{
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32((uint32_t)msg->job_id, buffer);
		pack32((uint32_t)msg->job_step_id, buffer);
		pack32((uint32_t)msg->signal, buffer);
	} else {
		error("_pack_cancel_tasks_msg: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

static int
_unpack_cancel_tasks_msg(kill_tasks_msg_t ** msg_ptr, Buf buffer,
			 uint16_t protocol_version)
{
	kill_tasks_msg_t *msg;

	msg = xmalloc(sizeof(kill_tasks_msg_t));
	*msg_ptr = msg;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->job_id, buffer);
		safe_unpack32(&msg->job_step_id, buffer);
		safe_unpack32(&msg->signal, buffer);
	} else {
		error("_unpack_cancel_tasks_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_kill_tasks_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_checkpoint_tasks_msg(checkpoint_tasks_msg_t * msg, Buf buffer,
			   uint16_t protocol_version)
{
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->job_id, buffer);
		pack32(msg->job_step_id, buffer);
		pack_time(msg->timestamp, buffer);
		packstr(msg->image_dir, buffer);
	} else {
		error("_pack_checkpoint_tasks_msg: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

static int
_unpack_checkpoint_tasks_msg(checkpoint_tasks_msg_t ** msg_ptr, Buf buffer,
			     uint16_t protocol_version)
{
	checkpoint_tasks_msg_t *msg;
	uint32_t uint32_tmp;

	msg = xmalloc(sizeof(checkpoint_tasks_msg_t));
	*msg_ptr = msg;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->job_id, buffer);
		safe_unpack32(&msg->job_step_id, buffer);
		safe_unpack_time(&msg->timestamp, buffer);
		safe_unpackstr_xmalloc(&msg->image_dir, &uint32_tmp, buffer);
	} else {
		error("_unpack_checkpoint_tasks_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_checkpoint_tasks_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_reboot_msg(reboot_msg_t * msg, Buf buffer,
		 uint16_t protocol_version)
{
	if (msg && msg->node_list)
		packstr(msg->node_list, buffer);
	else
		packnull(buffer);
}

static int
_unpack_reboot_msg(reboot_msg_t ** msg_ptr, Buf buffer,
		   uint16_t protocol_version)
{
	reboot_msg_t *msg;
	uint32_t uint32_tmp;

	msg = xmalloc(sizeof(reboot_msg_t));
	*msg_ptr = msg;

	safe_unpackstr_xmalloc(&msg->node_list, &uint32_tmp, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_reboot_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_shutdown_msg(shutdown_msg_t * msg, Buf buffer,
		   uint16_t protocol_version)
{
	pack16((uint16_t)msg->options, buffer);
}

static int
_unpack_shutdown_msg(shutdown_msg_t ** msg_ptr, Buf buffer,
		     uint16_t protocol_version)
{
	shutdown_msg_t *msg;

	msg = xmalloc(sizeof(shutdown_msg_t));
	*msg_ptr = msg;

	safe_unpack16(&msg->options, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_shutdown_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

/* _pack_job_step_kill_msg
 * packs a slurm job step signal message
 * IN msg - pointer to the job step signal message
 * IN/OUT buffer - destination of the pack, contains pointers that are
 *			automatically updated
 */
static void
_pack_job_step_kill_msg(job_step_kill_msg_t * msg, Buf buffer,
			uint16_t protocol_version)
{
	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		packstr(msg->sjob_id, buffer);
		pack32((uint32_t)msg->job_id, buffer);
		pack32((uint32_t)msg->job_step_id, buffer);
		pack16((uint16_t)msg->signal, buffer);
		pack16((uint16_t)msg->flags, buffer);
	} else {
		pack32((uint32_t)msg->job_id, buffer);
		pack32((uint32_t)msg->job_step_id, buffer);
		pack16((uint16_t)msg->signal, buffer);
		pack16((uint16_t)msg->flags, buffer);
	}
}

/* _unpack_job_step_kill_msg
 * unpacks a slurm job step signal message
 * OUT msg_ptr - pointer to the job step signal message buffer
 * IN/OUT buffer - source of the unpack, contains pointers that are
 *			automatically updated
 */
static int
_unpack_job_step_kill_msg(job_step_kill_msg_t ** msg_ptr, Buf buffer,
			  uint16_t protocol_version)
{
	job_step_kill_msg_t *msg;
	uint32_t cc;

	msg = xmalloc(sizeof(job_step_kill_msg_t));
	*msg_ptr = msg;

	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&(msg)->sjob_id, &cc, buffer);
		safe_unpack32(&msg->job_id, buffer);
		safe_unpack32(&msg->job_step_id, buffer);
		safe_unpack16(&msg->signal, buffer);
		safe_unpack16(&msg->flags, buffer);
	} else {
		safe_unpack32(&msg->job_id, buffer);
		safe_unpack32(&msg->job_step_id, buffer);
		safe_unpack16(&msg->signal, buffer);
		safe_unpack16(&msg->flags, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_step_kill_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_update_job_step_msg(step_update_request_msg_t * msg, Buf buffer,
			  uint16_t protocol_version)
{
	uint8_t with_jobacct = 0;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_time(msg->end_time, buffer);
		pack32(msg->exit_code, buffer);
		pack32(msg->job_id, buffer);
		if (msg->jobacct)
			with_jobacct = 1;
		pack8(with_jobacct, buffer);
		if (with_jobacct)
			jobacctinfo_pack(msg->jobacct, protocol_version,
					 PROTOCOL_TYPE_SLURM, buffer);
		packstr(msg->name, buffer);
		pack_time(msg->start_time, buffer);
		pack32(msg->step_id, buffer);
		pack32(msg->time_limit, buffer);
	} else {
		pack32(msg->job_id, buffer);
		pack32(msg->step_id, buffer);
		pack32(msg->time_limit, buffer);
	}
}

static int
_unpack_update_job_step_msg(step_update_request_msg_t ** msg_ptr, Buf buffer,
			    uint16_t protocol_version)
{
	step_update_request_msg_t *msg;
	uint8_t with_jobacct = 0;
	uint32_t uint32_tmp;

	msg = xmalloc(sizeof(step_update_request_msg_t));
	*msg_ptr = msg;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		unpack_time(&msg->end_time, buffer);
		safe_unpack32(&msg->exit_code, buffer);
		safe_unpack32(&msg->job_id, buffer);
		safe_unpack8(&with_jobacct, buffer);
		if (with_jobacct)
			if (jobacctinfo_unpack(&msg->jobacct, protocol_version,
					       PROTOCOL_TYPE_SLURM, buffer, 1)
			    != SLURM_SUCCESS)
				goto unpack_error;
		safe_unpackstr_xmalloc(&msg->name, &uint32_tmp, buffer);
		unpack_time(&msg->start_time, buffer);
		safe_unpack32(&msg->step_id, buffer);
		safe_unpack32(&msg->time_limit, buffer);
	} else {
		safe_unpack32(&msg->job_id, buffer);
		safe_unpack32(&msg->step_id, buffer);
		safe_unpack32(&msg->time_limit, buffer);
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_update_step_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_complete_job_allocation_msg(
	complete_job_allocation_msg_t * msg, Buf buffer,
	uint16_t protocol_version)
{
	pack32((uint32_t)msg->job_id, buffer);
	pack32((uint32_t)msg->job_rc, buffer);
}

static int
_unpack_complete_job_allocation_msg(
	complete_job_allocation_msg_t ** msg_ptr, Buf buffer,
	uint16_t protocol_version)
{
	complete_job_allocation_msg_t *msg;

	msg = xmalloc(sizeof(complete_job_allocation_msg_t));
	*msg_ptr = msg;

	safe_unpack32(&msg->job_id, buffer);
	safe_unpack32(&msg->job_rc, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_complete_job_allocation_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_complete_prolog_msg(
	complete_prolog_msg_t * msg, Buf buffer,
	uint16_t protocol_version)
{
	pack32((uint32_t)msg->job_id, buffer);
	pack32((uint32_t)msg->prolog_rc, buffer);
}

static int
_unpack_complete_prolog_msg(
	complete_prolog_msg_t ** msg_ptr, Buf buffer,
	uint16_t protocol_version)
{
	complete_prolog_msg_t *msg;

	msg = xmalloc(sizeof(complete_prolog_msg_t));
	*msg_ptr = msg;

	safe_unpack32(&msg->job_id, buffer);
	safe_unpack32(&msg->prolog_rc, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_complete_prolog_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_prolog_launch_msg(
	prolog_launch_msg_t * msg, Buf buffer,
	uint16_t protocol_version)
{
	xassert(msg != NULL);

	pack32(msg->job_id, buffer);
	pack32(msg->uid, buffer);
	pack32(msg->gid, buffer);

	packstr(msg->alias_list, buffer);
	packstr(msg->nodes, buffer);
	packstr(msg->partition, buffer);
	packstr(msg->std_err, buffer);
	packstr(msg->std_out, buffer);
	packstr(msg->work_dir, buffer);
	packstr_array(msg->spank_job_env, msg->spank_job_env_size, buffer);
}

static int
_unpack_prolog_launch_msg(
	prolog_launch_msg_t ** msg, Buf buffer,
	uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	prolog_launch_msg_t *launch_msg_ptr;

	xassert(msg != NULL);
	launch_msg_ptr = xmalloc(sizeof(prolog_launch_msg_t));
	*msg = launch_msg_ptr;

	safe_unpack32(&launch_msg_ptr->job_id, buffer);
	safe_unpack32(&launch_msg_ptr->uid, buffer);
	safe_unpack32(&launch_msg_ptr->gid, buffer);

	safe_unpackstr_xmalloc(&launch_msg_ptr->alias_list, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&launch_msg_ptr->nodes, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&launch_msg_ptr->partition, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&launch_msg_ptr->std_err, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&launch_msg_ptr->std_out, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&launch_msg_ptr->work_dir, &uint32_tmp, buffer);

	safe_unpackstr_array(&launch_msg_ptr->spank_job_env,
			     &launch_msg_ptr->spank_job_env_size,
			     buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_prolog_launch_msg(launch_msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_complete_batch_script_msg(
	complete_batch_script_msg_t * msg, Buf buffer,
	uint16_t protocol_version)
{
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		jobacctinfo_pack(msg->jobacct, protocol_version,
				 PROTOCOL_TYPE_SLURM, buffer);
		pack32(msg->job_id, buffer);
		pack32(msg->job_rc, buffer);
		pack32(msg->slurm_rc, buffer);
		pack32(msg->user_id, buffer);
		packstr(msg->node_name, buffer);
	} else {
		error("_pack_complete_batch_script_msg: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

static int
_unpack_complete_batch_script_msg(
	complete_batch_script_msg_t ** msg_ptr, Buf buffer,
	uint16_t protocol_version)
{
	complete_batch_script_msg_t *msg;
	uint32_t uint32_tmp;

	msg = xmalloc(sizeof(complete_batch_script_msg_t));
	*msg_ptr = msg;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (jobacctinfo_unpack(&msg->jobacct, protocol_version,
				       PROTOCOL_TYPE_SLURM, buffer, 1)
		    != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&msg->job_id, buffer);
		safe_unpack32(&msg->job_rc, buffer);
		safe_unpack32(&msg->slurm_rc, buffer);
		safe_unpack32(&msg->user_id, buffer);
		safe_unpackstr_xmalloc(&msg->node_name, &uint32_tmp, buffer);
	} else {
		error("_unpack_complete_batch_script_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_complete_batch_script_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_job_step_stat(job_step_stat_t * msg, Buf buffer,
		    uint16_t protocol_version)
{
	pack32((uint32_t)msg->return_code, buffer);
	pack32((uint32_t)msg->num_tasks, buffer);
	jobacctinfo_pack(msg->jobacct, protocol_version,
			 PROTOCOL_TYPE_SLURM, buffer);
	_pack_job_step_pids(msg->step_pids, buffer, protocol_version);
}


static int
_unpack_job_step_stat(job_step_stat_t ** msg_ptr, Buf buffer,
		      uint16_t protocol_version)
{
	job_step_stat_t *msg;
	int rc = SLURM_SUCCESS;

	msg = xmalloc(sizeof(job_step_stat_t));
	*msg_ptr = msg;

	safe_unpack32(&msg->return_code, buffer);
	safe_unpack32(&msg->num_tasks, buffer);
	if (jobacctinfo_unpack(&msg->jobacct, protocol_version,
			       PROTOCOL_TYPE_SLURM, buffer, 1)
	    != SLURM_SUCCESS)
		goto unpack_error;
	rc = _unpack_job_step_pids(&msg->step_pids, buffer, protocol_version);

	return rc;

unpack_error:
	slurm_free_job_step_stat(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_job_step_id_msg(job_step_id_msg_t * msg, Buf buffer,
		      uint16_t protocol_version)
{
	pack32((uint32_t)msg->job_id, buffer);
	pack32((uint32_t)msg->step_id, buffer);
}


static int
_unpack_job_step_id_msg(job_step_id_msg_t ** msg_ptr, Buf buffer,
			uint16_t protocol_version)
{
	job_step_id_msg_t *msg;

	msg = xmalloc(sizeof(job_step_id_msg_t));
	*msg_ptr = msg;

	safe_unpack32(&msg->job_id, buffer);
	safe_unpack32(&msg->step_id, buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_step_id_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_job_step_pids(job_step_pids_t *msg, Buf buffer,
		    uint16_t protocol_version)
{
	if (!msg) {
		packnull(buffer);
		pack32(0, buffer);
		return;
	}
	packstr(msg->node_name, buffer);
	pack32_array(msg->pid, msg->pid_cnt, buffer);
}

static int
_unpack_job_step_pids(job_step_pids_t **msg_ptr, Buf buffer,
		      uint16_t protocol_version)
{
	job_step_pids_t *msg;
	uint32_t uint32_tmp;

	msg = xmalloc(sizeof(job_step_pids_t));
	*msg_ptr = msg;

	safe_unpackstr_xmalloc(&msg->node_name, &uint32_tmp, buffer);
	safe_unpack32_array(&msg->pid, &msg->pid_cnt, buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_step_pids(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_step_complete_msg(step_complete_msg_t * msg, Buf buffer,
			uint16_t protocol_version)
{
	pack32((uint32_t)msg->job_id, buffer);
	pack32((uint32_t)msg->job_step_id, buffer);
	pack32((uint32_t)msg->range_first, buffer);
	pack32((uint32_t)msg->range_last, buffer);
	pack32((uint32_t)msg->step_rc, buffer);
	jobacctinfo_pack(msg->jobacct, protocol_version,
			 PROTOCOL_TYPE_SLURM, buffer);
}

static int
_unpack_step_complete_msg(step_complete_msg_t ** msg_ptr, Buf buffer,
			  uint16_t protocol_version)
{
	step_complete_msg_t *msg;

	msg = xmalloc(sizeof(step_complete_msg_t));
	*msg_ptr = msg;

	safe_unpack32(&msg->job_id, buffer);
	safe_unpack32(&msg->job_step_id, buffer);
	safe_unpack32(&msg->range_first, buffer);
	safe_unpack32(&msg->range_last, buffer);
	safe_unpack32(&msg->step_rc, buffer);
	if (jobacctinfo_unpack(&msg->jobacct, protocol_version,
			       PROTOCOL_TYPE_SLURM, buffer, 1)
	    != SLURM_SUCCESS)
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_step_complete_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_job_info_request_msg(job_info_request_msg_t * msg, Buf buffer,
			   uint16_t protocol_version)
{
	pack_time(msg->last_update, buffer);
	pack16((uint16_t)msg->show_flags, buffer);
}

static int
_unpack_job_info_request_msg(job_info_request_msg_t** msg,
			     Buf buffer,
			     uint16_t protocol_version)
{
	job_info_request_msg_t*job_info;

	job_info = xmalloc(sizeof(job_step_info_request_msg_t));
	*msg = job_info;

	safe_unpack_time(&job_info->last_update, buffer);
	safe_unpack16(&job_info->show_flags, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_info_request_msg(job_info);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_block_info_req_msg(block_info_request_msg_t *msg, Buf buffer,
			 uint16_t protocol_version)
{
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_time(msg->last_update, buffer);
		pack16(msg->show_flags, buffer);
	} else {
		error("_pack_block_info_req_msg: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

static int
_unpack_block_info_req_msg(block_info_request_msg_t **msg,
			   Buf buffer,
			   uint16_t protocol_version)
{
	block_info_request_msg_t *node_sel_info;

	node_sel_info = xmalloc(sizeof(block_info_request_msg_t));
	*msg = node_sel_info;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack_time(&node_sel_info->last_update, buffer);
		safe_unpack16(&node_sel_info->show_flags, buffer);
	} else {
		error("_unpack_block_info_req_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_block_info_request_msg(node_sel_info);
	*msg = NULL;
	return SLURM_ERROR;
}

static int _unpack_block_job_info(block_job_info_t **job_info, Buf buffer,
				  uint16_t protocol_version)
{
	block_job_info_t *job;
	uint32_t uint32_tmp;
	char *cnode_inx_str = NULL;

	job = xmalloc(sizeof(block_job_info_t));
	*job_info = job;

	safe_unpackstr_xmalloc(&job->cnodes, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&cnode_inx_str, &uint32_tmp, buffer);
	if (cnode_inx_str == NULL) {
		job->cnode_inx = bitfmt2int("");
	} else {
		job->cnode_inx = bitfmt2int(cnode_inx_str);
		xfree(cnode_inx_str);
	}
	safe_unpack32(&job->job_id, buffer);
	safe_unpack32(&job->user_id, buffer);
	safe_unpackstr_xmalloc(&job->user_name, &uint32_tmp, buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_block_job_info(job);
	*job_info = NULL;
	return SLURM_ERROR;
}

/* NOTE: There is a matching pack function directly in the select/bluegene
 * plugin dealing with the bg_record_t structure there.  If anything
 * changes here please update that as well.
 */
static void _pack_block_info_msg(block_info_t *block_info, Buf buffer,
				 uint16_t protocol_version)
{
	uint32_t cluster_dims = (uint32_t)slurmdb_setup_cluster_dims();
	int dim, count = NO_VAL;
	ListIterator itr;
	block_job_info_t *job;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!block_info) {
			packnull(buffer);
			packnull(buffer);
			packnull(buffer);

			pack32(1, buffer);
			pack16((uint16_t)NO_VAL, buffer);

			packnull(buffer);
			packnull(buffer);

			pack32(NO_VAL, buffer);

			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack16((uint16_t)NO_VAL, buffer);
			packnull(buffer);
			packnull(buffer);
			pack16((uint16_t)NO_VAL, buffer);
			packnull(buffer);
			return;
		}

		packstr(block_info->bg_block_id, buffer);
		packstr(block_info->blrtsimage, buffer);

		if (block_info->mp_inx) {
			char *bitfmt = inx2bitfmt(block_info->mp_inx);
			packstr(bitfmt, buffer);
			xfree(bitfmt);
		} else
			packnull(buffer);

		pack32(cluster_dims, buffer);
		for (dim = 0; dim < cluster_dims; dim++)
			pack16(block_info->conn_type[dim], buffer);

		packstr(block_info->ionode_str, buffer);

		if (block_info->ionode_inx) {
			char *bitfmt =
				inx2bitfmt(block_info->ionode_inx);
			packstr(bitfmt, buffer);
			xfree(bitfmt);
		} else
			packnull(buffer);

		if (block_info->job_list)
			count = list_count(block_info->job_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(block_info->job_list);
			while ((job = list_next(itr))) {
				slurm_pack_block_job_info(job, buffer,
							  protocol_version);
			}
			list_iterator_destroy(itr);
		}

		packstr(block_info->linuximage, buffer);
		packstr(block_info->mloaderimage, buffer);
		packstr(block_info->mp_str, buffer);
		pack32(block_info->cnode_cnt, buffer);
		pack32(block_info->cnode_err_cnt, buffer);
		pack16(block_info->node_use, buffer);
		packstr(block_info->ramdiskimage, buffer);
		packstr(block_info->reason, buffer);
		pack16(block_info->state, buffer);
	} else {
		error("_pack_block_info_msg: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

extern void slurm_pack_block_job_info(block_job_info_t *block_job_info,
				      Buf buffer, uint16_t protocol_version)
{
	if (!block_job_info) {
		packnull(buffer);
		packnull(buffer);
		pack32(0, buffer);
		pack32(0, buffer);
		packnull(buffer);
		return;
	}

	packstr(block_job_info->cnodes, buffer);
	if (block_job_info->cnode_inx) {
		char *bitfmt = inx2bitfmt(block_job_info->cnode_inx);
		packstr(bitfmt, buffer);
		xfree(bitfmt);
	} else
		packnull(buffer);
	pack32(block_job_info->job_id, buffer);
	pack32(block_job_info->user_id, buffer);
	packstr(block_job_info->user_name, buffer);
}

extern int slurm_unpack_block_info_members(block_info_t *block_info, Buf buffer,
					   uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	char *mp_inx_str = NULL;
	int i;
	uint32_t count;
	block_job_info_t *job = NULL;

	memset(block_info, 0, sizeof(block_info_t));

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&block_info->bg_block_id,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&block_info->blrtsimage,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&mp_inx_str, &uint32_tmp, buffer);
		if (mp_inx_str == NULL) {
			block_info->mp_inx = bitfmt2int("");
		} else {
			block_info->mp_inx = bitfmt2int(mp_inx_str);
			xfree(mp_inx_str);
		}

		safe_unpack32(&count, buffer);
		if (count > HIGHEST_DIMENSIONS) {
			error("slurm_unpack_block_info_members: count of "
			      "system is %d but we can only handle %d",
			      count, HIGHEST_DIMENSIONS);
			goto unpack_error;
		}
		for (i=0; i<count; i++)
			safe_unpack16(&block_info->conn_type[i], buffer);
		safe_unpackstr_xmalloc(&(block_info->ionode_str),
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&mp_inx_str, &uint32_tmp, buffer);
		if (mp_inx_str == NULL) {
			block_info->ionode_inx = bitfmt2int("");
		} else {
			block_info->ionode_inx = bitfmt2int(mp_inx_str);
			xfree(mp_inx_str);
		}
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			block_info->job_list =
				list_create(slurm_free_block_job_info);
			for (i=0; i<count; i++) {
				if (_unpack_block_job_info(&job, buffer,
							   protocol_version)
				    == SLURM_ERROR)
					goto unpack_error;
				list_append(block_info->job_list, job);
			}
		}

		safe_unpackstr_xmalloc(&block_info->linuximage,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&block_info->mloaderimage,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&(block_info->mp_str), &uint32_tmp,
				       buffer);
		safe_unpack32(&block_info->cnode_cnt, buffer);
		safe_unpack32(&block_info->cnode_err_cnt, buffer);
		safe_unpack16(&block_info->node_use, buffer);
		safe_unpackstr_xmalloc(&block_info->ramdiskimage,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&block_info->reason,
				       &uint32_tmp, buffer);
		safe_unpack16(&block_info->state, buffer);
	} else {
		error("slurm_unpack_block_info_members: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	error("slurm_unpack_block_info_members: error unpacking here");
	slurm_free_block_info_members(block_info);
	return SLURM_ERROR;
}

extern int slurm_unpack_block_info_msg(
	block_info_msg_t **block_info_msg_pptr, Buf buffer,
	uint16_t protocol_version)
{
	int i;
	block_info_msg_t *buf;

	buf = xmalloc(sizeof(block_info_msg_t));
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&(buf->record_count), buffer);
		safe_unpack_time(&(buf->last_update), buffer);

		buf->block_array = xmalloc(sizeof(block_info_t) *
					   buf->record_count);

		for (i=0; i<buf->record_count; i++) {
			if (slurm_unpack_block_info_members(
				    &(buf->block_array[i]), buffer,
				    protocol_version))
				goto unpack_error;
		}
	} else {
		error("slurm_unpack_block_info_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	*block_info_msg_pptr = buf;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_block_info_msg(buf);
	*block_info_msg_pptr = NULL;
	return SLURM_ERROR;
}

static int _unpack_block_info(block_info_t **block_info, Buf buffer,
			      uint16_t protocol_version)
{
	int rc = SLURM_SUCCESS;
	block_info_t *bg_rec = xmalloc(sizeof(block_info_t));

	if ((rc = slurm_unpack_block_info_members(
		    bg_rec, buffer, protocol_version))
	    != SLURM_SUCCESS)
		xfree(bg_rec);
	else
		*block_info = bg_rec;
	return rc;
}

static void
_pack_job_step_info_req_msg(job_step_info_request_msg_t * msg, Buf buffer,
			    uint16_t protocol_version)
{
	pack_time(msg->last_update, buffer);
	pack32((uint32_t)msg->job_id, buffer);
	pack32((uint32_t)msg->step_id, buffer);
	pack16((uint16_t)msg->show_flags, buffer);
}

static int
_unpack_job_step_info_req_msg(job_step_info_request_msg_t ** msg, Buf buffer,
			      uint16_t protocol_version)
{
	job_step_info_request_msg_t *job_step_info;

	job_step_info = xmalloc(sizeof(job_step_info_request_msg_t));
	*msg = job_step_info;

	safe_unpack_time(&job_step_info->last_update, buffer);
	safe_unpack32(&job_step_info->job_id, buffer);
	safe_unpack32(&job_step_info->step_id, buffer);
	safe_unpack16(&job_step_info->show_flags, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_step_info_request_msg(job_step_info);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_node_info_request_msg(node_info_request_msg_t * msg, Buf buffer,
			    uint16_t protocol_version)
{
	pack_time(msg->last_update, buffer);
	pack16(msg->show_flags, buffer);
}

static int
_unpack_node_info_request_msg(node_info_request_msg_t ** msg, Buf buffer,
			      uint16_t protocol_version)
{
	node_info_request_msg_t* node_info;

	node_info = xmalloc(sizeof(node_info_request_msg_t));
	*msg = node_info;

	safe_unpack_time(&node_info->last_update, buffer);
	safe_unpack16(&node_info->show_flags, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_node_info_request_msg(node_info);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_node_info_single_msg(node_info_single_msg_t * msg, Buf buffer,
			   uint16_t protocol_version)
{
	packstr(msg->node_name, buffer);
	pack16(msg->show_flags, buffer);
}

static int
_unpack_node_info_single_msg(node_info_single_msg_t ** msg, Buf buffer,
			     uint16_t protocol_version)
{
	node_info_single_msg_t* node_info;
	uint32_t uint32_tmp;

	node_info = xmalloc(sizeof(node_info_single_msg_t));
	*msg = node_info;

	safe_unpackstr_xmalloc(&node_info->node_name, &uint32_tmp, buffer);
	safe_unpack16(&node_info->show_flags, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_node_info_single_msg(node_info);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_front_end_info_request_msg(front_end_info_request_msg_t * msg,
				 Buf buffer, uint16_t protocol_version)
{
	pack_time(msg->last_update, buffer);
}

static int
_unpack_front_end_info_request_msg(front_end_info_request_msg_t ** msg,
				   Buf buffer, uint16_t protocol_version)
{
	front_end_info_request_msg_t* front_end_info;

	front_end_info = xmalloc(sizeof(front_end_info_request_msg_t));
	*msg = front_end_info;

	safe_unpack_time(&front_end_info->last_update, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_front_end_info_request_msg(front_end_info);
	*msg = NULL;
	return SLURM_ERROR;
}

static int
_unpack_front_end_info_msg(front_end_info_msg_t ** msg, Buf buffer,
			   uint16_t protocol_version)
{
	int i;
	front_end_info_t *front_end = NULL;

	xassert(msg != NULL);
	*msg = xmalloc(sizeof(front_end_info_msg_t));

	/* load buffer's header (data structure version and time) */
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&((*msg)->record_count), buffer);
		safe_unpack_time(&((*msg)->last_update), buffer);
		front_end = xmalloc(sizeof(front_end_info_t) *
				    (*msg)->record_count);
		(*msg)->front_end_array = front_end;

		/* load individual front_end info */
		for (i = 0; i < (*msg)->record_count; i++) {
			if (_unpack_front_end_info_members(&front_end[i],
							   buffer,
							   protocol_version))
				goto unpack_error;
		}
	} else {
		error("_unpack_front_end_info_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_front_end_info_msg(*msg);
	*msg = NULL;
	return SLURM_ERROR;
}

static int
_unpack_front_end_info_members(front_end_info_t *front_end, Buf buffer,
			       uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	uint16_t tmp_state;

	xassert(front_end != NULL);

	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&front_end->allow_groups, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&front_end->allow_users, &uint32_tmp,
				       buffer);
		safe_unpack_time(&front_end->boot_time, buffer);
		safe_unpackstr_xmalloc(&front_end->deny_groups, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&front_end->deny_users, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&front_end->name, &uint32_tmp, buffer);
		safe_unpack32(&front_end->node_state, buffer);
		safe_unpackstr_xmalloc(&front_end->version, &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&front_end->reason, &uint32_tmp, buffer);
		safe_unpack_time(&front_end->reason_time, buffer);
		safe_unpack32(&front_end->reason_uid, buffer);

		safe_unpack_time(&front_end->slurmd_start_time, buffer);

	} else if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&front_end->allow_groups, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&front_end->allow_users, &uint32_tmp,
				       buffer);
		safe_unpack_time(&front_end->boot_time, buffer);
		safe_unpackstr_xmalloc(&front_end->deny_groups, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&front_end->deny_users, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&front_end->name, &uint32_tmp, buffer);
		safe_unpack16(&tmp_state, buffer);
		safe_unpackstr_xmalloc(&front_end->version, &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&front_end->reason, &uint32_tmp, buffer);
		safe_unpack_time(&front_end->reason_time, buffer);
		safe_unpack32(&front_end->reason_uid, buffer);

		safe_unpack_time(&front_end->slurmd_start_time, buffer);
		front_end->node_state = tmp_state;

	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&front_end->allow_groups, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&front_end->allow_users, &uint32_tmp,
				       buffer);
		safe_unpack_time(&front_end->boot_time, buffer);
		safe_unpackstr_xmalloc(&front_end->deny_groups, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&front_end->deny_users, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&front_end->name, &uint32_tmp, buffer);
		safe_unpack16(&tmp_state, buffer);

		safe_unpackstr_xmalloc(&front_end->reason, &uint32_tmp, buffer);
		safe_unpack_time(&front_end->reason_time, buffer);
		safe_unpack32(&front_end->reason_uid, buffer);

		safe_unpack_time(&front_end->slurmd_start_time, buffer);
		front_end->node_state = tmp_state;
	} else {
		error("_unpack_front_end_info_members: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_front_end_info_members(front_end);
	return SLURM_ERROR;
}

static void
_pack_part_info_request_msg(part_info_request_msg_t * msg, Buf buffer,
			    uint16_t protocol_version)
{
	pack_time(msg->last_update, buffer);
	pack16((uint16_t)msg->show_flags, buffer);
}

static int
_unpack_part_info_request_msg(part_info_request_msg_t ** msg, Buf buffer,
			      uint16_t protocol_version)
{
	part_info_request_msg_t* part_info;

	part_info = xmalloc(sizeof(part_info_request_msg_t));
	*msg = part_info;

	safe_unpack_time(&part_info->last_update, buffer);
	safe_unpack16(&part_info->show_flags, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_part_info_request_msg(part_info);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_resv_info_request_msg(resv_info_request_msg_t * msg, Buf buffer,
			    uint16_t protocol_version)
{
	pack_time(msg->last_update, buffer);
}

static int
_unpack_resv_info_request_msg(resv_info_request_msg_t ** msg, Buf buffer,
			      uint16_t protocol_version)
{
	resv_info_request_msg_t* resv_info;

	resv_info = xmalloc(sizeof(resv_info_request_msg_t));
	*msg = resv_info;

	safe_unpack_time(&resv_info->last_update, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_resv_info_request_msg(resv_info);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_slurm_addr_array(slurm_addr_t * slurm_address,
		       uint32_t size_val, Buf buffer,
		       uint16_t protocol_version)
{
	slurm_pack_slurm_addr_array(slurm_address, size_val, buffer);
}

static int
_unpack_slurm_addr_array(slurm_addr_t ** slurm_address,
			 uint32_t * size_val, Buf buffer,
			 uint16_t protocol_version)
{
	return slurm_unpack_slurm_addr_array(slurm_address, size_val, buffer);
}


static void
_pack_ret_list(List ret_list,
	       uint16_t size_val, Buf buffer,
	       uint16_t protocol_version)
{
	ListIterator itr;
	ret_data_info_t *ret_data_info = NULL;
	slurm_msg_t msg;

	slurm_msg_t_init(&msg);
	msg.protocol_version = protocol_version;
	itr = list_iterator_create(ret_list);
	while ((ret_data_info = list_next(itr))) {
		pack32((uint32_t)ret_data_info->err, buffer);
		pack16((uint16_t)ret_data_info->type, buffer);
		packstr(ret_data_info->node_name, buffer);

		msg.msg_type = ret_data_info->type;
		msg.data = ret_data_info->data;
		pack_msg(&msg, buffer);
	}
	list_iterator_destroy(itr);
}

static int
_unpack_ret_list(List *ret_list,
		 uint16_t size_val, Buf buffer,
		 uint16_t protocol_version)
{
	int i = 0;
	uint32_t uint32_tmp;
	ret_data_info_t *ret_data_info = NULL;
	slurm_msg_t msg;

	slurm_msg_t_init(&msg);
	msg.protocol_version = protocol_version;

	*ret_list = list_create(destroy_data_info);

	for (i=0; i<size_val; i++) {
		ret_data_info = xmalloc(sizeof(ret_data_info_t));
		list_push(*ret_list, ret_data_info);

		safe_unpack32((uint32_t *)&ret_data_info->err, buffer);
		safe_unpack16(&ret_data_info->type, buffer);
		safe_unpackstr_xmalloc(&ret_data_info->node_name,
				       &uint32_tmp, buffer);
		msg.msg_type = ret_data_info->type;
		if (unpack_msg(&msg, buffer) != SLURM_SUCCESS)
			goto unpack_error;
		ret_data_info->data = msg.data;
	}

	return SLURM_SUCCESS;

unpack_error:
	if (ret_data_info && ret_data_info->type) {
		error("_unpack_ret_list: message type %u, record %d of %u",
		      ret_data_info->type, i, size_val);
	}
	list_destroy(*ret_list);
	*ret_list = NULL;
	return SLURM_ERROR;
}

static void
_pack_batch_job_launch_msg(batch_job_launch_msg_t * msg, Buf buffer,
			   uint16_t protocol_version)
{
	xassert(msg != NULL);

	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		pack32(msg->job_id, buffer);
		pack32(msg->step_id, buffer);
		pack32(msg->uid, buffer);
		packstr(msg->partition, buffer);
		packstr(msg->user_name, buffer);
		pack32(msg->gid, buffer);
		pack32(msg->ntasks, buffer);
		pack32(msg->pn_min_memory, buffer);

		pack8(msg->open_mode, buffer);
		pack8(msg->overcommit, buffer);

		pack32(msg->array_job_id,   buffer);
		pack32(msg->array_task_id,  buffer);

		packstr(msg->acctg_freq,     buffer);
		pack16(msg->cpu_bind_type,  buffer);
		pack16(msg->cpus_per_task,  buffer);
		pack16(msg->restart_cnt,    buffer);
		pack16(msg->job_core_spec,  buffer);

		pack32(msg->num_cpu_groups, buffer);
		if (msg->num_cpu_groups) {
			pack16_array(msg->cpus_per_node, msg->num_cpu_groups,
				     buffer);
			pack32_array(msg->cpu_count_reps, msg->num_cpu_groups,
				     buffer);
		}

		packstr(msg->alias_list, buffer);
		packstr(msg->cpu_bind, buffer);
		packstr(msg->nodes,    buffer);
		packstr(msg->script,   buffer);
		packstr(msg->work_dir, buffer);
		packstr(msg->ckpt_dir, buffer);
		packstr(msg->restart_dir, buffer);

		packstr(msg->std_err, buffer);
		packstr(msg->std_in, buffer);
		packstr(msg->std_out, buffer);

		pack32(msg->argc, buffer);
		packstr_array(msg->argv, msg->argc, buffer);
		packstr_array(msg->spank_job_env, msg->spank_job_env_size,
			      buffer);

		pack32(msg->envc, buffer);
		packstr_array(msg->environment, msg->envc, buffer);

		pack32(msg->job_mem, buffer);

		slurm_cred_pack(msg->cred, buffer, protocol_version);

		select_g_select_jobinfo_pack(msg->select_jobinfo, buffer,
					     protocol_version);
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		pack32(msg->job_id, buffer);
		pack32(msg->step_id, buffer);
		pack32(msg->uid, buffer);
		pack32(msg->gid, buffer);
		pack32(msg->ntasks, buffer);
		pack32(msg->pn_min_memory, buffer);

		pack8(msg->open_mode, buffer);
		pack8(msg->overcommit, buffer);

		pack32(msg->array_job_id,   buffer);
		pack16((uint16_t) msg->array_task_id, buffer);

		packstr(msg->acctg_freq,     buffer);
		pack16(msg->cpu_bind_type,  buffer);
		pack16(msg->cpus_per_task,  buffer);
		pack16(msg->restart_cnt,    buffer);

		pack32(msg->num_cpu_groups, buffer);
		if (msg->num_cpu_groups) {
			pack16_array(msg->cpus_per_node, msg->num_cpu_groups,
				     buffer);
			pack32_array(msg->cpu_count_reps, msg->num_cpu_groups,
				     buffer);
		}

		packstr(msg->alias_list, buffer);
		packstr(msg->cpu_bind, buffer);
		packstr(msg->nodes,    buffer);
		packstr(msg->script,   buffer);
		packstr(msg->work_dir, buffer);
		packstr(msg->ckpt_dir, buffer);
		packstr(msg->restart_dir, buffer);

		packstr(msg->std_err, buffer);
		packstr(msg->std_in, buffer);
		packstr(msg->std_out, buffer);

		pack32(msg->argc, buffer);
		packstr_array(msg->argv, msg->argc, buffer);
		packstr_array(msg->spank_job_env, msg->spank_job_env_size,
			      buffer);

		pack32(msg->envc, buffer);
		packstr_array(msg->environment, msg->envc, buffer);

		pack32(msg->job_mem, buffer);

		slurm_cred_pack(msg->cred, buffer, protocol_version);

		select_g_select_jobinfo_pack(msg->select_jobinfo, buffer,
					     protocol_version);
	} else {
		error("_pack_batch_job_launch_msg: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

static int
_unpack_batch_job_launch_msg(batch_job_launch_msg_t ** msg, Buf buffer,
			     uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	uint16_t uint16_tmp = 0;
	batch_job_launch_msg_t *launch_msg_ptr;

	xassert(msg != NULL);
	launch_msg_ptr = xmalloc(sizeof(batch_job_launch_msg_t));
	*msg = launch_msg_ptr;

	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		safe_unpack32(&launch_msg_ptr->job_id, buffer);
		safe_unpack32(&launch_msg_ptr->step_id, buffer);
		safe_unpack32(&launch_msg_ptr->uid, buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->partition,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->user_name,
				       &uint32_tmp, buffer);
		safe_unpack32(&launch_msg_ptr->gid, buffer);
		safe_unpack32(&launch_msg_ptr->ntasks, buffer);
		safe_unpack32(&launch_msg_ptr->pn_min_memory, buffer);

		safe_unpack8(&launch_msg_ptr->open_mode, buffer);
		safe_unpack8(&launch_msg_ptr->overcommit, buffer);

		safe_unpack32(&launch_msg_ptr->array_job_id,   buffer);
		safe_unpack32(&launch_msg_ptr->array_task_id,  buffer);

		safe_unpackstr_xmalloc(&launch_msg_ptr->acctg_freq,
				       &uint32_tmp, buffer);
		safe_unpack16(&launch_msg_ptr->cpu_bind_type,  buffer);
		safe_unpack16(&launch_msg_ptr->cpus_per_task,  buffer);
		safe_unpack16(&launch_msg_ptr->restart_cnt,    buffer);
		safe_unpack16(&launch_msg_ptr->job_core_spec,  buffer);

		safe_unpack32(&launch_msg_ptr->num_cpu_groups, buffer);
		if (launch_msg_ptr->num_cpu_groups) {
			safe_unpack16_array(&(launch_msg_ptr->cpus_per_node),
					    &uint32_tmp, buffer);
			if (launch_msg_ptr->num_cpu_groups != uint32_tmp)
				goto unpack_error;
			safe_unpack32_array(&(launch_msg_ptr->cpu_count_reps),
					    &uint32_tmp, buffer);
			if (launch_msg_ptr->num_cpu_groups != uint32_tmp)
				goto unpack_error;
		}

		safe_unpackstr_xmalloc(&launch_msg_ptr->alias_list,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->cpu_bind, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->nodes,    &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->script,   &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->work_dir, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->ckpt_dir, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->restart_dir,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&launch_msg_ptr->std_err, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->std_in,  &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->std_out, &uint32_tmp,
				       buffer);

		safe_unpack32(&launch_msg_ptr->argc, buffer);
		safe_unpackstr_array(&launch_msg_ptr->argv,
				     &launch_msg_ptr->argc, buffer);
		safe_unpackstr_array(&launch_msg_ptr->spank_job_env,
				     &launch_msg_ptr->spank_job_env_size,
				     buffer);

		safe_unpack32(&launch_msg_ptr->envc, buffer);
		safe_unpackstr_array(&launch_msg_ptr->environment,
				     &launch_msg_ptr->envc, buffer);

		safe_unpack32(&launch_msg_ptr->job_mem, buffer);

		if (!(launch_msg_ptr->cred = slurm_cred_unpack(
			      buffer, protocol_version)))
			goto unpack_error;

		if (select_g_select_jobinfo_unpack(&launch_msg_ptr->
						   select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		safe_unpack32(&launch_msg_ptr->job_id, buffer);
		safe_unpack32(&launch_msg_ptr->step_id, buffer);
		safe_unpack32(&launch_msg_ptr->uid, buffer);
		safe_unpack32(&launch_msg_ptr->gid, buffer);
		safe_unpack32(&launch_msg_ptr->ntasks, buffer);
		safe_unpack32(&launch_msg_ptr->pn_min_memory, buffer);

		safe_unpack8(&launch_msg_ptr->open_mode, buffer);
		safe_unpack8(&launch_msg_ptr->overcommit, buffer);

		safe_unpack32(&launch_msg_ptr->array_job_id,   buffer);
		safe_unpack16(&uint16_tmp,  buffer);
		if (uint16_tmp == (uint16_t) NO_VAL)
			launch_msg_ptr->array_task_id = NO_VAL;
		else
			launch_msg_ptr->array_task_id = (uint32_t) uint16_tmp;

		safe_unpackstr_xmalloc(&launch_msg_ptr->acctg_freq,
				       &uint32_tmp, buffer);
		safe_unpack16(&launch_msg_ptr->cpu_bind_type,  buffer);
		safe_unpack16(&launch_msg_ptr->cpus_per_task,  buffer);
		safe_unpack16(&launch_msg_ptr->restart_cnt,    buffer);

		safe_unpack32(&launch_msg_ptr->num_cpu_groups, buffer);
		if (launch_msg_ptr->num_cpu_groups) {
			safe_unpack16_array(&(launch_msg_ptr->cpus_per_node),
					    &uint32_tmp, buffer);
			if (launch_msg_ptr->num_cpu_groups != uint32_tmp)
				goto unpack_error;
			safe_unpack32_array(&(launch_msg_ptr->cpu_count_reps),
					    &uint32_tmp, buffer);
			if (launch_msg_ptr->num_cpu_groups != uint32_tmp)
				goto unpack_error;
		}


		safe_unpackstr_xmalloc(&launch_msg_ptr->alias_list,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->cpu_bind, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->nodes,    &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->script,   &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->work_dir, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->ckpt_dir, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->restart_dir,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&launch_msg_ptr->std_err, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->std_in,  &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&launch_msg_ptr->std_out, &uint32_tmp,
				       buffer);

		safe_unpack32(&launch_msg_ptr->argc, buffer);
		safe_unpackstr_array(&launch_msg_ptr->argv,
				     &launch_msg_ptr->argc, buffer);
		safe_unpackstr_array(&launch_msg_ptr->spank_job_env,
				     &launch_msg_ptr->spank_job_env_size,
				     buffer);

		safe_unpack32(&launch_msg_ptr->envc, buffer);
		safe_unpackstr_array(&launch_msg_ptr->environment,
				     &launch_msg_ptr->envc, buffer);

		safe_unpack32(&launch_msg_ptr->job_mem, buffer);

		if (!(launch_msg_ptr->cred = slurm_cred_unpack(
			      buffer, protocol_version)))
			goto unpack_error;

		if (select_g_select_jobinfo_unpack(&launch_msg_ptr->
						   select_jobinfo,
						   buffer, protocol_version))
			goto unpack_error;
	} else {
		error("_unpack_batch_job_launch_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_launch_msg(launch_msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_job_id_request_msg(job_id_request_msg_t * msg, Buf buffer,
			 uint16_t protocol_version)
{
	xassert(msg != NULL);

	pack32((uint32_t)msg->job_pid, buffer);
}

static int
_unpack_job_id_request_msg(job_id_request_msg_t ** msg, Buf buffer,
			   uint16_t protocol_version)
{
	job_id_request_msg_t *tmp_ptr;

	/* alloc memory for structure */
	xassert(msg != NULL);
	tmp_ptr = xmalloc(sizeof(job_id_request_msg_t));
	*msg = tmp_ptr;

	/* load the data values */
	safe_unpack32(&tmp_ptr->job_pid, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_id_request_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_job_id_response_msg(job_id_response_msg_t * msg, Buf buffer,
			  uint16_t protocol_version)
{
	xassert(msg != NULL);

	pack32((uint32_t)msg->job_id, buffer);
	pack32((uint32_t)msg->return_code, buffer);
}

static int
_unpack_job_id_response_msg(job_id_response_msg_t ** msg, Buf buffer,
			    uint16_t protocol_version)
{
	job_id_response_msg_t *tmp_ptr;

	/* alloc memory for structure */
	xassert(msg != NULL);
	tmp_ptr = xmalloc(sizeof(job_id_response_msg_t));
	*msg = tmp_ptr;

	/* load the data values */
	safe_unpack32(&tmp_ptr->job_id, buffer);
	safe_unpack32(&tmp_ptr->return_code, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_id_response_msg(tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_srun_exec_msg(srun_exec_msg_t * msg, Buf buffer,
		    uint16_t protocol_version)
{
	xassert ( msg != NULL );

	pack32(msg ->job_id  , buffer ) ;
	pack32(msg ->step_id , buffer ) ;
	packstr_array(msg->argv, msg->argc, buffer);
}

static int
_unpack_srun_exec_msg(srun_exec_msg_t ** msg_ptr, Buf buffer,
		      uint16_t protocol_version)
{
	srun_exec_msg_t * msg;
	xassert ( msg_ptr != NULL );

	msg = xmalloc ( sizeof (srun_exec_msg_t) ) ;
	*msg_ptr = msg;

	safe_unpack32(&msg->job_id  , buffer ) ;
	safe_unpack32(&msg->step_id , buffer ) ;
	safe_unpackstr_array(&msg->argv, &msg->argc, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_srun_exec_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_srun_ping_msg(srun_ping_msg_t * msg, Buf buffer,
		    uint16_t protocol_version)
{
	xassert ( msg != NULL );

	pack32((uint32_t)msg ->job_id  , buffer ) ;
	pack32((uint32_t)msg ->step_id , buffer ) ;
}

static int
_unpack_srun_ping_msg(srun_ping_msg_t ** msg_ptr, Buf buffer,
		      uint16_t protocol_version)
{
	srun_ping_msg_t * msg;
	xassert ( msg_ptr != NULL );

	msg = xmalloc ( sizeof (srun_ping_msg_t) ) ;
	*msg_ptr = msg;

	safe_unpack32(&msg->job_id  , buffer ) ;
	safe_unpack32(&msg->step_id , buffer ) ;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_srun_ping_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_srun_node_fail_msg(srun_node_fail_msg_t * msg, Buf buffer,
			 uint16_t protocol_version)
{
	xassert ( msg != NULL );

	pack32(msg->job_id  , buffer ) ;
	pack32(msg->step_id , buffer ) ;
	packstr(msg->nodelist, buffer ) ;
}

static int
_unpack_srun_node_fail_msg(srun_node_fail_msg_t ** msg_ptr, Buf buffer,
			   uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	srun_node_fail_msg_t * msg;
	xassert ( msg_ptr != NULL );

	msg = xmalloc ( sizeof (srun_node_fail_msg_t) ) ;
	*msg_ptr = msg;

	safe_unpack32(&msg->job_id  , buffer ) ;
	safe_unpack32(&msg->step_id , buffer ) ;
	safe_unpackstr_xmalloc ( & msg->nodelist, &uint32_tmp, buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_srun_node_fail_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_srun_step_missing_msg(srun_step_missing_msg_t * msg, Buf buffer,
			    uint16_t protocol_version)
{
	xassert ( msg != NULL );

	pack32(msg->job_id  , buffer ) ;
	pack32(msg->step_id , buffer ) ;
	packstr(msg->nodelist, buffer ) ;
}

static int
_unpack_srun_step_missing_msg(srun_step_missing_msg_t ** msg_ptr, Buf buffer,
			      uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	srun_step_missing_msg_t * msg;
	xassert ( msg_ptr != NULL );

	msg = xmalloc ( sizeof (srun_step_missing_msg_t) ) ;
	*msg_ptr = msg;

	safe_unpack32(&msg->job_id  , buffer ) ;
	safe_unpack32(&msg->step_id , buffer ) ;
	safe_unpackstr_xmalloc ( & msg->nodelist, &uint32_tmp, buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_srun_step_missing_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_job_ready_msg(job_id_msg_t * msg, Buf buffer,
		    uint16_t protocol_version)
{
	xassert ( msg != NULL );

	pack32(msg->job_id  , buffer ) ;
	pack16(msg->show_flags, buffer);
}

static int
_unpack_job_ready_msg(job_id_msg_t ** msg_ptr, Buf buffer,
		      uint16_t protocol_version)
{
	job_id_msg_t * msg;
	xassert ( msg_ptr != NULL );

	msg = xmalloc ( sizeof (job_id_msg_t) );
	*msg_ptr = msg ;

	safe_unpack32(&msg->job_id  , buffer ) ;
	safe_unpack16(&msg->show_flags, buffer);
	return SLURM_SUCCESS;

unpack_error:
	*msg_ptr = NULL;
	slurm_free_job_id_msg(msg);
	return SLURM_ERROR;
}

static void
_pack_job_requeue_msg(requeue_msg_t *msg, Buf buf, uint16_t protocol_version)
{
	xassert(msg != NULL);

	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		pack32(msg->job_id, buf);
		packstr(msg->job_id_str, buf);
		pack32(msg->state, buf);
	} else if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		pack32(msg->job_id, buf);
		pack32(msg->state, buf);
	} else {
		/* For backward compatibility we emulate _pack_job_ready_msg()
		 */
		uint16_t cc;
		cc = 0;
		pack32(msg->job_id, buf);
		pack16(cc, buf);
	}
}

static int
_unpack_job_requeue_msg(requeue_msg_t **msg, Buf buf, uint16_t protocol_version)
{
	uint32_t uint32_tmp = 0;
	*msg = xmalloc(sizeof(requeue_msg_t));

	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		safe_unpack32(&(*msg)->job_id, buf);
		safe_unpackstr_xmalloc(&(*msg)->job_id_str, &uint32_tmp, buf);
		safe_unpack32(&(*msg)->state, buf);
	} else if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		safe_unpack32(&(*msg)->job_id, buf);
		safe_unpack32(&(*msg)->state, buf);
	} else {
		/* Translate job_id_msg_t into requeue_msg_t
		 */
		uint16_t cc;
		safe_unpack32(&(*msg)->job_id, buf) ;
		safe_unpack16(&cc, buf);
		/* Arghh.. versions < 1312 pack random bytes
		 * in the unused show_flag member.
		 */
		(*msg)->state = 0;
	}

	return SLURM_SUCCESS;
unpack_error:
	slurm_free_requeue_msg(*msg);
	*msg = NULL;
	return SLURM_ERROR;
}

static void
_pack_job_user_msg(job_user_id_msg_t * msg, Buf buffer,
		   uint16_t protocol_version)
{
	xassert ( msg != NULL );

	pack32(msg->user_id  , buffer ) ;
	pack16(msg->show_flags, buffer);
}

static int
_unpack_job_user_msg(job_user_id_msg_t ** msg_ptr, Buf buffer,
		     uint16_t protocol_version)
{
	job_user_id_msg_t * msg;
	xassert ( msg_ptr != NULL );

	msg = xmalloc ( sizeof (job_user_id_msg_t) );
	*msg_ptr = msg ;

	safe_unpack32(&msg->user_id  , buffer ) ;
	safe_unpack16(&msg->show_flags, buffer);
	return SLURM_SUCCESS;

unpack_error:
	*msg_ptr = NULL;
	slurm_free_job_user_id_msg(msg);
	return SLURM_ERROR;
}

static void
_pack_srun_timeout_msg(srun_timeout_msg_t * msg, Buf buffer,
		       uint16_t protocol_version)
{
	xassert ( msg != NULL );

	pack32((uint32_t)msg->job_id, buffer ) ;
	pack32((uint32_t)msg->step_id , buffer ) ;
	pack_time ( msg -> timeout, buffer );
}

static int
_unpack_srun_timeout_msg(srun_timeout_msg_t ** msg_ptr, Buf buffer,
			 uint16_t protocol_version)
{
	srun_timeout_msg_t * msg;
	xassert ( msg_ptr != NULL );

	msg = xmalloc ( sizeof (srun_timeout_msg_t) ) ;
	*msg_ptr = msg ;

	safe_unpack32(&msg->job_id, buffer ) ;
	safe_unpack32(&msg->step_id, buffer ) ;
	safe_unpack_time (&msg->timeout, buffer );
	return SLURM_SUCCESS;

unpack_error:
	*msg_ptr = NULL;
	slurm_free_srun_timeout_msg(msg);
	return SLURM_ERROR;
}

static void
_pack_srun_user_msg(srun_user_msg_t * msg, Buf buffer,
		    uint16_t protocol_version)
{
	xassert ( msg != NULL );

	pack32((uint32_t)msg->job_id,  buffer);
	packstr(msg->msg, buffer);
}

static int
_unpack_srun_user_msg(srun_user_msg_t ** msg_ptr, Buf buffer,
		      uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	srun_user_msg_t * msg_user;
	xassert ( msg_ptr != NULL );

	msg_user = xmalloc(sizeof (srun_user_msg_t)) ;
	*msg_ptr = msg_user;

	safe_unpack32(&msg_user->job_id, buffer);
	safe_unpackstr_xmalloc(&msg_user->msg, &uint32_tmp, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_srun_user_msg(msg_user);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_suspend_msg(suspend_msg_t *msg, Buf buffer,
			      uint16_t protocol_version)
{
	xassert ( msg != NULL );
	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		pack16(msg -> op, buffer);
		pack32(msg->job_id,  buffer);
		packstr(msg->job_id_str, buffer);
	} else {
		pack16(msg -> op, buffer);
		pack32(msg->job_id,  buffer);
	}
}

static int  _unpack_suspend_msg(suspend_msg_t **msg_ptr, Buf buffer,
				uint16_t protocol_version)
{
	suspend_msg_t * msg;
	uint32_t uint32_tmp = 0;
	xassert ( msg_ptr != NULL );

	msg = xmalloc ( sizeof (suspend_msg_t) );
	*msg_ptr = msg ;

	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		safe_unpack16(&msg->op,      buffer);
		safe_unpack32(&msg->job_id , buffer);
		safe_unpackstr_xmalloc(&msg->job_id_str, &uint32_tmp, buffer);
	} else {
		safe_unpack16(&msg->op,      buffer);
		safe_unpack32(&msg->job_id , buffer);
	}
	return SLURM_SUCCESS;

unpack_error:
	*msg_ptr = NULL;
	slurm_free_suspend_msg(msg);
	return SLURM_ERROR;
}

static void _pack_suspend_int_msg(suspend_int_msg_t *msg, Buf buffer,
				  uint16_t protocol_version)
{
	xassert ( msg != NULL );
	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		pack8(msg->indf_susp, buffer);
		pack16(msg->job_core_spec, buffer);
		pack32(msg->job_id,  buffer);
		pack16(msg->op, buffer);
		switch_g_job_suspend_info_pack(msg->switch_info, buffer,
					       protocol_version);
	} else if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		pack16(msg->job_core_spec, buffer);
		pack16(msg->op, buffer);
		pack32(msg->job_id,  buffer);
		pack8(msg->indf_susp, buffer);
		switch_g_job_suspend_info_pack(msg->switch_info, buffer,
					       protocol_version);
	} else {
		pack16(msg->op, buffer);
		pack32(msg->job_id,  buffer);
		pack8(msg->indf_susp, buffer);
		switch_g_job_suspend_info_pack(msg->switch_info, buffer,
					       protocol_version);
	}
}

static int  _unpack_suspend_int_msg(suspend_int_msg_t **msg_ptr, Buf buffer,
				    uint16_t protocol_version)
{
	suspend_int_msg_t * msg;
	xassert ( msg_ptr != NULL );

	msg = xmalloc ( sizeof (suspend_int_msg_t) );
	*msg_ptr = msg ;

	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		safe_unpack8(&msg->indf_susp, buffer);
		safe_unpack16(&msg->job_core_spec, buffer);
		safe_unpack32(&msg->job_id, buffer);
		safe_unpack16(&msg->op,     buffer);
		if (switch_g_job_suspend_info_unpack(&msg->switch_info, buffer,
						     protocol_version))
			goto unpack_error;
	} else if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		safe_unpack16(&msg->job_core_spec, buffer);
		safe_unpack16(&msg->op,     buffer);
		safe_unpack32(&msg->job_id, buffer);
		safe_unpack8(&msg->indf_susp, buffer);
		if (switch_g_job_suspend_info_unpack(&msg->switch_info, buffer,
						     protocol_version))
			goto unpack_error;
	} else {
		safe_unpack16(&msg->op,     buffer);
		safe_unpack32(&msg->job_id, buffer);
		safe_unpack8(&msg->indf_susp, buffer);
		if (switch_g_job_suspend_info_unpack(&msg->switch_info, buffer,
						     protocol_version))
			goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	*msg_ptr = NULL;
	slurm_free_suspend_int_msg(msg);
	return SLURM_ERROR;
}

static void _pack_forward_data_msg(forward_data_msg_t *msg,
				   Buf buffer, uint16_t protocol_version)
{
	xassert (msg != NULL);
	packstr(msg->address, buffer);
	pack32(msg->len, buffer);
	packmem(msg->data, msg->len, buffer);
}

static int _unpack_forward_data_msg(forward_data_msg_t **msg_ptr,
				    Buf buffer, uint16_t protocol_version)
{
	forward_data_msg_t *msg;
	uint32_t temp32;

	xassert (msg_ptr != NULL);
	msg = xmalloc(sizeof(forward_data_msg_t));
	*msg_ptr = msg;
	safe_unpackstr_xmalloc(&msg->address, &temp32, buffer);
	safe_unpack32(&msg->len, buffer);
	safe_unpackmem_xmalloc(&msg->data, &temp32, buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_forward_data_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_ping_slurmd_resp(ping_slurmd_resp_msg_t *msg,
				   Buf buffer, uint16_t protocol_version)
{
	xassert (msg != NULL);

	pack32(msg->cpu_load, buffer);
}

static int _unpack_ping_slurmd_resp(ping_slurmd_resp_msg_t **msg_ptr,
				    Buf buffer, uint16_t protocol_version)
{
	ping_slurmd_resp_msg_t *msg;

	xassert (msg_ptr != NULL);
	msg = xmalloc(sizeof(ping_slurmd_resp_msg_t));
	*msg_ptr = msg;
	safe_unpack32(&msg->cpu_load, buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_ping_slurmd_resp(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_checkpoint_msg(checkpoint_msg_t *msg, Buf buffer,
		     uint16_t protocol_version)
{
	xassert ( msg != NULL );

	pack16(msg->op,      buffer ) ;
	pack16(msg->data,    buffer ) ;
	pack32(msg->job_id,  buffer ) ;
	pack32(msg->step_id, buffer ) ;
	packstr((char *)msg->image_dir, buffer ) ;
}

static int
_unpack_checkpoint_msg(checkpoint_msg_t **msg_ptr, Buf buffer,
		       uint16_t protocol_version)
{
	checkpoint_msg_t * msg;
	uint32_t uint32_tmp;
	xassert ( msg_ptr != NULL );

	msg = xmalloc ( sizeof (checkpoint_msg_t) ) ;
	*msg_ptr = msg ;

	safe_unpack16(&msg->op, buffer ) ;
	safe_unpack16(&msg->data, buffer ) ;
	safe_unpack32(&msg->job_id, buffer ) ;
	safe_unpack32(&msg->step_id, buffer ) ;
	safe_unpackstr_xmalloc(&msg->image_dir, &uint32_tmp, buffer ) ;
	return SLURM_SUCCESS;

unpack_error:
	*msg_ptr = NULL;
	slurm_free_checkpoint_msg(msg);
	return SLURM_ERROR;
}

static void
_pack_checkpoint_comp(checkpoint_comp_msg_t *msg, Buf buffer,
		      uint16_t protocol_version)
{
	xassert ( msg != NULL );

	pack32((uint32_t)msg -> job_id,  buffer ) ;
	pack32((uint32_t)msg -> step_id, buffer ) ;
	pack32((uint32_t)msg -> error_code, buffer ) ;
	packstr ( msg -> error_msg, buffer ) ;
	pack_time ( msg -> begin_time, buffer ) ;
}

static int
_unpack_checkpoint_comp(checkpoint_comp_msg_t **msg_ptr, Buf buffer,
			uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	checkpoint_comp_msg_t * msg;
	xassert ( msg_ptr != NULL );

	msg = xmalloc ( sizeof (checkpoint_comp_msg_t) );
	*msg_ptr = msg ;

	safe_unpack32(& msg -> job_id  , buffer ) ;
	safe_unpack32(& msg -> step_id , buffer ) ;
	safe_unpack32(& msg -> error_code , buffer ) ;
	safe_unpackstr_xmalloc ( & msg -> error_msg, & uint32_tmp , buffer ) ;
	safe_unpack_time ( & msg -> begin_time , buffer ) ;
	return SLURM_SUCCESS;

unpack_error:
	*msg_ptr = NULL;
	slurm_free_checkpoint_comp_msg(msg);
	return SLURM_ERROR;
}

static void
_pack_checkpoint_task_comp(checkpoint_task_comp_msg_t *msg, Buf buffer,
			   uint16_t protocol_version)
{
	xassert ( msg != NULL );

	pack32((uint32_t)msg -> job_id,  buffer ) ;
	pack32((uint32_t)msg -> step_id, buffer ) ;
	pack32((uint32_t)msg -> task_id, buffer ) ;
	pack32((uint32_t)msg -> error_code, buffer ) ;
	packstr ( msg -> error_msg, buffer ) ;
	pack_time ( msg -> begin_time, buffer ) ;
}

static int
_unpack_checkpoint_task_comp(checkpoint_task_comp_msg_t **msg_ptr, Buf buffer,
			     uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	checkpoint_task_comp_msg_t * msg;
	xassert ( msg_ptr != NULL );

	msg = xmalloc ( sizeof (checkpoint_task_comp_msg_t) );
	*msg_ptr = msg ;

	safe_unpack32(& msg -> job_id  , buffer ) ;
	safe_unpack32(& msg -> step_id , buffer ) ;
	safe_unpack32(& msg -> task_id , buffer ) ;
	safe_unpack32(& msg -> error_code , buffer ) ;
	safe_unpackstr_xmalloc ( & msg -> error_msg, & uint32_tmp , buffer ) ;
	safe_unpack_time ( & msg -> begin_time , buffer ) ;
	return SLURM_SUCCESS;

unpack_error:
	*msg_ptr = NULL;
	slurm_free_checkpoint_task_comp_msg(msg);
	return SLURM_ERROR;
}

static void
_pack_checkpoint_resp_msg(checkpoint_resp_msg_t *msg, Buf buffer,
			  uint16_t protocol_version)
{
	xassert ( msg != NULL );

	pack_time ( msg -> event_time, buffer ) ;
	pack32((uint32_t)msg -> error_code,  buffer ) ;
	packstr ( msg -> error_msg, buffer ) ;
}

static int
_unpack_checkpoint_resp_msg(checkpoint_resp_msg_t **msg_ptr, Buf buffer,
			    uint16_t protocol_version)
{
	checkpoint_resp_msg_t * msg;
	uint32_t uint32_tmp;
	xassert ( msg_ptr != NULL );

	msg = xmalloc ( sizeof (checkpoint_resp_msg_t) ) ;
	*msg_ptr = msg ;

	safe_unpack_time ( & msg -> event_time, buffer ) ;
	safe_unpack32(& msg -> error_code , buffer ) ;
	safe_unpackstr_xmalloc ( & msg -> error_msg, & uint32_tmp , buffer ) ;
	return SLURM_SUCCESS;

unpack_error:
	*msg_ptr = NULL;
	slurm_free_checkpoint_resp_msg(msg);
	return SLURM_ERROR;
}

static void _pack_file_bcast(file_bcast_msg_t * msg , Buf buffer,
			     uint16_t protocol_version)
{
	xassert ( msg != NULL );

	grow_buf(buffer,  msg->block_len);

	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		pack16 ( msg->block_no, buffer );
		pack16 ( msg->last_block, buffer );
		pack16 ( msg->force, buffer );
		pack16 ( msg->modes, buffer );

		pack32 ( msg->uid, buffer );
		packstr ( msg->user_name, buffer );
		pack32 ( msg->gid, buffer );

		pack_time ( msg->atime, buffer );
		pack_time ( msg->mtime, buffer );

		packstr ( msg->fname, buffer );
		pack32 ( msg->block_len, buffer );
		packmem ( msg->block, msg->block_len, buffer );
		pack_sbcast_cred( msg->cred, buffer );
	} else {
		pack16 ( msg->block_no, buffer );
		pack16 ( msg->last_block, buffer );
		pack16 ( msg->force, buffer );
		pack16 ( msg->modes, buffer );

		pack32 ( msg->uid, buffer );
		pack32 ( msg->gid, buffer );

		pack_time ( msg->atime, buffer );
		pack_time ( msg->mtime, buffer );

		packstr ( msg->fname, buffer );
		pack32 ( msg->block_len, buffer );
		packmem ( msg->block, msg->block_len, buffer );
		pack_sbcast_cred( msg->cred, buffer );
	}
}

static int _unpack_file_bcast(file_bcast_msg_t ** msg_ptr , Buf buffer,
			      uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	file_bcast_msg_t *msg ;

	xassert ( msg_ptr != NULL );

	msg = xmalloc ( sizeof (file_bcast_msg_t) ) ;
	*msg_ptr = msg;

	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		safe_unpack16 ( & msg->block_no, buffer );
		safe_unpack16 ( & msg->last_block, buffer );
		safe_unpack16 ( & msg->force, buffer );
		safe_unpack16 ( & msg->modes, buffer );

		safe_unpack32 ( & msg->uid, buffer );
		safe_unpackstr_xmalloc ( &msg->user_name, &uint32_tmp, buffer );
		safe_unpack32 ( & msg->gid, buffer );

		safe_unpack_time ( & msg->atime, buffer );
		safe_unpack_time ( & msg->mtime, buffer );

		safe_unpackstr_xmalloc ( & msg->fname, &uint32_tmp, buffer );
		safe_unpack32 ( & msg->block_len, buffer );
		safe_unpackmem_xmalloc ( & msg->block, &uint32_tmp , buffer ) ;
		if ( uint32_tmp != msg->block_len )
			goto unpack_error;

		msg->cred = unpack_sbcast_cred( buffer );
		if (msg->cred == NULL)
			goto unpack_error;
	} else {
		safe_unpack16 ( & msg->block_no, buffer );
		safe_unpack16 ( & msg->last_block, buffer );
		safe_unpack16 ( & msg->force, buffer );
		safe_unpack16 ( & msg->modes, buffer );

		safe_unpack32 ( & msg->uid, buffer );
		safe_unpack32 ( & msg->gid, buffer );

		safe_unpack_time ( & msg->atime, buffer );
		safe_unpack_time ( & msg->mtime, buffer );

		safe_unpackstr_xmalloc ( & msg->fname, &uint32_tmp, buffer );
		safe_unpack32 ( & msg->block_len, buffer );
		safe_unpackmem_xmalloc ( & msg->block, &uint32_tmp , buffer ) ;
		if ( uint32_tmp != msg->block_len )
			goto unpack_error;

		msg->cred = unpack_sbcast_cred( buffer );
		if (msg->cred == NULL)
			goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_file_bcast_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_trigger_msg(trigger_info_msg_t *msg, Buf buffer,
			      uint16_t protocol_version)
{
	int i;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(msg->record_count, buffer);
		for (i = 0; i < msg->record_count; i++) {
			pack16 (msg->trigger_array[i].flags,     buffer);
			pack32 (msg->trigger_array[i].trig_id,   buffer);
			pack16 (msg->trigger_array[i].res_type,  buffer);
			packstr(msg->trigger_array[i].res_id,    buffer);
			pack32 (msg->trigger_array[i].trig_type, buffer);
			pack16 (msg->trigger_array[i].offset,    buffer);
			pack32 (msg->trigger_array[i].user_id,   buffer);
			packstr(msg->trigger_array[i].program,   buffer);
		}
	} else {
		error("_pack_trigger_msg: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

static int  _unpack_trigger_msg(trigger_info_msg_t ** msg_ptr , Buf buffer,
				uint16_t protocol_version)
{
	int i;
	uint32_t uint32_tmp;
	trigger_info_msg_t *msg = xmalloc(sizeof(trigger_info_msg_t));

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32  (&msg->record_count, buffer);
		msg->trigger_array = xmalloc(sizeof(trigger_info_t) *
					     msg->record_count);
		for (i=0; i<msg->record_count; i++) {
			safe_unpack16(&msg->trigger_array[i].flags,     buffer);
			safe_unpack32(&msg->trigger_array[i].trig_id,   buffer);
			safe_unpack16(&msg->trigger_array[i].res_type,  buffer);
			safe_unpackstr_xmalloc(&msg->trigger_array[i].res_id,
					       &uint32_tmp, buffer);
			safe_unpack32(&msg->trigger_array[i].trig_type, buffer);
			safe_unpack16(&msg->trigger_array[i].offset,    buffer);
			safe_unpack32(&msg->trigger_array[i].user_id,   buffer);
			safe_unpackstr_xmalloc(&msg->trigger_array[i].program,
					       &uint32_tmp, buffer);
		}
	} else {
		error("_unpack_trigger_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	*msg_ptr = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_trigger_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_kvs_host_rec(struct kvs_hosts *msg_ptr, Buf buffer,
			       uint16_t protocol_version)
{
	pack32(msg_ptr->task_id, buffer);
	pack16(msg_ptr->port, buffer);
	packstr(msg_ptr->hostname, buffer);
}

static int _unpack_kvs_host_rec(struct kvs_hosts *msg_ptr, Buf buffer,
				uint16_t protocol_version)
{
	uint32_t uint32_tmp;

	safe_unpack32(&msg_ptr->task_id, buffer);
	safe_unpack16(&msg_ptr->port, buffer);
	safe_unpackstr_xmalloc(&msg_ptr->hostname, &uint32_tmp, buffer);
	return SLURM_SUCCESS;

unpack_error:
	return SLURM_ERROR;
}

static void _pack_kvs_rec(struct kvs_comm *msg_ptr, Buf buffer,
			  uint16_t protocol_version)
{
	int i;
	xassert(msg_ptr != NULL);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg_ptr->kvs_name, buffer);
		pack32(msg_ptr->kvs_cnt, buffer);
		for (i=0; i<msg_ptr->kvs_cnt; i++) {
			packstr(msg_ptr->kvs_keys[i], buffer);
			packstr(msg_ptr->kvs_values[i], buffer);
		}
	} else {
		error("_pack_kvs_rec: protocol_version "
		      "%hu not supported", protocol_version);
	}
}
static int  _unpack_kvs_rec(struct kvs_comm **msg_ptr, Buf buffer,
			    uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	int i;
	struct kvs_comm *msg;

	msg = xmalloc(sizeof(struct kvs_comm));
	*msg_ptr = msg;
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&msg->kvs_name, &uint32_tmp, buffer);
		safe_unpack32(&msg->kvs_cnt, buffer);
		msg->kvs_keys   = xmalloc(sizeof(char *) * msg->kvs_cnt);
		msg->kvs_values = xmalloc(sizeof(char *) * msg->kvs_cnt);
		for (i=0; i<msg->kvs_cnt; i++) {
			safe_unpackstr_xmalloc(&msg->kvs_keys[i],
					       &uint32_tmp, buffer);
			safe_unpackstr_xmalloc(&msg->kvs_values[i],
					       &uint32_tmp, buffer);
		}
	} else {
		error("_unpack_kvs_rec: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	return SLURM_ERROR;
}
static void _pack_kvs_data(struct kvs_comm_set *msg_ptr, Buf buffer,
			   uint16_t protocol_version)
{
	int i;
	xassert(msg_ptr != NULL);

	pack16(msg_ptr->host_cnt, buffer);
	for (i=0; i<msg_ptr->host_cnt; i++)
		_pack_kvs_host_rec(&msg_ptr->kvs_host_ptr[i], buffer,
				   protocol_version);

	pack16(msg_ptr->kvs_comm_recs, buffer);
	for (i=0; i<msg_ptr->kvs_comm_recs; i++)
		_pack_kvs_rec(msg_ptr->kvs_comm_ptr[i], buffer,
			      protocol_version);
}

static int  _unpack_kvs_data(struct kvs_comm_set **msg_ptr, Buf buffer,
			     uint16_t protocol_version)
{
	struct kvs_comm_set *msg;
	int i, j;

	msg = xmalloc(sizeof(struct kvs_comm_set));
	*msg_ptr = msg;

	safe_unpack16(&msg->host_cnt, buffer);
	msg->kvs_host_ptr = xmalloc(sizeof(struct kvs_hosts) *
				    msg->host_cnt);
	for (i=0; i<msg->host_cnt; i++) {
		if (_unpack_kvs_host_rec(&msg->kvs_host_ptr[i], buffer,
					 protocol_version))
			goto unpack_error;
	}

	safe_unpack16(&msg->kvs_comm_recs, buffer);
	msg->kvs_comm_ptr = xmalloc(sizeof(struct kvs_comm) *
				    msg->kvs_comm_recs);
	for (i=0; i<msg->kvs_comm_recs; i++) {
		if (_unpack_kvs_rec(&msg->kvs_comm_ptr[i], buffer,
				    protocol_version))
			goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	for (i=0; i<msg->host_cnt; i++)
		xfree(msg->kvs_host_ptr[i].hostname);
	xfree(msg->kvs_host_ptr);
	for (i=0; i<msg->kvs_comm_recs; i++) {
		xfree(msg->kvs_comm_ptr[i]->kvs_name);
		for (j=0; j<msg->kvs_comm_ptr[i]->kvs_cnt; j++) {
			xfree(msg->kvs_comm_ptr[i]->kvs_keys[j]);
			xfree(msg->kvs_comm_ptr[i]->kvs_values[j]);
		}
		xfree(msg->kvs_comm_ptr[i]->kvs_keys);
		xfree(msg->kvs_comm_ptr[i]->kvs_values);
	}
	xfree(msg->kvs_comm_ptr);
	xfree(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_kvs_get(kvs_get_msg_t *msg_ptr, Buf buffer,
			  uint16_t protocol_version)
{
	pack32((uint32_t)msg_ptr->task_id, buffer);
	pack32((uint32_t)msg_ptr->size, buffer);
	pack16((uint16_t)msg_ptr->port, buffer);
	packstr(msg_ptr->hostname, buffer);
}

static int  _unpack_kvs_get(kvs_get_msg_t **msg_ptr, Buf buffer,
			    uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	kvs_get_msg_t *msg;

	msg = xmalloc(sizeof(struct kvs_get_msg));
	*msg_ptr = msg;
	safe_unpack32(&msg->task_id, buffer);
	safe_unpack32(&msg->size, buffer);
	safe_unpack16(&msg->port, buffer);
	safe_unpackstr_xmalloc(&msg->hostname, &uint32_tmp, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_get_kvs_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

extern void
pack_multi_core_data (multi_core_data_t *multi_core, Buf buffer,
		      uint16_t protocol_version)
{
	if (multi_core == NULL) {
		pack8((uint8_t) 0, buffer);	/* flag as Empty */
		return;
	}

	pack8((uint8_t) 0xff, buffer);		/* flag as Full */

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack16(multi_core->boards_per_node,  buffer);
		pack16(multi_core->sockets_per_board, buffer);
		pack16(multi_core->sockets_per_node, buffer);
		pack16(multi_core->cores_per_socket, buffer);
		pack16(multi_core->threads_per_core, buffer);

		pack16(multi_core->ntasks_per_board,  buffer);
		pack16(multi_core->ntasks_per_socket, buffer);
		pack16(multi_core->ntasks_per_core,   buffer);
		pack16(multi_core->plane_size,        buffer);
	} else {
		error("pack_multi_core_data: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

extern int
unpack_multi_core_data (multi_core_data_t **mc_ptr, Buf buffer,
			uint16_t protocol_version)
{
	uint8_t flag;
	multi_core_data_t *multi_core;

	*mc_ptr = NULL;
	safe_unpack8(&flag, buffer);
	if (flag == 0)
		return SLURM_SUCCESS;
	if (flag != 0xff)
		return SLURM_ERROR;

	multi_core = xmalloc(sizeof(multi_core_data_t));

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&multi_core->boards_per_node,  buffer);
		safe_unpack16(&multi_core->sockets_per_board, buffer);
		safe_unpack16(&multi_core->sockets_per_node, buffer);
		safe_unpack16(&multi_core->cores_per_socket, buffer);
		safe_unpack16(&multi_core->threads_per_core, buffer);
		safe_unpack16(&multi_core->ntasks_per_board,  buffer);
		safe_unpack16(&multi_core->ntasks_per_socket, buffer);
		safe_unpack16(&multi_core->ntasks_per_core,   buffer);
		safe_unpack16(&multi_core->plane_size,        buffer);
	} else {
		error("unpack_multi_core_data: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	*mc_ptr = multi_core;
	return SLURM_SUCCESS;

unpack_error:
	xfree(multi_core);
	return SLURM_ERROR;
}

static void _pack_slurmd_status(slurmd_status_t *msg, Buf buffer,
				uint16_t protocol_version)
{
	xassert(msg);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_time(msg->booted, buffer);
		pack_time(msg->last_slurmctld_msg, buffer);

		pack16(msg->slurmd_debug, buffer);
		pack16(msg->actual_cpus, buffer);
		pack16(msg->actual_boards, buffer);
		pack16(msg->actual_sockets, buffer);
		pack16(msg->actual_cores, buffer);
		pack16(msg->actual_threads, buffer);

		pack32(msg->actual_real_mem, buffer);
		pack32(msg->actual_tmp_disk, buffer);
		pack32(msg->pid, buffer);

		packstr(msg->hostname, buffer);
		packstr(msg->slurmd_logfile, buffer);
		packstr(msg->step_list, buffer);
		packstr(msg->version, buffer);
	} else {
		error("_pack_slurmd_status: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

static int _unpack_slurmd_status(slurmd_status_t **msg_ptr, Buf buffer,
				 uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	slurmd_status_t *msg;

	xassert(msg_ptr);

	msg = xmalloc(sizeof(slurmd_status_t));

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack_time(&msg->booted, buffer);
		safe_unpack_time(&msg->last_slurmctld_msg, buffer);

		safe_unpack16(&msg->slurmd_debug, buffer);
		safe_unpack16(&msg->actual_cpus, buffer);
		safe_unpack16(&msg->actual_boards, buffer);
		safe_unpack16(&msg->actual_sockets, buffer);
		safe_unpack16(&msg->actual_cores, buffer);
		safe_unpack16(&msg->actual_threads, buffer);

		safe_unpack32(&msg->actual_real_mem, buffer);
		safe_unpack32(&msg->actual_tmp_disk, buffer);
		safe_unpack32(&msg->pid, buffer);

		safe_unpackstr_xmalloc(&msg->hostname,
					&uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->slurmd_logfile,
					&uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->step_list,
					&uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg->version,
					&uint32_tmp, buffer);
	} else {
		error("_unpack_slurmd_status: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}

	*msg_ptr = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_slurmd_status(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_job_notify(job_notify_msg_t *msg, Buf buffer,
			     uint16_t protocol_version)
{
	xassert(msg);

	pack32(msg->job_id,      buffer);
	pack32(msg->job_step_id, buffer);
	packstr(msg->message,    buffer);
}

static int  _unpack_job_notify(job_notify_msg_t **msg_ptr, Buf buffer,
			       uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	job_notify_msg_t *msg;

	xassert(msg_ptr);

	msg = xmalloc(sizeof(job_notify_msg_t));

	safe_unpack32(&msg->job_id,      buffer);
	safe_unpack32(&msg->job_step_id, buffer);
	safe_unpackstr_xmalloc(&msg->message, &uint32_tmp, buffer);

	*msg_ptr = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_notify_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_set_debug_flags_msg(set_debug_flags_msg_t * msg, Buf buffer,
			  uint16_t protocol_version)
{
	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		pack64(msg->debug_flags_minus, buffer);
		pack64(msg->debug_flags_plus,  buffer);
	} else {
		pack32((uint32_t)msg->debug_flags_minus, buffer);
		pack32((uint32_t)msg->debug_flags_plus,  buffer);
	}
}

static int
_unpack_set_debug_flags_msg(set_debug_flags_msg_t ** msg_ptr, Buf buffer,
			    uint16_t protocol_version)
{
	set_debug_flags_msg_t *msg;

	msg = xmalloc(sizeof(set_debug_flags_msg_t));
	*msg_ptr = msg;

	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		safe_unpack64(&msg->debug_flags_minus, buffer);
		safe_unpack64(&msg->debug_flags_plus,  buffer);
	} else {
		uint32_t tmp_uint32;
		safe_unpack32(&tmp_uint32, buffer);
		msg->debug_flags_minus = tmp_uint32;
		safe_unpack32(&tmp_uint32, buffer);
		msg->debug_flags_plus = tmp_uint32;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_set_debug_flags_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_set_debug_level_msg(set_debug_level_msg_t * msg, Buf buffer,
			  uint16_t protocol_version)
{
	pack32(msg->debug_level, buffer);
}

static int
_unpack_set_debug_level_msg(set_debug_level_msg_t ** msg_ptr, Buf buffer,
			    uint16_t protocol_version)
{
	set_debug_level_msg_t *msg;

	msg = xmalloc(sizeof(set_debug_level_msg_t));
	*msg_ptr = msg;

	safe_unpack32(&msg->debug_level, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_set_debug_level_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void
_pack_will_run_response_msg(will_run_response_msg_t *msg, Buf buffer,
			    uint16_t protocol_version)
{
	uint32_t count = NO_VAL, *job_id_ptr;

	pack32(msg->job_id, buffer);
	pack32(msg->proc_cnt, buffer);
	pack_time(msg->start_time, buffer);
	packstr(msg->node_list, buffer);

	if (msg->preemptee_job_id)
		count = list_count(msg->preemptee_job_id);
	pack32(count, buffer);
	if (count && (count != NO_VAL)) {
		ListIterator itr = list_iterator_create(msg->preemptee_job_id);
		while ((job_id_ptr = list_next(itr)))
			pack32(job_id_ptr[0], buffer);
		list_iterator_destroy(itr);
	}
}

static void _pre_list_del(void *x)
{
	xfree(x);
}

static int
_unpack_will_run_response_msg(will_run_response_msg_t ** msg_ptr, Buf buffer,
			      uint16_t protocol_version)
{
	will_run_response_msg_t *msg;
	uint32_t count, i, uint32_tmp, *job_id_ptr;

	msg = xmalloc(sizeof(will_run_response_msg_t));
	safe_unpack32(&msg->job_id, buffer);
	safe_unpack32(&msg->proc_cnt, buffer);
	safe_unpack_time(&msg->start_time, buffer);
	safe_unpackstr_xmalloc(&msg->node_list, &uint32_tmp, buffer);

	safe_unpack32(&count, buffer);
	if (count && (count != NO_VAL)) {
		msg->preemptee_job_id = list_create(_pre_list_del);
		for (i=0; i<count; i++) {
			safe_unpack32(&uint32_tmp, buffer);
			job_id_ptr = xmalloc(sizeof(uint32_t));
			job_id_ptr[0] = uint32_tmp;
			list_append(msg->preemptee_job_id, job_id_ptr);
		}
	}

	*msg_ptr = msg;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_will_run_response_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_accounting_update_msg(accounting_update_msg_t *msg,
					Buf buffer,
					uint16_t protocol_version)
{
	uint32_t count = 0;
	ListIterator itr = NULL;
	slurmdb_update_object_t *rec = NULL;

	/* We need to work off the version sent in the message since
	   we might not know what the protocol_version is at this
	   moment (we might not of been updated before other parts of
	   SLURM).

	   IN 14.12 we can remove rpc_version from the mix and just
	   use protocol_version since we standarized them in 14_03.
	*/
	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		if (msg->update_list)
			count = list_count(msg->update_list);

		pack32(count, buffer);

		if (count) {
			itr = list_iterator_create(msg->update_list);
			while ((rec = list_next(itr))) {
				slurmdb_pack_update_object(
					rec, protocol_version, buffer);
			}
			list_iterator_destroy(itr);
		}
	} else {
		pack16(msg->rpc_version, buffer);
		if (msg->update_list)
			count = list_count(msg->update_list);

		pack32(count, buffer);

		if (count) {
			itr = list_iterator_create(msg->update_list);
			while ((rec = list_next(itr))) {
				slurmdb_pack_update_object(
					rec, msg->rpc_version, buffer);
			}
			list_iterator_destroy(itr);
		}
	}
}

static int _unpack_accounting_update_msg(accounting_update_msg_t **msg,
					 Buf buffer,
					 uint16_t protocol_version)
{
	uint32_t count = 0;
	int i = 0;
	accounting_update_msg_t *msg_ptr =
		xmalloc(sizeof(accounting_update_msg_t));
	slurmdb_update_object_t *rec = NULL;

	*msg = msg_ptr;

	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		msg_ptr->update_list = list_create(
			slurmdb_destroy_update_object);
		for (i=0; i<count; i++) {
			if ((slurmdb_unpack_update_object(
				    &rec, protocol_version, buffer))
			   == SLURM_ERROR)
				goto unpack_error;
			list_append(msg_ptr->update_list, rec);
		}
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		/* We need to work off the version sent in the message since
		   we might not know what the protocol_version is at this
		   moment (we might not of been updated before other parts of
		   SLURM).
		*/
		uint16_t rpc_version;
		safe_unpack16(&rpc_version, buffer);
		safe_unpack32(&count, buffer);
		msg_ptr->update_list = list_create(
			slurmdb_destroy_update_object);
		for (i=0; i<count; i++) {
			if ((slurmdb_unpack_update_object(
				    &rec, rpc_version, buffer))
			   == SLURM_ERROR)
				goto unpack_error;
			list_append(msg_ptr->update_list, rec);
		}
	} else {
		error("_unpack_accounting_update_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_accounting_update_msg(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void _pack_topo_info_msg(topo_info_response_msg_t *msg, Buf buffer,
				uint16_t protocol_version)
{
	int i;

	pack32(msg->record_count, buffer);
	for (i=0; i<msg->record_count; i++) {
		pack16(msg->topo_array[i].level,      buffer);
		pack32(msg->topo_array[i].link_speed, buffer);
  		packstr(msg->topo_array[i].name,      buffer);
  		packstr(msg->topo_array[i].nodes,     buffer);
  		packstr(msg->topo_array[i].switches,  buffer);
	}
}

static int _unpack_topo_info_msg(topo_info_response_msg_t **msg,
				 Buf buffer,
				 uint16_t protocol_version)
{
	int i = 0;
	uint32_t uint32_tmp;
	topo_info_response_msg_t *msg_ptr =
		xmalloc(sizeof(topo_info_response_msg_t));

	*msg = msg_ptr;
	safe_unpack32(&msg_ptr->record_count, buffer);
	msg_ptr->topo_array = xmalloc(sizeof(topo_info_t) *
				      msg_ptr->record_count);
	for (i=0; i<msg_ptr->record_count; i++) {
		safe_unpack16(&msg_ptr->topo_array[i].level,      buffer);
		safe_unpack32(&msg_ptr->topo_array[i].link_speed, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->topo_array[i].name,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->topo_array[i].nodes,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->topo_array[i].switches,
				       &uint32_tmp, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_topo_info_msg(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

static void _pack_spank_env_request_msg(spank_env_request_msg_t * msg,
					Buf buffer, uint16_t protocol_version)
{
	xassert(msg != NULL);

	pack32(msg->job_id, buffer);
}

static int _unpack_spank_env_request_msg(spank_env_request_msg_t ** msg_ptr,
					 Buf buffer, uint16_t protocol_version)
{
	spank_env_request_msg_t *msg;

	xassert(msg_ptr != NULL);
	msg = xmalloc(sizeof(spank_env_request_msg_t));
	*msg_ptr = msg;

	safe_unpack32(&msg->job_id, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_spank_env_request_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_spank_env_responce_msg(spank_env_responce_msg_t * msg,
					 Buf buffer, uint16_t protocol_version)
{
	xassert(msg != NULL);

	packstr_array(msg->spank_job_env, msg->spank_job_env_size, buffer);
}

static int _unpack_spank_env_responce_msg(spank_env_responce_msg_t ** msg_ptr,
					  Buf buffer, uint16_t protocol_version)
{
	spank_env_responce_msg_t *msg;

	xassert(msg_ptr != NULL);
	msg = xmalloc(sizeof(spank_env_responce_msg_t));
	*msg_ptr = msg;

	safe_unpackstr_array(&msg->spank_job_env, &msg->spank_job_env_size,
			     buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_spank_env_responce_msg(msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

static void _pack_stats_request_msg(stats_info_request_msg_t *msg, Buf buffer,
				    uint16_t protocol_version)
{
	xassert ( msg != NULL );

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack16(msg->command_id, buffer);
	} else {
		error("_pack_stats_request_msg: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

static int  _unpack_stats_request_msg(stats_info_request_msg_t **msg_ptr,
				      Buf buffer, uint16_t protocol_version)
{
	stats_info_request_msg_t * msg;
	xassert ( msg_ptr != NULL );

	msg = xmalloc ( sizeof(stats_info_request_msg_t) );
	*msg_ptr = msg ;
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&msg->command_id, buffer);
	} else {
		error(" _unpack_stats_request_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	info("SIM: unpack_stats_request_msg error");
	*msg_ptr = NULL;
	slurm_free_stats_info_request_msg(msg);
	return SLURM_ERROR;
}

static int  _unpack_stats_response_msg(stats_info_response_msg_t **msg_ptr,
				       Buf buffer, uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	stats_info_response_msg_t * msg;
	xassert ( msg_ptr != NULL );

	msg = xmalloc ( sizeof (stats_info_response_msg_t) );
	*msg_ptr = msg ;

	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		safe_unpack32(&msg->parts_packed,	buffer);
		if (msg->parts_packed) {
			safe_unpack_time(&msg->req_time,	buffer);
			safe_unpack_time(&msg->req_time_start,	buffer);
			safe_unpack32(&msg->server_thread_count,buffer);
			safe_unpack32(&msg->agent_queue_size,	buffer);
			safe_unpack32(&msg->jobs_submitted,	buffer);
			safe_unpack32(&msg->jobs_started,	buffer);
			safe_unpack32(&msg->jobs_completed,	buffer);
			safe_unpack32(&msg->jobs_canceled,	buffer);
			safe_unpack32(&msg->jobs_failed,	buffer);

			safe_unpack32(&msg->schedule_cycle_max,	buffer);
			safe_unpack32(&msg->schedule_cycle_last,buffer);
			safe_unpack32(&msg->schedule_cycle_sum,	buffer);
			safe_unpack32(&msg->schedule_cycle_counter, buffer);
			safe_unpack32(&msg->schedule_cycle_depth, buffer);
			safe_unpack32(&msg->schedule_queue_len,	buffer);

			safe_unpack32(&msg->bf_backfilled_jobs,	buffer);
			safe_unpack32(&msg->bf_last_backfilled_jobs, buffer);
			safe_unpack32(&msg->bf_cycle_counter,	buffer);
			safe_unpack32(&msg->bf_cycle_sum,	buffer);
			safe_unpack32(&msg->bf_cycle_last,	buffer);
			safe_unpack32(&msg->bf_last_depth,	buffer);
			safe_unpack32(&msg->bf_last_depth_try,	buffer);

			safe_unpack32(&msg->bf_queue_len,	buffer);
			safe_unpack32(&msg->bf_cycle_max,	buffer);
			safe_unpack_time(&msg->bf_when_last_cycle, buffer);
			safe_unpack32(&msg->bf_depth_sum,	buffer);
			safe_unpack32(&msg->bf_depth_try_sum,	buffer);
			safe_unpack32(&msg->bf_queue_len_sum,	buffer);
			safe_unpack32(&msg->bf_active,		buffer);
		}

		safe_unpack32(&msg->rpc_type_size,		buffer);
		safe_unpack16_array(&msg->rpc_type_id,   &uint32_tmp, buffer);
		safe_unpack32_array(&msg->rpc_type_cnt,  &uint32_tmp, buffer);
		safe_unpack64_array(&msg->rpc_type_time, &uint32_tmp, buffer);

		safe_unpack32(&msg->rpc_user_size,		buffer);
		safe_unpack32_array(&msg->rpc_user_id,   &uint32_tmp, buffer);
		safe_unpack32_array(&msg->rpc_user_cnt,  &uint32_tmp, buffer);
		safe_unpack64_array(&msg->rpc_user_time, &uint32_tmp, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&msg->parts_packed,	buffer);
		if (msg->parts_packed) {
			safe_unpack_time(&msg->req_time,	buffer);
			safe_unpack_time(&msg->req_time_start,	buffer);
			safe_unpack32(&msg->server_thread_count,buffer);
			safe_unpack32(&msg->agent_queue_size,	buffer);
			safe_unpack32(&msg->jobs_submitted,	buffer);
			safe_unpack32(&msg->jobs_started,	buffer);
			safe_unpack32(&msg->jobs_completed,	buffer);
			safe_unpack32(&msg->jobs_canceled,	buffer);
			safe_unpack32(&msg->jobs_failed,	buffer);

			safe_unpack32(&msg->schedule_cycle_max,	buffer);
			safe_unpack32(&msg->schedule_cycle_last,buffer);
			safe_unpack32(&msg->schedule_cycle_sum,	buffer);
			safe_unpack32(&msg->schedule_cycle_counter, buffer);
			safe_unpack32(&msg->schedule_cycle_depth, buffer);
			safe_unpack32(&msg->schedule_queue_len,	buffer);

			safe_unpack32(&msg->bf_backfilled_jobs,	buffer);
			safe_unpack32(&msg->bf_last_backfilled_jobs, buffer);
			safe_unpack32(&msg->bf_cycle_counter,	buffer);
			safe_unpack32(&msg->bf_cycle_sum,	buffer);
			safe_unpack32(&msg->bf_cycle_last,	buffer);
			safe_unpack32(&msg->bf_last_depth,	buffer);
			safe_unpack32(&msg->bf_last_depth_try,	buffer);

			safe_unpack32(&msg->bf_queue_len,	buffer);
			safe_unpack32(&msg->bf_cycle_max,	buffer);
			safe_unpack_time(&msg->bf_when_last_cycle, buffer);
			safe_unpack32(&msg->bf_depth_sum,	buffer);
			safe_unpack32(&msg->bf_depth_try_sum,	buffer);
			safe_unpack32(&msg->bf_queue_len_sum,	buffer);
			safe_unpack32(&msg->bf_active,		buffer);
		}
	} else {
		error("_unpack_stats_response_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	info("SIM: unpack_stats_response_msg error");
	*msg_ptr = NULL;
	slurm_free_stats_response_msg(msg);
	return SLURM_ERROR;
}

/* _pack_license_info_request_msg()
 */
static void
_pack_license_info_request_msg(license_info_request_msg_t *msg,
                               Buf buffer,
                               uint16_t protocol_version)
{
	pack_time(msg->last_update, buffer);
	pack16((uint16_t)msg->show_flags, buffer);
}

/* _unpack_license_info_request_msg()
 */
static int
_unpack_license_info_request_msg(license_info_request_msg_t **msg,
                                 Buf buffer,
                                 uint16_t protocol_version)
{
	*msg = xmalloc(sizeof(license_info_msg_t));

	safe_unpack_time(&(*msg)->last_update, buffer);
	safe_unpack16(&(*msg)->show_flags, buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_license_info_request_msg(*msg);
	*msg = NULL;
	return SLURM_ERROR;
}

/* _pack_license_info_msg()
 */
static inline void
_pack_license_info_msg(slurm_msg_t *msg, Buf buffer)
{
	_pack_buffer_msg(msg, buffer);
}

/* _unpack_license_info_msg()
 *
 * Decode the array of license as it comes from the
 * controller and build the API licenses structures
 * as defined in slurm.h
 *
 */
static int
_unpack_license_info_msg(license_info_msg_t **msg,
                         Buf buffer,
                         uint16_t protocol_version)
{
	int i;
	uint32_t zz;

	xassert(msg != NULL);
	*msg = xmalloc(sizeof(license_info_msg_t));

	/* load buffer's header (data structure version and time)
	 */
	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {

		safe_unpack32(&((*msg)->num_lic), buffer);
		safe_unpack_time(&((*msg)->last_update), buffer);

		(*msg)->lic_array = xmalloc(sizeof(slurm_license_info_t)
		                            * (*msg)->num_lic);

		/* Decode individual license data.
		 */
		for (i = 0; i < (*msg)->num_lic; i++) {
			safe_unpackstr_xmalloc(&((*msg)->lic_array[i]).name,
					       &zz, buffer);
			safe_unpack32(&((*msg)->lic_array[i]).total, buffer);
			safe_unpack32(&((*msg)->lic_array[i]).in_use, buffer);
			/* The total number of licenses can decrease
			 * at runtime.
			 */
			if ((*msg)->lic_array[i].total < (*msg)->lic_array[i].in_use)
				(*msg)->lic_array[i].available = 0;
			else
				(*msg)->lic_array[i].available =
					(*msg)->lic_array[i].total -
					(*msg)->lic_array[i].in_use;
			safe_unpack8(&((*msg)->lic_array[i]).remote, buffer);
		}

	} else {
		error("_unpack_license_info_msg: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_free_license_info_msg(*msg);
	*msg = NULL;
	return SLURM_ERROR;
}

static void _pack_job_array_resp_msg(job_array_resp_msg_t *msg, Buf buffer,
				     uint16_t protocol_version)
{
	uint32_t i, cnt = 0;

	if (!msg) {
		pack32(cnt, buffer);
		return;
	}

	pack32(msg->job_array_count, buffer);
	for (i = 0; i < msg->job_array_count; i++) {
		pack32(msg->error_code[i], buffer);
  		packstr(msg->job_array_id[i], buffer);
	}
}
static int  _unpack_job_array_resp_msg(job_array_resp_msg_t **msg, Buf buffer,
				       uint16_t protocol_version)
{
	job_array_resp_msg_t *resp;
	uint32_t i, uint32_tmp;

	resp = xmalloc(sizeof(job_array_resp_msg_t));
	safe_unpack32(&resp->job_array_count, buffer);
	resp->error_code   = xmalloc(sizeof(uint32_t) * resp->job_array_count);
	resp->job_array_id = xmalloc(sizeof(char *)   * resp->job_array_count);
	for (i = 0; i < resp->job_array_count; i++) {
		safe_unpack32(&resp->error_code[i], buffer);
		safe_unpackstr_xmalloc(&resp->job_array_id[i], &uint32_tmp,
				       buffer);
	}
	*msg = resp;
	return SLURM_SUCCESS;

unpack_error:
	slurm_free_job_array_resp(resp);
	*msg = NULL;
	return SLURM_ERROR;
}

/* template
   void pack_ ( * msg , Buf buffer )
   {
   xassert ( msg != NULL );

   pack16( msg -> , buffer ) ;
   pack32( msg -> , buffer ) ;
   pack_time( msg -> , buffer );
   packstr ( msg -> , buffer ) ;
   }

   int unpack_ ( ** msg_ptr , Buf buffer )
   {
   uint32_t uint32_tmp;
   * msg ;

   xassert ( msg_ptr != NULL );

   msg = xmalloc ( sizeof ( ) ) ;
   *msg_ptr = msg;

   safe_unpack16( & msg -> , buffer ) ;
   safe_unpack32( & msg -> , buffer ) ;
   safe_unpack_time ( & msg -> , buffer ) ;
   safe_unpackstr_xmalloc ( & msg -> x, & uint32_tmp , buffer ) ;
   return SLURM_SUCCESS;

   unpack_error:
   xfree(msg -> x);
   xfree(msg);
   *msg_ptr = NULL;
   return SLURM_ERROR;
   }
*/
