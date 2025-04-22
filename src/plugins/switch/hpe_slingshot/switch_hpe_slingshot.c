/*****************************************************************************\
 *  switch_hpe_slingshot.c - Library for managing HPE Slingshot networks
 *****************************************************************************
 *  Copyright 2021-2022 Hewlett Packard Enterprise Development LP
 *  Written by David Gloe <david.gloe@hpe.com>
 *  Written by Jim Nordby <james.nordby@hpe.com>
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

#include <sys/stat.h>

#include "src/common/slurm_xlator.h"
#include "src/common/fd.h"
#include "src/common/state_save.h"
#include "src/common/strlcpy.h"

#include "src/slurmctld/slurmctld.h"

#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#include "switch_hpe_slingshot.h"

typedef struct switch_stepinfo switch_stepinfo_t;

/*
 * These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
extern slurm_conf_t slurm_conf __attribute__((weak_import));
#else
slurm_conf_t slurm_conf;
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
const char plugin_name[] = "switch HPE Slingshot plugin";
const char plugin_type[] = "switch/hpe_slingshot";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;
const uint32_t plugin_id = SWITCH_PLUGIN_SLINGSHOT;

bool active_outside_ctld = false;

slingshot_state_t slingshot_state;    /* VNI min/max/last/bitmap */
slingshot_config_t slingshot_config;  /* Configuration defaults */

/*
 * Set up slingshot_state defaults
 */
static void _state_defaults(void)
{
	memset(&slingshot_state, 0, sizeof(slingshot_state_t));
	slingshot_state.version = SLINGSHOT_STATE_VERSION;
	slingshot_state.vni_min = SLINGSHOT_VNI_MIN_DEF;
	slingshot_state.vni_max = SLINGSHOT_VNI_MAX_DEF;
	slingshot_state.vni_last = slingshot_state.vni_min - 1;
	/* Don't set up state->vni_table yet */
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	debug("loaded");
	if (running_in_slurmctld()) {
		_state_defaults();
		if (!slingshot_setup_config(slurm_conf.switch_param))
			return SLURM_ERROR;
	}
	if (running_in_slurmstepd()) {
		if (!slingshot_stepd_init(slurm_conf.switch_param))
			return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	if (running_in_slurmctld() || active_outside_ctld) {
		FREE_NULL_BITMAP(slingshot_state.vni_table);
		xfree(slingshot_state.job_vnis);
		slingshot_fini_collectives();
		slingshot_free_config();
	} else
		slingshot_free_services();
	return SLURM_SUCCESS;
}

/*
 * switch functions for global state save/restore
 */
extern int switch_p_save(void)
{
	uint32_t actual_job_vnis, actual_job_hwcoll;
	int error_code = 0;
	buf_t *state_buf;

	if (!running_in_slurmctld())
		return SLURM_SUCCESS;

	/* Pack state into a buffer */
	state_buf = init_buf(BUF_SIZE);
	pack32(slingshot_state.version, state_buf);
	pack16(slingshot_state.vni_min, state_buf);
	pack16(slingshot_state.vni_max, state_buf);
	pack16(slingshot_state.vni_last, state_buf);
	pack_bit_str_hex(slingshot_state.vni_table, state_buf);

	/* Pack only job_vni slots that are being used */
	actual_job_vnis = 0;
	for (int i = 0; i < slingshot_state.num_job_vnis; i++) {
		if (slingshot_state.job_vnis[i].job_id)
			actual_job_vnis++;
	}
	pack32(actual_job_vnis, state_buf);
	if (actual_job_vnis > 0) {
		for (int i = 0; i < slingshot_state.num_job_vnis; i++) {
			if (slingshot_state.job_vnis[i].job_id) {
				pack32(slingshot_state.job_vnis[i].job_id,
				       state_buf);
				pack16(slingshot_state.job_vnis[i].vni,
				       state_buf);
			}
		}
	}
	/* Pack only job_hwcoll slots that are being used */
	actual_job_hwcoll = 0;
	for (int i = 0; i < slingshot_state.num_job_hwcoll; i++) {
		if (slingshot_state.job_hwcoll[i])
			actual_job_hwcoll++;
	}
	pack32(actual_job_hwcoll, state_buf);
	if (actual_job_hwcoll > 0) {
		for (int i = 0; i < slingshot_state.num_job_hwcoll; i++) {
			if (slingshot_state.job_hwcoll[i])
				pack32(slingshot_state.job_hwcoll[i],
				       state_buf);
		}
	}

	error_code = save_buf_to_state(SLINGSHOT_STATE_FILE, state_buf, NULL);

	FREE_NULL_BUFFER(state_buf);
	return error_code;
}

/*
 * Restore slingshot_state from state file
 */
extern int switch_p_restore(bool recover)
{
	char *state_file;
	struct stat stat_buf;
	uint32_t version;
	buf_t *state_buf;
	int i;

	if (!recover) {
		return SLURM_SUCCESS;
	}

	/* Get state file name */
	state_file = xstrdup(slurm_conf.state_save_location);
	xstrcat(state_file, "/" SLINGSHOT_STATE_FILE);

	/* Return success if file doesn't exist */
	if ((stat(state_file, &stat_buf) == -1) && (errno == ENOENT)) {
		debug("State file %s not found", state_file);
		return SLURM_SUCCESS;
	}

	/* mmap state file */
	state_buf = create_mmap_buf(state_file);
	if (state_buf == NULL) {
		error("Couldn't recover state file %s", state_file);
		goto error;
	}

	/* Validate version (support both version 1 and 2) */
	safe_unpack32(&version, state_buf);
	if (version != SLINGSHOT_STATE_VERSION) {
		if (version != SLINGSHOT_STATE_VERSION_VER1) {
			error("State file %s version %"PRIu32" != %d",
				state_file, version, SLINGSHOT_STATE_VERSION);
			goto error;
		}
	}

	/* Unpack the rest into global state structure */
	slingshot_state.version = version;
	safe_unpack16(&slingshot_state.vni_min, state_buf);
	safe_unpack16(&slingshot_state.vni_max, state_buf);
	safe_unpack16(&slingshot_state.vni_last, state_buf);

	FREE_NULL_BITMAP(slingshot_state.vni_table);
	unpack_bit_str_hex(&slingshot_state.vni_table, state_buf);
	free_vnis = bit_size(slingshot_state.vni_table) -
		bit_set_count(slingshot_state.vni_table);

	safe_unpack32(&slingshot_state.num_job_vnis, state_buf);
	slingshot_state.job_vnis = NULL;
	if (slingshot_state.num_job_vnis > 0) {
		debug("%s: unpacking %u job VNIs",
		      state_file, slingshot_state.num_job_vnis);
		slingshot_state.job_vnis = xcalloc(
			slingshot_state.num_job_vnis, sizeof(job_vni_t));
		for (i = 0; i < slingshot_state.num_job_vnis; i++) {
			safe_unpack32(&slingshot_state.job_vnis[i].job_id,
				state_buf);
			safe_unpack16(&slingshot_state.job_vnis[i].vni,
				state_buf);
		}
	}
	/* Unpack version 2 job_hwcoll state */
	slingshot_state.num_job_hwcoll = 0;
	slingshot_state.job_hwcoll = NULL;
	if (version == SLINGSHOT_STATE_VERSION) {
		safe_unpack32(&slingshot_state.num_job_hwcoll, state_buf);
		if (slingshot_state.num_job_hwcoll > 0) {
			debug("%s: unpacking %u job_hwcoll",
			      state_file, slingshot_state.num_job_hwcoll);
			slingshot_state.job_hwcoll = xcalloc(
				slingshot_state.num_job_hwcoll,
				sizeof(job_vni_t));
			for (i = 0; i < slingshot_state.num_job_hwcoll; i++)
				safe_unpack32(&slingshot_state.job_hwcoll[i],
					state_buf);
		}
	}

	debug("State file %s recovered", state_file);
	FREE_NULL_BUFFER(state_buf);
	xfree(state_file);
	return slingshot_update_vni_table();

unpack_error:
	error("Error unpacking state file %s", state_file);
error:
	FREE_NULL_BUFFER(state_buf);
	xfree(state_file);
	FREE_NULL_BITMAP(slingshot_state.vni_table);
	xfree(slingshot_state.job_vnis);

	return SLURM_ERROR;
}

extern void switch_p_pack_jobinfo(void *switch_jobinfo, buf_t *buffer,
				  uint16_t protocol_version)
{
	slingshot_jobinfo_t *jobinfo = switch_jobinfo;

	xassert(buffer);

	if (protocol_version >= SLURM_24_05_PROTOCOL_VERSION) {
		if (!jobinfo) {
			packbool(0, buffer);
			return;
		}

		packbool(1, buffer);
		pack16_array(jobinfo->vnis, jobinfo->num_vnis, buffer);
		packstr(jobinfo->extra, buffer);
	}
}

extern int switch_p_unpack_jobinfo(void **switch_jobinfo, buf_t *buffer,
				   uint16_t protocol_version)
{
	bool tmp_bool;
	slingshot_jobinfo_t *jobinfo;

	xassert(switch_jobinfo);
	xassert(buffer);

	*switch_jobinfo = jobinfo = xmalloc(sizeof(*jobinfo));

	if (protocol_version >= SLURM_24_05_PROTOCOL_VERSION) {
		safe_unpackbool(&tmp_bool, buffer);
		if (!tmp_bool) {
			slingshot_free_jobinfo(*switch_jobinfo);
			*switch_jobinfo = NULL;
			return SLURM_SUCCESS;
		}

		safe_unpack16_array(&jobinfo->vnis, &jobinfo->num_vnis, buffer);
		safe_unpackstr(&jobinfo->extra, buffer);
	}

	if (running_in_slurmstepd()) {
		/* Update vni table with allocated vnis for stepmgr job */
		active_outside_ctld = true;
		_state_defaults();
		slingshot_setup_config(slurm_conf.switch_param);
		slingshot_update_config(jobinfo);
	}

	return SLURM_SUCCESS;

unpack_error:
	error("error unpacking jobinfo struct");
	slingshot_free_jobinfo(jobinfo);
	*switch_jobinfo = NULL;
	return SLURM_ERROR;
}

static void _copy_stepinfo(slingshot_stepinfo_t *old, slingshot_stepinfo_t *new)
{
	/* Copy static (non-malloced) fields */
	memcpy(new, old, sizeof(slingshot_stepinfo_t));

	/* Copy malloced fields */
	if (old->num_vnis) {
		size_t vnisz = old->num_vnis * sizeof(uint16_t);
		new->vnis = xmalloc(vnisz);
		memcpy(new->vnis, old->vnis, vnisz);
	}

	if (old->num_profiles) {
		size_t profilesz = old->num_profiles *
				   sizeof(slingshot_comm_profile_t);
		new->profiles = xmalloc(profilesz);
		memcpy(new->profiles, old->profiles, profilesz);
	}

	if (old->num_nics) {
		size_t nicsz = old->num_nics * sizeof(slingshot_hsn_nic_t);
		new->nics = xmalloc(nicsz);
		memcpy(new->nics, old->nics, nicsz);
	}

	if (old->hwcoll) {
		size_t hwcollsz = sizeof(slingshot_hwcoll_t);
		new->hwcoll = xmalloc(hwcollsz);
		memcpy(new->hwcoll, old->hwcoll, hwcollsz);
		new->hwcoll->mcast_token = xstrdup(old->hwcoll->mcast_token);
		new->hwcoll->fm_url = xstrdup(old->hwcoll->fm_url);
	}
}

/*
 * Get the slingshot stepinfo structure from a given step record.
 */
static slingshot_stepinfo_t *_get_slingshot_stepinfo(step_record_t *step_ptr)
{
	if (!step_ptr || !step_ptr->switch_step || !step_ptr->switch_step->data)
		return NULL;
	return (slingshot_stepinfo_t *) step_ptr->switch_step->data;
}

/*
 * Copy slingshot stepinfo from the first het step in a non-het job
 */
static bool _copy_het_step_stepinfo(slingshot_stepinfo_t *stepinfo,
				   step_record_t *step_ptr)
{
	slingshot_stepinfo_t *het_stepinfo;
	step_record_t *het_step_ptr;
	job_record_t *job_ptr;
	slurm_step_id_t tmp_step_id;

	/* For the first component, we build the stepinfo, not copy it */
	if (step_ptr->step_id.step_het_comp == 0)
		return false;

	/* Get the step record for the first component */
	job_ptr = step_ptr->job_ptr;
	tmp_step_id.job_id = step_ptr->step_id.job_id,
	tmp_step_id.step_id = step_ptr->step_id.step_id,
	tmp_step_id.step_het_comp = 0,

	het_step_ptr = find_step_record(job_ptr, &tmp_step_id);
	het_stepinfo = _get_slingshot_stepinfo(het_step_ptr);

	/* Copy it to the current step */
	if (het_stepinfo) {
		log_flag(SWITCH, "Copying slingshot stepinfo from %pS to %pS",
			 het_step_ptr, step_ptr);
		_copy_stepinfo(het_stepinfo, stepinfo);
		return true;
	}
	return false;
}

/*
 * Copy slingshot stepinfo from the first het step in a het job
 */
static bool _copy_het_job_stepinfo(slingshot_stepinfo_t *stepinfo,
				  step_record_t *step_ptr)
{
	slingshot_stepinfo_t *het_stepinfo = NULL;
	job_record_t *het_job_leader, *het_job_ptr, *job_ptr;
	step_record_t *het_step_ptr;
	list_itr_t *job_iter;

	job_ptr = step_ptr->job_ptr;
	if (!(het_job_leader = find_job_record(job_ptr->het_job_id)))
		return false;

	/* Loop through all heterogeneous job components */
	job_iter = list_iterator_create(het_job_leader->het_job_list);
	while ((het_job_ptr = list_next(job_iter))) {
		slurm_step_id_t tmp_step_id = {
			.job_id = het_job_ptr->job_id,
			.step_id = step_ptr->step_id.step_id,
			.step_het_comp = NO_VAL,
		};

		/*
		 * If we get here without finding an existing stepinfo, we must
		 * be the first component, so there's nothing to copy from.
		 */
		if (job_ptr->job_id == het_job_ptr->job_id) {
			list_iterator_destroy(job_iter);
			return false;
		}

		/* Look for a step in this job matching our step id */
		het_step_ptr = find_step_record(het_job_ptr, &tmp_step_id);
		het_stepinfo = _get_slingshot_stepinfo(het_step_ptr);

		/* Copy it to the current step */
		if (het_stepinfo) {
			log_flag(SWITCH,
				 "Copying slingshot stepinfo from %pS to %pS",
				 het_step_ptr, step_ptr);
			_copy_stepinfo(het_stepinfo, stepinfo);
			list_iterator_destroy(job_iter);
			return true;
		}
	}
	list_iterator_destroy(job_iter);
	return false;
}

/*
 * Get the node count for a het step in a het job
 */
static uint32_t _get_het_job_node_cnt(step_record_t *step_ptr)
{
	hostlist_t *hl;
	job_record_t *het_job_leader, *het_job_ptr, *job_ptr;
	list_itr_t *job_iter;
	uint32_t node_cnt;

	job_ptr = step_ptr->job_ptr;
	if (!(het_job_leader = find_job_record(job_ptr->het_job_id)))
		return job_ptr->node_cnt;

	/* Loop through all heterogeneous job components */
	hl = hostlist_create(NULL);
	job_iter = list_iterator_create(het_job_leader->het_job_list);
	while ((het_job_ptr = list_next(job_iter)))
		hostlist_push(hl, het_job_ptr->nodes);
	list_iterator_destroy(job_iter);

	/* Remove duplicates in the list */
	hostlist_uniq(hl);
	node_cnt = hostlist_count(hl);
	hostlist_destroy(hl);
	return node_cnt;
}

extern int switch_p_build_stepinfo(switch_stepinfo_t **switch_job,
				   slurm_step_layout_t *step_layout,
				   step_record_t *step_ptr)
{
	slingshot_stepinfo_t *job = NULL;
	job_record_t *job_ptr;
	uint32_t job_id;
	uint32_t node_cnt;

	if (!step_ptr) {
		fatal("%s: step_ptr NULL not supported", __func__);
	}
	job_ptr = step_ptr->job_ptr;
	xassert(job_ptr);
	log_flag(SWITCH, "job_id=%u step_id=%u uid=%u network='%s'",
		step_ptr->step_id.job_id, step_ptr->step_id.step_id,
		job_ptr->user_id, step_ptr->network);

	job = xmalloc(sizeof(*job));
	job->version = SLURM_PROTOCOL_VERSION;
	*switch_job = (switch_stepinfo_t *) job;

	/*
	 * If this is a homogeneous step, or the first component in a
	 * heterogeneous step, get the job ID, node list, and node count to use.
	 *
	 * Note that for heterogeneous steps, at the point this function is
	 * called, the nodelist isn't available for all step components.
	 * Without an accurate nodelist Instant On won't work, so we skip it.
	 *
	 * If this is not the first component in a heterogeneous step, copy the
	 * stepinfo struct from the first component.
	 */
	if (job_ptr->het_job_id) {
		/* This is a het step in a het job */
		if (_copy_het_job_stepinfo(job, step_ptr))
			return SLURM_SUCCESS;

		node_cnt = _get_het_job_node_cnt(step_ptr);
		job_id = job_ptr->het_job_id;
	} else if (step_ptr->step_id.step_het_comp != NO_VAL) {
		/* This is a het step in a non-het job */
		if (_copy_het_step_stepinfo(job, step_ptr))
			return SLURM_SUCCESS;

		node_cnt = job_ptr->node_cnt;
		job_id = job_ptr->job_id;
	} else {
		/* This is a non-het step in a non-het job */
		node_cnt = step_layout->node_cnt;
		job_id = job_ptr->job_id;
	}

	/* Do VNI allocation/traffic classes/network limits */
	if (!slingshot_setup_job_step_vni(job, node_cnt, job_id,
					  step_ptr->network, job_ptr->network))
		return SLURM_ERROR;

	/*
	 * Reserve hardware collectives multicast addresses if configured
	 */
	if ((job_ptr->bit_flags & STEPMGR_ENABLED) &&
	    !slingshot_setup_collectives(job, node_cnt, job_id,
					 step_ptr->step_id.step_id))
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

extern void switch_p_duplicate_stepinfo(switch_stepinfo_t *tmp,
					switch_stepinfo_t **dest)
{
	slingshot_stepinfo_t *old = (slingshot_stepinfo_t *) tmp;
	slingshot_stepinfo_t *new = xmalloc(sizeof(*new));

	_copy_stepinfo(old, new);

	*dest = (switch_stepinfo_t *) new;
}

extern void switch_p_free_stepinfo(switch_stepinfo_t *switch_job)
{
	slingshot_stepinfo_t *stepinfo = (slingshot_stepinfo_t *) switch_job;
	xassert(stepinfo);
	xfree(stepinfo->vnis);
	xfree(stepinfo->profiles);
	xfree(stepinfo->nics);
	if (stepinfo->hwcoll) {
		xfree(stepinfo->hwcoll->mcast_token);
		xfree(stepinfo->hwcoll->fm_url);
		xfree(stepinfo->hwcoll);
	}
	xfree(stepinfo);
}

static void _pack_slingshot_limits(slingshot_limits_t *limits, buf_t *buffer)
{
	pack16(limits->max, buffer);
	pack16(limits->res, buffer);
	pack16(limits->def, buffer);
}

static bool _unpack_slingshot_limits(slingshot_limits_t *limits, buf_t *buffer)
{
	safe_unpack16(&limits->max, buffer);
	safe_unpack16(&limits->res, buffer);
	safe_unpack16(&limits->def, buffer);
	return true;

unpack_error:
	return false;
}

static void _pack_comm_profile(slingshot_comm_profile_t *profile, buf_t *buffer)
{
	pack32(profile->svc_id, buffer);
	pack16(profile->vnis[0], buffer);
	pack16(profile->vnis[1], buffer);
	pack16(profile->vnis[2], buffer);
	pack16(profile->vnis[3], buffer);
	pack32(profile->tcs, buffer);
	packstr(profile->device_name, buffer);
}

static void _pack_hsn_nic(slingshot_hsn_nic_t *nic, buf_t *buffer)
{
	pack32(nic->nodeidx, buffer);
	pack32(nic->address_type, buffer);
	packstr(nic->address, buffer);
	pack16(nic->numa_node, buffer);
	packstr(nic->device_name, buffer);
}

static void _pack_hwcoll(const slingshot_hwcoll_t *hwcoll, buf_t *buffer)
{
	/* Pack a boolean to let unpack routine know that hwcoll exists */
	if (hwcoll) {
		packbool(true, buffer);
		pack32(hwcoll->job_id, buffer);
		pack32(hwcoll->step_id, buffer);
		packstr(hwcoll->mcast_token, buffer);
		packstr(hwcoll->fm_url, buffer);
		pack32(hwcoll->addrs_per_job, buffer);
		pack32(hwcoll->num_nodes, buffer);
	} else {
		packbool(false, buffer);
	}
}

static bool _unpack_comm_profile(slingshot_comm_profile_t *profile,
				 buf_t *buffer)
{
	char *device_name;

	safe_unpack32(&profile->svc_id, buffer);
	safe_unpack16(&profile->vnis[0], buffer);
	safe_unpack16(&profile->vnis[1], buffer);
	safe_unpack16(&profile->vnis[2], buffer);
	safe_unpack16(&profile->vnis[3], buffer);
	safe_unpack32(&profile->tcs, buffer);

	safe_unpackstr(&device_name, buffer);
	if (!device_name)
		goto unpack_error;
	strlcpy(profile->device_name, device_name,
		sizeof(profile->device_name));
	xfree(device_name);

	return true;

unpack_error:
	return false;
}

static bool _unpack_hsn_nic(slingshot_hsn_nic_t *nic, buf_t *buffer)
{
	char *address, *device_name;

	safe_unpack32(&nic->nodeidx, buffer);
	safe_unpack32(&nic->address_type, buffer);

	safe_unpackstr(&address, buffer);
	if (!address)
		goto unpack_error;
	strlcpy(nic->address, address, sizeof(nic->address));
	xfree(address);

	safe_unpack16(&nic->numa_node, buffer);

	safe_unpackstr(&device_name, buffer);
	if (!device_name)
		goto unpack_error;
	strlcpy(nic->device_name, device_name, sizeof(nic->device_name));
	xfree(device_name);

	return true;

unpack_error:
	return false;
}

static bool _unpack_hwcoll(slingshot_hwcoll_t **hwcollp, buf_t *buffer)
{
	slingshot_hwcoll_t *hwcoll = NULL;
	bool got_hwcoll = false;

	*hwcollp = NULL;
	/* Unpack a boolean to see if hwcoll is packed in the buffer */
	safe_unpackbool(&got_hwcoll, buffer);
	if (got_hwcoll) {
		hwcoll = xmalloc(sizeof(*hwcoll));

		safe_unpack32(&hwcoll->job_id, buffer);
		safe_unpack32(&hwcoll->step_id, buffer);

		safe_unpackstr(&hwcoll->mcast_token, buffer);
		if (!hwcoll->mcast_token)
			goto unpack_error;

		safe_unpackstr(&hwcoll->fm_url, buffer);
		if (!hwcoll->fm_url)
			goto unpack_error;

		safe_unpack32(&hwcoll->addrs_per_job, buffer);
		safe_unpack32(&hwcoll->num_nodes, buffer);

		*hwcollp = hwcoll;
	}

	return true;

unpack_error:
	return false;
}

extern void switch_p_pack_stepinfo(switch_stepinfo_t *switch_job, buf_t *buffer,
				   uint16_t protocol_version)
{
	uint32_t pidx;
	slingshot_stepinfo_t *stepinfo = (slingshot_stepinfo_t *)switch_job;

	xassert(buffer);

	if (protocol_version >= SLURM_23_11_PROTOCOL_VERSION) {
		/* nothing to pack, pack special "null" version number */
		if (!stepinfo ||
		    (stepinfo->version == SLINGSHOT_JOBINFO_NULL_VERSION)) {
			debug("Nothing to pack");
			pack32(SLINGSHOT_JOBINFO_NULL_VERSION, buffer);
			return;
		}

		pack32(protocol_version, buffer);
		pack16_array(stepinfo->vnis, stepinfo->num_vnis, buffer);
		pack32(stepinfo->tcs, buffer);
		_pack_slingshot_limits(&stepinfo->limits.txqs, buffer);
		_pack_slingshot_limits(&stepinfo->limits.tgqs, buffer);
		_pack_slingshot_limits(&stepinfo->limits.eqs, buffer);
		_pack_slingshot_limits(&stepinfo->limits.cts, buffer);
		_pack_slingshot_limits(&stepinfo->limits.tles, buffer);
		_pack_slingshot_limits(&stepinfo->limits.ptes, buffer);
		_pack_slingshot_limits(&stepinfo->limits.les, buffer);
		_pack_slingshot_limits(&stepinfo->limits.acs, buffer);
		pack32(stepinfo->depth, buffer);
		pack32(stepinfo->num_profiles, buffer);
		for (pidx = 0; pidx < stepinfo->num_profiles; pidx++) {
			_pack_comm_profile(&stepinfo->profiles[pidx], buffer);
		}
		pack32(stepinfo->flags, buffer);
		pack32(stepinfo->num_nics, buffer);
		for (pidx = 0; pidx < stepinfo->num_nics; pidx++) {
			_pack_hsn_nic(&stepinfo->nics[pidx], buffer);
		}
		_pack_hwcoll(stepinfo->hwcoll, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		/* nothing to pack, pack special "null" version number */
		if (!stepinfo ||
		    (stepinfo->version == SLINGSHOT_JOBINFO_NULL_VERSION)) {
			debug("Nothing to pack");
			pack32(SLINGSHOT_JOBINFO_NULL_VERSION, buffer);
			return;
		}

		pack32(protocol_version, buffer);
		pack16_array(stepinfo->vnis, stepinfo->num_vnis, buffer);
		pack32(stepinfo->tcs, buffer);
		_pack_slingshot_limits(&stepinfo->limits.txqs, buffer);
		_pack_slingshot_limits(&stepinfo->limits.tgqs, buffer);
		_pack_slingshot_limits(&stepinfo->limits.eqs, buffer);
		_pack_slingshot_limits(&stepinfo->limits.cts, buffer);
		_pack_slingshot_limits(&stepinfo->limits.tles, buffer);
		_pack_slingshot_limits(&stepinfo->limits.ptes, buffer);
		_pack_slingshot_limits(&stepinfo->limits.les, buffer);
		_pack_slingshot_limits(&stepinfo->limits.acs, buffer);
		pack32(stepinfo->depth, buffer);
		pack32(stepinfo->num_profiles, buffer);
		for (pidx = 0; pidx < stepinfo->num_profiles; pidx++) {
			_pack_comm_profile(&stepinfo->profiles[pidx], buffer);
		}
		pack_bit_str_hex(NULL, buffer); /* formerly vni_pids, Unused */
		pack32(stepinfo->flags, buffer);
		pack32(stepinfo->num_nics, buffer);
		for (pidx = 0; pidx < stepinfo->num_nics; pidx++) {
			_pack_hsn_nic(&stepinfo->nics[pidx], buffer);
		}
	} else {
		/* invalid protocol specified */
		xassert(false);
	}
}

extern int switch_p_unpack_stepinfo(switch_stepinfo_t **switch_job,
				    buf_t *buffer, uint16_t protocol_version)
{
	uint32_t pidx = 0;
	slingshot_stepinfo_t *stepinfo;

	if (!switch_job) {
		debug("switch_job was NULL");
		return SLURM_SUCCESS;
	}

	xassert(buffer);

	stepinfo = xmalloc(sizeof(*stepinfo));
	*switch_job = (switch_stepinfo_t *) stepinfo;

	if (protocol_version >= SLURM_23_11_PROTOCOL_VERSION) {
		safe_unpack32(&stepinfo->version, buffer);
		if (stepinfo->version == SLINGSHOT_JOBINFO_NULL_VERSION) {
			debug("Nothing to unpack");
			return SLURM_SUCCESS;
		}
		if (stepinfo->version != protocol_version) {
			error("SLINGSHOT stepinfo version %"PRIu32" != %d",
			      stepinfo->version, protocol_version);
			goto error;
		}

		safe_unpack16_array(&stepinfo->vnis, &stepinfo->num_vnis, buffer);
		safe_unpack32(&stepinfo->tcs, buffer);
		if (!_unpack_slingshot_limits(&stepinfo->limits.txqs, buffer) ||
		    !_unpack_slingshot_limits(&stepinfo->limits.tgqs, buffer) ||
		    !_unpack_slingshot_limits(&stepinfo->limits.eqs, buffer) ||
		    !_unpack_slingshot_limits(&stepinfo->limits.cts, buffer) ||
		    !_unpack_slingshot_limits(&stepinfo->limits.tles, buffer) ||
		    !_unpack_slingshot_limits(&stepinfo->limits.ptes, buffer) ||
		    !_unpack_slingshot_limits(&stepinfo->limits.les, buffer) ||
		    !_unpack_slingshot_limits(&stepinfo->limits.acs, buffer))
			goto unpack_error;

		safe_unpack32(&stepinfo->depth, buffer);
		safe_unpack32(&stepinfo->num_profiles, buffer);
		stepinfo->profiles = xcalloc(stepinfo->num_profiles,
					    sizeof(slingshot_comm_profile_t));
		for (pidx = 0; pidx < stepinfo->num_profiles; pidx++) {
			if (!_unpack_comm_profile(&stepinfo->profiles[pidx],
						  buffer))
				goto unpack_error;
		}
		safe_unpack32(&stepinfo->flags, buffer);

		safe_unpack32(&stepinfo->num_nics, buffer);
		stepinfo->nics = xcalloc(stepinfo->num_nics,
					sizeof(slingshot_hsn_nic_t));
		for (pidx = 0; pidx < stepinfo->num_nics; pidx++) {
			if (!_unpack_hsn_nic(&stepinfo->nics[pidx], buffer))
				goto unpack_error;
		}
		_unpack_hwcoll(&stepinfo->hwcoll, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		bitstr_t *vni_pids = NULL;
		safe_unpack32(&stepinfo->version, buffer);
		if (stepinfo->version == SLINGSHOT_JOBINFO_NULL_VERSION) {
			debug("Nothing to unpack");
			return SLURM_SUCCESS;
		}
		if (stepinfo->version != protocol_version) {
			error("SLINGSHOT stepinfo version %"PRIu32" != %d",
			      stepinfo->version, protocol_version);
			goto error;
		}

		safe_unpack16_array(&stepinfo->vnis, &stepinfo->num_vnis, buffer);
		safe_unpack32(&stepinfo->tcs, buffer);
		if (!_unpack_slingshot_limits(&stepinfo->limits.txqs, buffer) ||
		    !_unpack_slingshot_limits(&stepinfo->limits.tgqs, buffer) ||
		    !_unpack_slingshot_limits(&stepinfo->limits.eqs, buffer) ||
		    !_unpack_slingshot_limits(&stepinfo->limits.cts, buffer) ||
		    !_unpack_slingshot_limits(&stepinfo->limits.tles, buffer) ||
		    !_unpack_slingshot_limits(&stepinfo->limits.ptes, buffer) ||
		    !_unpack_slingshot_limits(&stepinfo->limits.les, buffer) ||
		    !_unpack_slingshot_limits(&stepinfo->limits.acs, buffer))
			goto unpack_error;

		safe_unpack32(&stepinfo->depth, buffer);
		safe_unpack32(&stepinfo->num_profiles, buffer);
		stepinfo->profiles = xcalloc(stepinfo->num_profiles,
					    sizeof(slingshot_comm_profile_t));
		for (pidx = 0; pidx < stepinfo->num_profiles; pidx++) {
			if (!_unpack_comm_profile(&stepinfo->profiles[pidx],
						  buffer))
				goto unpack_error;
		}
		unpack_bit_str_hex(&vni_pids, buffer); /* Unused */
		FREE_NULL_BITMAP(vni_pids);
		safe_unpack32(&stepinfo->flags, buffer);

		safe_unpack32(&stepinfo->num_nics, buffer);
		stepinfo->nics = xcalloc(stepinfo->num_nics,
					sizeof(slingshot_hsn_nic_t));
		for (pidx = 0; pidx < stepinfo->num_nics; pidx++) {
			if (!_unpack_hsn_nic(&stepinfo->nics[pidx], buffer))
				goto unpack_error;
		}
		/* Not present in this version, set to none */
		stepinfo->hwcoll = NULL;
	} else {
		error("invalid protocol version");
		goto error;
	}

	return SLURM_SUCCESS;

unpack_error:
	error("error unpacking stepinfo struct");
error:
	switch_p_free_stepinfo(*switch_job);
	*switch_job = NULL;
	return SLURM_ERROR;
}

/*
 * Set up CXI Services for each of the CXI NICs on this host
 */
extern int switch_p_job_preinit(stepd_step_rec_t *step)
{
	xassert(step);
	slingshot_stepinfo_t *stepinfo = step->switch_step->data;
	xassert(stepinfo);
	int step_cpus = step->node_tasks * step->cpus_per_task;
	if (!slingshot_create_services(stepinfo, step->uid, step_cpus,
				       step->step_id.job_id))
		return SLURM_ERROR;
	if (!create_slingshot_apinfo(step))
		return SLURM_ERROR;
	return SLURM_SUCCESS;
}

/*
 * Privileged.
 */
extern int switch_p_job_init(stepd_step_rec_t *step)
{
	return SLURM_SUCCESS;
}

/*
 * Destroy CXI Services for each of the CXI NICs on this host
 */
extern int switch_p_job_postfini(stepd_step_rec_t *step)
{
	xassert(step);

	uid_t pgid = step->jmgr_pid;
	/*
	 *  Kill all processes in the job's session
	 */
	if (pgid) {
		debug2("Sending SIGKILL to pgid %lu", (unsigned long) pgid);
		kill(-pgid, SIGKILL);
	} else
		debug("%ps: Bad pid value %lu", &step->step_id,
		      (unsigned long) pgid);

	remove_slingshot_apinfo(step);

	slingshot_stepinfo_t *stepinfo;
	stepinfo = (slingshot_stepinfo_t *) step->switch_step->data;
	xassert(stepinfo);
	if (!slingshot_destroy_services(stepinfo, step->step_id.job_id))
		return SLURM_ERROR;
	return SLURM_SUCCESS;
}

/*
 * Set up environment variables for job step: each environment variable
 * represents data from one or more CXI services, separated by commas.
 * In addition, the SLINGSHOT_VNIS variable has one or more VNIs
 * separated by colons.
 */
extern int switch_p_job_attach(switch_stepinfo_t *stepinfo, char ***env,
			       uint32_t nodeid, uint32_t procid,
			       uint32_t nnodes, uint32_t nprocs, uint32_t rank)
{
	slingshot_stepinfo_t *job = (slingshot_stepinfo_t *)stepinfo;
	int pidx, vidx;
	char *svc_ids = NULL, *vnis = NULL, *devices = NULL, *tcss = NULL;

	if (job->num_profiles == 0)
		return SLURM_SUCCESS;

	/* svc_ids and devices are per-device, comma-separated */
	for (pidx = 0; pidx < job->num_profiles; pidx++) {
		char *sep = pidx ? "," : "";
		slingshot_comm_profile_t *profile = &job->profiles[pidx];
		xstrfmtcat(svc_ids, "%s%u", sep, profile->svc_id);
		xstrfmtcat(devices, "%s%s", sep, profile->device_name);
	}

	/* vnis are global (all services share VNIs), comma-separated */
	for (vidx = 0; vidx < job->num_vnis; vidx++)
		xstrfmtcat(vnis, "%s%hu", vidx ? "," : "", job->vnis[vidx]);

	/* traffic classes are global */
	xstrfmtcat(tcss, "0x%x", job->profiles[0].tcs);

	log_flag(SWITCH, "%s=%s %s=%s %s=%s %s=%s",
		SLINGSHOT_SVC_IDS_ENV, svc_ids, SLINGSHOT_VNIS_ENV, vnis,
		SLINGSHOT_DEVICES_ENV, devices, SLINGSHOT_TCS_ENV, tcss);

	env_array_overwrite(env, SLINGSHOT_SVC_IDS_ENV, svc_ids);
	env_array_overwrite(env, SLINGSHOT_VNIS_ENV, vnis);
	env_array_overwrite(env, SLINGSHOT_DEVICES_ENV, devices);
	env_array_overwrite(env, SLINGSHOT_TCS_ENV, tcss);

	xfree(svc_ids);
	xfree(vnis);
	xfree(devices);
	xfree(tcss);

	/* Add any collectives-related environment variables */
	slingshot_collectives_env(job, env);
	return SLURM_SUCCESS;
}

extern int switch_p_job_step_complete(switch_stepinfo_t *stepinfo, char *nodelist)
{
	slingshot_stepinfo_t *job = (slingshot_stepinfo_t *) stepinfo;

	/*
	 * job will not be set for any jobs running before the
	 * switch plugin was enabled
	 */
	if (job) {
		/* Free job step VNI */
		slingshot_free_job_step_vni(job);

		/* Release any hardware collectives multicast addresses */
		slingshot_release_collectives_job_step(job);
	}

	return SLURM_SUCCESS;
}

extern void switch_p_job_start(job_record_t *job_ptr)
{
	if (!(job_ptr->bit_flags & STEPMGR_ENABLED))
		return;

	if (!slingshot_setup_job_vni_pool(job_ptr))
		error("couldn't allocate vni pool for job %pJ", job_ptr);
}

/*
 * Free any job VNIs, as well as any Slingshot hardware collectives
 * multicast addresses associated with the job
 */
extern void switch_p_job_complete(job_record_t *job_ptr)
{
	uint32_t job_id = job_ptr->job_id;

	/* Free any job VNIs */
	xassert(running_in_slurmctld() || active_outside_ctld);
	log_flag(SWITCH, "switch_p_job_complete(%u)", job_id);
	slingshot_free_job_vni(job_id);

	slingshot_free_job_vni_pool(job_ptr->switch_jobinfo);
	slingshot_free_jobinfo(job_ptr->switch_jobinfo);
	job_ptr->switch_jobinfo = NULL;
}

extern int switch_p_fs_init(stepd_step_rec_t *step)
{
	return SLURM_SUCCESS;
}

extern void switch_p_extern_stepinfo(switch_stepinfo_t **stepinfo,
				     job_record_t *job_ptr)
{
	/* not supported */
}

extern void switch_p_extern_step_fini(int job_id)
{
	slingshot_release_collectives_job(job_id);
}
