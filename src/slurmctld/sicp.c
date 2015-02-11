/*****************************************************************************\
 *  sicp.c - Inter-cluster job management functions
 *****************************************************************************
 *  Copyright (C) 2015 SchedMD LLC (http://www.schedmd.com).
 *  Written by Morris Jette
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/fd.h"
#include "src/common/macros.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/sicp.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/state_save.h"

#define JOB_HASH_INX(_job_id)	(_job_id % hash_table_size)
#define JOB_ARRAY_HASH_INX(_job_id, _task_id) \
	((_job_id + _task_id) % hash_table_size)

static int		hash_table_size = 1000;
static sicp_job_t **	sicp_hash = NULL;
static List		sicp_job_list = NULL;

static int		sicp_interval = 10;
static bool		sicp_stop = false;
static pthread_t	sicp_thread = 0;
static pthread_mutex_t	sicp_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t	sicp_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t	thread_lock = PTHREAD_MUTEX_INITIALIZER;

static void		_add_job_hash(sicp_job_t *sicp_ptr);
static void		_dump_sicp_state(void);
static sicp_job_t *	_find_sicp(uint32_t job_id);
static void		_list_delete_sicp(void *sicp_entry);
static int		_list_find_sicp_old(void *sicp_entry, void *key);
static void		_load_sicp_state(void);
static void		_log_sicp_recs(void);
static void		_my_sleep(int add_secs);

/* _add_sicp_hash - add a sicp job to hash table */
static void _add_job_hash(sicp_job_t *sicp_ptr)
{
	int inx;

	inx = JOB_HASH_INX(sicp_ptr->job_id);
	sicp_ptr->sicp_next = sicp_hash[inx];
	sicp_hash[inx] = sicp_ptr;
}

static sicp_job_t *_find_sicp(uint32_t job_id)
{
	sicp_job_t *sicp_ptr;

	sicp_ptr = sicp_hash[JOB_HASH_INX(job_id)];
	while (sicp_ptr) {
		if (sicp_ptr->job_id == job_id)
			break;
		sicp_ptr = sicp_ptr->sicp_next;
	}
	return sicp_ptr;
}

static void _list_delete_sicp(void *sicp_entry)
{
	sicp_job_t *sicp_ptr = (sicp_job_t *) sicp_entry;
	sicp_job_t **sicp_pptr, *tmp_ptr;

	/* Remove the record from sicp hash table */
	sicp_pptr = &sicp_hash[JOB_HASH_INX(sicp_ptr->job_id)];
	while ((sicp_pptr != NULL) && (*sicp_pptr != NULL) &&
	       ((tmp_ptr = *sicp_pptr) != (sicp_job_t *) sicp_entry)) {
		sicp_pptr = &tmp_ptr->sicp_next;
	}
	if (sicp_pptr == NULL)
		error("sicp hash error");
	else
		*sicp_pptr = sicp_ptr->sicp_next;
	xfree(sicp_ptr);
}

static void _my_sleep(int add_secs)
{
	struct timespec ts = {0, 0};
	struct timeval  tv = {0, 0};

	if (gettimeofday(&tv, NULL)) {		/* Some error */
		sleep(1);
		return;
	}

	ts.tv_sec  = tv.tv_sec + add_secs;
	ts.tv_nsec = tv.tv_usec * 1000;
	pthread_mutex_lock(&sicp_lock);
	if (!sicp_stop)
		pthread_cond_timedwait(&sicp_cond, &sicp_lock, &ts);
	pthread_mutex_unlock(&sicp_lock);
}

static int _list_find_sicp_old(void *sicp_entry, void *key)
{
	sicp_job_t *sicp_ptr = (sicp_job_t *)sicp_entry;
	time_t old;

//FIXME: Do not purge if we lack current information from this cluster
	if (!(IS_JOB_FINISHED(sicp_ptr)))
		return 0;	/* Job still active */

	old = time(NULL) - (24 * 60 * 60);	/* One day */
	if (sicp_ptr->update_time > old)
		return 0;	/* Job still active */

	return 1;
}

/* Log all SICP job records */
static void _log_sicp_recs(void)
{
	ListIterator sicp_iterator;
	sicp_job_t *sicp_ptr;

	sicp_iterator = list_iterator_create(sicp_job_list);
	while ((sicp_ptr = (sicp_job_t *) list_next(sicp_iterator))) {
		info("SICP: Job_ID:%u State:%s", sicp_ptr->job_id,
		     job_state_string(sicp_ptr->job_state));
	}
	list_iterator_destroy(sicp_iterator);
}

static void _load_sicp_other_cluster(void)
{
int cluster_cnt = 1;
	sicp_info_msg_t * sicp_buffer_ptr = NULL;
	sicp_info_t *remote_sicp_ptr = NULL;
	sicp_job_t *sicp_ptr;
	int i, j, error_code;
	time_t now;

	for (i = 0; i < cluster_cnt; i++) {
//FIXME: Issue RPC to load table from every _other_ cluster
//This is just loading from the current cluster for testing purposes
		error_code = slurm_load_sicp(&sicp_buffer_ptr);
		if (error_code) {
			error("slurm_load_sicp(HOSTNAME) error: %s",
			      slurm_strerror(error_code));
			continue;
		}

		pthread_mutex_lock(&sicp_lock);
		now = time(NULL);
		for (j = 0, remote_sicp_ptr = sicp_buffer_ptr->sicp_array;
		     j < sicp_buffer_ptr->record_count;
		     j++, remote_sicp_ptr++) {
			sicp_ptr = _find_sicp(remote_sicp_ptr->job_id);
			if (!sicp_ptr) {
				sicp_ptr = xmalloc(sizeof(sicp_job_t));
				sicp_ptr->job_id = remote_sicp_ptr->job_id;
				sicp_ptr->job_state = remote_sicp_ptr->job_state;
				list_append(sicp_job_list, sicp_ptr);
				_add_job_hash(sicp_ptr);
			}
			sicp_ptr->update_time = now;
		}
		pthread_mutex_unlock(&sicp_lock);
		slurm_free_sicp_msg(sicp_buffer_ptr);
	}
}

extern void *_sicp_agent(void *args)
{
	static time_t last_sicp_time = 0;
	time_t now;
	double wait_time;

	while (!sicp_stop) {
		_my_sleep(1);
		if (sicp_stop)
			break;

		now = time(NULL);
		wait_time = difftime(now, last_sicp_time);
		if (wait_time < sicp_interval)
			continue;
		last_sicp_time = now;

		_load_sicp_other_cluster();

		pthread_mutex_lock(&sicp_lock);
		list_delete_all(sicp_job_list, &_list_find_sicp_old, "");
		if (slurm_get_debug_flags() & DEBUG_FLAG_SICP)
			_log_sicp_recs();
		pthread_mutex_unlock(&sicp_lock);

		_dump_sicp_state();	/* Has own locking */
	}
	return NULL;
}

static void _dump_sicp_state(void)
{
	char *old_file, *new_file, *reg_file;
	ListIterator sicp_iterator;
	sicp_job_t *sicp_ptr;
	Buf buffer;
	time_t now = time(NULL);
	int error_code = SLURM_SUCCESS, len, log_fd;

	pthread_mutex_lock(&sicp_lock);
	len = list_count(sicp_job_list) * 4 + 128;
	buffer = init_buf(len);

	packstr("PROTOCOL_VERSION", buffer);
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(now, buffer);

	sicp_iterator = list_iterator_create(sicp_job_list);
	while ((sicp_ptr = (sicp_job_t *) list_next(sicp_iterator))) {
		pack32(sicp_ptr->job_id, buffer);
		pack16(sicp_ptr->job_state, buffer);
	}
	list_iterator_destroy(sicp_iterator);
	pthread_mutex_unlock(&sicp_lock);

	old_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(old_file, "/sicp_state.old");
	reg_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(reg_file, "/sicp_state");
	new_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(new_file, "/sicp_state.new");

	lock_state_files();
	log_fd = creat(new_file, 0600);
	if (log_fd < 0) {
		error("Can't save state, create file %s error %m",
		      new_file);
		error_code = errno;
	} else {
		int pos = 0, nwrite, amount, rc;
		char *data;

		fd_set_close_on_exec(log_fd);
		nwrite = get_buf_offset(buffer);
		data = (char *)get_buf_data(buffer);
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

		rc = fsync_and_close(log_fd, "sicp");
		if (rc && !error_code)
			error_code = rc;
	}
	if (error_code) {
		(void) unlink(new_file);
	} else {			/* file shuffle */
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
	xfree(old_file);
	xfree(reg_file);
	xfree(new_file);
	unlock_state_files();

	free_buf(buffer);
}

static void _load_sicp_state(void)
{
	int data_allocated, data_read = 0;
	uint32_t data_size = 0;
	int state_fd, sicp_cnt = 0;
	char *data = NULL, *state_file;
	struct stat stat_buf;
	Buf buffer;
	char *ver_str = NULL;
	uint32_t ver_str_len;
	uint16_t protocol_version = (uint16_t)NO_VAL;
	uint32_t job_id = 0;
	uint16_t job_state = 0;
	sicp_job_t *sicp_ptr;
	time_t buf_time, now;

	/* read the file */
	lock_state_files();
	state_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(state_file, "/sicp_state");
	state_fd = open(state_file, O_RDONLY);
	if (state_fd < 0) {
		error("Could not open job state file %s: %m", state_file);
		unlock_state_files();
		xfree(state_file);
		return;
	} else if (fstat(state_fd, &stat_buf) < 0) {
		error("Could not stat job state file %s: %m", state_file);
		unlock_state_files();
		(void) close(state_fd);
		xfree(state_file);
		return;
	} else if (stat_buf.st_size < 10) {
		error("Job state file %s too small", state_file);
		unlock_state_files();
		(void) close(state_fd);
		xfree(state_file);
		return;
	}

	data_allocated = BUF_SIZE;
	data = xmalloc(data_allocated);
	while (1) {
		data_read = read(state_fd, &data[data_size], BUF_SIZE);
		if (data_read < 0) {
			if (errno == EINTR)
				continue;
			else {
				error("Read error on %s: %m", state_file);
				break;
			}
		} else if (data_read == 0)	/* eof */
			break;
		data_size      += data_read;
		data_allocated += data_read;
		xrealloc(data, data_allocated);
	}
	close(state_fd);
	xfree(state_file);
	unlock_state_files();

	buffer = create_buf(data, data_size);
	safe_unpackstr_xmalloc(&ver_str, &ver_str_len, buffer);
	debug3("Version string in sicp_state header is %s", ver_str);
	if (ver_str && !strcmp(ver_str, "PROTOCOL_VERSION"))
		safe_unpack16(&protocol_version, buffer);
	xfree(ver_str);

	if (protocol_version == (uint16_t)NO_VAL) {
		error("************************************************");
		error("Can not recover SICP state, incompatible version");
		error("************************************************");
		xfree(ver_str);
		free_buf(buffer);
		return;
	}
	safe_unpack_time(&buf_time, buffer);

	now = time(NULL);
	while (remaining_buf(buffer) > 0) {
		safe_unpack32(&job_id,    buffer);
		safe_unpack16(&job_state, buffer);
		sicp_ptr = xmalloc(sizeof(sicp_job_t));
		sicp_ptr->job_id      = job_id;
		sicp_ptr->job_state   = job_state;
		sicp_ptr->update_time = now;
		list_append(sicp_job_list, sicp_ptr);
		_add_job_hash(sicp_ptr);
		sicp_cnt++;
	}

	free_buf(buffer);
	info("Recovered information about %d sicp jobs", sicp_cnt);
	if (slurm_get_debug_flags() & DEBUG_FLAG_SICP)
		_log_sicp_recs();
	return;

unpack_error:
	error("Incomplete sicp data checkpoint file");
	info("Recovered information about %d sicp jobs", sicp_cnt);
	free_buf(buffer);
	return;
}

/* Start a thread to poll other clusters for inter-cluster job status */
extern void sicp_init(void)
{
	pthread_attr_t attr;

	pthread_mutex_lock(&thread_lock);
	if (sicp_thread) {
		error("%s: sicp thread already running", __func__);
		pthread_mutex_unlock(&thread_lock);
	}

	pthread_mutex_lock(&sicp_lock);
	sicp_stop = false;
	sicp_hash = xmalloc(sizeof(sicp_job_t) * hash_table_size);
	sicp_job_list = list_create(_list_delete_sicp);
	_load_sicp_state();
	pthread_mutex_unlock(&sicp_lock);
	slurm_attr_init(&attr);
	/* Since we do a join on thread later, don't make it detached */
	if (pthread_create(&sicp_thread, &attr, _sicp_agent, NULL))
		error("Unable to start power thread: %m");
	slurm_attr_destroy(&attr);
	pthread_mutex_unlock(&thread_lock);
}

/* Shutdown the inter-cluster job status thread */
extern void sicp_fini(void)
{
	pthread_mutex_lock(&thread_lock);
	pthread_mutex_lock(&sicp_lock);
	sicp_stop = true;
	pthread_cond_signal(&sicp_cond);
	pthread_mutex_unlock(&sicp_lock);

	pthread_join(sicp_thread, NULL);
	sicp_thread = 0;
	FREE_NULL_LIST(sicp_job_list);
	xfree(sicp_hash);
	pthread_mutex_unlock(&thread_lock);
}

/* For a given inter-cluster job ID, return its state (if found) or NO_VAL */
extern uint16_t sicp_get_state(uint32_t job_id)
{
	sicp_job_t *sicp_ptr;
	uint16_t job_state = (uint16_t) NO_VAL;

	pthread_mutex_lock(&sicp_lock);
	sicp_ptr = _find_sicp(job_id);
	if (sicp_ptr)
		job_state = sicp_ptr->job_state;
	pthread_mutex_unlock(&sicp_lock);

	return job_state;
}
