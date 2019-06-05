/*****************************************************************************\
 *  switch_cray_aries.c - Library for managing a switch on a Cray/Aries system.
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Copyright 2013 Cray Inc. All Rights Reserved.
 *  Written by Danny Auble <da@schedmd.com>
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

#if !(defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__))
#include "config.h"

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/limits.h>
#include <math.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "switch_cray_aries.h"
#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/pack.h"
#include "src/common/gres.h"

#ifdef HAVE_NATIVE_CRAY
#include <job.h> /* Cray's job module component */
#endif

#define SWITCH_BUF_SIZE (PORT_CNT + 128)
#define SWITCH_CRAY_STATE_VERSION "PROTOCOL_VERSION"

uint64_t debug_flags = 0;

#if defined(HAVE_NATIVE_CRAY) || defined(HAVE_CRAY_NETWORK)
static bool lustre_no_flush = false;
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
const char plugin_name[] = "switch Cray/Aries plugin";
const char plugin_type[] = "switch/cray_aries";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;
const uint32_t plugin_id      = SWITCH_PLUGIN_CRAY;

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
int init(void)
{
	debug("%s loaded.", plugin_name);
	debug_flags = slurm_get_debug_flags();

#if defined(HAVE_NATIVE_CRAY) || defined(HAVE_CRAY_NETWORK)
	start_lease_extender();
#endif
	return SLURM_SUCCESS;
}

int fini(void)
{
#if defined(HAVE_NATIVE_CRAY) || defined(HAVE_CRAY_NETWORK)
	cleanup_lease_extender();
#endif

	return SLURM_SUCCESS;
}

extern int switch_p_reconfig(void)
{
	debug_flags = slurm_get_debug_flags();
	return SLURM_SUCCESS;
}

/*
 * switch functions for global state save/restore
 */
extern int switch_p_libstate_save(char *dir_name)
{
	return SLURM_SUCCESS;
}

extern int switch_p_libstate_restore(char *dir_name, bool recover)
{
#ifdef HAVE_NATIVE_CRAY
	char *file_name;
	struct stat st;

	file_name = xstrdup(dir_name);
	xstrcat(file_name, "/switch_cray_state");
	if (stat(file_name, &st) == 0) {
		error("%s no longer used, please remove it, kill all running "
		      "jobs, and set MpiParams in slurm.conf", file_name);
		xfree(file_name);
		return SLURM_ERROR;
	}
	xfree(file_name);
#endif
	return SLURM_SUCCESS;
}

extern int switch_p_libstate_clear(void)
{
	return SLURM_SUCCESS;
}

/*
 * switch functions for job step specific credential
 */
extern int switch_p_alloc_jobinfo(
	switch_jobinfo_t **switch_job, uint32_t job_id, uint32_t step_id)
{
	slurm_cray_jobinfo_t *new;

	xassert(switch_job);
	new = (slurm_cray_jobinfo_t *) xmalloc(sizeof(slurm_cray_jobinfo_t));
	new->magic = CRAY_JOBINFO_MAGIC;
	new->num_cookies = 0;
	new->cookies = NULL;
	new->cookie_ids = NULL;
	new->apid = SLURM_ID_HASH(job_id, step_id);
	*switch_job = (switch_jobinfo_t *) new;
	return SLURM_SUCCESS;
}

extern int switch_p_build_jobinfo(switch_jobinfo_t *switch_job,
				  slurm_step_layout_t *step_layout,
				  char *network)
{
#if defined(HAVE_NATIVE_CRAY) || defined(HAVE_CRAY_NETWORK)
	int rc, cnt = 0;
	int32_t *nodes = NULL;
	slurm_cray_jobinfo_t *job = (slurm_cray_jobinfo_t *) switch_job;
	DEF_TIMERS;

	START_TIMER;

	if (!job || (job->magic == CRAY_NULL_JOBINFO_MAGIC)) {
		CRAY_DEBUG("switch_job was NULL");
		return SLURM_SUCCESS;
	}

	xassert(job->magic == CRAY_JOBINFO_MAGIC);

	// Get the list of nodes used for the cookie lease
	rc = list_str_to_array(step_layout->node_list, &cnt, &nodes);
	if (rc < 0) {
		CRAY_ERR("list_str_to_array failed");
		return SLURM_ERROR;
	}
	if (step_layout->node_cnt != cnt) {
		CRAY_ERR("list_str_to_array returned count %"
			 PRIu32 "does not match expected count %d",
			 cnt, step_layout->node_cnt);
	}

	// Get cookies for network configuration
	rc = lease_cookies(job, nodes, step_layout->node_cnt);

	END_TIMER;
	if (debug_flags & DEBUG_FLAG_TIME_CRAY)
		INFO_LINE("call took: %s", TIME_STR);

	xfree(nodes);
	if (rc != SLURM_SUCCESS) {
		return rc;
	}
#endif
	return SLURM_SUCCESS;
}

extern int switch_p_duplicate_jobinfo(switch_jobinfo_t *source,
				      switch_jobinfo_t **dest)
{
	slurm_cray_jobinfo_t *new;
	slurm_cray_jobinfo_t *old = (slurm_cray_jobinfo_t *) source;

	xassert(old);

	new = xmalloc(sizeof(slurm_cray_jobinfo_t));
	memcpy(new, old, sizeof(slurm_cray_jobinfo_t));

	if (old->num_cookies) {
		int i;
		new->cookie_ids = xcalloc(old->num_cookies, sizeof(uint32_t));
		memcpy(new->cookie_ids, old->cookie_ids,
		       sizeof(uint32_t) * old->num_cookies);
		new->cookies = xcalloc(old->num_cookies, sizeof(char *));
		for (i = 0; i < old->num_cookies; i++)
			new->cookies[i] = xstrdup(old->cookies[i]);
	}

	if (old->num_ptags) {
		new->ptags = xcalloc(old->num_ptags, sizeof(int));
		memcpy(new->ptags, old->ptags, sizeof(int) * old->num_ptags);
	}

	*dest = (switch_jobinfo_t *) new;
	return SLURM_SUCCESS;
}

/*
 *
 */
extern void switch_p_free_jobinfo(switch_jobinfo_t *switch_job)
{
	slurm_cray_jobinfo_t *job = (slurm_cray_jobinfo_t *) switch_job;
	int i;
	DEF_TIMERS;

	START_TIMER;

	if (!job) {
		CRAY_DEBUG("switch_job was NULL");
		return;
	}

	if (job->magic == CRAY_NULL_JOBINFO_MAGIC) {
		CRAY_DEBUG("switch_job was NULL MAGIC");
		goto endit;
	} else if (job->magic != CRAY_JOBINFO_MAGIC) {
		CRAY_ERR("job is not a switch/cray slurm_cray_jobinfo_t");
		return;
	}

	job->magic = 0;

	/*
	 * Free the cookies and the cookie_ids.
	 */
	if (job->num_cookies != 0) {
		xfree(job->cookie_ids);

		if (job->cookies) {
			// Free the individual cookie strings.
			for (i = 0; i < job->num_cookies; i++) {
				xfree(job->cookies[i]);
			}
			xfree(job->cookies);
		}
	}
	if (job->num_ptags)
		xfree(job->ptags);
endit:
	xfree(job);
	END_TIMER;
	if (debug_flags & DEBUG_FLAG_TIME_CRAY)
		INFO_LINE("call took: %s", TIME_STR);

	return;
}

extern int switch_p_pack_jobinfo(switch_jobinfo_t *switch_job, Buf buffer,
				 uint16_t protocol_version)
{
	slurm_cray_jobinfo_t *job = (slurm_cray_jobinfo_t *) switch_job;

	xassert(buffer);

	/*
	 * There is nothing to pack, so pack in magic telling unpack not to
	 * attempt to unpack anything.
	 */
	if (!job || (job->magic == CRAY_NULL_JOBINFO_MAGIC)) {
		pack32(CRAY_NULL_JOBINFO_MAGIC, buffer);
		return 0;
	}

	xassert(job->magic == CRAY_JOBINFO_MAGIC);

	if (debug_flags & DEBUG_FLAG_SWITCH) {
		CRAY_INFO("switch_jobinfo_t contents:");
		print_jobinfo(job);
	}

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(job->magic, buffer);
		pack32(job->num_cookies, buffer);
		packstr_array(job->cookies, job->num_cookies, buffer);
		pack32_array(job->cookie_ids, job->num_cookies, buffer);
		pack64(job->apid, buffer);
	}

	return 0;
}

extern int switch_p_unpack_jobinfo(switch_jobinfo_t **switch_job, Buf buffer,
				   uint16_t protocol_version)
{
	uint32_t num_cookies;
	slurm_cray_jobinfo_t *job;

	if (!switch_job) {
		CRAY_DEBUG("switch_job was NULL");
		return SLURM_SUCCESS;
	}

	xassert(buffer);

	job = xmalloc(sizeof(slurm_cray_jobinfo_t));
	*switch_job = (switch_jobinfo_t *)job;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&job->magic, buffer);

		if (job->magic == CRAY_NULL_JOBINFO_MAGIC) {
			CRAY_DEBUG("Nothing to unpack");
			return SLURM_SUCCESS;
		}

		xassert(job->magic == CRAY_JOBINFO_MAGIC);
		safe_unpack32(&(job->num_cookies), buffer);
		safe_unpackstr_array(&(job->cookies), &num_cookies, buffer);
		if (num_cookies != job->num_cookies) {
			CRAY_ERR("Wrong number of cookies received."
				 " Expected: %" PRIu32 "Received: %" PRIu32,
				 job->num_cookies, num_cookies);
			goto unpack_error;
		}
		safe_unpack32_array(&(job->cookie_ids), &num_cookies, buffer);
		if (num_cookies != job->num_cookies) {
			CRAY_ERR("Wrong number of cookie IDs received."
				 " Expected: %" PRIu32 "Received: %" PRIu32,
				 job->num_cookies, num_cookies);
			goto unpack_error;
		}
		safe_unpack64(&job->apid, buffer);
	}

#if defined(HAVE_NATIVE_CRAY) || defined(HAVE_CRAY_NETWORK)
	/*
	 * On recovery, we want to keep extending the life of
	 * cookies still in use. So lets track these cookies
	 * with the lease extender. Duplicate cookies are ignored.
	 */
	track_cookies(job);
#endif

	if (debug_flags & DEBUG_FLAG_SWITCH) {
		CRAY_INFO("Unpacked jobinfo");
		print_jobinfo(job);
	}

	return SLURM_SUCCESS;

unpack_error:

	CRAY_ERR("Unpacking error");
	switch_p_free_jobinfo(*switch_job);
	*switch_job = NULL;

	return SLURM_ERROR;
}

extern void switch_p_print_jobinfo(FILE *fp, switch_jobinfo_t *jobinfo)
{
	return;
}

extern char *switch_p_sprint_jobinfo(switch_jobinfo_t *switch_jobinfo,
				     char *buf, size_t size)
{
	if (buf && size) {
		buf[0] = '\0';
		return buf;
	}

	return NULL ;
}

/*
 * switch functions for job initiation
 */
extern int switch_p_node_init(void)
{
	return SLURM_SUCCESS;
}

extern int switch_p_node_fini(void)
{
	return SLURM_SUCCESS;
}

extern int switch_p_job_preinit(switch_jobinfo_t *jobinfo)
{
	return SLURM_SUCCESS;
}

extern int switch_p_job_init(stepd_step_rec_t *job)
{

#if defined(HAVE_NATIVE_CRAY) || defined(HAVE_CRAY_NETWORK)
	slurm_cray_jobinfo_t *sw_job = job->switch_job ?
		(slurm_cray_jobinfo_t *)job->switch_job->data : NULL;
	int rc, num_ptags;
	char *launch_params;
	int exclusive = 0, mem_scaling = 100, cpu_scaling = 100;
	int *ptags = NULL;
	char *err_msg = NULL;
	uint64_t cont_id = job->cont_id;
	alpsc_peInfo_t alpsc_pe_info = {-1, -1, -1, -1, NULL, NULL, NULL};
	int cmd_index = 0;
#ifdef HAVE_NATIVE_CRAY
	uint64_t gpu_cnt = 0;
	int control_nid = 0, num_branches = 0;
	struct sockaddr_in control_soc;
	alpsc_branchInfo_t alpsc_branch_info;
	uint32_t jobid;
#endif

#if defined(HAVE_NATIVE_CRAY) && !defined(HAVE_CRAY_NETWORK)
	char *npc = "none";
	int access = ALPSC_NET_PERF_CTR_NONE;
#endif
	DEF_TIMERS;

	START_TIMER;

#ifdef HAVE_CRAY_NETWORK
	/* No PAGG job containers; uid used instead to configure network */
	cont_id = (uint64_t)job->uid;
#endif

	if (!sw_job || (sw_job->magic == CRAY_NULL_JOBINFO_MAGIC)) {
		CRAY_DEBUG("job->switch_job was NULL");
		return SLURM_SUCCESS;
	}

	xassert(job->msg);
	xassert(sw_job->magic == CRAY_JOBINFO_MAGIC);

#ifdef HAVE_NATIVE_CRAY
	// Attach to the cncu container
	if (job->pack_jobid && (job->pack_jobid != NO_VAL))
		jobid = job->pack_jobid;
	else
		jobid = job->jobid;
	rc = alpsc_attach_cncu_container(&err_msg, jobid, job->cont_id);
	ALPSC_CN_DEBUG("alpsc_attach_cncu_container");
	if (rc != 1) {
		return SLURM_ERROR;
	}

	// Create the apid directory
	rc = create_apid_dir(sw_job->apid, job->uid, job->gid);
	if (rc != SLURM_SUCCESS) {
		return rc;
	}

	/*
	 * Not defined yet -- This one may be skipped because we may not need to
	 * find the PAGG JOB container based on the APID.  It is part of the
	 * stepd_step_rec_t struct in the cont_id member, so if we have access
	 * to the struct, then we have access to the JOB container.
	 */

	// alpsc_set_PAGG_apid()
#endif
	/*
	 * Fill in the alpsc_pe_info structure
	 */
	rc = build_alpsc_pe_info(job, &alpsc_pe_info, &cmd_index);
	if (rc != SLURM_SUCCESS) {
		return rc;
	}

	/*
	 * Configure the network
	 *
	 * Cray shmem still uses the network, even when it's using only one
	 * node, so we must always configure the network.
	 */
	launch_params = slurm_get_launch_params();
	if (launch_params && strstr(launch_params, "cray_net_exclusive")) {
		/*
		 * Grant exclusive access and all aries resources to the job.
		 * Not recommended if you may run multiple steps within
		 * the job, and will cause problems if you suspend or allow
		 * nodes to be shared across multiple jobs.
		 */
		/*
		 * TODO: determine if this can be managed per-job, rather
		 * than globally across the cluster.
		 */
		exclusive = 1;
	}
	if (launch_params && strstr(launch_params, "lustre_no_flush")) {
		/* Lustre cache flush can cause job bus errors, see bug 4309 */
		lustre_no_flush = true;
	}
	xfree(launch_params);

	if (!exclusive) {
		/*
		 * Calculate percentages of cpu and mem to assign to
		 * non-exclusive jobs.
		 */

		cpu_scaling = get_cpu_scaling(job);
		if (cpu_scaling == -1)
			return SLURM_ERROR;

		mem_scaling = get_mem_scaling(job);
		if (mem_scaling == -1)
			return SLURM_ERROR;
	}

	if (debug_flags & DEBUG_FLAG_SWITCH) {
		CRAY_INFO("Network Scaling: Exclusive %d CPU %d Memory %d",
			  exclusive, cpu_scaling, mem_scaling);
	}

	rc = alpsc_configure_nic(&err_msg, exclusive, cpu_scaling, mem_scaling,
				 cont_id, sw_job->num_cookies,
				 (const char **) sw_job->cookies,
				 &num_ptags, &ptags, NULL);
	ALPSC_CN_DEBUG("alpsc_configure_nic");
	if (rc != 1) {
		free(ptags);
		free_alpsc_pe_info(&alpsc_pe_info);
		return SLURM_ERROR;
	}
	/*
	 * xmalloc the ptags and copy the ptag array to the xmalloced
	 * space, so they can be xfreed later
	 */
	if (num_ptags) {
		sw_job->ptags = xcalloc(num_ptags, sizeof(int));
		memcpy(sw_job->ptags, ptags, sizeof(int) * num_ptags);
		free(ptags);
		sw_job->num_ptags = num_ptags;
	}

#if defined(HAVE_NATIVE_CRAY) || defined(HAVE_CRAY_NETWORK)
	// Write the IAA file
	rc = write_iaa_file(job, sw_job, sw_job->ptags, sw_job->num_ptags,
			    &alpsc_pe_info);
	if (rc != SLURM_SUCCESS) {
		free_alpsc_pe_info(&alpsc_pe_info);
		return rc;
	}
#endif

#if defined(HAVE_NATIVE_CRAY) && !defined(HAVE_CRAY_NETWORK)
	/*
	 * If there is reserved access to network performance counters,
	 * configure the appropriate access permission in the kernel.
	 */
	access = ALPSC_NET_PERF_CTR_NONE;
	select_g_select_jobinfo_get(job->msg->select_jobinfo,
		SELECT_JOBDATA_NETWORK, &npc);
	CRAY_DEBUG("network performance counters SELECT_JOBDATA_NETWORK %s",
		npc);
	if (xstrcasecmp(npc, "system") == 0) {
		access = ALPSC_NET_PERF_CTR_SYSTEM;
	} else if (xstrcasecmp(npc, "blade") == 0) {
		access = ALPSC_NET_PERF_CTR_BLADE;
	}
	if (access != ALPSC_NET_PERF_CTR_NONE) {
		rc = alpsc_set_perf_ctr_perms(&err_msg, job->cont_id, access);
		ALPSC_CN_DEBUG("alpsc_set_perf_ctr_perms");
		if (rc != 1) {
			free_alpsc_pe_info(&alpsc_pe_info);
			return SLURM_ERROR;
		}
	}

	/*
	 * Some of the input parameters for alpsc_write_placement_file do not
	 * apply for Slurm.  These parameters will be given zero values.
	 * They are
	 *  int control_nid
	 *  struct sockaddr_in control_soc
	 *  int num_branches
	 *  alpsc_branchInfo_t alpsc_branch_info
	 */
	control_soc.sin_port = 0;
	control_soc.sin_addr.s_addr = 0;
	/* Just assigning control_soc because it's already zero. */
	alpsc_branch_info.tAddr = control_soc;
	alpsc_branch_info.tIndex = 0;
	alpsc_branch_info.tLen = 0;
	alpsc_branch_info.targ = 0;

	rc = alpsc_write_placement_file(&err_msg, sw_job->apid, cmd_index,
					&alpsc_pe_info, control_nid,
					control_soc, num_branches,
					&alpsc_branch_info);

	ALPSC_CN_DEBUG("alpsc_write_placement_file");
	if (rc != 1) {
		free_alpsc_pe_info(&alpsc_pe_info);
		return SLURM_ERROR;
	}

	/*
	 * Also write a placement file with the legacy apid to support old
	 * statically linked Cray PMI applications. We can't simply symlink
	 * the old format to the new because the apid is written to the file.
	 */
	if (sw_job->apid != SLURM_ID_HASH_LEGACY(sw_job->apid)) {
		rc = alpsc_write_placement_file(&err_msg,
			SLURM_ID_HASH_LEGACY(sw_job->apid),
			cmd_index, &alpsc_pe_info, control_nid,	control_soc,
			num_branches, &alpsc_branch_info);
		ALPSC_CN_DEBUG("alpsc_write_placement_file");
		if (rc != 1) {
			free_alpsc_pe_info(&alpsc_pe_info);
			return SLURM_ERROR;
		}
	}
#endif
	/* Clean up alpsc_pe_info*/
	free_alpsc_pe_info(&alpsc_pe_info);
	/*
	 * Write some environment variables used by LLI and PMI
	 */
	rc = set_job_env(job, sw_job);
	if (rc != SLURM_SUCCESS)
		return rc;

#ifdef HAVE_NATIVE_CRAY
	/*
	 * Query the generic resources to see if the GPU should be allocated
	 */

	rc = gres_get_step_info(job->step_gres_list, "gpu", 0,
				GRES_STEP_DATA_COUNT, &gpu_cnt);
	CRAY_INFO("gres_cnt: %d %"PRIu64, rc, gpu_cnt);
	if (gpu_cnt > 0)
		setup_gpu(job);

	/*
	 * Set the Job's APID
	 */
	job_setapid(getpid(), sw_job->apid);
#endif

	END_TIMER;
	if (debug_flags & DEBUG_FLAG_TIME_CRAY)
		INFO_LINE("call took: %s", TIME_STR);
#endif

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
extern void switch_p_job_suspend_info_pack(void *suspend_info, Buf buffer,
					   uint16_t protocol_version)
{
	return;
}

extern int switch_p_job_suspend_info_unpack(void **suspend_info, Buf buffer,
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

extern int switch_p_job_fini(switch_jobinfo_t *jobinfo)
{
#if defined(HAVE_NATIVE_CRAY) || defined(HAVE_CRAY_NETWORK)
	slurm_cray_jobinfo_t *job = (slurm_cray_jobinfo_t *) jobinfo;
	DEF_TIMERS;

	START_TIMER;

	if (!job || (job->magic == CRAY_NULL_JOBINFO_MAGIC)) {
		CRAY_ERR("jobinfo pointer was NULL");
		return SLURM_SUCCESS;
	}

	xassert(job->magic == CRAY_JOBINFO_MAGIC);

#ifdef HAVE_NATIVE_CRAY
	int rc;
	rc = remove_spool_files(job->apid);
	if (rc != SLURM_SUCCESS) {
	    return rc;
	}
#endif

#if defined(HAVE_NATIVE_CRAY) || defined(HAVE_CRAY_NETWORK)
	// Remove the IAA file
	unlink_iaa_file(job);
#endif

	END_TIMER;
	if (debug_flags & DEBUG_FLAG_TIME_CRAY)
		INFO_LINE("call took: %s", TIME_STR);
#endif
	return SLURM_SUCCESS;
}

extern int switch_p_job_postfini(stepd_step_rec_t *job)
{
#if defined(HAVE_NATIVE_CRAY) || defined(HAVE_CRAY_NETWORK)
	int rc;
	char *err_msg = NULL;
	uid_t pgid = job->jmgr_pid;
#ifdef HAVE_NATIVE_CRAY
        uint64_t gpu_cnt = 0;
#endif
	DEF_TIMERS;

	START_TIMER;

	if (!job->switch_job) {
		CRAY_DEBUG("job->switch_job was NULL");
	}

	/*
	 *  Kill all processes in the job's session
	 */
	if (pgid) {
		CRAY_DEBUG("Sending SIGKILL to pgid %lu", (unsigned long) pgid);
		kill(-pgid, SIGKILL);
	} else
		CRAY_INFO("Job %u.%u: Bad pid value %lu",
			  job->jobid, job->stepid, (unsigned long) pgid);
	/*
	 * Clean-up
	 *
	 * 0. Reset GPU proxy
	 * 1. Flush Lustre caches
	 * 2. Flush virtual memory
	 * 3. Compact memory
	 */

#ifdef HAVE_NATIVE_CRAY
	// Set the proxy back to the default state.
	rc = gres_get_step_info(job->step_gres_list, "gpu", 0,
				GRES_STEP_DATA_COUNT, &gpu_cnt);
	if (gpu_cnt > 0) {
		reset_gpu(job);
	}
#endif
	if (!lustre_no_flush) {
		// Flush Lustre Cache
		rc = alpsc_flush_lustre(&err_msg);
		ALPSC_CN_DEBUG("alpsc_flush_lustre");
		if (rc != 1) {
			return SLURM_ERROR;
		}

		// Flush virtual memory
		rc = system("echo 3 > /proc/sys/vm/drop_caches");
		if (rc != -1) {
			rc = WEXITSTATUS(rc);
		}
		if (rc) {
			CRAY_ERR("Flushing virtual memory failed. Return code: %d",
				 rc);
		}
	}

	END_TIMER;
	if (debug_flags & DEBUG_FLAG_TIME_CRAY)
		INFO_LINE("call took: %s", TIME_STR);
#endif
	return SLURM_SUCCESS;
}

extern int switch_p_job_attach(switch_jobinfo_t *jobinfo, char ***env,
			       uint32_t nodeid, uint32_t procid,
			       uint32_t nnodes, uint32_t nprocs,
			       uint32_t rank)
{
	return SLURM_SUCCESS;
}

extern int switch_p_get_jobinfo(switch_jobinfo_t *switch_job, int key,
				void *resulting_data)
{
	slurm_seterrno(EINVAL);
	return SLURM_ERROR;
}

/*
 * node switch state monitoring functions
 * required for IBM Federation switch
 */
extern int switch_p_clear_node_state(void)
{
	return SLURM_SUCCESS;
}

extern int switch_p_alloc_node_info(switch_node_info_t **switch_node)
{
	return SLURM_SUCCESS;
}

extern int switch_p_build_node_info(switch_node_info_t *switch_node)
{
	return SLURM_SUCCESS;
}

extern int switch_p_pack_node_info(switch_node_info_t *switch_node, Buf buffer,
				   uint16_t protocol_version)
{
	return 0;
}

extern int switch_p_unpack_node_info(switch_node_info_t **switch_node,
				     Buf buffer, uint16_t protocol_version)
{
	return SLURM_SUCCESS;
}

extern int switch_p_free_node_info(switch_node_info_t **switch_node)
{
	return SLURM_SUCCESS;
}

extern char*switch_p_sprintf_node_info(switch_node_info_t *switch_node,
				       char *buf, size_t size)
{
	if (buf && size) {
		buf[0] = '\0';
		return buf;
	}

	return NULL ;
}

extern int switch_p_job_step_complete(switch_jobinfo_t *jobinfo,
				      char *nodelist)
{
#if defined(HAVE_NATIVE_CRAY) || defined(HAVE_CRAY_NETWORK)
	slurm_cray_jobinfo_t *job = (slurm_cray_jobinfo_t *) jobinfo;
	int rc = 0;
	DEF_TIMERS;

	START_TIMER;

	if (!job || (job->magic == CRAY_NULL_JOBINFO_MAGIC)) {
		CRAY_DEBUG("switch_job was NULL");
		return SLURM_SUCCESS;
	}

	if (debug_flags & DEBUG_FLAG_SWITCH) {
		CRAY_INFO("switch_p_job_step_complete");
	}

	/* Release the cookies */
	rc = release_cookies(job);
	if (rc != SLURM_SUCCESS) {
		return rc;
	}
	END_TIMER;
	if (debug_flags & DEBUG_FLAG_TIME_CRAY)
		INFO_LINE("call took: %s", TIME_STR);
#endif
	return SLURM_SUCCESS;
}

extern int switch_p_job_step_part_comp(switch_jobinfo_t *jobinfo,
				       char *nodelist)
{
	return SLURM_SUCCESS;
}

extern bool switch_p_part_comp(void)
{
	return false;
}

extern int switch_p_job_step_allocated(switch_jobinfo_t *jobinfo,
				       char *nodelist)
{
	return SLURM_SUCCESS;
}

extern int switch_p_slurmctld_init(void)
{
	return SLURM_SUCCESS;
}

extern int switch_p_slurmd_init(void)
{
	return SLURM_SUCCESS;
}

extern int switch_p_slurmd_step_init(void)
{
	return SLURM_SUCCESS;
}

/*
 * Functions for suspend/resume support
 */
extern int switch_p_job_step_pre_suspend(stepd_step_rec_t *job)
{
#if _DEBUG
	info("switch_p_job_step_pre_suspend(job %u.%u)",
		job->jobid, job->stepid);
#endif
#if defined(HAVE_NATIVE_CRAY) && !defined(HAVE_CRAY_NETWORK)
	slurm_cray_jobinfo_t *jobinfo = job->switch_job ?
		(slurm_cray_jobinfo_t *)job->switch_job->data : NULL;
	char *err_msg = NULL;
	int rc;
	DEF_TIMERS;

	START_TIMER;

	rc = alpsc_pre_suspend(&err_msg, job->cont_id, jobinfo->ptags,
			       jobinfo->num_ptags, SUSPEND_TIMEOUT_MSEC);
	ALPSC_CN_DEBUG("alpsc_pre_suspend");
	if (rc != 1) {
		return SLURM_ERROR;
	}
	END_TIMER;
	if (debug_flags & DEBUG_FLAG_TIME_CRAY)
		INFO_LINE("call took: %s", TIME_STR);
#endif
	return SLURM_SUCCESS;
}

extern int switch_p_job_step_post_suspend(stepd_step_rec_t *job)
{
#if _DEBUG
	info("switch_p_job_step_post_suspend(job %u.%u)",
		job->jobid, job->stepid);
#endif
#if defined(HAVE_NATIVE_CRAY) && !defined(HAVE_CRAY_NETWORK)
	char *err_msg = NULL;
	int rc;
	DEF_TIMERS;

	START_TIMER;

	rc = alpsc_post_suspend(&err_msg, job->cont_id);
	ALPSC_CN_DEBUG("alpsc_post_suspend");
	if (rc != 1) {
		return SLURM_ERROR;
	}
	END_TIMER;
	if (debug_flags & DEBUG_FLAG_TIME_CRAY)
		INFO_LINE("call took: %s", TIME_STR);
#endif
	return SLURM_SUCCESS;
}

extern int switch_p_job_step_pre_resume(stepd_step_rec_t *job)
{
#if _DEBUG
	info("switch_p_job_step_pre_resume(job %u.%u)",
		job->jobid, job->stepid);
#endif
#if defined(HAVE_NATIVE_CRAY) && !defined(HAVE_CRAY_NETWORK)
	slurm_cray_jobinfo_t *jobinfo = job->switch_job ?
		(slurm_cray_jobinfo_t *)job->switch_job->data : NULL;
	char *err_msg = NULL;
	int rc;
	DEF_TIMERS;

	START_TIMER;

	rc = alpsc_pre_resume(&err_msg, job->cont_id, jobinfo->ptags,
			       jobinfo->num_ptags);
	ALPSC_CN_DEBUG("alpsc_pre_resume");
	if (rc != 1) {
		return SLURM_ERROR;
	}
	END_TIMER;
	if (debug_flags & DEBUG_FLAG_TIME_CRAY)
		INFO_LINE("call took: %s", TIME_STR);
#endif
	return SLURM_SUCCESS;
}

extern int switch_p_job_step_post_resume(stepd_step_rec_t *job)
{
#if _DEBUG
	info("switch_p_job_step_post_resume(job %u.%u)",
		job->jobid, job->stepid);
#endif
#if defined(HAVE_NATIVE_CRAY) && !defined(HAVE_CRAY_NETWORK)
	char *err_msg = NULL;
	int rc;
	DEF_TIMERS;

	START_TIMER;

	rc = alpsc_post_resume(&err_msg, job->cont_id);
	ALPSC_CN_DEBUG("alpsc_post_resume");
	if (rc != 1) {
		return SLURM_ERROR;
	}
	END_TIMER;
	if (debug_flags & DEBUG_FLAG_TIME_CRAY)
		INFO_LINE("call took: %s", TIME_STR);
#endif
	return SLURM_SUCCESS;
}
#endif /* !defined(__FreeBSD__) */
