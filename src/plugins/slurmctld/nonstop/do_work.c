/*****************************************************************************\
 *  do_work.c - Define functions that do most of the operations.
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Written by Morris Jette <jette@schedmd.com>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com>.
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

#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "slurm/smd_ns.h"

#include "src/common/slurm_xlator.h"	/* Must be first */
#include "src/common/bitstring.h"
#include "src/common/fd.h"
#include "src/common/job_resources.h"
#include "src/common/list.h"
#include "src/common/node_conf.h"
#include "src/common/parse_time.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/state_save.h"
#include "src/plugins/slurmctld/nonstop/do_work.h"
#include "src/plugins/slurmctld/nonstop/msg.h"
#include "src/plugins/slurmctld/nonstop/read_config.h"

/* Periodic activities, interval in seconds */
#define NONSTOP_EVENT_PERIOD	10
#define NONSTOP_SAVE_PERIOD	60

#define FAILURE_MAGIC 0x1234beef

/* Record of job's node failures */
typedef struct job_failures {
	slurm_addr_t		callback_addr;
	uint32_t		callback_flags;
	uint16_t		callback_port;
	uint32_t		job_id;
	struct job_record *	job_ptr;
	uint32_t		fail_node_cnt;
	uint32_t *		fail_node_cpus;
	char **			fail_node_names;
	uint32_t		magic;
	uint16_t		pending_job_delay;
	uint32_t		pending_job_id;
	char *			pending_node_name;
	uint32_t		replace_node_cnt;
	uint32_t		time_extend_avail;
	uint32_t		user_id;
} job_failures_t;
static List job_fail_list = NULL;
static pthread_mutex_t job_fail_mutex = PTHREAD_MUTEX_INITIALIZER;
static time_t job_fail_save_time   = (time_t) 0;
static time_t job_fail_update_time = (time_t) 0;

static bool thread_running = false;
static bool thread_shutdown = false;
static pthread_mutex_t thread_flag_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t msg_thread_id;

static void _job_fail_del(void *x)
{
	int i;
	job_failures_t *job_fail_ptr = (job_failures_t *) x;
	struct job_record *job_ptr;

	xassert(job_fail_ptr->magic == FAILURE_MAGIC);
	if (job_fail_ptr->pending_job_id) {
		job_ptr = find_job_record(job_fail_ptr->pending_job_id);
		if (job_ptr && (job_ptr->user_id == job_fail_ptr->user_id)) {
			(void) job_signal(job_ptr, SIGKILL, 0, 0, false);
		}
	}
	xfree(job_fail_ptr->fail_node_cpus);
	for (i = 0; i < job_fail_ptr->fail_node_cnt; i++)
		xfree(job_fail_ptr->fail_node_names[i]);
	xfree(job_fail_ptr->fail_node_names);
	job_fail_ptr->magic = 0;
	xfree(job_fail_ptr->pending_node_name);
	xfree(job_fail_ptr);
}

static int _job_fail_find(void *x, void *key)
{
	job_failures_t *job_fail_ptr = (job_failures_t *) x;
	uint32_t *job_id_ptr = (uint32_t *) key;

	if ((job_fail_ptr->job_id  == *job_id_ptr) &&
	    (job_fail_ptr->job_ptr != NULL) &&
	    (job_fail_ptr->job_ptr->job_id == *job_id_ptr) &&
	    (job_fail_ptr->job_ptr->magic == JOB_MAGIC))
		return 1;
	return 0;
}

static void _job_fail_log(job_failures_t *job_fail_ptr)
{
	uint16_t port;
	char ip[32];
	int i;

	if (nonstop_debug > 0) {
		info("nonstop: =====================");
		info("nonstop: job_id: %u", job_fail_ptr->job_id);
		slurm_get_ip_str(&job_fail_ptr->callback_addr, &port,
				 ip, sizeof(ip));
		info("nonstop: callback_addr: %s", ip);
		info("nonstop: callback_flags: %x",
		     job_fail_ptr->callback_flags);
		info("nonstop: callback_port: %u",
		     job_fail_ptr->callback_port);
		info("nonstop: fail_node_cnt: %u",
		     job_fail_ptr->fail_node_cnt);
		for (i = 0; i < job_fail_ptr->fail_node_cnt; i++) {
			info("nonstop: fail_node_cpus[%d]: %u",
			     i, job_fail_ptr->fail_node_cpus[i]);
			info("nonstop: fail_node_names[%d]: %s",
			     i, job_fail_ptr->fail_node_names[i]);
		}
		info("nonstop: pending_job_delay: %hu",
		     job_fail_ptr->pending_job_delay);
		info("nonstop: pending_job_id: %u",
		     job_fail_ptr->pending_job_id);
		info("nonstop: pending_node_name: %s",
		     job_fail_ptr->pending_node_name);
		info("nonstop: replace_node_cnt: %u",
		     job_fail_ptr->replace_node_cnt);
		info("nonstop: time_extend_avail: %u",
		     job_fail_ptr->time_extend_avail);
		info("nonstop: user_id: %u", job_fail_ptr->user_id);
		info("nonstop: =====================");
	}
}

static bool _valid_job_ptr(job_failures_t *job_fail_ptr)
{
	if ((job_fail_ptr->job_ptr != NULL) &&
	    (job_fail_ptr->job_ptr->job_id == job_fail_ptr->job_id) &&
	    (job_fail_ptr->job_ptr->magic == JOB_MAGIC))
		return true;

	job_fail_ptr->job_ptr = NULL;
	return false;
}

static bool _valid_drain_user(uid_t cmd_uid)
{
	int i;

	for (i = 0; i < user_drain_deny_cnt; i++) {
		if ((user_drain_deny[i] == cmd_uid) ||
		    (user_drain_deny[i] == NO_VAL)) /* ALL */
			return false;
	}
	for (i = 0; i < user_drain_allow_cnt; i++) {
		if ((user_drain_allow[i] == cmd_uid) ||
		    (user_drain_allow[i] == NO_VAL)) /* ALL */
			return true;
	}
	return false;
}

static void _pack_job_state(job_failures_t *job_fail_ptr, Buf buffer)
{
	int i;

	slurm_pack_slurm_addr(&job_fail_ptr->callback_addr, buffer);
	pack32(job_fail_ptr->callback_flags, buffer);
	pack16(job_fail_ptr->callback_port, buffer);
	pack32(job_fail_ptr->job_id, buffer);
	pack32(job_fail_ptr->fail_node_cnt, buffer);
	for (i = 0; i < job_fail_ptr->fail_node_cnt; i++) {
		pack32(job_fail_ptr->fail_node_cpus[i], buffer);
		packstr(job_fail_ptr->fail_node_names[i], buffer);
	}
	pack16(job_fail_ptr->pending_job_delay, buffer);
	pack32(job_fail_ptr->pending_job_id, buffer);
	packstr(job_fail_ptr->pending_node_name, buffer);
	pack32(job_fail_ptr->replace_node_cnt, buffer);
	pack32(job_fail_ptr->time_extend_avail, buffer);
	pack32(job_fail_ptr->user_id, buffer);
}

static int _unpack_job_state(job_failures_t **job_pptr, Buf buffer)
{
	job_failures_t *job_fail_ptr;
	uint32_t dummy32;
	int i;

	job_fail_ptr = xmalloc(sizeof(job_failures_t));
	if (slurm_unpack_slurm_addr_no_alloc(&job_fail_ptr->callback_addr,
					     buffer))
		goto unpack_error;
	safe_unpack32(&job_fail_ptr->callback_flags, buffer);
	safe_unpack16(&job_fail_ptr->callback_port, buffer);
	safe_unpack32(&job_fail_ptr->job_id, buffer);
	safe_unpack32(&job_fail_ptr->fail_node_cnt, buffer);
	safe_xcalloc(job_fail_ptr->fail_node_cpus,job_fail_ptr->fail_node_cnt,
		     sizeof(uint32_t));
	safe_xcalloc(job_fail_ptr->fail_node_names, job_fail_ptr->fail_node_cnt,
		     sizeof(char *));
	for (i = 0; i < job_fail_ptr->fail_node_cnt; i++) {
		safe_unpack32(&job_fail_ptr->fail_node_cpus[i], buffer);
		safe_unpackstr_xmalloc(&job_fail_ptr->fail_node_names[i],
				       &dummy32, buffer);
	}
	job_fail_ptr->magic = FAILURE_MAGIC;
	safe_unpack16(&job_fail_ptr->pending_job_delay, buffer);
	safe_unpack32(&job_fail_ptr->pending_job_id, buffer);
	safe_unpackstr_xmalloc(&job_fail_ptr->pending_node_name,
			       &dummy32, buffer);
	safe_unpack32(&job_fail_ptr->replace_node_cnt, buffer);
	safe_unpack32(&job_fail_ptr->time_extend_avail, buffer);
	safe_unpack32(&job_fail_ptr->user_id, buffer);
	_job_fail_log(job_fail_ptr);
	*job_pptr = job_fail_ptr;
	return SLURM_SUCCESS;

unpack_error:
	*job_pptr = NULL;
	xfree(job_fail_ptr->fail_node_cpus);
	for (i = 0; i < job_fail_ptr->fail_node_cnt; i++)
		xfree(job_fail_ptr->fail_node_names[i]);
	xfree(job_fail_ptr->fail_node_names);
	xfree(job_fail_ptr->pending_node_name);
	xfree(job_fail_ptr);
	return SLURM_ERROR;
}

static int _update_job(job_desc_msg_t * job_specs, uid_t uid)
{
	slurm_msg_t msg;

	msg.data= job_specs;
	msg.conn_fd = -1;
	return update_job(&msg, uid, true);
}

/*
 * Save all nonstop plugin state information
 */
extern int save_nonstop_state(void)
{
	char *dir_path, *old_file, *new_file, *reg_file;
	Buf buffer = init_buf(0);
	time_t now = time(NULL);
	job_failures_t *job_fail_ptr;
	ListIterator job_iterator;
	uint32_t job_cnt = 0;
	int error_code = SLURM_SUCCESS;
	int log_fd;

	/* write header: version, time */
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(now, buffer);

	/* write individual job records */
	slurm_mutex_lock(&job_fail_mutex);
	if (job_fail_list) {
		job_cnt = list_count(job_fail_list);
		pack32(job_cnt, buffer);
		job_iterator = list_iterator_create(job_fail_list);
		while ((job_fail_ptr=(job_failures_t *)list_next(job_iterator)))
			_pack_job_state(job_fail_ptr, buffer);
		list_iterator_destroy(job_iterator);
	} else {
		pack32(job_cnt, buffer);
	}
	job_fail_save_time = now;
	slurm_mutex_unlock(&job_fail_mutex);

	/* write the buffer to file */
	dir_path = slurm_get_state_save_location();
	old_file = xstrdup(dir_path);
	xstrcat(old_file, "/nonstop_state.old");
	reg_file = xstrdup(dir_path);
	xstrcat(reg_file, "/nonstop_state");
	new_file = xstrdup(dir_path);
	xstrcat(new_file, "/nonstop_state.new");

	log_fd = creat(new_file, 0600);
	if (log_fd < 0) {
		error("Can't save state, create file %s error %m",
		      new_file);
		error_code = errno;
	} else {
		int pos = 0, nwrite = get_buf_offset(buffer), amount, rc;
		char *data = (char *)get_buf_data(buffer);
		while (nwrite > 0) {
			amount = write(log_fd, &data[pos], nwrite);
			if ((amount < 0) && (errno != EINTR)) {
				error("Error writing file %s, %m", new_file);
				error_code = errno;
				break;
			}
			nwrite -= amount;
			pos    += amount;
		}

		rc = fsync_and_close(log_fd, "job");
		if (rc && !error_code)
			error_code = rc;
	}
	if (error_code)
		(void) unlink(new_file);
	else {			/* file shuffle */
		(void) unlink(old_file);
		if (link(reg_file, old_file))
			debug4("unable to create link for %s -> %s: %m",
			       reg_file, old_file);
		(void) unlink(reg_file);
		if (link(new_file, reg_file))
			debug4("unable to create link for %s -> %s: %m",
			       new_file, reg_file);
		(void) unlink(new_file);
	}
	xfree(dir_path);
	xfree(old_file);
	xfree(reg_file);
	xfree(new_file);
	free_buf(buffer);

	return error_code;
}

/*
 * Restore all nonstop plugin state information
 */
extern int restore_nonstop_state(void)
{
	char *dir_path, *state_file;
	uint32_t job_cnt = 0;
	uint16_t protocol_version = NO_VAL16;
	Buf buffer;
	int error_code = SLURM_SUCCESS, i;
	time_t buf_time;
	job_failures_t *job_fail_ptr = NULL;

	dir_path = slurm_get_state_save_location();
	state_file = xstrdup(dir_path);
	xstrcat(state_file, "/nonstop_state");
	xfree(dir_path);

	if (!(buffer = create_mmap_buf(state_file))) {
		error("No nonstop state file (%s) to recover", state_file);
		xfree(state_file);
		return error_code;
	}
	xfree(state_file);

	/* Validate state version */
	safe_unpack16(&protocol_version, buffer);
	debug3("Version in slurmctld/nonstop header is %u", protocol_version);

	if (protocol_version == NO_VAL16) {
		if (!ignore_state_errors)
			fatal("Can not recover slurmctld/nonstop state, incompatible version, start with '-i' to ignore this");
		error("*************************************************************");
		error("Can not recover slurmctld/nonstop state, incompatible version");
		error("*************************************************************");
		free_buf(buffer);
		return EFAULT;
	}
	safe_unpack_time(&buf_time, buffer);
	safe_unpack32(&job_cnt, buffer);
	slurm_mutex_lock(&job_fail_mutex);
	for (i = 0; i < job_cnt; i++) {
		error_code = _unpack_job_state(&job_fail_ptr, buffer);
		if (error_code)
			break;
		job_fail_ptr->job_ptr = find_job_record(job_fail_ptr->job_id);
		if (!job_fail_ptr->job_ptr ||
		    (job_fail_ptr->job_ptr->user_id != job_fail_ptr->user_id)) {
			_job_fail_del(job_fail_ptr);
			continue;
		}
		list_append(job_fail_list, job_fail_ptr);
	}
	slurm_mutex_unlock(&job_fail_mutex);
	free_buf(buffer);
	return error_code;

unpack_error:
	if (!ignore_state_errors)
		fatal("Incomplete nonstop state file, start with '-i' to ignore this");
	error("Incomplete nonstop state file");
	free_buf(buffer);
	return SLURM_ERROR;
}

extern void init_job_db(void)
{
	slurm_mutex_lock(&job_fail_mutex);
	if (!job_fail_list)
		job_fail_list = list_create(_job_fail_del);
	slurm_mutex_unlock(&job_fail_mutex);
}

extern void term_job_db(void)
{
	slurm_mutex_lock(&job_fail_mutex);
	FREE_NULL_LIST(job_fail_list);
	slurm_mutex_unlock(&job_fail_mutex);
}

static uint32_t _get_job_cpus(struct job_record *job_ptr, int node_inx)
{
	struct node_record *node_ptr;
	uint32_t cpus_alloc;
	int i, j;

	node_ptr = node_record_table_ptr + node_inx;
	cpus_alloc =  node_ptr->cpus;
	if (job_ptr->job_resrcs &&
	    job_ptr->job_resrcs->cpus &&
	    job_ptr->job_resrcs->node_bitmap &&
	    ((i = bit_ffs(job_ptr->job_resrcs->node_bitmap)) >= 0)) {
		for (j = 0 ; i <= node_inx; i++) {
			if (i == node_inx) {
				cpus_alloc = job_ptr->job_resrcs->cpus[j];
				break;
			}
			if (bit_test(job_ptr->job_resrcs->node_bitmap, i))
				j++;
		}
	}
	return cpus_alloc;
}

/* Some node is failing, but we lack a specific job ID, so see what jobs
 * have registered and have this node in their job allocaiton */
static void _failing_node(struct node_record *node_ptr)
{
	job_failures_t *job_fail_ptr;
	ListIterator job_iterator;
	struct job_record *job_ptr;
	time_t now = time(NULL);
	uint32_t event_flag = 0;
	int node_inx;

	info("node_fail_callback for node:%s", node_ptr->name);
	if (!job_fail_list)
		return;
	if (IS_NODE_DOWN(node_ptr))
		event_flag |= SMD_EVENT_NODE_FAILED;
	if (IS_NODE_FAIL(node_ptr))
		event_flag |= SMD_EVENT_NODE_FAILING;
	node_inx = node_ptr - node_record_table_ptr;
	slurm_mutex_lock(&job_fail_mutex);
	job_iterator = list_iterator_create(job_fail_list);
	while ((job_fail_ptr = (job_failures_t *) list_next(job_iterator))) {
		if (!_valid_job_ptr(job_fail_ptr))
			continue;
		job_ptr = job_fail_ptr->job_ptr;
		if (IS_JOB_FINISHED(job_ptr) || !job_ptr->node_bitmap ||
		    !bit_test(job_ptr->node_bitmap, node_inx))
			continue;
		job_fail_ptr->callback_flags |= event_flag;
		job_fail_update_time = now;
	}
	list_iterator_destroy(job_iterator);
	slurm_mutex_unlock(&job_fail_mutex);
}

extern void node_fail_callback(struct job_record *job_ptr,
			       struct node_record *node_ptr)
{
	job_failures_t *job_fail_ptr;
	uint32_t event_flag = 0;
	int node_inx;

	if (!job_ptr) {
		_failing_node(node_ptr);
		return;
	}

	info("node_fail_callback for job:%u node:%s",
	     job_ptr->job_id, node_ptr->name);
	if (IS_NODE_DOWN(node_ptr))
		event_flag |= SMD_EVENT_NODE_FAILED;
	if (IS_NODE_FAIL(node_ptr))
		event_flag |= SMD_EVENT_NODE_FAILING;
	slurm_mutex_lock(&job_fail_mutex);
	job_fail_ptr = list_find_first(job_fail_list, _job_fail_find,
				       &job_ptr->job_id);
	if (!job_fail_ptr) {
		job_fail_ptr = xmalloc(sizeof(job_failures_t));
		job_fail_ptr->job_id  = job_ptr->job_id;
		job_fail_ptr->job_ptr = job_ptr;
		job_fail_ptr->magic   = FAILURE_MAGIC;
		job_fail_ptr->user_id = job_ptr->user_id;
		list_append(job_fail_list, job_fail_ptr);
	}
	job_fail_ptr->callback_flags |= event_flag;
	job_fail_ptr->fail_node_cnt++;
	xrealloc(job_fail_ptr->fail_node_cpus,
		 (sizeof(uint32_t) * job_fail_ptr->fail_node_cnt));
	node_inx = node_ptr - node_record_table_ptr;
	job_fail_ptr->fail_node_cpus[job_fail_ptr->fail_node_cnt - 1] =
		_get_job_cpus(job_ptr, node_inx);
	xrealloc(job_fail_ptr->fail_node_names,
		 (sizeof(char *) * job_fail_ptr->fail_node_cnt));
	job_fail_ptr->fail_node_names[job_fail_ptr->fail_node_cnt - 1] =
			xstrdup(node_ptr->name);
	job_fail_ptr->time_extend_avail += time_limit_extend;
	job_fail_update_time = time(NULL);
	slurm_mutex_unlock(&job_fail_mutex);
}

extern void job_begin_callback(struct job_record *job_ptr)
{
	job_failures_t *job_fail_ptr = NULL;
	struct depend_spec *depend_ptr;
	ListIterator depend_iterator;

	info("job_begin_callback for job:%u", job_ptr->job_id);
	if (!job_fail_list || !job_ptr->details ||
	   !job_ptr->details->depend_list)
		return;
	slurm_mutex_lock(&job_fail_mutex);
	depend_iterator = list_iterator_create(job_ptr->details->depend_list);
	depend_ptr = (struct depend_spec *) list_next(depend_iterator);
	if (depend_ptr && (depend_ptr->depend_type == SLURM_DEPEND_EXPAND)) {
		job_fail_ptr = list_find_first(job_fail_list, _job_fail_find,
					       &depend_ptr->job_id);
	}
	if (job_fail_ptr) {
		job_fail_ptr->callback_flags |= SMD_EVENT_NODE_REPLACE;
		job_fail_update_time = time(NULL);
		debug("%s: jobid %d flags 0x%x", __func__, job_ptr->job_id,
		      job_fail_ptr->callback_flags);
	}
	list_iterator_destroy(depend_iterator);
	slurm_mutex_unlock(&job_fail_mutex);
}

extern void job_fini_callback(struct job_record *job_ptr)
{
	info("job_fini_callback for job:%u", job_ptr->job_id);
	slurm_mutex_lock(&job_fail_mutex);
	list_delete_all(job_fail_list, _job_fail_find, &job_ptr->job_id);
	/* job_fail_update_time = time(NULL);	not critical */
	slurm_mutex_unlock(&job_fail_mutex);
}

/*
 * Drain nodes which a user believes are bad
 * cmd_ptr IN - Input format "DRAIN:NODES:name:REASON:string"
 * cmd_uid IN - User issuing the RPC
 * protocol_version IN - Communication protocol version number
 * RET - Response string, must be freed by the user
 */
extern char *drain_nodes_user(char *cmd_ptr, uid_t cmd_uid,
			      uint32_t protocol_version)
{
	update_node_msg_t update_node_msg;
	char *node_names = NULL, *reason = NULL;
	char *sep1, *sep2;
	char *resp = NULL;
	int rc;

	if (!_valid_drain_user(cmd_uid)) {
		char *user_name = uid_to_string(cmd_uid);
		error("slurmctld/nonstop: User %s(%u) attempted to drain node. "
		      "Permission denied", user_name, cmd_uid);
		xfree(user_name);
		xstrfmtcat(resp, "%s EPERM", SLURM_VERSION_STRING);
		goto fini;
	}
	sep1 = cmd_ptr + 12;
	if (sep1[0] == '\"') {
		node_names = xstrdup(sep1 + 1);
		sep2 = strchr(node_names, '\"');
		if (!sep2) {
			xstrfmtcat(resp, "%s ECMD", SLURM_VERSION_STRING);
			goto fini;
		}
		sep2[0] = '\0';
	} else {
		node_names = xstrdup(sep1);
		sep2 = strchr(node_names, ':');
		if (!sep2) {
			xstrfmtcat(resp, "%s ECMD", SLURM_VERSION_STRING);
			goto fini;
		}
		sep2[0] = '\0';
	}

	sep1 = strstr(cmd_ptr + 12, "REASON:");
	if (!sep1) {
		xstrfmtcat(resp, "%s ECMD", SLURM_VERSION_STRING);
		goto fini;
	}
	sep1 += 7;
	if (sep1[0] == '\"') {
		reason = xstrdup(sep1 + 1);
		sep2 = strchr(reason, '\"');
		if (!sep2) {
			xstrfmtcat(resp, "%s ECMD", SLURM_VERSION_STRING);
			goto fini;
		}
		sep2[0] = '\0';
	} else {
		reason = xstrdup(sep1);
		sep2 = strchr(reason, ':');
		if (!sep2) {
			xstrfmtcat(resp, "%s ECMD", SLURM_VERSION_STRING);
			goto fini;
		}
		sep2[0] = '\0';
	}

	slurm_init_update_node_msg(&update_node_msg);
	update_node_msg.node_names = node_names;
	update_node_msg.node_state = NODE_STATE_FAIL;
	update_node_msg.reason = reason;
	update_node_msg.reason_uid = cmd_uid;
	rc = update_node(&update_node_msg);
	if (rc) {
		/* Log it but send back only the error with the version.
		 * xstrfmtcat(resp, "%s EUPDNODE %s", SLURM_VERSION_STRING,
		 * slurm_strerror(rc));
		 */
		xstrfmtcat(resp, "%s EUPDNODE", SLURM_VERSION_STRING);
	} else {
		xstrfmtcat(resp, "%s ENOERROR", SLURM_VERSION_STRING);
	}

fini:	xfree(node_names);
	xfree(reason);
	debug("%s: replying to library: %s", __func__, resp);
	return resp;
}

/*
 * Identify a job's failed and failing nodes
 * cmd_ptr IN - Input format "GET_FAIL_NODES:JOBID:#:STATE_FLAGS:#"
 * cmd_uid IN - User issuing the RPC
 * protocol_version IN - Communication protocol version number
 * RET - Response string, must be freed by the user
 */
extern char *fail_nodes(char *cmd_ptr, uid_t cmd_uid,
			uint32_t protocol_version)
{
	job_failures_t *job_fail_ptr;
	struct node_record *node_ptr;
	struct job_record *job_ptr;
	uint32_t job_id;
	char *sep1;
	char *resp = NULL;
	int i, i_first, i_last;
	int fail_cnt = 0;
	int state_flags;

	sep1 = cmd_ptr + 21;
	job_id = atoi(sep1);
	sep1 = strstr(sep1, "STATE_FLAGS:");
	if (!sep1) {
		xstrfmtcat(resp, "%s ECMD", SLURM_VERSION_STRING);
		goto fini;
	}
	state_flags = atoi(sep1 + 12);

	slurm_mutex_lock(&job_fail_mutex);
	job_ptr = find_job_record(job_id);
	if (!job_ptr) {
		xstrfmtcat(resp, "%s EJOBID", SLURM_VERSION_STRING);
		goto fini;
	}

	if ((cmd_uid != job_ptr->user_id) &&
	    (cmd_uid != 0) &&
	    (cmd_uid != getuid())) {
		info("slurmctld/nonstop: Security violation, User ID %u "
		     "attempting to get information about job ID %u",
		     cmd_uid, job_ptr->job_id);
		xstrfmtcat(resp, "%s EPERM", SLURM_VERSION_STRING);
		goto fini;
	}

	xstrfmtcat(resp, "%s ENOERROR ", SLURM_VERSION_STRING);
	if ((state_flags & FAILING_NODES) && job_ptr->node_bitmap) {
		i_first = bit_ffs(job_ptr->node_bitmap);
		if (i_first == -1)
			i_last = -2;
		else
			i_last = bit_fls(job_ptr->node_bitmap);
		for (i = i_first; i <= i_last; i++) {
			if (!bit_test(job_ptr->node_bitmap, i))
				continue;
			node_ptr = node_record_table_ptr + i;
			if (!IS_NODE_FAIL(node_ptr))
				continue;
			fail_cnt++;
			/* Format: nodename number_of_cpus state */
			xstrfmtcat(resp, "%s %u %u ",
				   node_ptr->name,
				   _get_job_cpus(job_ptr, i),
				   FAILING_NODES);
		}
	}

	if (state_flags & FAILED_NODES) {
		job_fail_ptr = list_find_first(job_fail_list, _job_fail_find,
					       &job_id);
		if (job_fail_ptr && _valid_job_ptr(job_fail_ptr)) {
			for (i = 0; i < job_fail_ptr->fail_node_cnt; i++) {
				/* Format: nodename number_of_cpus state */
				xstrfmtcat(resp, "%s %u %u ",
					   job_fail_ptr->fail_node_names[i],
					   job_fail_ptr->fail_node_cpus[i],
					   FAILED_NODES);
			}
		}
	}

fini:	slurm_mutex_unlock(&job_fail_mutex);
	debug("%s: replying to library: %s", __func__, resp);
	return resp;
}

static void _kill_job(uint32_t job_id, uid_t cmd_uid)
{
	int rc;

	rc = job_signal_id(job_id, SIGKILL, 0, cmd_uid, false);
	if (rc) {
		info("slurmctld/nonstop: can not kill job %u: %s",
		     job_id, slurm_strerror(rc));
	}
}

/*
 * Register a callback port for job events, set port to zero to clear
 * cmd_ptr IN - Input format "CALLBACK:JOBID:#:PORT:#"
 * cmd_uid IN - User issuing the RPC
 * cli_addr IN - Client communication address (host for response)
 * protocol_version IN - Communication protocol version number
 * RET - Response string, must be freed by the user
 */
extern char *register_callback(char *cmd_ptr, uid_t cmd_uid,
			       slurm_addr_t cli_addr,
			       uint32_t protocol_version)
{
	job_failures_t *job_fail_ptr;
	struct job_record *job_ptr;
	char *resp = NULL, *sep1;
	uint32_t job_id;
	int port_id = -1;

	sep1 = cmd_ptr + 15;
	job_id = atoi(sep1);
	sep1 = strstr(sep1, "PORT:");
	if (sep1)
		port_id = atoi(sep1 + 5);
	slurm_mutex_lock(&job_fail_mutex);
	if ((sep1 == NULL) || (port_id <= 0)) {
		xstrfmtcat(resp, "%s EPORT", SLURM_VERSION_STRING);
		goto fini;
	}
	job_fail_ptr = list_find_first(job_fail_list, _job_fail_find, &job_id);
	if (!job_fail_ptr || !_valid_job_ptr(job_fail_ptr)) {
		job_ptr = find_job_record(job_id);
		if (!job_ptr) {
			xstrfmtcat(resp, "%s EJOBID", SLURM_VERSION_STRING);
			goto fini;
		}
		if (!job_fail_ptr) {
			job_fail_ptr = xmalloc(sizeof(job_failures_t));
			job_fail_ptr->job_id  = job_ptr->job_id;
			job_fail_ptr->magic   = FAILURE_MAGIC;
			job_fail_ptr->user_id = job_ptr->user_id;
			list_append(job_fail_list, job_fail_ptr);
		}
		job_fail_ptr->job_ptr = job_ptr;
	} else {
		job_ptr = job_fail_ptr->job_ptr;
	}

	if (job_ptr->user_id != job_fail_ptr->user_id) {
		xstrfmtcat(resp, "%s EUID", SLURM_VERSION_STRING);
		goto fini;
	}
	job_fail_ptr->callback_addr = cli_addr;
	job_fail_ptr->callback_port = port_id;
	xstrfmtcat(resp, "%s ENOERROR", SLURM_VERSION_STRING);

fini:	slurm_mutex_unlock(&job_fail_mutex);
	debug("%s: replying to library: %s", __func__, resp);
	return resp;
}

/* For a given job and node to be replaced, identify the relevant node features.
 * The logic here is imperfect. If the job specifies a feature with any operator
 * and the node has the referenced feature, then the replacement node must have
 * the same feature(s).
 * Return value must be xfreed. */
static char *_job_node_features(struct job_record *job_ptr,
				struct node_record *node_ptr)
{
	node_feature_t *node_feat_ptr;
	job_feature_t *job_feat_ptr;
	ListIterator job_iter, node_iter;
	char *req_feat = NULL;
	int node_inx;

	if (!job_ptr->details || !job_ptr->details->features ||
	    !job_ptr->details->feature_list)
		return req_feat;

	node_inx = node_ptr - node_record_table_ptr;
	job_iter = list_iterator_create(job_ptr->details->feature_list);
	while ((job_feat_ptr = (job_feature_t *) list_next(job_iter))) {
		node_iter = list_iterator_create(active_feature_list);
		while ((node_feat_ptr = (node_feature_t *)
					list_next(node_iter))) {
			if (!job_feat_ptr->name  ||
			    !node_feat_ptr->name ||
			    !node_feat_ptr->node_bitmap ||
			    !bit_test(node_feat_ptr->node_bitmap, node_inx) ||
			    xstrcmp(job_feat_ptr->name, node_feat_ptr->name))
				continue;
			if (req_feat)
				xstrcat(req_feat, "&");
			xstrcat(req_feat, job_feat_ptr->name);
		}
		list_iterator_destroy(node_iter);
	}
	list_iterator_destroy(job_iter);

	return req_feat;
}

/*
 * Remove a job's failed or failing node from its allocation
 * cmd_ptr IN - Input format "DROP_NODE:JOBID:#:NODE:name"
 * cmd_uid IN - User issuing the RPC
 * protocol_version IN - Communication protocol version number
 * RET - Response string, must be freed by the user
 */
extern char *drop_node(char *cmd_ptr, uid_t cmd_uid,
		       uint32_t protocol_version)
{
	job_desc_msg_t job_alloc_req;
	job_failures_t *job_fail_ptr;
	struct job_record *job_ptr, *new_job_ptr = NULL;
	uint32_t cpu_cnt = 0, job_id;
	char *sep1;
	char *resp = NULL;
	char *node_name;
	int i, rc;
	struct node_record *node_ptr = NULL;
	int failed_inx = -1, node_inx = -1;
	hostlist_t hl = NULL;

	sep1 = cmd_ptr + 16;
	job_id = atoi(sep1);

	sep1 = strstr(cmd_ptr + 15, "NODE:");
	if (!sep1) {
		xstrfmtcat(resp, "%s ECMD", SLURM_VERSION_STRING);
		goto fini;
	}
	node_name = sep1 + 5;

	slurm_mutex_lock(&job_fail_mutex);
	job_fail_ptr = list_find_first(job_fail_list, _job_fail_find, &job_id);
	if (!job_fail_ptr || !_valid_job_ptr(job_fail_ptr)) {
		job_ptr = find_job_record(job_id);
		if (!job_ptr) {
			xstrfmtcat(resp, "%s EJOBID", SLURM_VERSION_STRING);
			goto fini;
		}
		if (!job_fail_ptr) {
			job_fail_ptr = xmalloc(sizeof(job_failures_t));
			job_fail_ptr->job_id  = job_ptr->job_id;
			job_fail_ptr->magic   = FAILURE_MAGIC;
			job_fail_ptr->user_id = job_ptr->user_id;
			list_append(job_fail_list, job_fail_ptr);
		}
		job_fail_ptr->job_ptr = job_ptr;
	} else {
		job_ptr = job_fail_ptr->job_ptr;
	}

	if ((cmd_uid != job_ptr->user_id) &&
	    (cmd_uid != 0) &&
	    (cmd_uid != getuid())) {
		info("slurmctld/nonstop: Security violation, User ID %u "
		     "attempting to modify job ID %u",
		     cmd_uid, job_ptr->job_id);
		xstrfmtcat(resp, "%s EPERM", SLURM_VERSION_STRING);
		goto fini;
	}
	if (!IS_JOB_RUNNING(job_ptr)) {
		xstrfmtcat(resp, "%s EJOBNOTRUNRROR", SLURM_VERSION_STRING);
		goto fini;
	}

	for (i = 0; i < job_fail_ptr->fail_node_cnt; i++) {
		if (!xstrcmp(node_name, job_fail_ptr->fail_node_names[i])) {
			cpu_cnt = job_fail_ptr->fail_node_cpus[i];
			failed_inx = i;
			break;
		}
	}
	if (failed_inx == -1) {
		node_ptr = find_node_record(node_name);
		if (!node_ptr) {
			xstrfmtcat(resp, "%s ENOHOST", SLURM_VERSION_STRING);
			goto fini;
		}
		if (IS_NODE_FAIL(node_ptr)) {
			node_inx = node_ptr - node_record_table_ptr;
			cpu_cnt = _get_job_cpus(job_ptr, node_inx);
		} else {
			node_ptr = NULL;
		}
	}

	if ((failed_inx == -1) && (node_ptr == NULL)) {
		xstrfmtcat(resp, "%s ENODENOTFAIL", SLURM_VERSION_STRING);
		goto fini;
	}

	if (cpu_cnt == 0) {
		xstrfmtcat(resp, "%s NODENOTINJOB", SLURM_VERSION_STRING);
		goto fini;
	}

	/* Abort previously submitted job merge request */
	if (job_fail_ptr->pending_node_name &&
	    (job_fail_ptr->pending_job_id == 0)) {
		error("slurmctld/nonstop: pending_node_name set, but "
		      "pending_job_id is zero for job %u", job_id);
		xfree(job_fail_ptr->pending_node_name);
	}
	if (job_fail_ptr->pending_node_name &&
	    job_fail_ptr->pending_job_id) {
		new_job_ptr = find_job_record(job_fail_ptr->pending_job_id);
		if (!new_job_ptr ||
		    (new_job_ptr->user_id != job_fail_ptr->user_id) ||
		    IS_JOB_FINISHED(new_job_ptr)) {
			info("slurmctld/nonstop: pending_job_id %u missing "
			     "for merge to job %u",
			     job_fail_ptr->pending_job_id, job_id);
			job_fail_ptr->pending_job_delay = 0;
			job_fail_ptr->pending_job_id = 0;
			xfree(job_fail_ptr->pending_node_name);
		}
	}
	if (job_fail_ptr->pending_node_name &&
	    !xstrcmp(job_fail_ptr->pending_node_name, node_name)) {
		/* Abort pending replacement request and get back time
		 * extension (if any) */
		_kill_job(job_fail_ptr->pending_job_id, cmd_uid);
		if (job_fail_ptr->pending_job_delay >
		    job_fail_ptr->time_extend_avail) {
			job_fail_ptr->time_extend_avail = 0;
		} else {
			job_fail_ptr->time_extend_avail -=
				job_fail_ptr->pending_job_delay;
		}
		job_fail_ptr->pending_job_delay = 0;
		job_fail_ptr->pending_job_id = 0;
		xfree(job_fail_ptr->pending_node_name);
	}

	/* Remove failed node from our job's list of failures */
	if (failed_inx == -1) {
		job_fail_ptr->time_extend_avail += time_limit_drop;
	} else {
		job_fail_ptr->time_extend_avail += time_limit_drop;
		job_fail_ptr->time_extend_avail -= time_limit_extend;
		job_fail_ptr->fail_node_cpus[failed_inx] = 0;
		xfree(job_fail_ptr->fail_node_names[failed_inx]);
		job_fail_ptr->fail_node_cnt--;
		for (i = failed_inx; i < job_fail_ptr->fail_node_cnt; i++) {
			job_fail_ptr->fail_node_cpus[i] =
				job_fail_ptr->fail_node_cpus[i+1];
			job_fail_ptr->fail_node_names[i] =
				job_fail_ptr->fail_node_names[i+1];
		}
	}

	/* If we are removing a FAILING node from the old job, do it now */
	if (node_inx != -1)
		hl = hostlist_create(job_ptr->nodes);
	if (hl) {
		(void) hostlist_delete(hl, node_name);
		slurm_init_job_desc_msg(&job_alloc_req);
		job_alloc_req.job_id	= job_id;
		job_alloc_req.req_nodes	= hostlist_ranged_string_xmalloc(hl);
		hostlist_destroy(hl);
		rc = _update_job(&job_alloc_req, cmd_uid);
		if (rc) {
			info("slurmctld/nonstop: can remove failing node %s "
			     "from job %u: %s",
			     node_name, job_id, slurm_strerror(rc));
		}
	}

	/* Work complete */
	xstrfmtcat(resp, "%s ENOERROR NewNodeList %s NewNodeCount %u",
		   SLURM_VERSION_STRING, job_ptr->nodes, job_ptr->node_cnt);
	if (job_ptr->job_resrcs) {
		char *sep = "";
		xstrfmtcat(resp, " NewCpusPerNode ");
		for (i = 0; i < job_ptr->job_resrcs->cpu_array_cnt; i++) {
			if (job_ptr->job_resrcs->cpu_array_value[i] == 0)
				continue;
			xstrfmtcat(resp, "%s%u", sep,
				   job_ptr->job_resrcs->cpu_array_value[i]);
			if (job_ptr->job_resrcs->cpu_array_reps[i] > 1) {
				xstrfmtcat(resp, "(x%u)",
					   job_ptr->job_resrcs->
					   cpu_array_reps[i]);
			}
			sep = ",";
		}
	}

fini:	job_fail_update_time = time(NULL);
	debug("%s: replying to library: %s", __func__, resp);
	slurm_mutex_unlock(&job_fail_mutex);
	return resp;
}

/*
 * Replace a job's failed or failing node
 * cmd_ptr IN - Input format "REPLACE_NODE:JOBID:#:NODE:name"
 * cmd_uid IN - User issuing the RPC
 * protocol_version IN - Communication protocol version number
 * RET - Response string, must be freed by the user
 */
extern char *replace_node(char *cmd_ptr, uid_t cmd_uid,
			  uint32_t protocol_version)
{
	job_desc_msg_t job_alloc_req;
	job_failures_t *job_fail_ptr;
	struct job_record *job_ptr, *new_job_ptr = NULL;
	uint32_t cpu_cnt = 0, job_id;
	char *sep1;
	char *resp = NULL;
	char *node_name, *new_node_name = NULL;
	int i, rc;
	struct node_record *node_ptr = NULL;
	int failed_inx = -1, node_inx = -1;
	hostlist_t hl = NULL;
	will_run_response_msg_t *will_run = NULL;
	time_t will_run_idle = 0, will_run_resv = 0, will_run_time = 0;

	sep1 = cmd_ptr + 19;
	job_id = atoi(sep1);

	sep1 = strstr(cmd_ptr + 19, "NODE:");
	if (!sep1) {
		xstrfmtcat(resp, "%s ECMD", SLURM_VERSION_STRING);
		goto fini;
	}
	node_name = sep1 + 5;

	slurm_mutex_lock(&job_fail_mutex);
	job_fail_ptr = list_find_first(job_fail_list, _job_fail_find, &job_id);
	if (!job_fail_ptr || !_valid_job_ptr(job_fail_ptr)) {
		job_ptr = find_job_record(job_id);
		if (!job_ptr) {
			xstrfmtcat(resp, "%s EJOBID", SLURM_VERSION_STRING);
			goto fini;
		}
		if (!job_fail_ptr) {
			job_fail_ptr = xmalloc(sizeof(job_failures_t));
			job_fail_ptr->job_id  = job_ptr->job_id;
			job_fail_ptr->magic   = FAILURE_MAGIC;
			job_fail_ptr->user_id = job_ptr->user_id;
			list_append(job_fail_list, job_fail_ptr);
		}
		job_fail_ptr->job_ptr = job_ptr;
	} else {
		job_ptr = job_fail_ptr->job_ptr;
	}

	if ((cmd_uid != job_ptr->user_id) &&
	    (cmd_uid != 0) &&
	    (cmd_uid != getuid())) {
		info("slurmctld/nonstop: Security violation, User ID %u "
		     "attempting to modify job ID %u",
		     cmd_uid, job_ptr->job_id);
		xstrfmtcat(resp, "%s EPERM", SLURM_VERSION_STRING);
		goto fini;
	}
	if (!IS_JOB_RUNNING(job_ptr)) {
		xstrfmtcat(resp, "%s EJOBNOTRUN", SLURM_VERSION_STRING);
		goto fini;
	}

	for (i = 0; i < job_fail_ptr->fail_node_cnt; i++) {
		if (!xstrcmp(node_name, job_fail_ptr->fail_node_names[i])) {
			cpu_cnt = job_fail_ptr->fail_node_cpus[i];
			failed_inx = i;
			break;
		}
	}
	if (failed_inx == -1) {
		node_ptr = find_node_record(node_name);
		if (!node_ptr) {
			xstrfmtcat(resp, "%s ENOHOST", SLURM_VERSION_STRING);
			goto fini;
		}
		if (IS_NODE_FAIL(node_ptr)) {
			node_inx = node_ptr - node_record_table_ptr;
			cpu_cnt = _get_job_cpus(job_ptr, node_inx);
		} else {
			node_ptr = NULL;
		}
	}

	/* Process previously submitted job merge */
	if (job_fail_ptr->pending_node_name &&
	    (job_fail_ptr->pending_job_id == 0)) {
		error("slurmctld/nonstop: pending_node_name set, but "
		      "pending_job_id is zero for job %u", job_id);
		xfree(job_fail_ptr->pending_node_name);
	}
	if (job_fail_ptr->pending_node_name &&
	    job_fail_ptr->pending_job_id) {
		new_job_ptr = find_job_record(job_fail_ptr->pending_job_id);
		if (!new_job_ptr ||
		    (new_job_ptr->user_id != job_fail_ptr->user_id) ||
		    IS_JOB_FINISHED(new_job_ptr)) {
			info("slurmctld/nonstop: pending_job_id %u missing "
			     "for merge to job %u",
			     job_fail_ptr->pending_job_id, job_id);
			job_fail_ptr->pending_job_delay = 0;
			job_fail_ptr->pending_job_id = 0;
			xfree(job_fail_ptr->pending_node_name);
		} else if (IS_JOB_PENDING(new_job_ptr)) {
			xstrfmtcat(resp,
				   "%s EREPLACELATER %"PRIu64"",
				   SLURM_VERSION_STRING,
				   (uint64_t) new_job_ptr->start_time);
			goto fini;
		}
	}
	if (job_fail_ptr->pending_node_name) {
		if (xstrcmp(job_fail_ptr->pending_node_name, node_name)) {
			xstrfmtcat(resp, "%s EREPLACEPENDING %s",
				   SLURM_VERSION_STRING,
				   job_fail_ptr->pending_node_name);
			goto fini;
		}
		goto merge;
	}

	if ((max_spare_node_count != 0) &&
	    (job_fail_ptr->replace_node_cnt >= max_spare_node_count)) {
		xstrfmtcat(resp, "%s EMAXSPARECOUNT %u", SLURM_VERSION_STRING,
			   max_spare_node_count);
		goto fini;
	}

	if ((failed_inx == -1) && (node_ptr == NULL)) {
		xstrfmtcat(resp, "%s ENODENOTFAIL", SLURM_VERSION_STRING);
		goto fini;
	}

	if (cpu_cnt == 0) {
		xstrfmtcat(resp, "%s ENODENOCPU", SLURM_VERSION_STRING);
		goto fini;
	}

	/*
	 * Create a job with replacement resources,
	 * which will later be merged into the original job
	 */
	slurm_init_job_desc_msg(&job_alloc_req);
	job_alloc_req.account = xstrdup(job_ptr->account);
	xstrfmtcat(job_alloc_req.dependency, "expand:%u", job_ptr->job_id);
	job_alloc_req.exc_nodes = xstrdup(job_ptr->nodes);
	job_alloc_req.features  = _job_node_features(job_ptr, node_ptr);
	job_alloc_req.group_id	= job_ptr->group_id;
	job_alloc_req.immediate	= 1;
	job_alloc_req.max_cpus	= cpu_cnt;
	job_alloc_req.max_nodes	= 1;
	job_alloc_req.min_cpus	= cpu_cnt;
	job_alloc_req.min_nodes	= 1;
	job_alloc_req.name	= xstrdup(job_ptr->name);
	job_alloc_req.network	= xstrdup(job_ptr->network);
	job_alloc_req.partition	= xstrdup(job_ptr->partition);
	job_alloc_req.priority	= NO_VAL - 1;
	if (job_ptr->qos_ptr)
		job_alloc_req.qos = xstrdup(job_ptr->qos_ptr->name);
	job_alloc_req.tres_per_job    = xstrdup(job_ptr->tres_per_job);
	job_alloc_req.tres_per_node   = xstrdup(job_ptr->tres_per_node);
	job_alloc_req.tres_per_socket = xstrdup(job_ptr->tres_per_socket);
	job_alloc_req.tres_per_task   = xstrdup(job_ptr->tres_per_task);

	/*
	 * Without unlock, the job_begin_callback() function will deadlock.
	 * Not a great solution, but perhaps the least bad solution.
	 */
	slurm_mutex_unlock(&job_fail_mutex);

	job_alloc_req.user_id	= job_ptr->user_id;
	/* Ignore default wckey (it starts with '*') */
	if (job_ptr->wckey && (job_ptr->wckey[0] != '*'))
		job_alloc_req.wckey = xstrdup(job_ptr->wckey);

	/* First: Try to allocate from idle node rather than deplete
	 * supply of hot spare nodes */
	rc = job_allocate(&job_alloc_req,	/* job specification */
			  1,			/* immediate */
			  0,			/* will_run */
			  NULL,			/* will_run response */
			  1,			/* allocate */
			  cmd_uid,		/* submit UID */
			  &new_job_ptr,		/* pointer to new job */
			  NULL,                 /* error message */
			  SLURM_PROTOCOL_VERSION);
	if (rc != SLURM_SUCCESS) {
		/* Determine expected start time */
		i = job_allocate(&job_alloc_req, 1, 1, &will_run, 1,
				 cmd_uid, &new_job_ptr, NULL,
				 SLURM_PROTOCOL_VERSION);
		if (i == SLURM_SUCCESS) {
			will_run_idle = will_run->start_time;
			slurm_free_will_run_response_msg(will_run);
		}
	}

	if (rc != SLURM_SUCCESS) {
		/* Second: Try to allocate from hot spare nodes */
		resv_desc_msg_t resv_desc;

		xstrfmtcat(job_alloc_req.reservation, "HOT_SPARE_%s",
			   job_ptr->partition);
		if (find_resv_name(job_alloc_req.reservation)) {
			slurm_init_resv_desc_msg(&resv_desc);
			resv_desc.name = job_alloc_req.reservation;
			xstrfmtcat(resv_desc.users, "+%u", cmd_uid);
			(void) update_resv(&resv_desc);
			xfree(resv_desc.users);
			rc = job_allocate(&job_alloc_req, 1, 0,	NULL, 1,
					  cmd_uid, &new_job_ptr, NULL,
					  SLURM_PROTOCOL_VERSION);
			if (rc != SLURM_SUCCESS) {
				/* Determine expected start time */
				i = job_allocate(&job_alloc_req, 1, 1,
						 &will_run, 1, cmd_uid,
						 &new_job_ptr, NULL,
						 SLURM_PROTOCOL_VERSION);
				if (i == SLURM_SUCCESS) {
					will_run_resv = will_run->start_time;
					slurm_free_will_run_response_msg(
							will_run);
				}
				if (will_run_resv) {
					/* Submit job in resv for later use */
					i = job_allocate(&job_alloc_req, 0, 0,
							 NULL, 1, cmd_uid,
							 &new_job_ptr, NULL,
							 SLURM_PROTOCOL_VERSION);
					if (i == SLURM_SUCCESS)
						will_run_time = will_run_resv;
				}
			}
			xstrfmtcat(resv_desc.users, "-%u", cmd_uid);
			(void) update_resv(&resv_desc);
			xfree(resv_desc.users);
		}
		xfree(job_alloc_req.reservation);
	}

	if ((rc != SLURM_SUCCESS) && (will_run_time == 0) && will_run_idle) {
		/* Submit job for later use without using reservation */
		i = job_allocate(&job_alloc_req, 0, 0, NULL, 1, cmd_uid,
				 &new_job_ptr, NULL, SLURM_PROTOCOL_VERSION);
		if (i == SLURM_SUCCESS)
			will_run_time = will_run_idle;
	}
	xfree(job_alloc_req.account);
	xfree(job_alloc_req.dependency);
	xfree(job_alloc_req.exc_nodes);
	xfree(job_alloc_req.features);
	xfree(job_alloc_req.name);
	xfree(job_alloc_req.network);
	xfree(job_alloc_req.partition);
	xfree(job_alloc_req.qos);
	xfree(job_alloc_req.tres_per_node);
	xfree(job_alloc_req.tres_per_socket);
	xfree(job_alloc_req.tres_per_task);
	xfree(job_alloc_req.wckey);

	slurm_mutex_lock(&job_fail_mutex);	/* Resume lock */

	if (rc != SLURM_SUCCESS) {
		if (will_run_time) {
			long int delay, extend;
			delay = (long int) (will_run_time - time(NULL));
			delay = MIN(delay, 0);
			info("slurmctld/nonstop: job %u to get resources "
			     "from job %u in %ld seconds)",
			     job_ptr->job_id, new_job_ptr->job_id, delay);
			xstrfmtcat(resp, "%s EREPLACELATER %"PRIu64"",
				   SLURM_VERSION_STRING, (uint64_t) will_run_time);
			job_fail_ptr->pending_job_id = new_job_ptr->job_id;
			xfree(job_fail_ptr->pending_node_name);
			job_fail_ptr->pending_node_name = xstrdup(node_name);
			if (time_limit_delay) {
				delay = (delay + 59) / 60;
				extend = MIN(delay, time_limit_delay);
				extend = MAX(extend, time_limit_extend);
				job_fail_ptr->time_extend_avail += extend;
				job_fail_ptr->pending_job_delay = extend;
			}
		} else {
			xstrfmtcat(resp, "%s ENODEREPLACEFAIL %s",
				   SLURM_VERSION_STRING, slurm_strerror(rc));
		}
		goto fini;
	}

merge:
	if (!new_job_ptr) {	/* Fix for CLANG false positive */
		error("%s: New job is NULL", __func__);
		return resp;
	}
	new_node_name = strdup(new_job_ptr->nodes);

	/* Shrink the size of the new job to zero */
	slurm_init_job_desc_msg(&job_alloc_req);
	job_alloc_req.job_id	= new_job_ptr->job_id;
	job_alloc_req.min_nodes	= 0;
	rc = _update_job(&job_alloc_req, cmd_uid);

	/* Without unlock, the job_fini_callback() function will deadlock.
	 * Not a great solution, but perhaps the least bad solution. */
	slurm_mutex_unlock(&job_fail_mutex);

	if (rc) {
		info("slurmctld/nonstop: can not shrink job %u: %s",
		     new_job_ptr->job_id, slurm_strerror(rc));
		_kill_job(new_job_ptr->job_id, cmd_uid);
		xstrfmtcat(resp, "%s ENODEREPLACEFAIL %s:",
			   SLURM_VERSION_STRING,
			   slurm_strerror(rc));
		slurm_mutex_lock(&job_fail_mutex);	/* Resume lock */
		goto fini;
	}
	_kill_job(new_job_ptr->job_id, cmd_uid);
	slurm_mutex_lock(&job_fail_mutex);	/* Resume lock */

	/* Grow the size of the old job to include the new node */
	slurm_init_job_desc_msg(&job_alloc_req);
	job_alloc_req.job_id	= job_id;
	job_alloc_req.min_nodes	= INFINITE;
	rc = _update_job(&job_alloc_req, cmd_uid);
	if (rc) {
		info("slurmctld/nonstop: can not grow job %u: %s",
		     job_id, slurm_strerror(rc));
		xstrfmtcat(resp, "%s ENODEREPLACEFAIL %s:",
			   SLURM_VERSION_STRING,
			   slurm_strerror(rc));
		goto fini;
	}

	job_fail_ptr->replace_node_cnt++;

	/* Remove failed node from our job's list of failures */
	if (failed_inx == -1) {
		job_fail_ptr->time_extend_avail += time_limit_extend;
	} else {
		job_fail_ptr->fail_node_cpus[failed_inx] = 0;
		xfree(job_fail_ptr->fail_node_names[failed_inx]);
		job_fail_ptr->fail_node_cnt--;
		for (i = failed_inx; i < job_fail_ptr->fail_node_cnt; i++) {
			job_fail_ptr->fail_node_cpus[i] =
				job_fail_ptr->fail_node_cpus[i+1];
			job_fail_ptr->fail_node_names[i] =
				job_fail_ptr->fail_node_names[i+1];
		}
	}

	/* If we are removing a FAILING node from the old job, do it now */
	if (node_inx != -1)
		hl = hostlist_create(job_ptr->nodes);
	if (hl) {
		(void) hostlist_delete(hl, node_name);
		slurm_init_job_desc_msg(&job_alloc_req);
		job_alloc_req.job_id	= job_id;
		job_alloc_req.req_nodes	= hostlist_ranged_string_xmalloc(hl);
		hostlist_destroy(hl);
		rc = _update_job(&job_alloc_req, cmd_uid);
		if (rc) {
			info("slurmctld/nonstop: can remove failing node %s "
			     "from job %u: %s",
			     node_name, job_id, slurm_strerror(rc));
		}
		xfree(job_alloc_req.req_nodes);
	}

	/* Work complete */
	xstrfmtcat(resp, "%s ENOERROR ReplacementNode %s NewNodeList %s "
		   "NewNodeCount %u",  SLURM_VERSION_STRING,
		   new_node_name, job_ptr->nodes, job_ptr->node_cnt);
	if (job_ptr->job_resrcs) {
		char *sep = "";
		xstrfmtcat(resp, " NewCpusPerNode ");
		for (i = 0; i < job_ptr->job_resrcs->cpu_array_cnt; i++) {
			if (job_ptr->job_resrcs->cpu_array_value[i] == 0)
				continue;
			xstrfmtcat(resp, "%s%u", sep,
				   job_ptr->job_resrcs->cpu_array_value[i]);
			if (job_ptr->job_resrcs->cpu_array_reps[i] > 1) {
				xstrfmtcat(resp, "(x%u)",
					   job_ptr->job_resrcs->
					   cpu_array_reps[i]);
			}
			sep = ",";
		}
	}

fini:	job_fail_update_time = time(NULL);
	slurm_mutex_unlock(&job_fail_mutex);
	if (new_node_name)
		free(new_node_name);
	debug("%s: replying to library: %s", __func__, resp);
	return resp;
}

/*
 * Report nonstop plugin global state/configuration information
 * cmd_ptr IN - Input format "SHOW_CONFIG
 * cmd_uid IN - User issuing the RPC
 * protocol_version IN - Communication protocol version number
 * RET - Response string, must be freed by the user
 */
extern char *show_config(char *cmd_ptr, uid_t cmd_uid,
			 uint32_t protocol_version)
{
	char *resp = NULL;

	xstrfmtcat(resp, "%s ENOERROR ", SLURM_VERSION_STRING);

	if (nonstop_backup_addr)
		xstrfmtcat(resp, "BackupAddr \"%s\" ", nonstop_backup_addr);
	else
		xstrfmtcat(resp, "BackupAddr \"none\" ");

	xstrfmtcat(resp, "ControlAddr \"%s\" ", nonstop_control_addr);
	xstrfmtcat(resp, "Debug %u ", nonstop_debug);
	xstrfmtcat(resp, "HotSpareCount \"%s\" ", hot_spare_count_str);
	xstrfmtcat(resp, "MaxSpareNodeCount %u ", max_spare_node_count);
	xstrfmtcat(resp, "Port %u ", nonstop_comm_port);
	xstrfmtcat(resp, "TimeLimitDelay %hu ", time_limit_delay);
	xstrfmtcat(resp, "TimeLimitDrop %hu ", time_limit_drop);
	xstrfmtcat(resp, "TimeLimitExtend %hu ", time_limit_extend);

	if (user_drain_allow_str)
		xstrfmtcat(resp, "UserDrainAllow \"%s\" ",
			   user_drain_allow_str);
	else
		xstrfmtcat(resp, "UserDrainAllow \"none\" ");

	if (user_drain_deny_str)
		xstrfmtcat(resp, "UserDrainDeny \"%s\" ", user_drain_deny_str);
	else
		xstrfmtcat(resp, "UserDrainDeny \"none\"");

	debug("%s: replying to library: ENOERROR", __func__);
	return resp;
}

/*
 * Report nonstop plugin state information for a particular job
 * cmd_ptr IN - Input format "SHOW_JOB:JOBID:#
 * cmd_uid IN - User issuing the RPC
 * protocol_version IN - Communication protocol version number
 * RET - Response string, must be freed by the user
 */
extern char *show_job(char *cmd_ptr, uid_t cmd_uid, uint32_t protocol_version)
{
	struct job_record *job_ptr;
	struct node_record *node_ptr;
	job_failures_t *job_fail_ptr;
	uint32_t job_id;
	char *sep1;
	char *resp = NULL, *failing_nodes = NULL;
	int failing_cnt = 0;
	int i, i_first, i_last;

	sep1 = cmd_ptr + 15;
	job_id = atoi(sep1);

	slurm_mutex_lock(&job_fail_mutex);

	job_fail_ptr = list_find_first(job_fail_list, _job_fail_find, &job_id);
	if (!job_fail_ptr || !_valid_job_ptr(job_fail_ptr)) {
		job_ptr = find_job_record(job_id);
		if (!job_ptr) {
			xstrfmtcat(resp, "%s EJOBID", SLURM_VERSION_STRING);
			goto fini;
		}
		job_fail_ptr = xmalloc(sizeof(job_failures_t));
		job_fail_ptr->job_id  = job_ptr->job_id;
		job_fail_ptr->job_ptr = job_ptr;
		job_fail_ptr->magic   = FAILURE_MAGIC;
		job_fail_ptr->user_id = job_ptr->user_id;
		list_append(job_fail_list, job_fail_ptr);
	}

	if ((cmd_uid != 0) && (cmd_uid != getuid()) &&
	    (cmd_uid != job_fail_ptr->job_ptr->user_id)) {
		xstrfmtcat(resp, "%s EPERM", SLURM_VERSION_STRING);
		goto fini;
	}

	xstrfmtcat(resp, "%s ENOERROR ", SLURM_VERSION_STRING);

	job_ptr = job_fail_ptr->job_ptr;
	i_first = bit_ffs(job_ptr->node_bitmap);
	if (i_first == -1)
		i_last = -2;
	else
		i_last = bit_fls(job_ptr->node_bitmap);
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(job_ptr->node_bitmap, i))
			continue;
		node_ptr = node_record_table_ptr + i;
		if (!IS_NODE_FAIL(node_ptr))
			continue;
		failing_cnt++;
		/* Format: nodename number_of_cpus state */
		xstrfmtcat(failing_nodes, "%s %u ",
			   node_ptr->name,
			   _get_job_cpus(job_ptr, i));
	}
	xstrfmtcat(resp, "FAIL_NODE_CNT %u ",
		   job_fail_ptr->fail_node_cnt + failing_cnt);
	if (job_fail_ptr->fail_node_cnt) {
		for (i = 0; i < job_fail_ptr->fail_node_cnt; i++) {
			xstrfmtcat(resp, "%s %u ",
					   job_fail_ptr->fail_node_names[i],
					   job_fail_ptr->fail_node_cpus[i]);
		}
	}
	xstrfmtcat(resp, "%s", failing_nodes);

	xstrfmtcat(resp, "PENDING_JOB_DELAY %hu ",
			   job_fail_ptr->pending_job_delay);
	xstrfmtcat(resp, "PENDING_JOB_ID %u ", job_fail_ptr->pending_job_id);

	if (job_fail_ptr->pending_node_name)
		xstrfmtcat(resp, "PENDING_NODE_NAME \"%s\" ",
				   job_fail_ptr->pending_node_name);
	else
		xstrfmtcat(resp, "PENDING_NODE_NAME \"none\" ");

	xstrfmtcat(resp, "REPLACE_NODE_CNT %u ",
		   job_fail_ptr->replace_node_cnt);
	xstrfmtcat(resp, "TIME_EXTEND_AVAIL %u",
		   job_fail_ptr->time_extend_avail);

fini:	slurm_mutex_unlock(&job_fail_mutex);
	debug("%s: replying to library: %s", __func__, resp);
	return resp;
}

/*
 * Reset a job's time limit
 * cmd_ptr IN - Input format "TIME_INCR:JOBID:#:MINUTES:#
 * cmd_uid IN - User issuing the RPC
 * protocol_version IN - Communication protocol version number
 * RET - Response string, must be freed by the user
 */
extern char *time_incr(char *cmd_ptr, uid_t cmd_uid, uint32_t protocol_version)
{
	job_failures_t *job_fail_ptr;
	job_desc_msg_t job_specs;
	uint32_t job_id, minutes;
	char *sep1;
	char *resp = NULL;
	int rc = 0;

	sep1 = cmd_ptr + 16;
	job_id = atoi(sep1);

	slurm_mutex_lock(&job_fail_mutex);
	sep1 = strstr(cmd_ptr + 16, "MINUTES:");
	if (!sep1) {
		xstrfmtcat(resp, "%s ECMD", SLURM_VERSION_STRING);
		goto fini;
	}
	sep1 += 8;
	minutes = atoi(sep1);

	job_fail_ptr = list_find_first(job_fail_list, _job_fail_find, &job_id);
	if (!job_fail_ptr || !_valid_job_ptr(job_fail_ptr)) {
		if (find_job_record(job_id)) {
			xstrfmtcat(resp, "%s ENOINCREASETIMELIMIT",
				   SLURM_VERSION_STRING);
		} else
			xstrfmtcat(resp, "%s EJOBID", SLURM_VERSION_STRING);
		goto fini;
	}
	if (minutes == 0) {
		minutes = job_fail_ptr->time_extend_avail;
		job_fail_ptr->time_extend_avail = 0;
	} else if (minutes <= job_fail_ptr->time_extend_avail) {
		job_fail_ptr->time_extend_avail -= minutes;
	} else {
		/* Log it but send back only the error number.
		 * xstrfmtcat(resp, "%s ETIMEOVERLIMIT %u  %u",
		 * SLURM_VERSION_STRING, minutes,
		 * job_fail_ptr->time_extend_avail);
		 */
		xstrfmtcat(resp, "%s ETIMEOVERLIMIT", SLURM_VERSION_STRING);
		goto fini;
	}

	if (job_fail_ptr->job_ptr && IS_JOB_RUNNING(job_fail_ptr->job_ptr) &&
	    (job_fail_ptr->job_ptr->time_limit != INFINITE)) {
		slurm_init_job_desc_msg(&job_specs);
		job_specs.job_id = job_id;
		job_specs.time_limit  = job_fail_ptr->job_ptr->time_limit;
		job_specs.time_limit += minutes;
		rc = _update_job(&job_specs, cmd_uid);
	}
	if (rc) {
		xstrfmtcat(resp, "%s EJOBUPDATE %s", SLURM_VERSION_STRING,
			   slurm_strerror(rc));
		job_fail_ptr->time_extend_avail += minutes;
	} else {
		xstrfmtcat(resp, "%s ENOERROR", SLURM_VERSION_STRING);
	}

fini:	job_fail_update_time = time(NULL);
	slurm_mutex_unlock(&job_fail_mutex);
	debug("%s: replying to library: %s", __func__, resp);
	return resp;
}

/* Send nonstop event notification to the user.
 * NOTE: The message has no authentication and only consists of a uint32_t
 * with event flags. */
static void _send_event_callbacks(void)
{
	job_failures_t *job_fail_ptr;
	ListIterator job_iterator;
	slurm_addr_t callback_addr;
	uint32_t callback_flags, callback_jobid;
	int fd;
	ssize_t sent;

	if (!job_fail_list)
		return;

	slurm_mutex_lock(&job_fail_mutex);
	job_iterator = list_iterator_create(job_fail_list);
	while ((job_fail_ptr = (job_failures_t *) list_next(job_iterator))) {
		if (job_fail_ptr->callback_flags == 0)
			continue;
		if (job_fail_ptr->callback_port) {
			if (nonstop_debug > 0) {
				info("nonstop: callback to job %u flags %x",
				     job_fail_ptr->job_id,
				     job_fail_ptr->callback_flags);
			}
			callback_addr = job_fail_ptr->callback_addr;
			callback_addr.sin_port =
				htons(job_fail_ptr->callback_port);
			callback_flags = job_fail_ptr->callback_flags;
			debug("%s: job_id %d flags 0x%x", __func__,
			      job_fail_ptr->job_id,
			      job_fail_ptr->callback_flags);
			job_fail_ptr->callback_flags = 0;
			callback_jobid = job_fail_ptr->job_id;
			/* Release locks for I/O, which could be slow */
			slurm_mutex_unlock(&job_fail_mutex);
			fd = slurm_open_msg_conn(&callback_addr);
			sent = 0;
			if (fd < 0) {
				error("nonstop: socket open fail for job %u: %m",
				      callback_jobid);
				goto io_fini;
			}
			sent = slurm_msg_sendto_timeout(fd,
					(char *) &callback_flags,
					sizeof(uint32_t), 100000);
			(void) close(fd);
			/* Reset locks and clean-up as needed */
io_fini:		slurm_mutex_lock(&job_fail_mutex);
			if ((sent != sizeof(uint32_t)) &&
			    (job_fail_ptr->magic == FAILURE_MAGIC) &&
			    (callback_jobid == job_fail_ptr->job_id)) {
				/* Failed to send flags */
				job_fail_ptr->callback_flags |= callback_flags;
			}
		}
		job_fail_ptr->callback_flags = 0;
	}
	list_iterator_destroy(job_iterator);
	job_fail_save_time = time(NULL);
	slurm_mutex_unlock(&job_fail_mutex);
}

static void *_state_thread(void *no_data)
{
	static time_t last_save_time;
	static time_t last_callback_time;
	time_t now;

	last_save_time = last_callback_time = time(NULL);
	while (!thread_shutdown) {
		usleep(200000);

		now = time(NULL);
		if (difftime(now, last_callback_time) >= NONSTOP_EVENT_PERIOD) {
			_send_event_callbacks();
			last_callback_time = now;
		}
		if (thread_shutdown ||
		    (difftime(now, last_save_time) >= NONSTOP_SAVE_PERIOD)) {
			save_nonstop_state();
			last_save_time = now;
		}
	}
	pthread_exit((void *) 0);
	return NULL;
}

/* Spawn thread to periodically save nonstop plugin state to disk */
extern int spawn_state_thread(void)
{
	slurm_mutex_lock(&thread_flag_mutex);
	if (thread_running) {
		slurm_mutex_unlock(&thread_flag_mutex);
		return SLURM_ERROR;
	}

	slurm_thread_create(&msg_thread_id, _state_thread, NULL);
	thread_running = true;
	slurm_mutex_unlock(&thread_flag_mutex);

	return SLURM_SUCCESS;
}

/* Terminate thread used to periodically save nonstop plugin state to disk */
extern void term_state_thread(void)
{
	slurm_mutex_lock(&thread_flag_mutex);
	if (thread_running) {
		thread_shutdown = true;
		pthread_join(msg_thread_id, NULL);
		msg_thread_id = 0;
		thread_shutdown = false;
		thread_running = false;
	}
	slurm_mutex_unlock(&thread_flag_mutex);
}
