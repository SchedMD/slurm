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

#include "src/common/slurm_xlator.h"
#include "src/common/data.h"
#include "src/common/fd.h"
#include "src/common/list.h"
#include "src/common/parse_time.h"
#include "src/interfaces/jobcomp.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_time.h"
#include "src/common/slurmdb_defs.h"
#include "src/common/xstring.h"
#include "src/interfaces/serializer.h"
#include "src/plugins/jobcomp/common/jobcomp_common.h"
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

/* Load jobcomp data from save state file */
static int _load_pending_jobs(void)
{
	int i, rc = SLURM_SUCCESS;
	char *job_data = NULL;
	uint32_t job_cnt = 0, tmp32 = 0;
	buf_t *buffer = NULL;
	struct job_node *jnode;

	slurm_mutex_lock(&save_lock);
	if (!(buffer = jobcomp_common_load_state_file(save_state_file))) {
		slurm_mutex_unlock(&save_lock);
		return SLURM_ERROR;
	}
	slurm_mutex_unlock(&save_lock);

	safe_unpack32(&job_cnt, buffer);
	for (i = 0; i < job_cnt; i++) {
		safe_unpackstr_xmalloc(&job_data, &tmp32, buffer);
		jnode = xmalloc(sizeof(struct job_node));
		jnode->serialized_job = job_data;
		list_enqueue(jobslist, jnode);
	}
	if (job_cnt > 0) {
		log_flag(JOBCOMP, "Loaded %u jobs from state file", job_cnt);
	}
	FREE_NULL_BUFFER(buffer);

	return rc;

unpack_error:
	error("%s: Error unpacking file %s", plugin_type, save_state_file);
	FREE_NULL_BUFFER(buffer);
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
		log_flag(JOBCOMP, "Could not connect to: %s , reason: %s",
			 log_url, curl_easy_strerror(res));
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
		log_flag(JOBCOMP, "HTTP status code %s received from %s",
			 token, log_url);
		log_flag(JOBCOMP, "HTTP response:\n%s", chunk.message);
		rc = SLURM_ERROR;
	} else {
		token = strtok((char *)jobcomp, ",");
		(void)  strtok(token, ":");
		token = strtok(NULL, ":");
		log_flag(JOBCOMP, "Job with jobid %s indexed into elasticsearch",
			 token);
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
	int rc = SLURM_SUCCESS;
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

	slurm_mutex_lock(&save_lock);
	jobcomp_common_write_state_file(buffer, save_state_file);
	slurm_mutex_unlock(&save_lock);

	FREE_NULL_BUFFER(buffer);

	return rc;
}

extern int jobcomp_p_log_record(job_record_t *job_ptr)
{
	struct job_node *jnode = NULL;
	data_t *record = NULL;
	int rc;

	if (list_count(jobslist) > MAX_JOBS) {
		error("%s: Limit of %d enqueued jobs in memory waiting to be indexed reached. %pJ discarded",
		      plugin_type, MAX_JOBS, job_ptr);
		return SLURM_ERROR;
	}

	record = jobcomp_common_job_record_to_data(job_ptr);
	jnode = xmalloc(sizeof(struct job_node));
	if ((rc = serialize_g_data_to_string(&jnode->serialized_job, NULL,
					     record, MIME_TYPE_JSON,
					     SER_FLAGS_COMPACT))) {
		xfree(jnode);
		log_flag(JOBCOMP, "unable to serialize %pJ to JSON: %s",
			 job_ptr, slurm_strerror(rc));
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
			log_flag(JOBCOMP, "index success:%d fail:%d wait_retry:%d",
				 success_cnt, fail_cnt,
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

	if ((rc = data_init())) {
		error("%s: unable to init data structures: %s",
		      __func__, slurm_strerror(rc));
		return rc;
	}

	if ((rc = serializer_g_init(MIME_TYPE_JSON_PLUGIN, NULL))) {
		error("%s: unable to load JSON serializer: %s",
		      __func__, slurm_strerror(rc));
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
	FREE_NULL_LIST(jobslist);
	xfree(log_url);
	return SLURM_SUCCESS;
}

/*
 * The remainder of this file implements the standard Slurm job completion
 * logging API.
 */
extern int jobcomp_p_set_location(void)
{
	char *location = slurm_conf.job_comp_loc;
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
