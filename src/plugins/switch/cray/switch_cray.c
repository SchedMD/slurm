/*****************************************************************************\
 *  switch_cray.c - Library for managing a switch on a Cray system.
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Copyright 2013 Cray Inc. All Rights Reserved.
 *  Written by Danny Auble <da@schedmd.com>
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

#if !defined(__FreeBSD__)
#if     HAVE_CONFIG_H
#include "config.h"
#endif

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <fcntl.h>
#include "limits.h"
#include <linux/limits.h>
#include <sched.h>
#include <math.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/common/pack.h"
#include "src/common/gres.h"
#include "switch_cray.h"

#ifdef HAVE_NATIVE_CRAY
#include <job.h> /* Cray's job module component */
#endif

#define SWITCH_BUF_SIZE (PORT_CNT + 128)
#define SWITCH_CRAY_STATE_VERSION "PROTOCOL_VERSION"

uint32_t debug_flags = 0;

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
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "switch" for SLURM switch) and <method> is a description
 * of how this plugin satisfies that application.  SLURM will only load
 * a switch plugin if the plugin_type string has a prefix of "switch/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum version for their plugins as this API matures.
 */
const char plugin_name[] = "switch CRAY plugin";
const char plugin_type[] = "switch/cray";
const uint32_t plugin_version = 100;

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
int init(void)
{
	verbose("%s loaded.", plugin_name);
	debug_flags = slurm_get_debug_flags();
#ifdef HAVE_NATIVE_CRAY
	if (MAX_PORT < MIN_PORT) {
		CRAY_ERR("MAX_PORT: %d < MIN_PORT: %d", MAX_PORT, MIN_PORT);
		return SLURM_ERROR;
	}
#endif
	return SLURM_SUCCESS;
}

int fini(void)
{

#ifdef HAVE_NATIVE_CRAY
	pthread_mutex_lock(&port_mutex);
	FREE_NULL_BITMAP(port_resv);
	pthread_mutex_unlock(&port_mutex);
#endif

	return SLURM_SUCCESS;
}

extern int switch_p_reconfig(void)
{
	return SLURM_SUCCESS;
}

#ifdef HAVE_NATIVE_CRAY
static void _state_read_buf(Buf buffer)
{
	uint16_t protocol_version = (uint16_t) NO_VAL;
	uint32_t min_port, max_port;
	int i;

	/* Validate state version */
	safe_unpack16(&protocol_version, buffer);
	debug3("Version in switch_cray header is %u", protocol_version);
	if (protocol_version < SLURM_MIN_PROTOCOL_VERSION) {
		error("******************************************************");
		error("Can't recover switch/cray state, incompatible version");
		error("******************************************************");
		return;
	}

	pthread_mutex_lock(&port_mutex);
	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		safe_unpack32(&min_port, buffer);
		safe_unpack32(&max_port, buffer);
		safe_unpack32(&last_alloc_port, buffer);
		/* make sure we are NULL here */
		FREE_NULL_BITMAP(port_resv);
		unpack_bit_str(&port_resv, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		uint8_t port_set = 0;
		safe_unpack32(&min_port, buffer);
		safe_unpack32(&max_port, buffer);
		safe_unpack32(&last_alloc_port, buffer);
		/* make sure we are NULL here */
		FREE_NULL_BITMAP(port_resv);
		port_resv = bit_alloc(PORT_CNT);
		for (i = 0; i < PORT_CNT; i++) {
			safe_unpack8(&port_set, buffer);
			if (port_set)
				bit_set(port_resv, i);
		}
	}

	if (!port_resv || (bit_size(port_resv) != PORT_CNT)) {
		error("_state_read_buf: Reserve Port size was %d not %d, "
		      "reallocating",
		      port_resv ? bit_size(port_resv) : -1, PORT_CNT);
		port_resv = bit_realloc(port_resv, PORT_CNT);
	}
	pthread_mutex_unlock(&port_mutex);

	if ((min_port != MIN_PORT) || (max_port != MAX_PORT)) {
		error("******************************************************");
		error("Can not recover switch/cray state");
		error("Changed MIN_PORT (%u != %u) and/or MAX_PORT (%u != %u)",
		      min_port, MIN_PORT, max_port, MAX_PORT);
		error("******************************************************");
		return;
	}

	return;

unpack_error:
	CRAY_ERR("unpack error");
	return;
}

static void _state_write_buf(Buf buffer)
{
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack32(MIN_PORT, buffer);
	pack32(MAX_PORT, buffer);

	pthread_mutex_lock(&port_mutex);

	pack32(last_alloc_port, buffer);
	pack_bit_str(port_resv, buffer);

	pthread_mutex_unlock(&port_mutex);
}
#endif
/*
 * switch functions for global state save/restore
 */
int switch_p_libstate_save(char *dir_name)
{
#ifdef HAVE_NATIVE_CRAY
	Buf buffer;
	char *file_name;
	int ret = SLURM_SUCCESS;
	int state_fd;

	xassert(dir_name != NULL);

	if (debug_flags & DEBUG_FLAG_SWITCH)
		CRAY_INFO("save to %s", dir_name);

	buffer = init_buf(SWITCH_BUF_SIZE);
	_state_write_buf(buffer);
	file_name = xstrdup(dir_name);
	xstrcat(file_name, "/switch_cray_state");
	(void) unlink(file_name);
	state_fd = creat(file_name, 0600);
	if (state_fd < 0) {
		CRAY_ERR("Can't save state, error creating file %s %m",
		      file_name);
		ret = SLURM_ERROR;
	} else {
		char  *buf = get_buf_data(buffer);
		size_t len = get_buf_offset(buffer);
		while (1) {
			int wrote = write(state_fd, buf, len);
			if ((wrote < 0) && (errno == EINTR))
				continue;
			if (wrote == 0)
				break;
			if (wrote < 0) {
				CRAY_ERR("Can't save switch state: %m");
				ret = SLURM_ERROR;
				break;
			}
			buf += wrote;
			len -= wrote;
		}
		close(state_fd);
	}
	xfree(file_name);

	if (buffer)
		free_buf(buffer);

	return ret;
#else
	return SLURM_SUCCESS;
#endif
}

int switch_p_libstate_restore(char *dir_name, bool recover)
{
#ifdef HAVE_NATIVE_CRAY
	char *data = NULL, *file_name;
	Buf buffer = NULL;
	int error_code = SLURM_SUCCESS;
	int state_fd, data_allocated = 0, data_read = 0, data_size = 0;

	xassert(dir_name != NULL);

	if (debug_flags & DEBUG_FLAG_SWITCH) {
		CRAY_INFO("restore from %s, recover %d",
			  dir_name,  (int) recover);
	}

	if (!recover)		/* clean start, no recovery */
		return SLURM_SUCCESS;

	file_name = xstrdup(dir_name);
	xstrcat(file_name, "/switch_cray_state");
	state_fd = open (file_name, O_RDONLY);
	if (state_fd >= 0) {
		data_allocated = SWITCH_BUF_SIZE;
		data = xmalloc(data_allocated);
		while (1) {
			data_read = read (state_fd, &data[data_size],
					  SWITCH_BUF_SIZE);
			if ((data_read < 0) && (errno == EINTR))
				continue;
			if (data_read < 0) {
				CRAY_ERR("Read error on %s, %m", file_name);
				error_code = SLURM_ERROR;
				break;
			} else if (data_read == 0)
				break;
			data_size      += data_read;
			data_allocated += data_read;
			xrealloc(data, data_allocated);
		}
		close (state_fd);
		(void) unlink(file_name);	/* One chance to recover */
		xfree(file_name);
	} else {
		CRAY_ERR("No %s file for switch/cray state recovery",
			 file_name);
		CRAY_ERR("Starting switch/cray with clean state");
		xfree(file_name);
		return SLURM_SUCCESS;
	}

	if (error_code == SLURM_SUCCESS) {
		buffer = create_buf (data, data_size);
		data = NULL;	/* now in buffer, don't xfree() */
		_state_read_buf(buffer);
	}

	if (buffer)
		free_buf(buffer);
	xfree(data);
#endif
	return SLURM_SUCCESS;
}

int switch_p_libstate_clear(void)
{
#ifdef HAVE_NATIVE_CRAY
	pthread_mutex_lock(&port_mutex);

	bit_nclear(port_resv, 0, PORT_CNT - 1);
	last_alloc_port = 0;

	pthread_mutex_unlock(&port_mutex);
#endif
	return SLURM_SUCCESS;
}

/*
 * switch functions for job step specific credential
 */
int switch_p_alloc_jobinfo(switch_jobinfo_t **switch_job, uint32_t job_id,
			   uint32_t step_id)
{
	slurm_cray_jobinfo_t *new;

	xassert(switch_job);
	new = (slurm_cray_jobinfo_t *) xmalloc(sizeof(slurm_cray_jobinfo_t));
	new->magic = CRAY_JOBINFO_MAGIC;
	new->num_cookies = 0;
	new->cookies = NULL;
	new->cookie_ids = NULL;
	new->jobid = job_id;
	new->stepid = step_id;
	new->apid = SLURM_ID_HASH(job_id, step_id);
	*switch_job = (switch_jobinfo_t *) new;

	if (debug_flags & DEBUG_FLAG_SWITCH) {
		CRAY_INFO("switch_jobinfo_t contents");
		print_jobinfo(new);
	}

	return SLURM_SUCCESS;
}

int switch_p_build_jobinfo(switch_jobinfo_t *switch_job,
			   slurm_step_layout_t *step_layout, char *network)
{
#if defined(HAVE_NATIVE_CRAY) || defined(HAVE_CRAY_NETWORK)
	int i, rc, cnt = 0;
	uint32_t port = 0;
	int num_cookies = 2;
	char *err_msg = NULL;
	char **cookies = NULL, **s_cookies = NULL;
	int32_t *nodes = NULL, *cookie_ids = NULL;
	uint32_t *s_cookie_ids = NULL;
	slurm_cray_jobinfo_t *job = (slurm_cray_jobinfo_t *) switch_job;

	if (!job || (job->magic == CRAY_NULL_JOBINFO_MAGIC)) {
		CRAY_DEBUG("switch_job was NULL");
		return SLURM_SUCCESS;
	}

	xassert(job->magic == CRAY_JOBINFO_MAGIC);

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

	/*
	 * Get cookies for network configuration
	 *
	 * TODO: I could specify a lease time if I knew the wall-clock limit of
	 * the job.  However, if the job got suspended, then all bets are off.
	 * An infinite release time seems safest for now.
	 *
	 * TODO: I'm hard-coding the number of cookies for now to two.
	 * Maybe we'll have a dynamic way to ascertain the number of
	 * cookies later.
	 *
	 * TODO: I could ensure that the nodes list was sorted either by doing
	 * some research to see if it comes in sorted or calling a sort
	 * routine.
	 */
	rc = alpsc_lease_cookies(&err_msg, "SLURM", job->apid,
				 ALPSC_INFINITE_LEASE, nodes,
				 step_layout->node_cnt, num_cookies,
				 &cookies, &cookie_ids);
	ALPSC_SN_DEBUG("alpsc_lease_cookies");
	xfree(nodes);
	if (rc != 0) {
		return SLURM_ERROR;
	}

	/*
	 * Cookie ID safety check: The cookie_ids should be positive numbers.
	 */
	for (i = 0; i < num_cookies; i++) {
		if (cookie_ids[i] < 0) {
			CRAY_ERR("alpsc_lease_cookies returned a cookie ID "
				 "number %d with a negative value: %d",
				 i, cookie_ids[i]);
			return SLURM_ERROR;
		}
	}

	/*
	 * xmalloc the space for the cookies and cookie_ids, so it can be freed
	 * with xfree later, which is consistent with SLURM practices and how
	 * the rest of the structure will be freed.
	 * We must free() the ALPS Common library allocated memory using free(),
	 * not xfree().
	 */
	s_cookie_ids = (uint32_t *) xmalloc(sizeof(uint32_t) * num_cookies);
	memcpy(s_cookie_ids, cookie_ids, sizeof(uint32_t) * num_cookies);
	free(cookie_ids);

	s_cookies = (char **) xmalloc(sizeof(char **) * num_cookies);
	for (i = 0; i < num_cookies; i++) {
		s_cookies[i] = xstrdup(cookies[i]);
		free(cookies[i]);
	}
	free(cookies);

#ifdef HAVE_NATIVE_CRAY
	/*
	 * Get a unique port for PMI communications
	 */
	rc = assign_port(&port);
	if (rc < 0) {
		CRAY_INFO("assign_port failed");
		return SLURM_ERROR;
	}
#endif
	/*
	 * Populate the switch_jobinfo_t struct
	 */
	job->num_cookies = num_cookies;
	job->cookies = s_cookies;
	job->cookie_ids = s_cookie_ids;
	job->port = port;

#endif
	return SLURM_SUCCESS;
}

/*
 *
 */
void switch_p_free_jobinfo(switch_jobinfo_t *switch_job)
{
	slurm_cray_jobinfo_t *job = (slurm_cray_jobinfo_t *) switch_job;
	int i;

	if (!job || (job->magic == CRAY_NULL_JOBINFO_MAGIC)) {
		CRAY_DEBUG("switch_job was NULL");
		return;
	}

	if (job->magic != CRAY_JOBINFO_MAGIC) {
		CRAY_ERR("job is not a switch/cray slurm_cray_jobinfo_t");
		return;
	}

	job->magic = 0;

	/*
	 * Free the cookies and the cookie_ids.
	 */
	if (job->num_cookies != 0) {
		// Free the cookie_ids
		if (job->cookie_ids) {
			xfree(job->cookie_ids);
		}

		if (job->cookies) {
			// Free the individual cookie strings.
			for (i = 0; i < job->num_cookies; i++) {
				if (job->cookies[i]) {
					xfree(job->cookies[i]);
				}
			}

			// Free the cookie array
			xfree(job->cookies);
		}
	}

	// Free the ptags (if any)
	if (job->num_ptags)
		xfree(job->ptags);

	xfree(job);

	return;
}

int switch_p_pack_jobinfo(switch_jobinfo_t *switch_job, Buf buffer,
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

	pack32(job->magic, buffer);
	pack32(job->num_cookies, buffer);
	packstr_array(job->cookies, job->num_cookies, buffer);
	pack32_array(job->cookie_ids, job->num_cookies, buffer);
	pack32(job->port, buffer);

	return 0;
}

int switch_p_unpack_jobinfo(switch_jobinfo_t *switch_job, Buf buffer,
			    uint16_t protocol_version)
{
	uint32_t num_cookies;
	slurm_cray_jobinfo_t *job;

	if (!switch_job) {
		CRAY_DEBUG("switch_job was NULL");
		return SLURM_SUCCESS;
	}

	xassert(buffer);

	job = (slurm_cray_jobinfo_t *) switch_job;

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
	safe_unpack32(&job->port, buffer);

#ifndef HAVE_CRAY_NETWORK
	/* If the libstate save/restore failed, at least make sure that we
	 * do not re-allocate ports assigned to job steps that we
	 * recover.
	 *
	 * NOTE: port_resv is NULL when unpacking things in srun.
	 */
	pthread_mutex_lock(&port_mutex);
	if (port_resv && (job->port >= MIN_PORT) && (job->port <= MAX_PORT))
		bit_set(port_resv, job->port - MIN_PORT);
	pthread_mutex_unlock(&port_mutex);
#endif

	if (debug_flags & DEBUG_FLAG_SWITCH) {
		CRAY_INFO("switch_jobinfo_t contents:");
		print_jobinfo(job);
	}

	return SLURM_SUCCESS;

unpack_error:
	CRAY_ERR("Unpacking error");
	if (job->num_cookies) {
		// Free the cookie_ids
		if (job->cookie_ids)
			xfree(job->cookie_ids);

		if (job->cookies) {
			int i;
			// Free the individual cookie strings.
			for (i = 0; i < job->num_cookies; i++) {
				if (job->cookies[i])
					xfree(job->cookies[i]);
			}

			// Free the cookie array
			xfree(job->cookies);
		}
	}
	// Free the ptags (if any)
	if (job->num_ptags)
		xfree(job->ptags);

	return SLURM_ERROR;
}

void switch_p_print_jobinfo(FILE *fp, switch_jobinfo_t *jobinfo)
{
	return;
}

char *switch_p_sprint_jobinfo(switch_jobinfo_t *switch_jobinfo, char *buf,
			      size_t size)
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
int switch_p_node_init(void)
{
	return SLURM_SUCCESS;
}

int switch_p_node_fini(void)
{
	return SLURM_SUCCESS;
}

int switch_p_job_preinit(switch_jobinfo_t *jobinfo)
{
	return SLURM_SUCCESS;
}

extern int switch_p_job_init(stepd_step_rec_t *job)
{

#if defined(HAVE_NATIVE_CRAY) || defined(HAVE_CRAY_NETWORK)
	slurm_cray_jobinfo_t *sw_job = (slurm_cray_jobinfo_t *) job->switch_job;
	int rc, num_ptags;
	int mem_scaling, cpu_scaling;
	int *ptags = NULL;
	char *err_msg = NULL;
	uint64_t cont_id = job->cont_id;
	alpsc_peInfo_t alpsc_pe_info = {-1, -1, -1, -1, NULL, NULL, NULL};
#ifdef HAVE_NATIVE_CRAY
	int gpu_cnt = 0, cmd_index = 0;
	int control_nid = 0, num_branches = 0;
	struct sockaddr_in control_soc;
	alpsc_branchInfo_t alpsc_branch_info;
#endif

#if defined(HAVE_NATIVE_CRAY_GA) && !defined(HAVE_CRAY_NETWORK)
	char *npc = "none";
	int access = ALPSC_NET_PERF_CTR_NONE;
#endif

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
	rc = alpsc_attach_cncu_container(&err_msg, sw_job->jobid, job->cont_id);
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
	rc = build_alpsc_pe_info(job, sw_job, &alpsc_pe_info);
	if (rc != SLURM_SUCCESS) {
		return rc;
	}

	/*
	 * Configure the network
	 *
	 * I'm setting exclusive flag to zero for now until we can figure out a
	 * way to guarantee that the application not only has exclusive access
	 * to the node but also will not be suspended.  This may not happen.
	 *
	 * Cray shmem still uses the network, even when it's using only one
	 * node, so we must always configure the network.
	 */
	cpu_scaling = get_cpu_scaling(job);
	if (cpu_scaling == -1) {
		return SLURM_ERROR;
	}

	mem_scaling = get_mem_scaling(job);
	if (mem_scaling == -1) {
		return SLURM_ERROR;
	}

	if (debug_flags & DEBUG_FLAG_SWITCH) {
		CRAY_INFO("Network Scaling: CPU %d Memory %d",
			  cpu_scaling, mem_scaling);
	}

	rc = alpsc_configure_nic(&err_msg, 0, cpu_scaling, mem_scaling,
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
		sw_job->ptags = xmalloc(sizeof(int) * num_ptags);
		memcpy(sw_job->ptags, ptags, sizeof(int) * num_ptags);
		free(ptags);
		sw_job->num_ptags = num_ptags;
	}

#if defined(HAVE_NATIVE_CRAY_GA) || defined(HAVE_CRAY_NETWORK)
	// Write the IAA file
	rc = write_iaa_file(job, sw_job, sw_job->ptags, sw_job->num_ptags,
			    &alpsc_pe_info);
	if (rc != SLURM_SUCCESS) {
		free_alpsc_pe_info(&alpsc_pe_info);
		return rc;
	}
#endif

#if defined(HAVE_NATIVE_CRAY_GA) && !defined(HAVE_CRAY_NETWORK)
	/*
	 * If there is reserved access to network performance counters,
	 * configure the appropriate access permission in the kernel.
	 */
	access = ALPSC_NET_PERF_CTR_NONE;
	select_g_select_jobinfo_get(job->msg->select_jobinfo,
		SELECT_JOBDATA_NETWORK, &npc);
	CRAY_DEBUG("network performance counters SELECT_JOBDATA_NETWORK %s",
		npc);
	if (strcasecmp(npc, "system") == 0) {
		access = ALPSC_NET_PERF_CTR_SYSTEM;
	} else if (strcasecmp(npc, "blade") == 0) {
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
	 * Set the cmd_index
	 */

	if (!job->multi_prog)
		cmd_index = 0;

	/*
	 * Some of the input parameters for alpsc_write_placement_file do not
	 * apply for SLURM.  These parameters will be given zero values.
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
	CRAY_INFO("gres_cnt: %d %u", rc, gpu_cnt);
	if (gpu_cnt > 0)
		setup_gpu(job);

	/*
	 * Set the Job's APID
	 */
	job_setapid(getpid(), sw_job->apid);
#endif

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

int switch_p_job_fini(switch_jobinfo_t *jobinfo)
{
#if defined(HAVE_NATIVE_CRAY) || defined(HAVE_CRAY_NETWORK)
	slurm_cray_jobinfo_t *job = (slurm_cray_jobinfo_t *) jobinfo;

	if (!job || (job->magic == CRAY_NULL_JOBINFO_MAGIC)) {
		CRAY_ERR("jobinfo pointer was NULL");
		return SLURM_SUCCESS;
	}

	xassert(job->magic == CRAY_JOBINFO_MAGIC);

#ifdef HAVE_NATIVE_CRAY
	int rc;
	char *path_name = NULL;

	/*
	 * Remove the APID directory LEGACY_SPOOL_DIR/<APID>
	 */
	path_name = xstrdup_printf(LEGACY_SPOOL_DIR "%" PRIu64, job->apid);

	// Stolen from ALPS
	recursive_rmdir(path_name);
	xfree(path_name);

	/*
	 * Remove the ALPS placement file.
	 * LEGACY_SPOOL_DIR/places<APID>
	 */
	path_name = xstrdup_printf(LEGACY_SPOOL_DIR "places%" PRIu64,
				   job->apid);
	rc = remove(path_name);
	if (rc) {
		CRAY_ERR("remove %s failed: %m", path_name);
		xfree(path_name);
		return SLURM_ERROR;
	}
	xfree(path_name);
#endif

#if defined(HAVE_NATIVE_CRAY_GA) || defined(HAVE_CRAY_NETWORK)
	// Remove the IAA file
	unlink_iaa_file(job);
#endif

#endif
	return SLURM_SUCCESS;
}

int switch_p_job_postfini(stepd_step_rec_t *job)
{
#if defined(HAVE_NATIVE_CRAY) || defined(HAVE_CRAY_NETWORK)
	int rc;
	char *err_msg = NULL;
	uid_t pgid = job->jmgr_pid;
#ifdef HAVE_NATIVE_CRAY
        int gpu_cnt = 0;
#endif

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

#endif
	return SLURM_SUCCESS;
}

int switch_p_job_attach(switch_jobinfo_t *jobinfo, char ***env, uint32_t nodeid,
			uint32_t procid, uint32_t nnodes, uint32_t nprocs,
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
 * switch functions for other purposes
 */
extern int switch_p_get_errno(void)
{
	return SLURM_SUCCESS;
}

extern char *switch_p_strerror(int errnum)
{
	return NULL ;
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

extern int switch_p_unpack_node_info(switch_node_info_t *switch_node,
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
	char *err_msg = NULL;
	int rc = 0;

	if (!job || (job->magic == CRAY_NULL_JOBINFO_MAGIC)) {
		CRAY_DEBUG("switch_job was NULL");
		return SLURM_SUCCESS;
	}

	if (debug_flags & DEBUG_FLAG_SWITCH) {
		CRAY_INFO("switch_p_job_step_complete");
	}

	/* Release the cookies */
	rc = alpsc_release_cookies(&err_msg, (int32_t *) job->cookie_ids,
				   (int32_t) job->num_cookies);
	ALPSC_SN_DEBUG("alpsc_release_cookies");
	if (rc != 0) {
		return SLURM_ERROR;

	}
#ifdef HAVE_NATIVE_CRAY
	/*
	 * Release the reserved PMI port
	 * If this fails, do not exit with an error.
	 */
	rc = release_port(job->port);
	if (rc != 0) {
		CRAY_ERR("Releasing port %" PRIu32 " failed.", job->port);
		// return SLURM_ERROR;
	}
#endif

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
#ifdef HAVE_NATIVE_CRAY
	/*
	 *  Initialize the port reservations.
	 *  Each job step will be allocated one port from amongst this set of
	 *  reservations for use by Cray's PMI for control tree communications.
	 */
	pthread_mutex_lock(&port_mutex);
	port_resv = bit_alloc(PORT_CNT);
	pthread_mutex_unlock(&port_mutex);
#endif
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
#if defined(HAVE_NATIVE_CRAY_GA) && !defined(HAVE_CRAY_NETWORK)
	slurm_cray_jobinfo_t *jobinfo = (slurm_cray_jobinfo_t *)job->switch_job;
	char *err_msg = NULL;
	int rc;

	rc = alpsc_pre_suspend(&err_msg, job->cont_id, jobinfo->ptags,
			       jobinfo->num_ptags, SUSPEND_TIMEOUT_MSEC);
	ALPSC_CN_DEBUG("alpsc_pre_suspend");
	if (rc != 1) {
		return SLURM_ERROR;
	}
#endif
	return SLURM_SUCCESS;
}

extern int switch_p_job_step_post_suspend(stepd_step_rec_t *job)
{
#if _DEBUG
	info("switch_p_job_step_post_suspend(job %u.%u)",
		job->jobid, job->stepid);
#endif
#if defined(HAVE_NATIVE_CRAY_GA) && !defined(HAVE_CRAY_NETWORK)
	char *err_msg = NULL;
	int rc;

	rc = alpsc_post_suspend(&err_msg, job->cont_id);
	ALPSC_CN_DEBUG("alpsc_post_suspend");
	if (rc != 1) {
		return SLURM_ERROR;
	}
#endif
	return SLURM_SUCCESS;
}

extern int switch_p_job_step_pre_resume(stepd_step_rec_t *job)
{
#if _DEBUG
	info("switch_p_job_step_pre_resume(job %u.%u)",
		job->jobid, job->stepid);
#endif
#if defined(HAVE_NATIVE_CRAY_GA) && !defined(HAVE_CRAY_NETWORK)
	slurm_cray_jobinfo_t *jobinfo = (slurm_cray_jobinfo_t *)job->switch_job;
	char *err_msg = NULL;
	int rc;

	rc = alpsc_pre_resume(&err_msg, job->cont_id, jobinfo->ptags,
			       jobinfo->num_ptags);
	ALPSC_CN_DEBUG("alpsc_pre_resume");
	if (rc != 1) {
		return SLURM_ERROR;
	}
#endif
	return SLURM_SUCCESS;
}

extern int switch_p_job_step_post_resume(stepd_step_rec_t *job)
{
#if _DEBUG
	info("switch_p_job_step_post_resume(job %u.%u)",
		job->jobid, job->stepid);
#endif
#if defined(HAVE_NATIVE_CRAY_GA) && !defined(HAVE_CRAY_NETWORK)
	char *err_msg = NULL;
	int rc;

	rc = alpsc_post_resume(&err_msg, job->cont_id);
	ALPSC_CN_DEBUG("alpsc_post_resume");
	if (rc != 1) {
		return SLURM_ERROR;
	}
#endif
	return SLURM_SUCCESS;
}
#endif /* !defined(__FreeBSD__) */
