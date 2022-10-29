/*****************************************************************************\
 *  jobcomp_elasticsearch.c - elasticsearch slurm job completion logging plugin.
 *****************************************************************************
 *  Produced at Barcelona Supercomputing Center, in collaboration with
 *  Barcelona School of Informatics.
 *  Written by Alejandro Sanchez Graells <alejandro.sanchezgraells@bsc.es>,
 *  <asanchez1987@gmail.com>, who borrowed heavily from jobcomp/filetxt
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

#include <curl/curl.h>
#include <fcntl.h>
#include <inttypes.h>
#include <grp.h>
#include <pwd.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/assoc_mgr.h"
#include "src/common/data.h"
#include "src/common/fd.h"
#include "src/common/list.h"
#include "src/common/parse_time.h"
#include "src/common/slurm_jobcomp.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_time.h"
#include "src/common/slurmdb_defs.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/state_save.h"

#define MAX_STR_LEN 10240	/* 10 KB */
#define MAX_JOBS 1000000

/*
 * These variables are required by the generic plugin interface. If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin. There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything. Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "jobcomp" for Slurm job completion logging) and <method>
 * is a description of how this plugin satisfies that application. Slurm will
 * only load job completion logging plugins if the plugin_type string has a
 * prefix of "jobcomp/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin. If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000. Various Slurm versions will likely require a certain
 * minimum version for their plugins as the job completion logging API
 * matures.
 */
const char plugin_name[] = "Job completion elasticsearch logging plugin";
const char plugin_type[] = "jobcomp/elasticsearch";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

#define INDEX_RETRY_INTERVAL 30

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined. They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
extern uint16_t accounting_enforce __attribute__((weak_import));
extern void *acct_db_conn __attribute__((weak_import));
#else
uint16_t accounting_enforce = 0;
void *acct_db_conn = NULL;
#endif

/* Type for handling HTTP responses */
struct http_response {
	char *message;
	size_t size;
};

struct job_node {
	time_t last_index_retry;
	char * serialized_job;
};

char *save_state_file = "elasticsearch_state";
char *log_url = NULL;

static pthread_cond_t location_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t location_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t save_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t pend_jobs_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t job_handler_thread;
static List jobslist = NULL;
static bool thread_shutdown = false;

/* Get the user name for the give user_id */
static void _get_user_name(uint32_t user_id, char *user_name, int buf_size)
{
	static uint32_t cache_uid = 0;
	static char cache_name[32] = "root", *uname;

	if (user_id != cache_uid) {
		uname = uid_to_string((uid_t) user_id);
		snprintf(cache_name, sizeof(cache_name), "%s", uname);
		xfree(uname);
		cache_uid = user_id;
	}
	snprintf(user_name, buf_size, "%s", cache_name);
}

/* Get the group name for the give group_id */
static void _get_group_name(uint32_t group_id, char *group_name, int buf_size)
{
	static uint32_t cache_gid = 0;
	static char cache_name[32] = "root", *gname;

	if (group_id != cache_gid) {
		gname = gid_to_string((gid_t) group_id);
		snprintf(cache_name, sizeof(cache_name), "%s", gname);
		xfree(gname);
		cache_gid = group_id;
	}
	snprintf(group_name, buf_size, "%s", cache_name);
}

/* Read file to data variable */
static uint32_t _read_file(const char *file, char **data)
{
	uint32_t data_size = 0;
	int data_allocated, data_read, fd, fsize = 0;
	struct stat f_stat;

	fd = open(file, O_RDONLY);
	if (fd < 0) {
		log_flag(ESEARCH, "%s: Could not open state file %s",
			 plugin_type, file);
		return data_size;
	}
	if (fstat(fd, &f_stat)) {
		log_flag(ESEARCH, "%s: Could not stat state file %s",
			 plugin_type, file);
		close(fd);
		return data_size;
	}

	fsize = f_stat.st_size;
	data_allocated = BUF_SIZE;
	*data = xmalloc(data_allocated);
	while (1) {
		data_read = read(fd, &(*data)[data_size], BUF_SIZE);
		if (data_read < 0) {
			if (errno == EINTR)
				continue;
			else {
				error("%s: Read error on %s: %m", plugin_type,
				      file);
				break;
			}
		} else if (data_read == 0)	/* EOF */
			break;
		data_size += data_read;
		data_allocated += data_read;
		*data = xrealloc(*data, data_allocated);
	}
	close(fd);
	if (data_size != fsize) {
		error("%s: Could not read entire jobcomp state file %s (%d of %d)",
		      plugin_type, file, data_size, fsize);
	}
	return data_size;
}

/* Load jobcomp data from save state file */
static int _load_pending_jobs(void)
{
	int i, rc = SLURM_SUCCESS;
	char *saved_data = NULL, *state_file = NULL, *job_data = NULL;
	uint32_t data_size, job_cnt = 0, tmp32 = 0;
	buf_t *buffer;
	struct job_node *jnode;

	xstrfmtcat(state_file, "%s/%s",
		   slurm_conf.state_save_location, save_state_file);

	slurm_mutex_lock(&save_lock);
	data_size = _read_file(state_file, &saved_data);
	if ((data_size <= 0) || (saved_data == NULL)) {
		slurm_mutex_unlock(&save_lock);
		xfree(saved_data);
		xfree(state_file);
		return rc;
	}
	slurm_mutex_unlock(&save_lock);

	buffer = create_buf(saved_data, data_size);
	safe_unpack32(&job_cnt, buffer);
	for (i = 0; i < job_cnt; i++) {
		safe_unpackstr_xmalloc(&job_data, &tmp32, buffer);
		jnode = xmalloc(sizeof(struct job_node));
		jnode->serialized_job = job_data;
		list_enqueue(jobslist, jnode);
	}
	if (job_cnt > 0) {
		log_flag(ESEARCH, "%s: Loaded %u jobs from state file",
			 plugin_type, job_cnt);
	}
	free_buf(buffer);
	xfree(state_file);

	return rc;

unpack_error:
	error("%s: Error unpacking file %s", plugin_type, state_file);
	free_buf(buffer);
	xfree(state_file);
	return SLURM_ERROR;
}

/* Callback to handle the HTTP response */
static size_t _write_callback(void *contents, size_t size, size_t nmemb,
			      void *userp)
{
	size_t realsize = size * nmemb;
	struct http_response *mem = (struct http_response *) userp;

	mem->message = xrealloc(mem->message, mem->size + realsize + 1);

	memcpy(&(mem->message[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->message[mem->size] = 0;

	return realsize;
}

/* Try to index job into elasticsearch */
static int _index_job(const char *jobcomp)
{
	CURL *curl_handle = NULL;
	CURLcode res;
	struct http_response chunk;
	struct curl_slist *slist = NULL;
	int rc = SLURM_SUCCESS;
	char *token = NULL;

	slurm_mutex_lock(&location_mutex);
	if (log_url == NULL) {
		error("%s: JobCompLoc parameter not configured", plugin_type);
		slurm_mutex_unlock(&location_mutex);
		return SLURM_ERROR;
	}

	if (curl_global_init(CURL_GLOBAL_ALL) != 0) {
		error("%s: curl_global_init: %m", plugin_type);
		rc = SLURM_ERROR;
		goto cleanup_global_init;
	} else if ((curl_handle = curl_easy_init()) == NULL) {
		error("%s: curl_easy_init: %m", plugin_type);
		rc = SLURM_ERROR;
		goto cleanup_easy_init;
	}

	slist = curl_slist_append(slist, "Content-Type: " MIME_TYPE_JSON);

	if (slist == NULL) {
		error("%s: curl_slist_append: %m", plugin_type);
		rc = SLURM_ERROR;
		goto cleanup_easy_init;
	}

	chunk.message = xmalloc(1);
	chunk.size = 0;

	if (curl_easy_setopt(curl_handle, CURLOPT_URL, log_url) ||
	    curl_easy_setopt(curl_handle, CURLOPT_POST, 1) ||
	    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, jobcomp) ||
	    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE,
			     strlen(jobcomp)) ||
	    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, slist) ||
	    curl_easy_setopt(curl_handle, CURLOPT_HEADER, 1) ||
	    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION,
			     _write_callback) ||
	    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *) &chunk)) {
		error("%s: curl_easy_setopt() failed", plugin_type);
		rc = SLURM_ERROR;
		goto cleanup;
	}

	if ((res = curl_easy_perform(curl_handle)) != CURLE_OK) {
		log_flag(ESEARCH, "%s: Could not connect to: %s , reason: %s",
			 plugin_type, log_url, curl_easy_strerror(res));
		rc = SLURM_ERROR;
		goto cleanup;
	}

	token = strtok(chunk.message, " ");
	if (token == NULL) {
		error("%s: Could not receive the HTTP response status code from %s",
		      plugin_type, log_url);
		rc = SLURM_ERROR;
		goto cleanup;
	}
	token = strtok(NULL, " ");

	/* HTTP 100 (Continue). */
	if ((xstrcmp(token, "100") == 0)) {
		(void)  strtok(NULL, " ");
		token = strtok(NULL, " ");
	}

	/*
	 * HTTP 200 (OK)	- request succeed.
	 * HTTP 201 (Created)	- request succeed and resource created.
	 */
	if ((xstrcmp(token, "200") != 0) && (xstrcmp(token, "201") != 0)) {
		log_flag(ESEARCH, "%s: HTTP status code %s received from %s",
			 plugin_type, token, log_url);
		log_flag(ESEARCH, "%s: HTTP response:\n%s",
			 plugin_type, chunk.message);
		rc = SLURM_ERROR;
	} else {
		token = strtok((char *)jobcomp, ",");
		(void)  strtok(token, ":");
		token = strtok(NULL, ":");
		log_flag(ESEARCH, "%s: Job with jobid %s indexed into elasticsearch",
			 plugin_type, token);
	}

cleanup:
	curl_slist_free_all(slist);
	xfree(chunk.message);
cleanup_easy_init:
	curl_easy_cleanup(curl_handle);
cleanup_global_init:
	curl_global_cleanup();
	slurm_mutex_unlock(&location_mutex);
	return rc;
}

/* Saves the state of all jobcomp data for further indexing retries */
static int _save_state(void)
{
	int fd, rc = SLURM_SUCCESS;
	char *state_file = NULL, *new_file, *old_file;
	ListIterator iter;
	static int high_buffer_size = (1024 * 1024);
	buf_t *buffer = init_buf(high_buffer_size);
	uint32_t job_cnt;
	struct job_node *jnode;

	job_cnt = list_count(jobslist);
	pack32(job_cnt, buffer);
	iter = list_iterator_create(jobslist);
	while ((jnode = (struct job_node *)list_next(iter))) {
		packstr(jnode->serialized_job, buffer);
	}
	list_iterator_destroy(iter);

	xstrfmtcat(state_file, "%s/%s",
		   slurm_conf.state_save_location, save_state_file);

	old_file = xstrdup(state_file);
	new_file = xstrdup(state_file);
	xstrcat(new_file, ".new");
	xstrcat(old_file, ".old");

	slurm_mutex_lock(&save_lock);
	fd = open(new_file, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR |
		  O_CLOEXEC);
	if (fd < 0) {
		error("%s: Can't save jobcomp state, open file %s error %m",
		      plugin_type, new_file);
		rc = SLURM_ERROR;
	} else {
		int pos = 0, nwrite, amount, rc2;
		char *data;
		nwrite = get_buf_offset(buffer);
		data = (char *) get_buf_data(buffer);
		high_buffer_size = MAX(nwrite, high_buffer_size);
		while (nwrite > 0) {
			amount = write(fd, &data[pos], nwrite);
			if ((amount < 0) && (errno != EINTR)) {
				error("%s: Error writing file %s, %m",
				      plugin_type, new_file);
				rc = SLURM_ERROR;
				break;
			}
			nwrite -= amount;
			pos += amount;
		}
		if ((rc2 = fsync_and_close(fd, save_state_file)))
			rc = rc2;
	}

	if (rc == SLURM_ERROR)
		(void) unlink(new_file);
	else {
		(void) unlink(old_file);
		if (link(state_file, old_file)) {
			error("%s: Unable to create link for %s -> %s: %m",
			      plugin_type, state_file, old_file);
			rc = SLURM_ERROR;
		}
		(void) unlink(state_file);
		if (link(new_file, state_file)) {
			error("%s: Unable to create link for %s -> %s: %m",
			      plugin_type, new_file, state_file);
			rc = SLURM_ERROR;
		}
		(void) unlink(new_file);
	}

	xfree(old_file);
	xfree(state_file);
	xfree(new_file);
	slurm_mutex_unlock(&save_lock);

	free_buf(buffer);

	return rc;
}

/* This is a variation of slurm_make_time_str() in src/common/parse_time.h
 * This version uses ISO8601 format by default. */
static void _make_time_str(time_t * time, char *string, int size)
{
	struct tm time_tm;

	if (*time == (time_t) 0) {
		snprintf(string, size, "Unknown");
	} else {
		/* Format YYYY-MM-DDTHH:MM:SS, ISO8601 standard format */
		gmtime_r(time, &time_tm);
		strftime(string, size, "%FT%T", &time_tm);
	}
}

extern int jobcomp_p_log_record(job_record_t *job_ptr)
{
	char usr_str[32], grp_str[32], start_str[32], end_str[32], time_str[32];
	char *state_string = NULL;
	char *exit_code_str = NULL, *derived_ec_str = NULL;
	buf_t *script;
	enum job_states job_state;
	int i, tmp_int, tmp_int2;
	time_t elapsed_time;
	uint32_t time_limit;
	struct job_node *jnode;
	data_t *record = NULL;
	int rc;

	if (list_count(jobslist) > MAX_JOBS) {
		error("%s: Limit of %d enqueued jobs in memory waiting to be indexed reached. Job %lu discarded",
		      plugin_type, MAX_JOBS, (unsigned long)job_ptr->job_id);
		return SLURM_ERROR;
	}

	_get_user_name(job_ptr->user_id, usr_str, sizeof(usr_str));
	_get_group_name(job_ptr->group_id, grp_str, sizeof(grp_str));

	if ((job_ptr->time_limit == NO_VAL) && job_ptr->part_ptr)
		time_limit = job_ptr->part_ptr->max_time;
	else
		time_limit = job_ptr->time_limit;

	if (job_ptr->job_state & JOB_RESIZING) {
		time_t now = time(NULL);
		state_string = job_state_string(job_ptr->job_state);
		if (job_ptr->resize_time) {
			_make_time_str(&job_ptr->resize_time, start_str,
				       sizeof(start_str));
		} else {
			_make_time_str(&job_ptr->start_time, start_str,
				       sizeof(start_str));
		}
		_make_time_str(&now, end_str, sizeof(end_str));
	} else {
		/* Job state will typically have JOB_COMPLETING or JOB_RESIZING
		 * flag set when called. We remove the flags to get the eventual
		 * completion state: JOB_FAILED, JOB_TIMEOUT, etc. */
		job_state = job_ptr->job_state & JOB_STATE_BASE;
		state_string = job_state_string(job_state);
		if (job_ptr->resize_time) {
			_make_time_str(&job_ptr->resize_time, start_str,
				       sizeof(start_str));
		} else if (job_ptr->start_time > job_ptr->end_time) {
			/* Job cancelled while pending and
			 * expected start time is in the future. */
			snprintf(start_str, sizeof(start_str), "Unknown");
		} else {
			_make_time_str(&job_ptr->start_time, start_str,
				       sizeof(start_str));
		}
		_make_time_str(&job_ptr->end_time, end_str, sizeof(end_str));
	}

	elapsed_time = job_ptr->end_time - job_ptr->start_time;

	tmp_int = tmp_int2 = 0;
	if (job_ptr->derived_ec == NO_VAL)
		;
	else if (WIFSIGNALED(job_ptr->derived_ec))
		tmp_int2 = WTERMSIG(job_ptr->derived_ec);
	else if (WIFEXITED(job_ptr->derived_ec))
		tmp_int = WEXITSTATUS(job_ptr->derived_ec);
	xstrfmtcat(derived_ec_str, "%d:%d", tmp_int, tmp_int2);

	tmp_int = tmp_int2 = 0;
	if (job_ptr->exit_code == NO_VAL)
		;
	else if (WIFSIGNALED(job_ptr->exit_code))
		tmp_int2 = WTERMSIG(job_ptr->exit_code);
	else if (WIFEXITED(job_ptr->exit_code))
		tmp_int = WEXITSTATUS(job_ptr->exit_code);
	xstrfmtcat(exit_code_str, "%d:%d", tmp_int, tmp_int2);

	record = data_set_dict(data_new());

	data_set_int(data_key_set(record, "jobid"), job_ptr->job_id);
	data_set_string(data_key_set(record, "container"), job_ptr->container);
	data_set_string(data_key_set(record, "username"), usr_str);
	data_set_int(data_key_set(record, "user_id"), job_ptr->user_id);
	data_set_string(data_key_set(record, "groupname"), grp_str);
	data_set_int(data_key_set(record, "group_id"), job_ptr->group_id);
	data_set_string(data_key_set(record, "@start"), start_str);
	data_set_string(data_key_set(record, "@end"), end_str);
	data_set_int(data_key_set(record, "elapsed"), elapsed_time);
	data_set_string(data_key_set(record, "partition"), job_ptr->partition);
	data_set_string(data_key_set(record, "alloc_node"),
			job_ptr->alloc_node);
	data_set_string(data_key_set(record, "nodes"), job_ptr->nodes);
	data_set_int(data_key_set(record, "total_cpus"), job_ptr->total_cpus);
	data_set_int(data_key_set(record, "total_nodes"), job_ptr->total_nodes);
	data_set_string_own(data_key_set(record, "derived_ec"), derived_ec_str);
	derived_ec_str = NULL;
	data_set_string_own(data_key_set(record, "exit_code"), exit_code_str);
	exit_code_str = NULL;
	data_set_string(data_key_set(record, "state"), state_string);
	data_set_float(data_key_set(record, "cpu_hours"),
		       ((elapsed_time * job_ptr->total_cpus) / 3600.0f));

	if (job_ptr->array_task_id != NO_VAL) {
		data_set_int(data_key_set(record, "array_job_id"),
			     job_ptr->array_job_id);
		data_set_int(data_key_set(record, "array_task_id"),
			     job_ptr->array_task_id);
	}

	if (job_ptr->het_job_id != NO_VAL) {
		/* Continue supporting the old terms. */
		data_set_int(data_key_set(record, "pack_job_id"),
			     job_ptr->het_job_id);
		data_set_int(data_key_set(record, "pack_job_offset"),
			     job_ptr->het_job_offset);
		data_set_int(data_key_set(record, "het_job_id"),
			     job_ptr->het_job_id);
		data_set_int(data_key_set(record, "het_job_offset"),
			     job_ptr->het_job_offset);
	}

	if (job_ptr->details && job_ptr->details->submit_time) {
		_make_time_str(&job_ptr->details->submit_time,
			       time_str, sizeof(time_str));
		data_set_string(data_key_set(record, "@submit"), time_str);
	}

	if (job_ptr->details && job_ptr->details->begin_time) {
		_make_time_str(&job_ptr->details->begin_time,
			       time_str, sizeof(time_str));
		data_set_string(data_key_set(record, "@eligible"), time_str);
		if (job_ptr->start_time) {
			int64_t queue_wait = (int64_t)difftime(
				job_ptr->start_time,
				job_ptr->details->begin_time);
			data_set_int(data_key_set(record, "@queue_wait"),
				     queue_wait);
		}
	}

	if (job_ptr->details
	    && (job_ptr->details->work_dir && job_ptr->details->work_dir[0])) {
		data_set_string(data_key_set(record, "work_dir"),
				job_ptr->details->work_dir);
	}

	if (job_ptr->details
	    && (job_ptr->details->std_err && job_ptr->details->std_err[0])) {
		data_set_string(data_key_set(record, "std_err"),
				job_ptr->details->std_err);
	}

	if (job_ptr->details
	    && (job_ptr->details->std_in && job_ptr->details->std_in[0])) {
		data_set_string(data_key_set(record, "std_in"),
				job_ptr->details->std_in);
	}

	if (job_ptr->details
	    && (job_ptr->details->std_out && job_ptr->details->std_out[0])) {
		data_set_string(data_key_set(record, "std_out"),
				job_ptr->details->std_out);
	}

	if (job_ptr->assoc_ptr != NULL) {
		data_set_string(data_key_set(record, "cluster"),
				job_ptr->assoc_ptr->cluster);
	}

	if (job_ptr->qos_ptr != NULL) {
		data_set_string(data_key_set(record, "qos"),
				job_ptr->qos_ptr->name);
	}

	if (job_ptr->details && (job_ptr->details->num_tasks != NO_VAL)) {
		data_set_int(data_key_set(record, "ntasks"),
			     job_ptr->details->num_tasks);
	}

	if (job_ptr->details
	    && (job_ptr->details->ntasks_per_node != NO_VAL16)) {
		data_set_int(data_key_set(record, "ntasks_per_node"),
			     job_ptr->details->ntasks_per_node);
	}

	if (job_ptr->details
	    && (job_ptr->details->ntasks_per_tres != NO_VAL16)) {
		data_set_int(data_key_set(record, "ntasks_per_tres"),
			     job_ptr->details->ntasks_per_tres);
	}

	if (job_ptr->details
	    && (job_ptr->details->cpus_per_task != NO_VAL16)) {
		data_set_int(data_key_set(record, "cpus_per_task"),
			     job_ptr->details->cpus_per_task);
	}

	if (job_ptr->details
	    && (job_ptr->details->orig_dependency
		&& job_ptr->details->orig_dependency[0])) {
		data_set_string(data_key_set(record, "orig_dependency"),
				job_ptr->details->orig_dependency);
	}

	if (job_ptr->details
	    && (job_ptr->details->exc_nodes
		&& job_ptr->details->exc_nodes[0])) {
		data_set_string(data_key_set(record, "excluded_nodes"),
				job_ptr->details->exc_nodes);
	}

	if (time_limit != INFINITE) {
		data_set_int(data_key_set(record, "time_limit"),
			     (time_limit * 60));
	}

	if (job_ptr->name && job_ptr->name[0]) {
		data_set_string(data_key_set(record, "job_name"),
				job_ptr->name);
	}

	if (job_ptr->resv_name && job_ptr->resv_name[0]) {
		data_set_string(data_key_set(record, "reservation_name"),
				job_ptr->resv_name);
	}

	if (job_ptr->wckey && job_ptr->wckey[0]) {
		data_set_string(data_key_set(record, "wc_key"), job_ptr->wckey);
	}

	if (job_ptr->tres_fmt_req_str && job_ptr->tres_fmt_req_str[0]) {
		data_set_string(data_key_set(record, "tres_req"),
				job_ptr->tres_fmt_req_str);
	}

	if (job_ptr->tres_fmt_alloc_str && job_ptr->tres_fmt_alloc_str[0]) {
		data_set_string(data_key_set(record, "tres_alloc"),
				job_ptr->tres_fmt_alloc_str);
	}

	if (job_ptr->account && job_ptr->account[0]) {
		data_set_string(data_key_set(record, "account"),
				job_ptr->account);
	}

	script = get_job_script(job_ptr);
	if (script) {
		data_set_string(data_key_set(record, "script"),
				get_buf_data(script));
	}
	free_buf(script);

	if (job_ptr->assoc_ptr) {
		assoc_mgr_lock_t locks = { READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK,
					   NO_LOCK, NO_LOCK, NO_LOCK };
		slurmdb_assoc_rec_t *assoc_ptr = job_ptr->assoc_ptr;
		char *parent_accounts = NULL;
		char **acc_aux = NULL;
		int nparents = 0;

		assoc_mgr_lock(&locks);

		/* Start at the first parent and go up. When studying
		 * this code it was slightly faster to do 2 loops on
		 * the association linked list and only 1 xmalloc but
		 * we opted for cleaner looking code and going with a
		 * realloc. */
		while (assoc_ptr) {
			if (assoc_ptr->acct) {
				acc_aux = xrealloc(acc_aux,
						   sizeof(char *) *
						   (nparents + 1));
				acc_aux[nparents++] = assoc_ptr->acct;
			}
			assoc_ptr = assoc_ptr->usage->parent_assoc_ptr;
		}

		for (i = nparents - 1; i >= 0; i--)
			xstrfmtcat(parent_accounts, "/%s", acc_aux[i]);
		xfree(acc_aux);

		data_set_string(data_key_set(record, "parent_accounts"),
				parent_accounts);

		xfree(parent_accounts);

		assoc_mgr_unlock(&locks);
	}

	jnode = xmalloc(sizeof(struct job_node));

	if ((rc = data_g_serialize(&jnode->serialized_job, record,
				   MIME_TYPE_JSON, DATA_SER_FLAGS_COMPACT))) {
		xfree(jnode);
	} else {
		list_enqueue(jobslist, jnode);
	}

	FREE_NULL_DATA(record);
	return rc;
}

extern void *_process_jobs(void *x)
{
	ListIterator iter;
	struct job_node *jnode = NULL;
	struct timespec ts = {0, 0};
	time_t now;

	/* Wait for jobcomp_p_set_location log_url setup. */
	slurm_mutex_lock(&location_mutex);
	ts.tv_sec = time(NULL) + INDEX_RETRY_INTERVAL;
	slurm_cond_timedwait(&location_cond, &location_mutex, &ts);
	slurm_mutex_unlock(&location_mutex);

	while (!thread_shutdown) {
		int success_cnt = 0, fail_cnt = 0, wait_retry_cnt = 0;
		sleep(1);
		iter = list_iterator_create(jobslist);
		while ((jnode = (struct job_node *)list_next(iter)) &&
		       !thread_shutdown) {
			now = time(NULL);
			if ((jnode->last_index_retry == 0) ||
			    (difftime(now, jnode->last_index_retry) >=
					INDEX_RETRY_INTERVAL)) {
				if ((_index_job(jnode->serialized_job) ==
				     SLURM_SUCCESS)) {
					list_delete_item(iter);
					success_cnt++;
				} else {
					jnode->last_index_retry = now;
					fail_cnt++;
				}
			} else
				wait_retry_cnt++;
		}
		list_iterator_destroy(iter);
		if ((success_cnt || fail_cnt))
			log_flag(ESEARCH, "%s: index success:%d fail:%d wait_retry:%d",
				 plugin_type, success_cnt, fail_cnt,
				 wait_retry_cnt);
	}
	return NULL;
}

static void _jobslist_del(void *x)
{
	struct job_node *jnode = (struct job_node *) x;
	xfree(jnode->serialized_job);
	xfree(jnode);
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called. Put global initialization here.
 */
extern int init(void)
{
	int rc;

	if ((rc = data_init(MIME_TYPE_JSON_PLUGIN, NULL))) {
		error("%s: unable to load JSON serializer: %s", __func__,
		      slurm_strerror(rc));
		return rc;
	}

	jobslist = list_create(_jobslist_del);
	slurm_thread_create(&job_handler_thread, _process_jobs, NULL);
	slurm_mutex_lock(&pend_jobs_lock);
	(void) _load_pending_jobs();
	slurm_mutex_unlock(&pend_jobs_lock);

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	thread_shutdown = true;
	pthread_join(job_handler_thread, NULL);

	_save_state();
	list_destroy(jobslist);
	xfree(log_url);
	return SLURM_SUCCESS;
}

/*
 * The remainder of this file implements the standard Slurm job completion
 * logging API.
 */
extern int jobcomp_p_set_location(char *location)
{
	int rc = SLURM_SUCCESS;

	if (location == NULL) {
		error("%s: JobCompLoc parameter not configured", plugin_type);
		return SLURM_ERROR;
	}

	slurm_mutex_lock(&location_mutex);
	if (log_url)
		xfree(log_url);
	log_url = xstrdup(location);
	slurm_cond_broadcast(&location_cond);
	slurm_mutex_unlock(&location_mutex);

	return rc;
}

/*
 * get info from the database
 * in/out job_list List of job_rec_t *
 * note List needs to be freed when called
 */
extern List jobcomp_p_get_jobs(slurmdb_job_cond_t *job_cond)
{
	debug("%s function is not implemented", __func__);
	return NULL;
}
