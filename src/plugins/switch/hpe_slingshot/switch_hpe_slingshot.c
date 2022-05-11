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
#include <sys/types.h>
#include <unistd.h>

#include "src/common/slurm_xlator.h"
#include "src/common/strlcpy.h"

#include "switch_hpe_slingshot.h"

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
	if (running_in_slurmctld())
		_state_defaults();
	return SLURM_SUCCESS;
}

extern int fini(void)
{
/*FIXME definition doesn't belong here: */
	extern int switch_p_libstate_clear(void);

	if (running_in_slurmctld())
		switch_p_libstate_clear();
	else
		slingshot_free_services();
	return SLURM_SUCCESS;
}


/*
 * Called at slurmctld startup, or when re-reading slurm.conf
 * NOTE: assumed that this runs _after_ switch_p_libstate_restore(),
 * and slingshot_state may or may not already be filled in
 */
extern int switch_p_reconfig(void)
{
	if (running_in_slurmctld()) {
		if (!slingshot_setup_config(slurm_conf.switch_param))
			return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

/*
 * switch functions for global state save/restore
 */
extern int switch_p_libstate_save(char *dir_name)
{
	int state_fd;
	uint32_t actual_job_vnis;
	buf_t *state_buf;
	char *new_state_file, *state_file, *buf;
	size_t buflen;
	ssize_t nwrote;

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
	debug("%s: packing %u/%u job VNIs",
	       state_file, actual_job_vnis, slingshot_state.num_job_vnis);
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

	/* Get file names for the current and new state files */
	new_state_file = xstrdup(dir_name);
	xstrcat(new_state_file, "/" SLINGSHOT_STATE_FILE_NEW);
	state_file = xstrdup(dir_name);
	xstrcat(state_file, "/" SLINGSHOT_STATE_FILE);

	/* Write buffer to new state file */
	state_fd = creat(new_state_file, 0600);
	if (state_fd == -1) {
		error("Couldn't create %s for writing: %m", new_state_file);
		goto error;
	}

	buflen = get_buf_offset(state_buf);
	buf = get_buf_data(state_buf);
	nwrote = write(state_fd, buf, buflen);
	if (nwrote == -1) {
		error("Couldn't write to %s: %m", new_state_file);
		goto error;
	} else if (nwrote < buflen) {
		error("Wrote %zu of %zu bytes to %s", nwrote, buflen,
			new_state_file);
		goto error;
	}

	/* Overwrite the current state file with rename */
	if (rename(new_state_file, state_file) == -1) {
		error("Couldn't rename %s to %s: %m", new_state_file,
			state_file);
		goto error;
	}

	debug("State file %s saved", state_file);
	close(state_fd);
	free_buf(state_buf);
	xfree(new_state_file);
	xfree(state_file);
	return SLURM_SUCCESS;

error:
	close(state_fd);
	free_buf(state_buf);
	unlink(new_state_file);
	xfree(new_state_file);
	xfree(state_file);
	return SLURM_ERROR;
}

/*
 * Restore slingshot_state from state file
 * NOTE: assumes this runs before loading the slurm.conf config
 */
extern int switch_p_libstate_restore(char *dir_name, bool recover)
{
	char *state_file;
	struct stat stat_buf;
	uint32_t version;
	buf_t *state_buf;
	int i;

	/* If we're not recovering state, just set up defaults */
	if (!recover) {
		_state_defaults();
		return SLURM_SUCCESS;
	}

	/* Get state file name */
	state_file = xstrdup(dir_name);
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

	/* Validate version */
	safe_unpack32(&version, state_buf);
	if (version != SLINGSHOT_STATE_VERSION) {
		error("State file %s version %"PRIu32" != %d", state_file,
			version, SLINGSHOT_STATE_VERSION);
		goto error;
	}

	/* Unpack the rest into global state structure */
	slingshot_state.version = version;
	safe_unpack16(&slingshot_state.vni_min, state_buf);
	safe_unpack16(&slingshot_state.vni_max, state_buf);
	safe_unpack16(&slingshot_state.vni_last, state_buf);
	unpack_bit_str_hex(&slingshot_state.vni_table, state_buf);
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

	debug("State file %s recovered", state_file);
	free_buf(state_buf);
	xfree(state_file);
	return SLURM_SUCCESS;

unpack_error:
	error("Error unpacking state file %s", state_file);
error:
	free_buf(state_buf);
	xfree(state_file);
	if (slingshot_state.vni_table)
		bit_free(slingshot_state.vni_table);
	xfree(slingshot_state.job_vnis);

	return SLURM_ERROR;
}

extern int switch_p_libstate_clear(void)
{
	log_flag(SWITCH, "vni_table=%p job_vnis=%p num_job_vnis=%u",
		slingshot_state.vni_table, slingshot_state.job_vnis,
		slingshot_state.num_job_vnis);

	if (slingshot_state.vni_table)
		bit_free(slingshot_state.vni_table);
	xfree(slingshot_state.job_vnis);

	return SLURM_SUCCESS;
}

/*
 * switch functions for job step specific credential
 */
extern int switch_p_alloc_jobinfo(switch_jobinfo_t **switch_job,
				  uint32_t job_id, uint32_t step_id)
{
	slingshot_jobinfo_t *new = xmalloc(sizeof(*new));

	xassert(switch_job);

	new->version = SLINGSHOT_JOBINFO_VERSION;
	*switch_job = (switch_jobinfo_t *)new;

	return SLURM_SUCCESS;
}

extern int switch_p_build_jobinfo(switch_jobinfo_t *switch_job,
				  slurm_step_layout_t *step_layout,
				  step_record_t *step_ptr)
{
	slingshot_jobinfo_t *job = (slingshot_jobinfo_t *) switch_job;

	if (!step_ptr) {
		fatal("switch_p_build_jobinfo: step_ptr NULL not supported");
	}
	xassert(step_ptr->job_ptr);
	log_flag(SWITCH, "job_id=%u step_id=%u uid=%u network='%s'",
		step_ptr->step_id.job_id, step_ptr->step_id.step_id,
		step_ptr->job_ptr->user_id, step_ptr->network);

	if (!job) {
		debug("switch_job was NULL");
		return SLURM_SUCCESS;
	}
	xassert(job->version == SLINGSHOT_JOBINFO_VERSION);

	/* Do VNI allocation/traffic classes/network limits */
	if (!slingshot_setup_job_step(job, step_layout->node_cnt,
				      step_ptr->step_id.job_id,
				      step_ptr->network))
		return SLURM_ERROR;
	return SLURM_SUCCESS;
}

extern int switch_p_duplicate_jobinfo(switch_jobinfo_t *tmp,
				      switch_jobinfo_t **dest)
{
	slingshot_jobinfo_t *old = (slingshot_jobinfo_t *) tmp;
	slingshot_jobinfo_t *new = xmalloc(sizeof(*new));

	/* Copy static (non-malloced) fields */
	memcpy(new, old, sizeof(slingshot_jobinfo_t));

	/* Copy malloced fields */
	if (old->num_vnis > 0) {
		size_t vnisz = old->num_vnis * sizeof(uint16_t);
		new->vnis = xmalloc(vnisz);
		memcpy(new->vnis, old->vnis, vnisz);
	}

	if (old->num_profiles > 0) {
		size_t profilesz = old->num_profiles *
				   sizeof(pals_comm_profile_t);
		new->profiles = xmalloc(profilesz);
		memcpy(new->profiles, old->profiles, profilesz);
	}

	*dest = (switch_jobinfo_t *)new;
	return SLURM_SUCCESS;
}

extern void switch_p_free_jobinfo(switch_jobinfo_t *switch_job)
{
	slingshot_jobinfo_t *jobinfo = (slingshot_jobinfo_t *) switch_job;
	xassert(jobinfo);
	xfree(jobinfo->vnis);
	xfree(jobinfo->profiles);
	xfree(jobinfo);
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

static void _pack_comm_profile(pals_comm_profile_t *profile, buf_t *buffer)
{
	pack32(profile->svc_id, buffer);
	pack16(profile->vnis[0], buffer);
	pack16(profile->vnis[1], buffer);
	pack16(profile->vnis[2], buffer);
	pack16(profile->vnis[3], buffer);
	pack32(profile->tcs, buffer);
	packstr(profile->device_name, buffer);
}

static bool _unpack_comm_profile(pals_comm_profile_t *profile, buf_t *buffer)
{
	char *device_name;
	uint32_t name_len;

	safe_unpack32(&profile->svc_id, buffer);
	safe_unpack16(&profile->vnis[0], buffer);
	safe_unpack16(&profile->vnis[1], buffer);
	safe_unpack16(&profile->vnis[2], buffer);
	safe_unpack16(&profile->vnis[3], buffer);
	safe_unpack32(&profile->tcs, buffer);

	safe_unpackstr_xmalloc(&device_name, &name_len, buffer);
	strlcpy(profile->device_name, device_name,
		sizeof(profile->device_name));

	return true;

unpack_error:
	return false;
}

extern int switch_p_pack_jobinfo(switch_jobinfo_t *switch_job, buf_t *buffer,
				 uint16_t protocol_version)
{
	uint32_t pidx;
	slingshot_jobinfo_t *jobinfo = (slingshot_jobinfo_t *)switch_job;

	xassert(buffer);

	/* Nothing to pack, pack special "null" version number */
	if (!jobinfo ||
	    (jobinfo->version == SLINGSHOT_JOBINFO_NULL_VERSION)) {
		debug("Nothing to pack");
		pack32(SLINGSHOT_JOBINFO_NULL_VERSION, buffer);
		return SLURM_SUCCESS;
	}

	xassert(jobinfo->version == SLINGSHOT_JOBINFO_VERSION);
	pack32(jobinfo->version, buffer);
	pack16_array(jobinfo->vnis, jobinfo->num_vnis, buffer);
	pack32(jobinfo->tcs, buffer);
	_pack_slingshot_limits(&jobinfo->limits.txqs, buffer);
	_pack_slingshot_limits(&jobinfo->limits.tgqs, buffer);
	_pack_slingshot_limits(&jobinfo->limits.eqs, buffer);
	_pack_slingshot_limits(&jobinfo->limits.cts, buffer);
	_pack_slingshot_limits(&jobinfo->limits.tles, buffer);
	_pack_slingshot_limits(&jobinfo->limits.ptes, buffer);
	_pack_slingshot_limits(&jobinfo->limits.les, buffer);
	_pack_slingshot_limits(&jobinfo->limits.acs, buffer);
	pack32(jobinfo->depth, buffer);
	pack32(jobinfo->num_profiles, buffer);
	for (pidx = 0; pidx < jobinfo->num_profiles; pidx++) {
		_pack_comm_profile(&jobinfo->profiles[pidx], buffer);
	}

	return SLURM_SUCCESS;
}

extern int switch_p_unpack_jobinfo(switch_jobinfo_t **switch_job, buf_t *buffer,
				   uint16_t protocol_version)
{
	uint32_t pidx = 0;
	slingshot_jobinfo_t *jobinfo;

	if (!switch_job) {
		debug("switch_job was NULL");
		return SLURM_SUCCESS;
	}

	xassert(buffer);

	jobinfo = xmalloc(sizeof(*jobinfo));
	*switch_job = (switch_jobinfo_t *)jobinfo;

	safe_unpack32(&jobinfo->version, buffer);
	if (jobinfo->version == SLINGSHOT_JOBINFO_NULL_VERSION) {
		debug("Nothing to unpack");
		return SLURM_SUCCESS;
	}
	if (jobinfo->version != SLINGSHOT_JOBINFO_VERSION) {
		error("SLINGSHOT jobinfo version %"PRIu32" != %d",
			jobinfo->version, SLINGSHOT_JOBINFO_VERSION);
		goto error;
	}

	safe_unpack16_array(&jobinfo->vnis, &jobinfo->num_vnis, buffer);
	safe_unpack32(&jobinfo->tcs, buffer);
	if (!_unpack_slingshot_limits(&jobinfo->limits.txqs, buffer) ||
	    !_unpack_slingshot_limits(&jobinfo->limits.tgqs, buffer) ||
	    !_unpack_slingshot_limits(&jobinfo->limits.eqs, buffer) ||
	    !_unpack_slingshot_limits(&jobinfo->limits.cts, buffer) ||
	    !_unpack_slingshot_limits(&jobinfo->limits.tles, buffer) ||
	    !_unpack_slingshot_limits(&jobinfo->limits.ptes, buffer) ||
	    !_unpack_slingshot_limits(&jobinfo->limits.les, buffer) ||
	    !_unpack_slingshot_limits(&jobinfo->limits.acs, buffer))
		goto unpack_error;

	safe_unpack32(&jobinfo->depth, buffer);
	safe_unpack32(&jobinfo->num_profiles, buffer);
	jobinfo->profiles = xcalloc(jobinfo->num_profiles,
				    sizeof(pals_comm_profile_t));
	for (pidx = 0; pidx < jobinfo->num_profiles; pidx++) {
		if (!_unpack_comm_profile(&jobinfo->profiles[pidx], buffer))
			goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	error("error unpacking jobinfo struct");
error:
	switch_p_free_jobinfo(*switch_job);
	*switch_job = NULL;
	return SLURM_ERROR;
}

/*
 * Set up CXI Services for each of the CXI NICs on this host
 */
extern int switch_p_job_preinit(stepd_step_rec_t *job)
{
	xassert(job);
	slingshot_jobinfo_t *jobinfo = job->switch_job->data;
	xassert(jobinfo);
	int step_cpus = job->node_tasks * job->cpus_per_task;
	if (!slingshot_create_services(jobinfo, job->uid, step_cpus))
		return SLURM_ERROR;
	return SLURM_SUCCESS;
}

/*
 * Privileged, but no jobinfo
 */
extern int switch_p_job_init(stepd_step_rec_t *job)
{
	return SLURM_SUCCESS;
}

extern int switch_p_job_suspend_test(switch_jobinfo_t *jobinfo)
{
	return SLURM_SUCCESS;
}

extern void switch_p_job_suspend_info_get(switch_jobinfo_t *jobinfo,
					  void **suspend_info)
{
	return;
}

extern void switch_p_job_suspend_info_pack(void *suspend_info, buf_t *buffer,
					   uint16_t protocol_version)
{
	return;
}

extern int switch_p_job_suspend_info_unpack(void **suspend_info, buf_t *buffer,
					    uint16_t protocol_version)
{
	return SLURM_SUCCESS;
}

extern void switch_p_job_suspend_info_free(void *suspend_info)
{
	return;
}

extern int switch_p_job_suspend(void *suspend_info, int max_wait)
{
	return SLURM_SUCCESS;
}

extern int switch_p_job_resume(void *suspend_info, int max_wait)
{
	return SLURM_SUCCESS;
}

/*
 * Non-privileged
 */
extern int switch_p_job_fini(switch_jobinfo_t *jobinfo)
{
	return SLURM_SUCCESS;
}

/*
 * Destroy CXI Services for each of the CXI NICs on this host
 */
extern int switch_p_job_postfini(stepd_step_rec_t *job)
{
	xassert(job);

	uid_t pgid = job->jmgr_pid;
	/*
	 *  Kill all processes in the job's session
	 */
	if (pgid) {
		debug2("Sending SIGKILL to pgid %lu", (unsigned long) pgid);
		kill(-pgid, SIGKILL);
	} else
		debug("%ps: Bad pid value %lu", &job->step_id,
		      (unsigned long) pgid);

	slingshot_jobinfo_t *jobinfo;
	jobinfo = (slingshot_jobinfo_t *)job->switch_job->data;
	xassert(jobinfo);
	if (!slingshot_destroy_services(jobinfo))
		return SLURM_ERROR;
	return SLURM_SUCCESS;
}

/*
 * Set up environment variables for job step: each environment variable
 * represents data from one or more CXI services, separated by commas.
 * In addition, the SLINGSHOT_VNIS variable has one or more VNIs
 * separated by colons.
 */
extern int switch_p_job_attach(switch_jobinfo_t *jobinfo, char ***env,
			       uint32_t nodeid, uint32_t procid,
			       uint32_t nnodes, uint32_t nprocs, uint32_t rank)
{
	slingshot_jobinfo_t *job = (slingshot_jobinfo_t *)jobinfo;
	int pidx, vidx;
	char *svc_ids = NULL, *vnis = NULL, *devices = NULL, *tcss = NULL;

	if (job->num_profiles == 0)
		return SLURM_SUCCESS;

	/* svc_ids, devices, traffic classes are per-device, comma-separated */
	for (pidx = 0; pidx < job->num_profiles; pidx++) {
		char *sep = pidx ? "," : "";
		pals_comm_profile_t *profile = &job->profiles[pidx];
		xstrfmtcat(svc_ids, "%s%u", sep, profile->svc_id);
		xstrfmtcat(devices, "%s%s", sep, profile->device_name);
		xstrfmtcat(tcss, "%s%#x", sep, profile->tcs);
	}

	/* vnis are global (all services share VNIs), comma-separated */
	for (vidx = 0; vidx < job->num_vnis; vidx++)
		xstrfmtcat(vnis, "%s%hu", vidx ? "," : "", job->vnis[vidx]);

	log_flag(SWITCH, "SLINGSHOT_SVC_IDS=%s SLINGSHOT_VNIS=%s"
			" SLINGSHOT_DEVICES=%s SLINGSHOT_TCS=%s",
			svc_ids, vnis, devices, tcss);

	env_array_overwrite(env, SLINGSHOT_SVC_IDS_ENV, svc_ids);
	env_array_overwrite(env, SLINGSHOT_VNIS_ENV, vnis);
	env_array_overwrite(env, SLINGSHOT_DEVICES_ENV, devices);
	env_array_overwrite(env, SLINGSHOT_TCS_ENV, tcss);

	xfree(svc_ids);
	xfree(vnis);
	xfree(devices);
	xfree(tcss);

	return SLURM_SUCCESS;
}

extern int switch_p_get_jobinfo(switch_jobinfo_t *switch_job, int key,
				void *resulting_data)
{
	slurm_seterrno(EINVAL);
	return SLURM_ERROR;
}

extern int switch_p_job_step_complete(switch_jobinfo_t *jobinfo, char *nodelist)
{
	slingshot_jobinfo_t *job = (slingshot_jobinfo_t *) jobinfo;

	xassert(job);
	xassert(job->version == SLINGSHOT_JOBINFO_VERSION);

	/* Free job step VNI */
	slingshot_free_job_step(job);

	return SLURM_SUCCESS;
}

extern int switch_p_job_step_allocated(switch_jobinfo_t *jobinfo,
				       char *nodelist)
{
	return SLURM_SUCCESS;
}

extern int switch_p_job_step_pre_suspend(stepd_step_rec_t *job)
{
	return SLURM_SUCCESS;
}

extern int switch_p_job_step_post_suspend(stepd_step_rec_t *job)
{
	return SLURM_SUCCESS;
}

extern int switch_p_job_step_pre_resume(stepd_step_rec_t *job)
{
	return SLURM_SUCCESS;
}

extern int switch_p_job_step_post_resume(stepd_step_rec_t *job)
{
	return SLURM_SUCCESS;
}

extern void switch_p_job_complete(uint32_t job_id)
{
	/* Free any job VNIs */
	xassert(running_in_slurmctld());
	log_flag(SWITCH, "switch_p_job_complete(%u)", job_id);
	slingshot_free_job(job_id);
}
