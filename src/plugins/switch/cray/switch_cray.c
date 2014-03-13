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
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
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

#ifdef HAVE_NATIVE_CRAY
#include <job.h> /* Cray's job module component */
#include "alpscomm_cn.h"
#include "alpscomm_sn.h"
#endif


/* This allows for a BUILD time definition of LEGACY_SPOOL_DIR on the compile
 * line.
 * LEGACY_SPOOL_DIR can be customized to wherever the builder desires.
 * This customization could be important because the default is a hard-coded
 * path that does not vary regardless of where Slurm is installed.
 */
#ifndef LEGACY_SPOOL_DIR
#define LEGACY_SPOOL_DIR "/var/spool/alps/"
#endif

/*
 * CRAY_JOBINFO_MAGIC: The switch_jobinfo was not NULL.  The packed data
 *                     is good and can be safely unpacked.
 * CRAY_NULL_JOBINFO_MAGIC: The switch_jobinfo was NULL.  No data was packed.
 *                          Do not attempt to unpack any data.
 */
#define CRAY_JOBINFO_MAGIC	0xCAFECAFE
#define CRAY_NULL_JOBINFO_MAGIC	0xDEAFDEAF
#define MIN_PORT	20000
#define MAX_PORT	30000
#define ATTEMPTS	2
#define PORT_CNT	(MAX_PORT - MIN_PORT + 1)
#define SWITCH_BUF_SIZE (PORT_CNT + 128)

#ifdef HAVE_NATIVE_CRAY
#define SWITCH_CRAY_STATE_VERSION "PROTOCOL_VERSION"
static uint8_t port_resv[PORT_CNT];
static uint32_t last_alloc_port = MAX_PORT - MIN_PORT;
#endif

static uint32_t debug_flags = 0;

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

/* opaque data structures - no peeking! */
typedef struct slurm_cray_jobinfo {
	uint32_t magic;
	uint32_t num_cookies;	/* The number of cookies sent to configure the
	                           HSN */
	/* Double pointer to an array of cookie strings.
	 * cookie values here as NULL-terminated strings.
	 * There are num_cookies elements in the array.
	 * The caller is responsible for free()ing
	 * the array contents and the array itself.  */
	char     **cookies;
	/* The array itself must be free()d when this struct is destroyed. */
	uint32_t *cookie_ids;
	uint32_t port;/* Port for PMI Communications */
	uint32_t       jobid;  /* Current SLURM job id */
	uint32_t       stepid; /* Current step id */
	/* Cray Application ID; A unique combination of the job id and step id*/
	uint64_t apid;
} slurm_cray_jobinfo_t;

static void _print_jobinfo(slurm_cray_jobinfo_t *job);
#ifdef HAVE_NATIVE_CRAY
static int _list_str_to_array(char *list, int *cnt, int32_t **numbers);
static void _recursive_rmdir(const char *dirnm);
static int _get_first_pe(uint32_t nodeid, uint32_t task_count,
			 uint32_t **host_to_task_map, int32_t *first_pe);
static int _get_cpu_total(void);
static int _assign_port(uint32_t *ret_port);
static int _release_port(uint32_t real_port);
static void _free_alpsc_pe_info(alpsc_peInfo_t alpsc_pe_info);

static void _print_alpsc_pe_info(alpsc_peInfo_t alps_info)
{
	int i;
	info("*************************alpsc_peInfo Start"
	     "*************************");
	info("totalPEs: %d\nfirstPeHere: %d\npesHere: %d\npeDepth: %d\n",
	     alps_info.totalPEs, alps_info.firstPeHere, alps_info.pesHere,
	     alps_info.peDepth);
	for (i = 0; i < alps_info.totalPEs; i++) {
		info("Task: %d\tNode: %d", i, alps_info.peNidArray[i]);
	}
	info("*************************alpsc_peInfo Stop"
	     "*************************");
}
#endif

static void _print_jobinfo(slurm_cray_jobinfo_t *job)
{
	int i;

	if (!job || (job->magic == CRAY_NULL_JOBINFO_MAGIC)) {
		error("(%s: %d: %s) job pointer was NULL", THIS_FILE, __LINE__,
		      __FUNCTION__);
		return;
	}

	xassert(job->magic == CRAY_JOBINFO_MAGIC);

	info("Program: %s", slurm_prog_name);
	info("Address of slurm_cray_jobinfo_t structure: %p", job);
	info("--Begin Jobinfo--");
	info("  Magic: %" PRIx32, job->magic);
	info("  Job ID: %" PRIu32, job->jobid);
	info("  Step ID: %" PRIu32, job->stepid);
	info("  APID: %" PRIu64, job->apid);
	info("  PMI Port: %" PRIu32, job->port);
	info("  num_cookies: %" PRIu32, job->num_cookies);
	info("  --- cookies ---");
	for (i = 0; i < job->num_cookies; i++) {
		info("  cookies[%d]: %s", i, job->cookies[i]);
	}
	info("  --- cookie_ids ---");
	for (i = 0; i < job->num_cookies; i++) {
		info("  cookie_ids[%d]: %" PRIu32, i, job->cookie_ids[i]);
	}
	info("  ------");
	info("--END Jobinfo--");
}

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
		error("(%s: %d: %s) MAX_PORT: %d < MIN_PORT: %d",
		      THIS_FILE, __LINE__, __FUNCTION__, MAX_PORT, MIN_PORT);
		return SLURM_ERROR;
	}
#endif
	return SLURM_SUCCESS;
}

int fini(void)
{
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
	if (protocol_version == (uint16_t) NO_VAL) {
		error("*******************************************************");
		error("Can not recover switch/cray state, incompatible version");
		error("*******************************************************");
		return;
	}

	safe_unpack32(&min_port, buffer);
	safe_unpack32(&max_port, buffer);
	if ((min_port != MIN_PORT) || (max_port != MAX_PORT)) {
		error("*******************************************************");
		error("Can not recover switch/cray state");
		error("Changed MIN_PORT (%u != %u) and/or MAX_PORT (%u != %u)",
		      min_port, MIN_PORT, max_port, MAX_PORT);
		error("*******************************************************");
		return;
	}
	safe_unpack32(&last_alloc_port, buffer);
	for (i = 0; i < PORT_CNT; i++) {
		safe_unpack8(&port_resv[i], buffer);
	}
	return;

unpack_error:
	error("(%s: %d: %s) unpack error", THIS_FILE, __LINE__, __FUNCTION__);
	return;
}

static void _state_write_buf(Buf buffer)
{
	int i;

	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack32((uint32_t) MIN_PORT, buffer);
	pack32((uint32_t) MAX_PORT, buffer);
	pack32(last_alloc_port, buffer);
	for (i = 0; i < PORT_CNT; i++)
		pack8(port_resv[i], buffer);
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

	if (debug_flags & DEBUG_FLAG_SWITCH) {
		info("(%s: %d: %s) save to %s",
		     THIS_FILE, __LINE__, __FUNCTION__, dir_name);
	}

	buffer = init_buf(SWITCH_BUF_SIZE);
	_state_write_buf(buffer);
	file_name = xstrdup(dir_name);
	xstrcat(file_name, "/switch_cray_state");
	(void) unlink(file_name);
	state_fd = creat(file_name, 0600);
	if (state_fd < 0) {
		error("Can't save state, error creating file %s %m",
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
				error("Can't save switch state: %m");
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
		info("(%s: %d: %s) restore from %s, recover %d",
		     THIS_FILE, __LINE__, __FUNCTION__, dir_name,
		     (int) recover);
	}

	memset(port_resv, 0, PORT_CNT * sizeof(port_resv[0]));
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
				error ("Read error on %s, %m", file_name);
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
		error("No %s file for switch/cray state recovery", file_name);
		error("Starting switch/cray with clean state");
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
	memset(port_resv, 0, PORT_CNT * sizeof(port_resv[0]));
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
		info("(%s: %d: %s) switch_jobinfo_t contents",
		     THIS_FILE, __LINE__, __FUNCTION__);
		_print_jobinfo(new);
	}

	return SLURM_SUCCESS;
}

int switch_p_build_jobinfo(switch_jobinfo_t *switch_job,
			   slurm_step_layout_t *step_layout, char *network)
{

#ifdef HAVE_NATIVE_CRAY
	int i, rc, cnt = 0;
	uint32_t port = 0;
	int num_cookies = 2;
	char *err_msg = NULL;
	char **cookies = NULL, **s_cookies = NULL;
	int32_t *nodes = NULL, *cookie_ids = NULL;
	uint32_t *s_cookie_ids = NULL;
	slurm_cray_jobinfo_t *job = (slurm_cray_jobinfo_t *) switch_job;

	if (!job || (job->magic == CRAY_NULL_JOBINFO_MAGIC)) {
		debug2("(%s: %d: %s) switch_job was NULL", THIS_FILE, __LINE__,
		       __FUNCTION__);
		return SLURM_SUCCESS;
	}

	xassert(job->magic == CRAY_JOBINFO_MAGIC);

	rc = _list_str_to_array(step_layout->node_list, &cnt, &nodes);

	if (rc < 0) {
		error("(%s: %d: %s) list_str_to_array failed",
		      THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}
	if (step_layout->node_cnt != cnt) {
		error("(%s: %d: %s) list_str_to_array returned count %"
		      PRIu32 "does not match expected count %d",
		      THIS_FILE, __LINE__,
		      __FUNCTION__, cnt, step_layout->node_cnt);
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
	if (rc != 0) {
		if (err_msg) {
			error("(%s: %d: %s) alpsc_lease_cookies failed: %s",
			      THIS_FILE, __LINE__, __FUNCTION__, err_msg);
			free(err_msg);
		} else {
			error("(%s: %d: %s) alpsc_lease_cookies failed:"
			      " No error message present.",
			      THIS_FILE, __LINE__, __FUNCTION__);
		}
		xfree(nodes);
		return SLURM_ERROR;
	}
	if (err_msg) {
		info("(%s: %d: %s) alpsc_lease_cookies: %s",
		     THIS_FILE, __LINE__, __FUNCTION__, err_msg);
		free(err_msg);
	}

	xfree(nodes);


	/*
	 * Cookie ID safety check: The cookie_ids should be positive numbers.
	 */
	for (i=0; i<num_cookies; i++) {
		if (cookie_ids[i] < 0) {
			error("(%s: %d: %s) alpsc_lease_cookies returned a "
			      "cookie ID number %d with a negative value: %d",
			      THIS_FILE, __LINE__, __FUNCTION__, i,
			      cookie_ids[i]);
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

	/*
	 * Get a unique port for PMI communications
	 */
	rc = _assign_port(&port);
	if (rc < 0) {
		info("(%s: %d: %s) assign_port failed",
		     THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}

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
		debug2("(%s: %d: %s) switch_job was NULL", THIS_FILE, __LINE__,
		       __FUNCTION__);
		return;
	}

	if (job->magic != CRAY_JOBINFO_MAGIC) {
		error("job is not a switch/cray slurm_cray_jobinfo_t");
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
		info("(%s: %d: %s) switch_jobinfo_t contents",
		     THIS_FILE, __LINE__, __FUNCTION__);
		_print_jobinfo(job);
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
		debug2("(%s: %d: %s) switch_job was NULL", THIS_FILE, __LINE__,
		       __FUNCTION__);
		return SLURM_SUCCESS;
	}

	xassert(buffer);

	job = (slurm_cray_jobinfo_t *) switch_job;

	safe_unpack32(&job->magic, buffer);

	if (job->magic == CRAY_NULL_JOBINFO_MAGIC) {
		debug2("(%s: %d: %s) Nothing to unpack.",
		       THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_SUCCESS;
	}

	xassert(job->magic == CRAY_JOBINFO_MAGIC);
	safe_unpack32(&(job->num_cookies), buffer);
	safe_unpackstr_array(&(job->cookies), &num_cookies, buffer);
	if (num_cookies != job->num_cookies) {
		error("(%s: %d: %s) Wrong number of cookies received."
		      " Expected: %" PRIu32 "Received: %" PRIu32,
		      THIS_FILE, __LINE__, __FUNCTION__,
		      job->num_cookies, num_cookies);
		goto unpack_error;
	}
	safe_unpack32_array(&(job->cookie_ids), &num_cookies, buffer);
	if (num_cookies != job->num_cookies) {
		error("(%s: %d: %s) Wrong number of cookie IDs received."
		      " Expected: %" PRIu32 "Received: %" PRIu32,
		      THIS_FILE, __LINE__, __FUNCTION__,
		      job->num_cookies, num_cookies);
		goto unpack_error;
	}
	safe_unpack32(&job->port, buffer);
#ifdef HAVE_NATIVE_CRAY
	/* If the libstate save/restore failed, at least make sure that we
	 * do not re-allocate ports assigned to job steps that we recover. */
	if ((job->port >= MIN_PORT) && (job->port <= MAX_PORT))
		port_resv[job->port - MIN_PORT] = 1;
#endif

	if (debug_flags & DEBUG_FLAG_SWITCH) {
		info("(%s:%d: %s) switch_jobinfo_t contents:",
		     THIS_FILE, __LINE__, __FUNCTION__);
		_print_jobinfo(job);
	}

	return SLURM_SUCCESS;

unpack_error:
	error("(%s:%d: %s) Unpacking error", THIS_FILE, __LINE__,
	      __FUNCTION__);

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

#ifdef HAVE_NATIVE_CRAY
	slurm_cray_jobinfo_t *sw_job = (slurm_cray_jobinfo_t *) job->switch_job;
	int rc, num_ptags, cmd_index, num_app_cpus, i, j, cnt = 0;
	int mem_scaling, cpu_scaling;
	int total_cpus = 0;
	uint32_t total_mem = 0;
	int *ptags = NULL;
	char *err_msg = NULL, *apid_dir = NULL;
	alpsc_peInfo_t alpsc_pe_info = {-1, -1, -1, -1, NULL, NULL, NULL};
	FILE *f = NULL;
	size_t sz = 0;
	ssize_t lsz;
	char *lin = NULL;
	char meminfo_str[1024];
	int meminfo_value, gpu_enable = 0;
	uint32_t task;
	int32_t *task_to_nodes_map = NULL;
	int32_t *nodes = NULL;
	int32_t first_pe_here;
	gni_ntt_descriptor_t *ntt_desc_ptr = NULL;
	int gpu_cnt = 0;
	char *buff = NULL;
	size_t size;

	// Dummy variables to satisfy alpsc_write_placement_file
	int control_nid = 0, num_branches = 0;
	struct sockaddr_in control_soc;
	alpsc_branchInfo_t alpsc_branch_info;

	if (!sw_job || (sw_job->magic == CRAY_NULL_JOBINFO_MAGIC)) {
		debug2("(%s: %d: %s) job->switch_job was NULL",
		       THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_SUCCESS;
	}

	xassert(job->msg);
	xassert(sw_job->magic == CRAY_JOBINFO_MAGIC);

	if (debug_flags & DEBUG_FLAG_SWITCH) {
		info("(%s:%d: %s) Job ID (in JOB): %" PRIu32
		     "Job ID (in Switch jobinfo): %" PRIu32,
		     THIS_FILE, __LINE__, __FUNCTION__, job->jobid,
		     sw_job->jobid);
	}

	rc = alpsc_attach_cncu_container(&err_msg, sw_job->jobid, job->cont_id);
	if (rc != 1) {
		if (err_msg) {
			error("(%s: %d: %s) alpsc_attach_cncu_container failed:"
			      " %s",
			      THIS_FILE, __LINE__, __FUNCTION__, err_msg);
			free(err_msg);
		} else {
			error("(%s: %d: %s) alpsc_attach_cncu_container failed:"
			      " No error message present.",
			      THIS_FILE, __LINE__, __FUNCTION__);
		}
		return SLURM_ERROR;
	}
	if (err_msg) {
		info("(%s: %d: %s) alpsc_attach_cncu_container: %s", THIS_FILE,
		     __LINE__, __FUNCTION__, err_msg);
		free(err_msg);
	}

	/*
	 * Create APID directory
	 * Make its owner be the user who launched the application and under
	 * which the application will run.
	 */
	apid_dir = xstrdup_printf(LEGACY_SPOOL_DIR "%" PRIu64, sw_job->apid);
	if (NULL == apid_dir) {
		error("(%s: %d: %s) xstrdup_printf failed", THIS_FILE, __LINE__,
		      __FUNCTION__);
		return SLURM_ERROR;
	}

	rc = mkdir(apid_dir, 0700);
	if (rc) {
		xfree(apid_dir);
		error("(%s: %d: %s) mkdir failed to make directory %s: %m",
		      THIS_FILE, __LINE__, __FUNCTION__, apid_dir);
		return SLURM_ERROR;
	}

	rc = chown(apid_dir, job->uid, job->gid);
	if (rc) {
		free(apid_dir);
		error("(%s: %d: %s) chown failed: %m", THIS_FILE, __LINE__,
		      __FUNCTION__);
		return SLURM_ERROR;
	}

	xfree(apid_dir);
	/*
	 * Not defined yet -- This one may be skipped because we may not need to
	 * find the PAGG JOB container based on the APID.  It is part of the
	 * stepd_step_rec_t struct in the cont_id member, so if we have access
	 * to the struct, then we have access to the JOB container.
	 */

	// alpsc_set_PAGG_apid()

	/*
	 * Configure the network
	 *
	 * I'm setting exclusive flag to zero for now until we can figure out a
	 * way to guarantee that the application not only has exclusive access
	 * to the node but also will not be suspended.  This may not happen.
	 *
	 * Only configure the network if the application has more than one rank.
	 * Single rank applications have no other ranks to communicate with, so
	 * they do not need any network resources.
	 */

	if (job->ntasks > 1) {
		/*
		 * Get the number of CPUs.
		 */
		total_cpus = _get_cpu_total();
		if (total_cpus <= 0) {
			error("(%s: %d: %s) total_cpus <=0: %d",
			      THIS_FILE, __LINE__, __FUNCTION__, total_cpus);
			return SLURM_ERROR;
		}

		/*
		 * Use /proc/meminfo to get the total amount of memory on the
		 * node
		 */
		f = fopen("/proc/meminfo", "r");
		if (!f) {
			error("(%s: %d: %s) Failed to open /proc/meminfo: %m",
			      THIS_FILE, __LINE__, __FUNCTION__);
			return SLURM_ERROR;
		}

		while (!feof(f)) {
			lsz = getline(&lin, &sz, f);
			if (lsz > 0) {
				sscanf(lin, "%s %d", meminfo_str,
				       &meminfo_value);
				if (!strcmp(meminfo_str, "MemTotal:")) {
					total_mem = meminfo_value;
					break;
				}
			}
		}
		free(lin);
		TEMP_FAILURE_RETRY(fclose(f));

		if (total_mem == 0) {
			error("(%s: %d: %s) Scanning /proc/meminfo results in"
			      " MemTotal=0",
			      THIS_FILE, __LINE__, __FUNCTION__);
			return SLURM_ERROR;
		}

		/* If the submission didn't come from srun (API style)
		 * perhaps they didn't fill in things correctly.
		 */
		if (!job->cpus_per_task)
			job->cpus_per_task = 1;
		/*
		 * Scaling
		 * For the CPUS round the scaling to the nearest integer.
		 * If the scaling is greater than 100 percent, then scale it to
		 * 100%.
		 * If the scaling is zero, then return an error.
		 */
		num_app_cpus = job->node_tasks * job->cpus_per_task;
		if (num_app_cpus <= 0) {
			error("(%s: %d: %s) num_app_cpus <=0: %d",
			      THIS_FILE, __LINE__, __FUNCTION__, num_app_cpus);
			return SLURM_ERROR;
		}

		cpu_scaling = (((double) num_app_cpus / (double) total_cpus) *
			       (double) 100) + 0.5;
		if (cpu_scaling > 100) {
			error("(%s: %d: %s) Cpu scaling out of bounds: %d."
			      " Reducing to 100%%",
			      THIS_FILE, __LINE__, __FUNCTION__, cpu_scaling);
			cpu_scaling = 100;
		}
		if (cpu_scaling <= 0) {
			error("(%s: %d: %s) Cpu scaling out of bounds: %d."
			      " Increasing to 1%%",
			      THIS_FILE, __LINE__, __FUNCTION__, cpu_scaling);
			cpu_scaling = 1;
		}

		/*
		 * Scale total_mem, which is in kilobytes, to megabytes because
		 * app_mem is in megabytes.
		 * Round to the nearest integer.
		 * If the memory request is greater than 100 percent, then scale
		 * it to 100%.
		 * If the memory request is zero, then return an error.
		 *
		 * Note: Because this has caused some confusion in the past,
		 * The MEM_PER_CPU flag is used to indicate that job->step_mem
		 * is the amount of memory per CPU, not total.  However, this
		 * flag is read and cleared in slurmd prior to passing this
		 * value to slurmstepd.
		 * The value comes to slurmstepd already properly scaled.
		 * Thus, this function does not need to check the MEM_PER_CPU
		 * flag.
		 */
		mem_scaling = ((((double) job->step_mem /
				 ((double) total_mem / 1024)) * (double) 100))
			+ 0.5;
		if (mem_scaling > 100) {
			info("(%s: %d: %s) Memory scaling out of bounds: %d. "
			     "Reducing to 100%%.",
			     THIS_FILE, __LINE__, __FUNCTION__, mem_scaling);
			mem_scaling = 100;
		}

		if (mem_scaling <= 0) {
			error("(%s: %d: %s) Memory scaling out of bounds: %d. "
			      " Increasing to 1%%",
			      THIS_FILE, __LINE__, __FUNCTION__, mem_scaling);
			mem_scaling = 1;
		}

		if (debug_flags & DEBUG_FLAG_SWITCH) {
			info("(%s:%d: %s) --Network Scaling Start--",
			     THIS_FILE, __LINE__, __FUNCTION__);
			info("(%s:%d: %s) --CPU Scaling: %d--",
			     THIS_FILE, __LINE__, __FUNCTION__, cpu_scaling);

			info("(%s:%d: %s) --Memory Scaling: %d--",
			     THIS_FILE, __LINE__, __FUNCTION__, mem_scaling);
			info("(%s:%d: %s) --Network Scaling End--",
			     THIS_FILE, __LINE__, __FUNCTION__);

			info("(%s:%d: %s) --PAGG Job Container ID: %"PRIx64"--",
			     THIS_FILE, __LINE__, __FUNCTION__, job->cont_id);
		}

		rc = alpsc_configure_nic(&err_msg, 0, cpu_scaling, mem_scaling,
					 job->cont_id, sw_job->num_cookies,
					 (const char **) sw_job->cookies,
					 &num_ptags, &ptags, ntt_desc_ptr);
		/*
		 * We don't use the ptags because Cray's LLI acquires them
		 * itself, so they can be immediately discarded.
		 */
		free(ptags);
		if (rc != 1) {
			if (err_msg) {
				error("(%s: %d: %s) alpsc_configure_nic failed:"
				      " %s", THIS_FILE,
				      __LINE__, __FUNCTION__, err_msg);
				free(err_msg);
			} else {
				error("(%s: %d: %s) alpsc_configure_nic failed:"
				      " No error message present.",
				      THIS_FILE, __LINE__, __FUNCTION__);
			}
			return SLURM_ERROR;
		}
		if (err_msg) {
			info("(%s: %d: %s) alpsc_configure_nic: %s",
			     THIS_FILE, __LINE__, __FUNCTION__, err_msg);
			free(err_msg);
		}
	}

	// Not defined yet -- deferred
	//alpsc_config_gpcd();

	/*
	 * The following section will fill in the alpsc_peInfo structure which
	 * is the key argument to the alpsc_write_placement_file() call.
	 */
	alpsc_pe_info.totalPEs = job->ntasks;
	alpsc_pe_info.pesHere = job->node_tasks;
	alpsc_pe_info.peDepth = job->cpus_per_task;

	/*
	 * Fill in alpsc_pe_info.firstPeHere
	 */
	rc = _get_first_pe(job->nodeid, job->node_tasks,
			   job->msg->global_task_ids,
			   &first_pe_here);
	if (rc < 0) {
		error("(%s: %d: %s) get_first_pe failed", THIS_FILE, __LINE__,
		      __FUNCTION__);
		return SLURM_ERROR;
	}
	alpsc_pe_info.firstPeHere = first_pe_here;

	/*
	 * Fill in alpsc_pe_info.peNidArray
	 *
	 * The peNidArray maps tasks to nodes.
	 * Basically, reverse the tids variable which maps nodes to tasks.
	 */
	rc = _list_str_to_array(job->msg->complete_nodelist, &cnt, &nodes);
	if (rc < 0) {
		error("(%s: %d: %s) list_str_to_array failed",
		      THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}
	if (cnt == 0) {
		error("(%s: %d: %s) list_str_to_array returned a node count of"
		      " zero.",
		      THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_ERROR;
	}
	if (job->msg->nnodes != cnt) {
		error("(%s: %d: %s) list_str_to_array returned count %"
		      PRIu32 "does not match expected count %d",
		      THIS_FILE, __LINE__, __FUNCTION__, cnt,
		      job->msg->nnodes);
	}

	task_to_nodes_map = xmalloc(job->msg->ntasks * sizeof(int32_t));

	for (i = 0; i < job->msg->nnodes; i++) {
		for (j = 0; j < job->msg->tasks_to_launch[i]; j++) {
			task = job->msg->global_task_ids[i][j];
			task_to_nodes_map[task] = nodes[i];
			if (debug_flags & DEBUG_FLAG_SWITCH) {
				info("(%s:%d: %s) peNidArray:\tTask: %d\tNode:"
				     " %d", THIS_FILE,
				     __LINE__, __FUNCTION__, task, nodes[i]);
			}
		}
	}
	alpsc_pe_info.peNidArray = task_to_nodes_map;
	xfree(nodes);

	/*
	 * Fill in alpsc_pe_info.peCmdMapArray
	 *
	 * If the job is an SPMD job, then the command index (cmd_index) is 0.
	 * Otherwise, if the job is an MPMD job, then the command index
	 * (cmd_index) is equal to the number of executables in the job minus 1.
	 *
	 * TODO: Add MPMD support once SchedMD provides the needed MPMD data.
	 */

	if (!job->multi_prog) {
		/* SPMD Launch */
		cmd_index = 0;

		size = alpsc_pe_info.totalPEs * sizeof(int);
		alpsc_pe_info.peCmdMapArray = xmalloc(size);
		memset(alpsc_pe_info.peCmdMapArray, 0, size);
	} else {
		/* MPMD Launch */

		// Deferred support
		/*
		  ap.cmdIndex = ;
		  ap.peCmdMapArray = ;
		  ap.firstPeHere = ;

		  ap.pesHere = pesHere; // These need to be MPMD specific.
		*/

		error("(%s: %d: %s) MPMD Applications are not currently "
		      "supported.",
		      THIS_FILE, __LINE__, __FUNCTION__);
		goto error_free_alpsc_pe_info_t;
	}

	/*
	 * Fill in alpsc_pe_info.nodeCpuArray
	 * I don't know how to get this information from SLURM.
	 * Cray's PMI does not need the information.
	 * It may be used by debuggers like ATP or lgdb.  If so, then it will
	 * have to be filled in when support for them is added.
	 * Currently, it's all zeros.
	 */
	size = job->msg->nnodes * sizeof(int);
	alpsc_pe_info.nodeCpuArray = xmalloc(size);
	memset(alpsc_pe_info.nodeCpuArray, 0, size);
	if (job->msg->nnodes && !alpsc_pe_info.nodeCpuArray) {
		free(alpsc_pe_info.peCmdMapArray);
		error("(%s: %d: %s) failed to calloc nodeCpuArray.", THIS_FILE,
		      __LINE__, __FUNCTION__);
		goto error_free_alpsc_pe_info_t;
	}

	if (debug_flags & DEBUG_FLAG_SWITCH) {
		_print_alpsc_pe_info(alpsc_pe_info);
	}

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
	if (rc != 1) {
		if (err_msg) {
			error("(%s: %d: %s) alpsc_write_placement_file failed:"
			      " %s",
			      THIS_FILE, __LINE__, __FUNCTION__, err_msg);
			free(err_msg);
		} else {
			error("(%s: %d: %s) alpsc_write_placement_file failed:"
			      " No error message present.",
			      THIS_FILE, __LINE__, __FUNCTION__);
		}
		goto error_free_alpsc_pe_info_t;
	}
	if (err_msg) {
		info("(%s: %d: %s) alpsc_write_placement_file: %s",
		     THIS_FILE, __LINE__, __FUNCTION__, err_msg);
		free(err_msg);
	}


	/* Clean up alpsc_pe_info*/
	_free_alpsc_pe_info(alpsc_pe_info);

	/*
	 * Write the CRAY_NUM_COOKIES and CRAY_COOKIES variables out, too.
	 */
	rc = env_array_overwrite_fmt(&job->env, "CRAY_NUM_COOKIES", "%" PRIu32,
				     sw_job->num_cookies);
	if (rc == 0) {
		info("Failed to set env variable CRAY_NUM_COOKIES");
		return SLURM_ERROR;
	}

	/*
	 * Create the CRAY_COOKIES environment variable in the application's
	 * environment.
	 * Create one string containing a comma separated list of cookies.
	 */
	for (i = 0; i < sw_job->num_cookies; i++) {
		if (i > 0) {
			xstrfmtcat(buff, ",%s", sw_job->cookies[i]);
		} else
			xstrcat(buff, sw_job->cookies[i]);
	}

	rc = env_array_overwrite(&job->env, "CRAY_COOKIES", buff);
	if (rc == 0) {
		info("Failed to set env variable CRAY_COOKIES = %s", buff);
		xfree(buff);
		return SLURM_ERROR;
	}
	xfree(buff);

	/*
	 * Write the PMI_CONTROL_PORT
	 * Cray's PMI uses this is the port to communicate its control tree
	 * information.
	 */
	rc = env_array_overwrite_fmt(&job->env, "PMI_CONTROL_PORT", "%" PRIu32,
				     sw_job->port);
	if (rc == 0) {
		info("Failed to set env variable PMI_CONTROL_PORT");
		return SLURM_ERROR;
	}

	/*
	 * Query the generic resources to see if the GPU should be allocated
	 * TODO: Determine whether the proxy should be enabled or disabled by
	 * reading the user's environment variable.
	 */

	rc = gres_get_step_info(job->step_gres_list, "gpu", 0,
				GRES_STEP_DATA_COUNT, &gpu_cnt);
	info("gres_cnt: %d %u", rc, gpu_cnt);
	if (gpu_cnt > 0) {
		rc = alpsc_pre_launch_GPU_mps(&err_msg, gpu_enable);
		if (rc != 1) {
			if (err_msg) {
				error("(%s: %d: %s) alpsc_prelaunch_GPU_mps"
				      " failed: %s", THIS_FILE,
				      __LINE__, __FUNCTION__, err_msg);
				free(err_msg);
			} else {
				error("(%s: %d: %s) alpsc_prelaunch_GPU_mps"
				      " failed: No error message present.",
				      THIS_FILE, __LINE__, __FUNCTION__);
			}
			return SLURM_ERROR;
		}
		if (err_msg) {
			info("(%s: %d: %s) alpsc_prelaunch_GPU_mps: %s",
			     THIS_FILE, __LINE__, __FUNCTION__, err_msg);
			free(err_msg);
		}
	}

	/*
	 * Set the Job's APID
	 */
	job_setapid(getpid(), sw_job->apid);

	return SLURM_SUCCESS;

error_free_alpsc_pe_info_t:
	_free_alpsc_pe_info(alpsc_pe_info);
	return SLURM_ERROR;
#else
	return SLURM_SUCCESS;
#endif
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
#ifdef HAVE_NATIVE_CRAY
	slurm_cray_jobinfo_t *job = (slurm_cray_jobinfo_t *) jobinfo;

	if (!job || (job->magic == CRAY_NULL_JOBINFO_MAGIC)) {
		error("(%s: %d: %s) jobinfo pointer was NULL",
		      THIS_FILE, __LINE__, __FUNCTION__);
		return SLURM_SUCCESS;
	}

	xassert(job->magic == CRAY_JOBINFO_MAGIC);
	int rc;
	char *path_name = NULL;

	/*
	 * Remove the APID directory LEGACY_SPOOL_DIR/<APID>
	 */
	path_name = xstrdup_printf(LEGACY_SPOOL_DIR "%" PRIu64, job->apid);
	if (NULL == path_name) {
		error("(%s: %d: %s) xstrdup_printf failed", THIS_FILE, __LINE__,
		      __FUNCTION__);
		return SLURM_ERROR;
	}

	// Stolen from ALPS
	_recursive_rmdir(path_name);
	xfree(path_name);

	/*
	 * Remove the ALPS placement file.
	 * LEGACY_SPOOL_DIR/places<APID>
	 */
	path_name = xstrdup_printf(LEGACY_SPOOL_DIR "places%" PRIu64,
				   job->apid);
	if (NULL == path_name) {
		error("(%s: %d: %s) xstrdup_printf failed", THIS_FILE, __LINE__,
		      __FUNCTION__);
		return SLURM_ERROR;
	}
	rc = remove(path_name);
	if (rc) {
		error("(%s: %d: %s) remove %s failed: %m", THIS_FILE, __LINE__,
		      __FUNCTION__, path_name);
		return SLURM_ERROR;
	}
	xfree(path_name);

	/*
	 * TODO:
	 * Set the proxy back to the default state.
	 */
#endif
	return SLURM_SUCCESS;
}

int switch_p_job_postfini(stepd_step_rec_t *job)
{
#ifdef HAVE_NATIVE_CRAY
	int rc;
	char *err_msg = NULL;
	uid_t pgid = job->jmgr_pid;

	if (NULL == job->switch_job) {
		debug2("(%s: %d: %s) job->switch_job was NULL",
		       THIS_FILE, __LINE__, __FUNCTION__);
	}

	/*
	 *  Kill all processes in the job's session
	 */
	if (pgid) {
		debug2("Sending SIGKILL to pgid %lu", (unsigned long) pgid);
		kill(-pgid, SIGKILL);
	} else
		info("Job %u.%u: Bad pid value %lu", job->jobid, job->stepid,
		     (unsigned long) pgid);
	/*
	 * Clean-up
	 *
	 * 1. Flush Lustre caches
	 * 2. Flush virtual memory
	 * 3. Compact memory
	 */

	// Flush Lustre Cache
	rc = alpsc_flush_lustre(&err_msg);
	if (rc != 1) {
		if (err_msg) {
			error("(%s: %d: %s) alpsc_flush_lustre failed: %s",
			      THIS_FILE, __LINE__, __FUNCTION__, err_msg);
			free(err_msg);
		} else {
			error("(%s: %d: %s) alpsc_flush_lustre failed:"
			      " No error message present.",
			      THIS_FILE, __LINE__, __FUNCTION__);
		}
		return SLURM_ERROR;
	}
	if (err_msg) {
		info("(%s: %d: %s) alpsc_flush_lustre: %s", THIS_FILE, __LINE__,
		     __FUNCTION__, err_msg);
		free(err_msg);
	}

	// Flush virtual memory
	rc = system("echo 3 > /proc/sys/vm/drop_caches");
	if (rc != -1) {
		rc = WEXITSTATUS(rc);
	}
	if (rc) {
		error("(%s: %d: %s) Flushing virtual memory failed."
		      " Return code: %d",
		      THIS_FILE, __LINE__, __FUNCTION__, rc);
	}
	// do_drop_caches();

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
#ifdef HAVE_NATIVE_CRAY
	slurm_cray_jobinfo_t *job = (slurm_cray_jobinfo_t *) jobinfo;
	char *err_msg = NULL;
	int rc = 0;

	if (!job || (job->magic == CRAY_NULL_JOBINFO_MAGIC)) {
		debug2("(%s: %d: %s) switch_job was NULL", THIS_FILE, __LINE__,
		       __FUNCTION__);
		return SLURM_SUCCESS;
	}

	if (debug_flags & DEBUG_FLAG_SWITCH) {
		info("(%s:%d: %s) switch_p_job_step_complete", THIS_FILE,
		     __LINE__, __FUNCTION__);
	}

	/* Release the cookies */
	rc = alpsc_release_cookies(&err_msg, (int32_t *) job->cookie_ids,
				   (int32_t) job->num_cookies);

	if (rc != 0) {

		if (err_msg) {
			error("(%s: %d: %s) alpsc_release_cookies failed: %s",
			      THIS_FILE, __LINE__, __FUNCTION__, err_msg);
			free(err_msg);
		} else {
			error("(%s: %d: %s) alpsc_release_cookies failed:"
			      " No error message present.",
			      THIS_FILE, __LINE__, __FUNCTION__);
		}
		return SLURM_ERROR;

	}
	if (err_msg) {
		info("(%s: %d: %s) alpsc_release_cookies: %s",
		     THIS_FILE, __LINE__, __FUNCTION__, err_msg);
		free(err_msg);
	}

	/*
	 * Release the reserved PMI port
	 * If this fails, do not exit with an error.
	 */
	rc = _release_port(job->port);
	if (rc != 0) {
		error("(%s: %d: %s) Releasing port %" PRIu32 " failed.",
		      THIS_FILE, __LINE__, __FUNCTION__, job->port);
		// return SLURM_ERROR;
	}

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
	memset(port_resv, 0, PORT_CNT * sizeof(port_resv[0]));
#endif
	return SLURM_SUCCESS;
}

extern int switch_p_slurmd_init(void)
{
#ifdef HAVE_NATIVE_CRAY
	int rc = 0;
	char *err_msg = NULL;

	// Establish GPU's default state
	rc = alpsc_establish_GPU_mps_def_state(&err_msg);
	if (rc != 1) {
		if (err_msg) {
			error("(%s: %d: %s) alpsc_establish_GPU_mps_def_state"
			      " failed: %s",
			      THIS_FILE, __LINE__, __FUNCTION__, err_msg);
			free(err_msg);
		} else {
			error("(%s: %d: %s) alpsc_establish_GPU_mps_def_state"
			      " failed: No error message present.",
			      THIS_FILE, __LINE__, __FUNCTION__);
		}
		return SLURM_ERROR;
	}
	if (err_msg) {
		info("(%s: %d: %s) alpsc_establish_GPU_mps_def_state: %s",
		     THIS_FILE,	__LINE__, __FUNCTION__, err_msg);
		free(err_msg);
	}
#endif
	return SLURM_SUCCESS;
}

extern int switch_p_slurmd_step_init(void)
{
	return SLURM_SUCCESS;
}

#ifdef HAVE_NATIVE_CRAY

/*
 * Function: get_first_pe
 * Description:
 * Returns the first (i.e. lowest) PE on the node.
 *
 * IN:
 * nodeid -- Index of the node in the host_to_task_map
 * task_count -- Number of tasks on the node
 * host_to_task_map -- 2D array mapping the host to its tasks
 *
 * OUT:
 * first_pe -- The first (i.e. lowest) PE on the node
 *
 * RETURN
 * 0 on success and -1 on error
 */
static int _get_first_pe(uint32_t nodeid, uint32_t task_count,
			 uint32_t **host_to_task_map, int32_t *first_pe)
{

	int i, ret = 0;

	if (task_count == 0) {
		error("(%s: %d: %s) task_count == 0", THIS_FILE, __LINE__,
		      __FUNCTION__);
		return -1;
	}
	if (!host_to_task_map) {
		error("(%s: %d: %s) host_to_task_map == NULL",
		      THIS_FILE, __LINE__, __FUNCTION__);
		return -1;
	}
	*first_pe = host_to_task_map[nodeid][0];
	for (i = 0; i < task_count; i++) {
		if (host_to_task_map[nodeid][i] < *first_pe) {
			*first_pe = host_to_task_map[nodeid][i];
		}
	}
	return ret;
}

/*
 * Function: _list_str_to_array
 * Description:
 * 	Convert the list string into an array of integers.
 *
 * IN list     -- The list string
 * OUT cnt     -- The number of numbers in the list string
 * OUT numbers -- Array of integers;  Caller is responsible to xfree()
 *                this.
 *
 * N.B. Caller is responsible to xfree() numbers.
 *
 * RETURNS
 * Returns 0 on success and -1 on failure.
 */

static int _list_str_to_array(char *list, int *cnt, int32_t **numbers)
{

	int32_t *item_ptr = NULL;
	hostlist_t hl;
	int i, ret = 0;
	char *str, *cptr = NULL;

	/*
	 * Create a hostlist
	 */
	if (!(hl = hostlist_create(list))) {
		error("hostlist_create error on %s", list);
		return -1;
	}

	*cnt = hostlist_count(hl);

	if (!*cnt) {
		*numbers = NULL;
		return 0;
	}

	/*
	 * Create an integer array of item_ptr in the same order as in the list.
	 */
	i = 0;
	item_ptr = *numbers = xmalloc((*cnt) * sizeof(int32_t));
	while ((str = hostlist_shift(hl))) {
		if (!(cptr = strpbrk(str, "0123456789"))) {
			error("(%s: %d: %s) Error: Node was not recognizable:"
			      " %s",
			      THIS_FILE, __LINE__, __FUNCTION__, str);
			free(str);
			xfree(item_ptr);
			*numbers = NULL;
			hostlist_destroy(hl);
			return -1;
		}
		item_ptr[i] = atoll(cptr);
		i++;
		free(str);
	}

	// Clean up
	hostlist_destroy(hl);

	return ret;
}

/*
 * Recursive directory delete
 *
 * Call with a directory name and this function will delete
 * all files and directories rooted in this name. Finally
 * the named directory will be deleted.
 * If called with a file name, only that file will be deleted.
 *
 * Stolen from the ALPS code base.  I may need to write my own.
 */
static void _recursive_rmdir(const char *dirnm)
{
	int st;
	size_t dirnm_len, fnm_len, name_len;
	char *fnm = 0;
	DIR *dirp;
	struct dirent *dir;
	struct stat st_buf;

	/* Don't do anything if there is no directory name */
	if (!dirnm) {
		return;
	}
	dirp = opendir(dirnm);
	if (!dirp) {
		if (errno == ENOTDIR)
			goto fileDel;
		error("Error opening directory %s", dirnm);
		return;
	}

	dirnm_len = strlen(dirnm);
	if (dirnm_len == 0)
		return;
	while ((dir = readdir(dirp))) {
		name_len = strlen(dir->d_name);
		if (name_len == 1 && dir->d_name[0] == '.')
			continue;
		if (name_len == 2 && strcmp(dir->d_name, "..") == 0)
			continue;
		fnm_len = dirnm_len + name_len + 2;
		free(fnm);
		fnm = malloc(fnm_len);
		snprintf(fnm, fnm_len, "%s/%s", dirnm, dir->d_name);
		st = stat(fnm, &st_buf);
		if (st < 0) {
			error("stat of %s", fnm);
			continue;
		}
		if (st_buf.st_mode & S_IFDIR) {
			_recursive_rmdir(fnm);
		} else {

			st = unlink(fnm);
			if (st < 0 && errno == EISDIR)
				st = rmdir(fnm);
			if (st < 0 && errno != ENOENT) {
				error("(%s: %d: %s) Error removing %s",
				      THIS_FILE, __LINE__, __FUNCTION__, fnm);
			}
		}
	}
	free(fnm);
	closedir(dirp);
fileDel: st = unlink(dirnm);
	if (st < 0 && errno == EISDIR)
		st = rmdir(dirnm);
	if (st < 0 && errno != ENOENT) {
		error("(%s: %d: %s) Error removing %s", THIS_FILE, __LINE__,
		      __FUNCTION__, dirnm);
	}
}

/*
 * Function: get_cpu_total
 * Description:
 *  Get the total number of online cpus on the node.
 *
 * RETURNS
 *  Returns the number of online cpus on the node.  On error, it returns -1.
 *
 * TODO:
 * 	Danny suggests using xcgroup_get_param to read the CPU values instead of
 * 	this function.  Look at the way task/cgroup/task_cgroup_cpuset.c or
 * 	jobacct_gather/cgroup/jobacct_gather_cgroup.c does it.
 */
static int _get_cpu_total(void)
{
	FILE *f = NULL;
	char * token = NULL, *token1 = NULL, *token2 = NULL, *lin = NULL;
	char *saveptr = NULL, *saveptr1 = NULL, *endptr = NULL;
	int total = 0;
	ssize_t lsz;
	size_t sz;
	long int number1, number2;

	f = fopen("/sys/devices/system/cpu/online", "r");

	if (!f) {
		error("(%s: %d: %s) Failed to open file"
		      " /sys/devices/system/cpu/online: %m",
		      THIS_FILE, __LINE__, __FUNCTION__);
		return -1;
	}

	while (!feof(f)) {
		lsz = getline(&lin, &sz, f);
		if (lsz > 0) {
			token = strtok_r(lin, ",", &saveptr);
			while (token) {
				// Check for ranged sub-list
				token1 = strtok_r(token, "-", &saveptr1);
				if (token1) {
					number1 = strtol(token1, &endptr, 10);
					if ((number1 == LONG_MIN) ||
					    (number1 == LONG_MAX)) {
						error("(%s: %d: %s) Error: %m",
						      THIS_FILE, __LINE__,
						      __FUNCTION__);
						free(lin);
						TEMP_FAILURE_RETRY(fclose(f));
						return -1;
					} else if (endptr == token1) {
						error("(%s: %d: %s) Error:"
						      " Not a number: %s\n",
						      THIS_FILE, __LINE__,
						      __FUNCTION__, endptr);
						free(lin);
						TEMP_FAILURE_RETRY(fclose(f));
						return -1;
					}

					token2 = strtok_r(NULL, "-", &saveptr1);
					if (token2) {
						number2 = strtol(token2,
								 &endptr, 10);
						if ((number2 == LONG_MIN) ||
						    (number2 == LONG_MAX)) {
							error("(%s: %d: %s)"
							      " Error: %m",
							      THIS_FILE,
							      __LINE__,
							      __FUNCTION__);
							free(lin);
							TEMP_FAILURE_RETRY(
								fclose(f));
							return -1;
						} else if (endptr == token2) {
							error("(%s: %d: %s)"
							      " Error: Not a"
							      " number: '%s'\n",
							      THIS_FILE,
							      __LINE__,
							      __FUNCTION__,
							      endptr);
							free(lin);
							TEMP_FAILURE_RETRY(
								fclose(f));
							return -1;
						}

						total += number2 - number1 + 1;
					} else {
						total += 1;
					}
				}
				token = strtok_r(NULL, ",", &saveptr);
			}
		}
	}
	free(lin);
	TEMP_FAILURE_RETRY(fclose(f));
	return total;
}

/*
 * Function: assign_port
 * Description:
 *  Looks for and assigns the next free port.   This port is used by Cray's
 *  PMI for its communications to manage its control tree.
 *
 *  To avoid port conflicts, this function selects a large range of
 *  ports within the middle of the port range where it assumes no
 *  ports are used.  No special precautions are taken to handle a
 *  selected port already in use by some other non-SLURM component
 *  on the node.
 *
 *  If there are no free ports, then it loops through the entire table
 *  ATTEMPTS number of times before declaring a failure.
 *
 * Returns:
 *  0 on success and -1 on failure.
 */
static int _assign_port(uint32_t *real_port)
{
	int port, tmp, attempts = 0;

	if (!real_port) {
		error("(%s: %d: %s) real_port address was NULL.",
		      THIS_FILE, __LINE__, __FUNCTION__);
		return -1;
	}

	/*
	 * Ports is an index into the reserved port table.
	 * The ports range from 0 up to PORT_CNT.
	 */
	port = ++last_alloc_port % PORT_CNT;

	/*
	 * Find an unreserved port to assign.
	 * Abandon the attempt if we've been through the available ports ATTEMPT
	 * number of times
	 */
	while (port_resv[port] == 1) {
		tmp = ++port % PORT_CNT;
		port = tmp;
		attempts++;
		if ((attempts / PORT_CNT) >= ATTEMPTS) {
			error("(%s: %d: %s) No free ports among %d ports. "
			      " Went through entire port list %d times",
			      THIS_FILE, __LINE__, __FUNCTION__, PORT_CNT,
			      ATTEMPTS);
			return -1;
		}
	}

	port_resv[port] = 1;
	last_alloc_port = port;

	/*
	 * The port index must be scaled up by the MIN_PORT.
	 */
	*real_port = (port + MIN_PORT);
	return 0;
}

/*
 * Function: release_port
 * Description:
 *  Release the port.
 *
 * Returns:
 *  0 on success and -1 on failure.
 */
static int _release_port(uint32_t real_port)
{

	uint32_t port;

	if ((real_port < MIN_PORT) || (real_port >= MAX_PORT)) {
		error("(%s: %d: %s) Port %" PRIu32
		      "outside of valid range %" PRIu32
		      ": %" PRIu32, THIS_FILE, __LINE__,
		      __FUNCTION__, real_port, MIN_PORT, MAX_PORT);
		return -1;
	}

	port = real_port - MIN_PORT;

	if (port_resv[port]) {
		port_resv[port] = 0;
	} else {
		error("(%s: %d: %s) Attempting to release port %d,"
		      " but it was not reserved.",
		      THIS_FILE, __LINE__, __FUNCTION__, real_port);
		return -1;
	}
	return 0;
}

/*
 * Function: _free_alpsc_pe_info
 * Description:
 * 	Frees any allocated members of alpsc_pe_info.
 * Parameters:
 * IN	alpsc_pe_info:  alpsc_peInfo_t structure needing to be freed
 *
 * Returns
 * 	Void.
 */
static void _free_alpsc_pe_info(alpsc_peInfo_t alpsc_pe_info)
{
	if (alpsc_pe_info.peNidArray) {
		xfree(alpsc_pe_info.peNidArray);
	}
	if (alpsc_pe_info.peCmdMapArray) {
		xfree(alpsc_pe_info.peCmdMapArray);
	}
	if (alpsc_pe_info.nodeCpuArray) {
		xfree(alpsc_pe_info.nodeCpuArray);
	}
	return;
}
#endif
#endif
