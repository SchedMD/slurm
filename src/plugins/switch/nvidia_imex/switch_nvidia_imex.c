/*****************************************************************************\
 *  switch_nvidia_imex.c
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

#include <stdbool.h>
#include <stdint.h>

#include "src/common/slurm_xlator.h"

#include "src/common/bitstring.h"
#include "src/common/list.h"
#include "src/common/pack.h"
#include "src/common/run_in_daemon.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/xstring.h"
#include "src/interfaces/gres.h"
#include "src/interfaces/switch.h"
#include "src/interfaces/topology.h"

#include "src/plugins/switch/nvidia_imex/imex_device.h"

#if defined(__APPLE__)
extern list_t *job_list __attribute__((weak_import));
#else
list_t *job_list;
#endif

 /*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "switch" for Slurm switch) and <method> is a description
 * of how this plugin satisfies that application.  Slurm will only load
 * a switch plugin if the plugin_type string has a prefix of "switch/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "switch NVIDIA IMEX plugin";
const char plugin_type[] = "switch/nvidia_imex";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;
const uint32_t plugin_id = SWITCH_PLUGIN_NVIDIA_IMEX;

#define SWITCH_INFO_MAGIC 0xFF00FF00

typedef struct {
	uint32_t id;
	char *node_list;
} channel_t;

typedef struct {
	list_t *channel_list;
	uint32_t magic;
} switch_info_t;

typedef struct {
	list_t **channel_list;
	job_record_t *job_ptr;
	int *rc_ptr;
	bool test_only;
} allocate_channel_args_t;

static uint32_t max_channel_count = 2048;
static bitstr_t *imex_channels = NULL;

static void _channel_free(
	void *x)
{
	channel_t *channel = x;

	if (!channel)
		return;

	xfree(channel->node_list);
	xfree(channel);
}

static switch_info_t *_create_info(
	list_t *channel_list)
{
	switch_info_t *new = xmalloc(sizeof(*new));
	new->magic = SWITCH_INFO_MAGIC;

	if (channel_list)
		new->channel_list = list_shallow_copy(channel_list);
	else
		new->channel_list = list_create(_channel_free);

	return new;
}

static void _setup_controller(void)
{
	char *tmp_str = NULL;

	if ((tmp_str = conf_get_opt_str(slurm_conf.switch_param,
					"imex_channel_count="))) {
		max_channel_count = atoi(tmp_str);
		xfree(tmp_str);
	}

	log_flag(SWITCH, "managing %u channels", max_channel_count);

	/* Allocate one extra space for the '0' index bit to always be set */
	imex_channels = bit_alloc(max_channel_count + 1);

	/* Reserve channel 0 */
	bit_set(imex_channels, 0);
}

extern int init(void)
{
	if (running_in_slurmctld())
		_setup_controller();
	else if (running_in_slurmd())
		return slurmd_init();
	else if (running_in_slurmstepd())
		return stepd_init();

	return SLURM_SUCCESS;
}

extern void fini(void)
{
	return;
}

extern int switch_p_save(void)
{
	/*
	 * Skip managing our own state file, just recover the allocations
	 * data from the job_list after restart.
	 */
	return SLURM_SUCCESS;
}

static int _mark_used_channel(
	void *x,
	void *arg)
{
	channel_t *channel = x;
	job_record_t *job_ptr = arg;

	if (IS_JOB_FINISHED(job_ptr)) {
		log_flag(SWITCH, "finished %pJ was using channel id %u, not marking as used.",
			 job_ptr, channel->id);
		return 1;
	}

	if (channel->id <= max_channel_count) {
		log_flag(SWITCH, "marking channel id %u used by %pJ",
		      channel->id, job_ptr);
		bit_set(imex_channels, channel->id);
	} else {
		error("%s: channel id %u outside of tracked range, ignoring",
		      plugin_type, channel->id);
	}

	return 1;
}

static int _mark_used_channels_in_job(
	void *x,
	void *arg)
{
	job_record_t *job_ptr = x;
	switch_info_t *switch_info = job_ptr->switch_jobinfo;

	if (!switch_info || !switch_info->channel_list)
		return 1;

	list_for_each(switch_info->channel_list, _mark_used_channel, job_ptr);

	return 1;
}

extern int switch_p_restore(bool recover)
{
	/*
	 * FIXME: this is run too soon at slurmctld startup to be used here.
	 * See switch_p_job_start() for the current workaround.
	 */
	return SLURM_SUCCESS;
}

void _pack_channel(
	void *object,
	uint16_t protocol_version,
	buf_t *buffer)
{
	channel_t *channel = object;

	if (protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		pack32(channel->id, buffer);
		packstr(channel->node_list, buffer);
		log_flag(SWITCH, "channel id %u allocated to nodes '%s'",
			 channel->id, channel->node_list);
	}
}

int _unpack_channel(
	void **object,
	uint16_t protocol_version,
	buf_t *buffer)
{
	channel_t *channel = xmalloc(sizeof(*channel));

	if (protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		safe_unpack32(&(channel->id), buffer);
		safe_unpackstr(&(channel->node_list), buffer);
		log_flag(SWITCH, "channel id %u allocated to nodes '%s'",
			 channel->id, channel->node_list);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	*object = channel;

	return SLURM_SUCCESS;

unpack_error:
	_channel_free(channel);

	return SLURM_ERROR;
}

extern void switch_p_jobinfo_pack(switch_info_t *switch_info, buf_t *buffer,
				  uint16_t protocol_version)
{
	if (protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		list_t *channel_list = NULL;

		if (switch_info) {
			xassert(switch_info->magic == SWITCH_INFO_MAGIC);
			channel_list = switch_info->channel_list;
		}

		slurm_pack_list(channel_list, _pack_channel, buffer,
				protocol_version);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		channel_t *channel;
		uint32_t channel_id = NO_VAL;

		/*
		 * Only pack the first channel in the channel list. There should
		 * only be one channel allocated to older versioned jobs
		 * anyways.
		 */
		if (switch_info && switch_info->channel_list &&
		    (channel = list_peek(switch_info->channel_list))) {
			xassert(switch_info->magic == SWITCH_INFO_MAGIC);
			xassert((list_count(switch_info->channel_list) == 1));
			channel_id = channel->id;
		}

		log_flag(SWITCH, "channel id %u", channel_id);

		pack32(channel_id, buffer);
	}
}

extern int switch_p_jobinfo_unpack(switch_info_t **switch_info, buf_t *buffer,
				   uint16_t protocol_version)
{
	xassert(switch_info);

	*switch_info = xmalloc(sizeof(**switch_info));
	(*switch_info)->magic = SWITCH_INFO_MAGIC;

	if (protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		if (slurm_unpack_list(&((*switch_info)->channel_list),
				      _unpack_channel, _channel_free, buffer,
				      protocol_version))
			goto unpack_error;
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		channel_t *channel;
		uint32_t channel_id = NO_VAL;

		*switch_info = NULL;

		safe_unpack32(&channel_id, buffer);

		if (channel_id != NO_VAL) {
			*switch_info = _create_info(NULL);
			channel = xmalloc(sizeof(*channel));
			channel->id = channel_id;
			list_append((*switch_info)->channel_list, channel);
		}

		log_flag(SWITCH, "channel id %u", channel_id);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	error("%s: unpack error", __func__);
	return SLURM_ERROR;
}

/* Used to free switch_jobinfo when switch_p_job_complete can't be used */
extern void switch_p_jobinfo_free(job_record_t *job_ptr)
{
	if (job_ptr->switch_jobinfo) {
		switch_info_t *switch_jobinfo = job_ptr->switch_jobinfo;

		xassert(switch_jobinfo->magic == SWITCH_INFO_MAGIC);
		FREE_NULL_LIST(switch_jobinfo->channel_list);
	}
	xfree(job_ptr->switch_jobinfo);
	job_ptr->switch_jobinfo = NULL;
}

static int _log_channel_job(
	void *x,
	void *arg)
{
	channel_t *channel = x;
	job_record_t *job_ptr = arg;

	log_flag(SWITCH, "using channel id %u for %pJ",
		 channel->id, job_ptr);

	return 1;
}

extern int switch_p_stepinfo_build(switch_info_t **switch_step,
				   switch_info_t *switch_jobinfo,
				   step_record_t *step_ptr)
{
	if (!switch_jobinfo) {
		return SLURM_SUCCESS;
	}

	if (!switch_jobinfo->channel_list)
		return SLURM_SUCCESS;

	/* Copy job channel list to step switch info */
	*switch_step = _create_info(switch_jobinfo->channel_list);

	return SLURM_SUCCESS;
}

extern void switch_p_stepinfo_duplicate(switch_info_t *orig,
					switch_info_t **dest)
{
	if (!orig)
		return;

	*dest = _create_info(orig->channel_list);
}

extern void switch_p_stepinfo_free(switch_info_t *switch_step)
{
	if (switch_step) {
		xassert(switch_step->magic == SWITCH_INFO_MAGIC);
		FREE_NULL_LIST(switch_step->channel_list);
	}
	xfree(switch_step);
}

extern void switch_p_stepinfo_pack(switch_info_t *switch_step, buf_t *buffer,
				   uint16_t protocol_version)
{
	switch_p_jobinfo_pack(switch_step, buffer, protocol_version);
}

extern int switch_p_stepinfo_unpack(switch_info_t **switch_step, buf_t *buffer,
				    uint16_t protocol_version)
{
	return switch_p_jobinfo_unpack(switch_step, buffer, protocol_version);
}

extern int switch_p_job_preinit(stepd_step_rec_t *step)
{
	return SLURM_SUCCESS;
}

static int _find_channel(
	void *x,
	void *arg)
{
	channel_t *channel = x;
	char *node_name_key = arg;
	char *node_name;
	hostlist_t *node_hostlist;
	int rc = 0;

	if (!channel->node_list) {
		log_flag(SWITCH, "Channel id %u has no node list, using this channel.",
			 channel->id);
		return 1;
	}

	node_hostlist = hostlist_create(channel->node_list);
	while ((node_name = hostlist_shift(node_hostlist))) {
		if (!xstrcmp(node_name, node_name_key)) {
			log_flag(SWITCH, "Node name %s found in node list %s, using channel id %u",
				 node_name_key, channel->node_list,
				 channel->id);
			rc = 1;
		}
		free(node_name);

		if (rc)
			break;
	}
	FREE_NULL_HOSTLIST(node_hostlist);

	return rc;
}

static int _stepd_setup_imex_channel(
	stepd_step_rec_t *step)
{
	switch_info_t *switch_info;
	channel_t *channel;

	if (!step->switch_step || !(switch_info = step->switch_step->data) ||
	    !switch_info->channel_list ||
	    !list_count(switch_info->channel_list)) {
		log_flag(SWITCH, "No channel info provided, no IMEX channel will be setup.");
		return SLURM_SUCCESS;
	}

	channel = list_find_first(switch_info->channel_list, _find_channel,
				  step->node_name);

	if (!channel || (channel->id == NO_VAL)) {
		log_flag(SWITCH, "No channel found for this node, '%s', no IMEX channel will be setup.",
                         step->node_name);
		return SLURM_SUCCESS;
	}

	return setup_imex_channel(channel->id, true);
}

extern int switch_p_job_init(stepd_step_rec_t *step)
{
	if (xstrcasestr(slurm_conf.job_container_plugin, "tmpfs")) {
		error("%s: %s: skipping due incompatibility with job_container/tmpfs",
		      plugin_type, __func__);
		return SLURM_SUCCESS;
	}

	log_flag(SWITCH, "%s: Running IMEX channel setup", __func__);

	return _stepd_setup_imex_channel(step);
}

extern int switch_p_job_postfini(stepd_step_rec_t *step)
{
	return SLURM_SUCCESS;
}

extern int switch_p_job_attach(switch_info_t *stepinfo, char ***env,
			       uint32_t nodeid, uint32_t procid,
			       uint32_t nnodes, uint32_t nprocs, uint32_t rank)
{
	return SLURM_SUCCESS;
}

extern int switch_p_job_step_complete(switch_info_t *stepinfo, char *nodelist)
{
	return SLURM_SUCCESS;
}

static int _release_channel(
	void *x,
	void *arg)
{
	channel_t *channel = x;
	job_record_t *job_ptr = arg;

	if (channel->id <= max_channel_count) {
		log_flag(SWITCH, "marking channel id %u released by %pJ", channel->id, job_ptr);
		bit_clear(imex_channels, channel->id);
	} else {
		error("%s: %s: channel id %u outside of tracked range, ignoring release",
		      plugin_type, __func__, channel->id);
	}

	return 1;
}

static void _allocate_channel(
	allocate_channel_args_t *args,
	char *node_list)
{
	int id = bit_ffc(imex_channels);
	channel_t *channel;
	list_t **channel_list = args->channel_list;
	job_record_t *job_ptr = args->job_ptr;
	int *rc_ptr = args->rc_ptr;
	bool test_only = args->test_only;

	if (test_only) {
		xassert(!channel_list);

		if (id < 0) {
			*rc_ptr = SLURM_ERROR;
			job_ptr->state_reason = WAIT_NVIDIA_IMEX_CHANNELS;
		}
		return;
	}

	if (id > 0) {
		log_flag(SWITCH, "allocating channel %d to %pJ with node_list %s",
		      id, job_ptr, node_list);

		channel = xmalloc(sizeof(*channel));
		channel->id = id;
		channel->node_list = xstrdup(node_list);

		bit_set(imex_channels, channel->id);
		list_append(*channel_list, channel);
	} else {
		error("%s: %s: no more IMEX channels available, releasing all allocated channels for %pJ",
		      plugin_type, __func__, job_ptr);

		list_for_each(*channel_list, _release_channel, job_ptr);
		FREE_NULL_LIST(*channel_list);
	}
}

static int _allocate_channel_per_segment(
	void *x,
	void *arg)
{
	char *node_list = x;
	allocate_channel_args_t *args = arg;

	_allocate_channel(arg, node_list);

	if ((*args->rc_ptr) != SLURM_SUCCESS)
		return -1;

	return 1;
}

extern int switch_p_job_start(job_record_t *job_ptr, bool test_only)
{
	static bool first_alloc = true;
	list_t *segment_list = NULL;
	switch_info_t *switch_jobinfo;
	int rc = SLURM_SUCCESS;

	allocate_channel_args_t args = {
		.job_ptr = job_ptr,
		.rc_ptr = &rc,
		.test_only = test_only,
	};

	/*
	 * FIXME: this is hacked in here as switch_p_restore() is called
	 * before the job_list has been repopulated. Instead, before we
	 * allocate any new channels, scan the job_list to work out which
	 * are already in use.
	 */
	if (first_alloc) {
		list_for_each(job_list, _mark_used_channels_in_job, NULL);
		first_alloc = false;
	}

	if (!test_only) {
		job_ptr->switch_jobinfo = switch_jobinfo = _create_info(NULL);
		args.channel_list = &switch_jobinfo->channel_list;
	}

	log_flag(SWITCH, "%s: Starting %pJ", __func__, job_ptr);

	if (job_ptr->start_protocol_ver <= SLURM_25_05_PROTOCOL_VERSION) {
		/*
		 * Remove this case when 25.05 support is no longer supported.
		 *
		 * Older versioned slurmstepd's are only expecting one channel
		 * for the job and would not be able to determine which channel
		 * to use. Here only one channel is allocated as only one
		 * channel can be packed for older versions anyways.
		 */
		log_flag(SWITCH, "%s: Allocating only one channel for %pJ with older protocol version %d",
			 __func__, job_ptr, job_ptr->start_protocol_ver);
		_allocate_channel(&args, NULL);
	} else if (xstrstr("unique-channel-per-segment", job_ptr->network) &&
		   job_ptr->topo_jobinfo &&
		   (topology_g_jobinfo_get(TOPO_JOBINFO_SEGMENT_LIST,
					   job_ptr->topo_jobinfo,
					   &segment_list) == SLURM_SUCCESS) &&
		   segment_list && list_count(segment_list)) {
		/*
		 * Allocate one channel for each segment in the job.
		 */
		(void) list_for_each(segment_list,
				     _allocate_channel_per_segment, &args);
	} else {
		/*
		 * Allocate one channel for the entire job.
		 */
		_allocate_channel(&args, NULL);
	}

	if (test_only)
		return rc;

	if ((slurm_conf.debug_flags & DEBUG_FLAG_SWITCH) &&
	    (rc == SLURM_SUCCESS) && switch_jobinfo->channel_list) {
		list_for_each(switch_jobinfo->channel_list, _log_channel_job,
			      job_ptr);
	}

	return rc;
}

extern void switch_p_job_complete(job_record_t *job_ptr)
{
	switch_info_t *switch_jobinfo = job_ptr->switch_jobinfo;

	if (!switch_jobinfo || !switch_jobinfo->channel_list)
		return;

	list_for_each(switch_jobinfo->channel_list, _release_channel, job_ptr);
}

extern int switch_p_fs_init(stepd_step_rec_t *step)
{
	log_flag(SWITCH, "%s: Running IMEX channel setup", __func__);

	return _stepd_setup_imex_channel(step);
}

extern void switch_p_extern_stepinfo(switch_info_t **stepinfo,
				     job_record_t *job_ptr)
{
	switch_info_t *jobinfo;

	if (!(jobinfo = job_ptr->switch_jobinfo) || !(jobinfo->channel_list)) {
		log_flag(SWITCH, "no channels for %pJ", job_ptr);
		return;
	}

	log_flag(SWITCH, "%s: Creating extern step info for %pJ",
		 __func__, job_ptr);

	/* Copy job channel list to step switch info */
	*stepinfo = _create_info(jobinfo->channel_list);

	if (slurm_conf.debug_flags & DEBUG_FLAG_SWITCH) {
		list_for_each(jobinfo->channel_list, _log_channel_job, job_ptr);
	}
}

extern void switch_p_extern_step_fini(int job_id)
{
	/* not supported */
}
