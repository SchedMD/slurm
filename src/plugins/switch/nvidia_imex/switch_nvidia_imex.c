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
#include "src/common/xstring.h"
#include "src/interfaces/gres.h"
#include "src/interfaces/switch.h"

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
	uint32_t magic;
	uint32_t channel;
} switch_info_t;

static uint32_t channel_count = 2048;
static bitstr_t *imex_channels = NULL;

static switch_info_t *_create_info(uint32_t channel)
{
	switch_info_t *new = xmalloc(sizeof(*new));
	new->magic = SWITCH_INFO_MAGIC;
	new->channel = channel;
	return new;
}

static void _setup_controller(void)
{
	char *tmp_str = NULL;

	if ((tmp_str = conf_get_opt_str(slurm_conf.switch_param,
					"imex_channel_count="))) {
		channel_count = atoi(tmp_str);
		xfree(tmp_str);
	}

	log_flag(SWITCH, "managing %u channels", channel_count);

	imex_channels = bit_alloc(channel_count);
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

extern int fini(void)
{
	return SLURM_SUCCESS;
}

extern int switch_p_save(void)
{
	/*
	 * Skip managing our own state file, just recover the allocations
	 * data from the job_list after restart.
	 */
	return SLURM_SUCCESS;
}

static int _mark_used(void *x, void *arg)
{
	job_record_t *job_ptr = x;
	switch_info_t *switch_info = job_ptr->switch_jobinfo;

	if (!switch_info)
		return 1;

	if (switch_info->channel < channel_count) {
		debug("marking channel %u used by %pJ",
		      switch_info->channel, job_ptr);
		bit_set(imex_channels, switch_info->channel);
	} else {
		error("%s: channel %u outside of tracked range, ignoring",
		      plugin_type, switch_info->channel);
	}

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

extern void switch_p_pack_jobinfo(switch_info_t *switch_info, buf_t *buffer,
				  uint16_t protocol_version)
{
	log_flag(SWITCH, "channel %u",
		 (switch_info ? switch_info->channel : NO_VAL));

	if (protocol_version >= SLURM_24_05_PROTOCOL_VERSION) {
		if (!switch_info) {
			pack32(NO_VAL, buffer);
			return;
		}

		xassert(switch_info->magic == SWITCH_INFO_MAGIC);
		pack32(switch_info->channel, buffer);
	}
}

extern int switch_p_unpack_jobinfo(switch_info_t **switch_info, buf_t *buffer,
				   uint16_t protocol_version)
{
	uint32_t channel = NO_VAL;

	*switch_info = NULL;

	if (protocol_version >= SLURM_24_05_PROTOCOL_VERSION) {
		safe_unpack32(&channel, buffer);
	}

	if (channel != NO_VAL)
		*switch_info = _create_info(channel);

	log_flag(SWITCH, "channel %u", channel);

	return SLURM_SUCCESS;

unpack_error:
	error("%s: unpack error", __func__);
	return SLURM_ERROR;
}

extern int switch_p_build_stepinfo(switch_info_t **switch_step,
				   slurm_step_layout_t *step_layout,
				   step_record_t *step_ptr)
{
	if (step_ptr->job_ptr && step_ptr->job_ptr->switch_jobinfo) {
		switch_info_t *jobinfo = step_ptr->job_ptr->switch_jobinfo;
		*switch_step = _create_info(jobinfo->channel);
		log_flag(SWITCH, "using channel %u for %pS",
			 jobinfo->channel, step_ptr);
	} else {
		log_flag(SWITCH, "no channel for %pS", step_ptr);
	}

	return SLURM_SUCCESS;
}

extern void switch_p_duplicate_stepinfo(switch_info_t *orig,
					switch_info_t **dest)
{
	if (orig)
		*dest = _create_info(orig->channel);
}

extern void switch_p_free_stepinfo(switch_info_t *switch_step)
{
	xfree(switch_step);
}

extern void switch_p_pack_stepinfo(switch_info_t *switch_step, buf_t *buffer,
				   uint16_t protocol_version)
{
	switch_p_pack_jobinfo(switch_step, buffer, protocol_version);
}

extern int switch_p_unpack_stepinfo(switch_info_t **switch_step, buf_t *buffer,
				    uint16_t protocol_version)
{
	return switch_p_unpack_jobinfo(switch_step, buffer, protocol_version);
}

extern int switch_p_job_preinit(stepd_step_rec_t *step)
{
	return SLURM_SUCCESS;
}

extern int switch_p_job_init(stepd_step_rec_t *step)
{
	if (xstrcasestr(slurm_conf.job_container_plugin, "tmpfs")) {
		error("%s: %s: skipping due incompatibility with job_container/tmpfs",
		      plugin_type, __func__);
		return SLURM_SUCCESS;
	}

	if (step->switch_step && step->switch_step->data) {
		switch_info_t *switch_info = step->switch_step->data;

		if (switch_info->channel != NO_VAL)
			return setup_imex_channel(switch_info->channel, true);
	}

	return SLURM_SUCCESS;
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

extern void switch_p_job_start(job_record_t *job_ptr)
{
	static bool first_alloc = true;
	int channel = -1;

	/*
	 * FIXME: this is hacked in here as switch_p_restore() is called
	 * before the job_list has been repopulated. Instead, before we
	 * allocate any new channels, scan the job_list to work out which
	 * are already in use.
	 */
	if (first_alloc) {
		list_for_each(job_list, _mark_used, NULL);
		first_alloc = false;
	}

	channel = bit_ffc(imex_channels);

	if (channel > 0) {
		debug("allocating channel %d to %pJ", channel, job_ptr);
		bit_set(imex_channels, channel);
		job_ptr->switch_jobinfo = _create_info(channel);
	} else {
		error("%s: %s: no channel available",
		      plugin_type, __func__);
	}
}

extern void switch_p_job_complete(job_record_t *job_ptr)
{
	switch_info_t *switch_jobinfo = job_ptr->switch_jobinfo;

	if (!switch_jobinfo)
		return;

	if (switch_jobinfo->channel < channel_count) {
		debug("marking channel %u released by %pJ",
		      switch_jobinfo->channel, job_ptr);
		bit_clear(imex_channels, switch_jobinfo->channel);
		xfree(job_ptr->switch_jobinfo);
	} else {
		error("%s: %s: channel %u outside of tracked range, ignoring release",
		      plugin_type, __func__, switch_jobinfo->channel);
	}
}

extern int switch_p_fs_init(stepd_step_rec_t *step)
{
	if (step->switch_step && step->switch_step->data) {
		switch_info_t *switch_info = step->switch_step->data;
		if (switch_info->channel != NO_VAL)
			return setup_imex_channel(switch_info->channel, false);
	}

	return SLURM_SUCCESS;
}

extern void switch_p_extern_stepinfo(switch_info_t **stepinfo,
				     job_record_t *job_ptr)
{
	if (job_ptr->switch_jobinfo) {
                switch_info_t *jobinfo = job_ptr->switch_jobinfo;
                *stepinfo = _create_info(jobinfo->channel);
                log_flag(SWITCH, "using channel %u for %pJ",
                         jobinfo->channel, job_ptr);
	}
}

extern void switch_p_extern_step_fini(int job_id)
{
	/* not supported */
}
