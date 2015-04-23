/*****************************************************************************\
 *  jobcomp_elasticsearch.c - elasticsearch slurm job completion logging plugin.
 *****************************************************************************
 *  Produced at Barcelona Supercomputing Center, in collaboration with 
 *  Barcelona School of Informatics.
 *  Written by Alejandro Sanchez Graells <alejandro.sanchezgraells@bsc.es>,
 *  <asanchez1987@gmail.com>, who borrowed heavily from jobcomp/filetxt 
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

#if HAVE_CONFIG_H
#   include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include <curl/curl.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "src/common/parse_time.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_jobcomp.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"


#define USE_ISO8601 1

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
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "jobcomp" for SLURM job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load job completion logging plugins if the plugin_type string has a
 * prefix of "jobcomp/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum version for their plugins as the job completion logging API
 * matures.
 */
const char plugin_name[] = "Job completion elasticsearch logging plugin";
const char plugin_type[] = "jobcomp/elasticsearch";
const uint32_t plugin_version = 100;

#define JOBCOMP_DATA_FORMAT "{\"jobid\":%lu,\"username\":\"%s\","\
	"\"user_id\":%lu,\"groupname\":\"%s\",\"group_id\":%lu,"\
	"\"@start\":\"%s\",\"@end\":\"%s\",\"elapsed\":%ld,"\
	"\"partition\":\"%s\",\"alloc_node\":\"%s\","\
	"\"nodes\":\"%s\",\"total_cpus\":%lu,\"total_nodes\":%lu,"\
	"\"derived_exitcode\":%hu,\"exitcode\":%lu,\"state\":\"%s\""

/* Type for error string table entries */
typedef struct {
	int xe_number;
	char *xe_message;
} slurm_errtab_t;

static slurm_errtab_t slurm_errtab[] = {
	{0, "No error"},
	{-1, "Unspecified error"}
};

/* Type for handling HTTP responses */
struct http_response {
	char *message;
	size_t size;
};

/* Type for jobcomp data pending to be indexed */
typedef struct {
	uint32_t nelems;
	char **jobs;
} pending_jobs_t;

pending_jobs_t pend_jobs;

char *save_state_file = "elasticsearch_state";
char *index_type = "/slurm/jobcomp";
char *log_url = NULL;

static pthread_mutex_t save_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t pend_jobs_lock = PTHREAD_MUTEX_INITIALIZER;

/* A plugin-global errno. */
static int plugin_errno = SLURM_SUCCESS;


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

/*
 * Linear search through table of errno values and strings,
 * returns NULL on error, string on success.
 */
static char *_lookup_slurm_api_errtab(int errnum)
{
	char *res = NULL;
	int i;

	for (i = 0; i < sizeof(slurm_errtab) / sizeof(slurm_errtab_t); i++) {
		if (slurm_errtab[i].xe_number == errnum) {
			res = slurm_errtab[i].xe_message;
			break;
		}
	}
	return res;
}

/* Read file to data variable */
uint32_t _read_file(const char *file, char **data)
{
	uint32_t data_size = 0;
	int data_allocated, data_read, fd, fsize = 0;
	struct stat f_stat;

	fd = open(file, O_RDONLY);
	if (fd < 0) {
		debug("Could not open jobcomp state file %s", file);
		return data_size;
	}
	if (fstat(fd, &f_stat)) {
		debug("Could not stat jobcomp state file %s", file);
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
				debug("Read error on %s: %m", file);
				break;
			}
		} else if (data_read == 0)	/* eof */
			break;
		data_size += data_read;
		data_allocated += data_read;
		xrealloc(*data, data_allocated);
	}
	close(fd);
	return data_size;
}

/* Load jobcomp data from save state file */
int _load_pending_jobs()
{
	int rc = SLURM_SUCCESS;
	char *saved_data = NULL;
	char *state_file;
	uint32_t data_size;
	Buf buffer;
	pend_jobs.nelems = 0;
	pend_jobs.jobs = NULL;

	state_file = slurm_get_state_save_location();

	if (state_file == NULL) {
		debug("Could not retrieve StateSaveLocation from conf");
		return SLURM_ERROR;
	}

	if (state_file[strlen(state_file) - 1] != '/')
		xstrcat(state_file, "/");
	xstrcat(state_file, save_state_file);

	slurm_mutex_lock(&save_lock);
	data_size = _read_file(state_file, &saved_data);
	if (data_size <= 0 || saved_data == NULL) {
		slurm_mutex_unlock(&save_lock);
		xfree(saved_data);
		xfree(state_file);
		return rc;
	}
	slurm_mutex_unlock(&save_lock);

	buffer = create_buf(saved_data, data_size);
	safe_unpackstr_array(&pend_jobs.jobs, &pend_jobs.nelems, buffer);
	if (pend_jobs.nelems > 0) {
		debug("Loaded jobcomp pending data about %d jobs",
		      pend_jobs.nelems);
	}
	free_buf(buffer);
	xfree(state_file);

	return rc;

      unpack_error:
	error("Error unpacking file %s", state_file);
	free_buf(buffer);
	return SLURM_FAILURE;
}

/* Callback to handle the HTTP response */
static size_t _write_callback(void *contents, size_t size, size_t nmemb,
			      void *userp)
{
	size_t realsize = size * nmemb;
	struct http_response *mem = (struct http_response *) userp;

	xrealloc(mem->message, mem->size + realsize + 1);

	memcpy(&(mem->message[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->message[mem->size] = 0;

	return realsize;
}

/* Try to index job into elasticsearch */
int _index_job(const char *jobcomp)
{
	int rc = SLURM_SUCCESS;

	if (log_url == NULL) {
		debug("JobCompLoc parameter not configured");
		return SLURM_ERROR;
	}

	CURL *curl_handle;
	CURLcode res;
	struct http_response chunk;

	chunk.message = xmalloc(1);
	chunk.size = 0;

	curl_global_init(CURL_GLOBAL_ALL);
	curl_handle = curl_easy_init();

	if (curl_handle) {
		char *url = xstrdup(log_url);
		xstrcat(url, index_type);

		curl_easy_setopt(curl_handle, CURLOPT_URL, url);
		curl_easy_setopt(curl_handle, CURLOPT_POST, 1);
		curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, jobcomp);
		curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE,
				 strlen(jobcomp));
		curl_easy_setopt(curl_handle, CURLOPT_HEADER, 1);
		curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION,
				 _write_callback);
		curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA,
				 (void *) &chunk);

		res = curl_easy_perform(curl_handle);
		if (res != CURLE_OK) {
			debug2("Could not connect to: %s , reason: %s", url,
			       curl_easy_strerror(res));
			rc = SLURM_ERROR;
		} else {
			char *token, *response;
			response = xstrdup(chunk.message);
			token = strtok(chunk.message, " ");
			if (token == NULL) {
				debug("Could not receive the HTTP response "
				      "status code from %s", url);
				rc = SLURM_ERROR;
			} else {
				token = strtok(NULL, " ");
				if ((xstrcmp(token, "100") == 0)) {
					token = strtok(NULL, " ");
					token = strtok(NULL, " ");
				}
				if ((xstrcmp(token, "200") != 0)
				    && (xstrcmp(token, "201") != 0)) {
					debug
					    ("HTTP status code %s received from"
					     " %s", token, url);
					debug("Check wether index writes and "
					      "metadata changes are enabled on"
					      " %s", url);
					debug3("HTTP Response:\n%s", response);
					rc = SLURM_ERROR;
				} else {
					token = strtok((char *)jobcomp, ",");
					token = strtok(token, ":");
					token = strtok(NULL, ":");
					debug
					    ("Jobcomp data related to jobid %s "
					     "indexed into elasticsearch",
					     token);
				}
				xfree(chunk.message);
				xfree(response);
			}
		}
		curl_easy_cleanup(curl_handle);
		xfree(url);
	}
	curl_global_cleanup();

	return rc;
}

/* Escape characters according to RFC7159 */
char *_json_escape(const char *str)
{
	char *ret = NULL;
	int i;
	for (i = 0; i < strlen(str); ++i) {
		switch (str[i]) {
		case '\\':
			xstrcat(ret, "\\\\");
			break;
		case '"':
			xstrcat(ret, "\\\"");
			break;
		case '\n':
			xstrcat(ret, "\\n");
			break;
		case '\b':
			xstrcat(ret, "\\b");
			break;
		case '\f':
			xstrcat(ret, "\\f");
			break;
		case '\r':
			xstrcat(ret, "\\r");
			break;
		case '\t':
			xstrcat(ret, "\\t");
			break;
		case '<':
			xstrcat(ret, "\\u003C");
			break;
		default:
			xstrcatchar(ret, str[i]);
		}
	}
	return ret;
}

/* Saves the state of all jobcomp data for further indexing retries */
int _save_state()
{
	int fd, rc = SLURM_SUCCESS;
	char *state_file, *new_file, *old_file;
	static int high_buffer_size = (1024 * 1024);
	Buf buffer = init_buf(high_buffer_size);

	slurm_mutex_lock(&pend_jobs_lock);
	packstr_array(pend_jobs.jobs, pend_jobs.nelems, buffer);
	slurm_mutex_unlock(&pend_jobs_lock);

	state_file = slurm_get_state_save_location();
	if (state_file == NULL || strlen(state_file) == 0) {
		debug("Could not retrieve StateSaveLocation from conf");
		return SLURM_ERROR;
	}

	if (state_file[strlen(state_file) - 1] != '/')
		xstrcat(state_file, "/");

	xstrcat(state_file, save_state_file);
	old_file = xstrdup(state_file);
	new_file = xstrdup(state_file);
	xstrcat(new_file, ".new");
	xstrcat(old_file, ".old");

	slurm_mutex_lock(&save_lock);
	fd = open(new_file, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		debug("Can't save jobcomp state, open file %s error %m",
		      new_file);
		rc = SLURM_ERROR;
	} else {
		int pos = 0, nwrite, amount;
		char *data;
		fd_set_close_on_exec(fd);
		nwrite = get_buf_offset(buffer);
		data = (char *) get_buf_data(buffer);
		high_buffer_size = MAX(nwrite, high_buffer_size);
		while (nwrite > 0) {
			amount = write(fd, &data[pos], nwrite);
			if ((amount < 0) && (errno != EINTR)) {
				debug("Error writing file %s, %m", new_file);
				rc = SLURM_ERROR;
				break;
			}
			nwrite -= amount;
			pos += amount;
		}
		rc = fsync_and_close(fd, save_state_file);
	}

	if (rc == SLURM_ERROR)
		(void) unlink(new_file);
	else {
		(void) unlink(old_file);
		if (link(state_file, old_file)) {
			debug("Unable to create link for %s -> %s: %m",
			      state_file, old_file);
			rc = SLURM_ERROR;
		}
		(void) unlink(state_file);
		if (link(new_file, state_file)) {
			debug("Unable to create link for %s -> %s: %m",
			      new_file, state_file);
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

/* Add jobcomp data to the pending jobs structure */
void _push_pending_job(char *j)
{
	xrealloc(pend_jobs.jobs, sizeof(char *) * (pend_jobs.nelems + 1));
	pend_jobs.jobs[pend_jobs.nelems] = xstrdup(j);
	pend_jobs.nelems++;
}

/* Updates pending jobs structure with the jobs that
 * failed to be indexed */
void _update_pending_jobs(int *m)
{
	int i;
	pending_jobs_t aux;
	aux.jobs = NULL;
	aux.nelems = 0;

	for (i = 0; i < pend_jobs.nelems; i++) {
		if (!m[i]) {
			xrealloc(aux.jobs, sizeof(char *) * (aux.nelems + 1));
			aux.jobs[aux.nelems] = xstrdup(pend_jobs.jobs[i]);
			aux.nelems++;
			xfree(pend_jobs.jobs[i]);
		}
	}

	xfree(pend_jobs.jobs);
	//pend_jobs.jobs = xmalloc(1);
	//xrealloc(pend_jobs.jobs, sizeof(char *) * (aux.nelems));
	pend_jobs = aux;
}

/* Try to index all the jobcomp data for pending jobs */
int _index_retry()
{
	int i, rc = SLURM_SUCCESS, marks = 0;

	slurm_mutex_lock(&pend_jobs_lock);
	int pop_marks[pend_jobs.nelems];

	for (i = 0; i < pend_jobs.nelems; i++) {
		pop_marks[i] = 0;
		debug("TESTTT: %s", pend_jobs.jobs[i]);
		if (_index_job(pend_jobs.jobs[i]) == SLURM_ERROR)
			rc = SLURM_ERROR;
		else {
			marks = 1;
			pop_marks[i] = 1;
			xfree(pend_jobs.jobs[i]);
		}
	}

	if (marks)
		_update_pending_jobs(pop_marks);

	slurm_mutex_unlock(&pend_jobs_lock);
	if (_save_state() == SLURM_ERROR)
		rc = SLURM_ERROR;

	return rc;
}

/* This is a variation of slurm_make_time_str() in src/common/parse_time.h
 * This version uses ISO8601 format by default. */
static void _make_time_str(time_t * time, char *string, int size)
{
	struct tm time_tm;

	gmtime_r(time, &time_tm);
	if (*time == (time_t) 0) {
		snprintf(string, size, "Unknown");
	} else {
#if USE_ISO8601
		/* Format YYYY-MM-DDTHH:MM:SS, ISO8601 standard format,
		 * NOTE: This is expected to break Maui, Moab and LSF
		 * schedulers management of SLURM. */
		snprintf(string, size,
			 "%4.4u-%2.2u-%2.2uT%2.2u:%2.2u:%2.2u",
			 (time_tm.tm_year + 1900), (time_tm.tm_mon + 1),
			 time_tm.tm_mday, time_tm.tm_hour, time_tm.tm_min,
			 time_tm.tm_sec);
#else
		/* Format MM/DD-HH:MM:SS */
		snprintf(string, size,
			 "%2.2u/%2.2u-%2.2u:%2.2u:%2.2u",
			 (time_tm.tm_mon + 1), time_tm.tm_mday,
			 time_tm.tm_hour, time_tm.tm_min, time_tm.tm_sec);

#endif
	}
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called. Put global initialization here.
 */
int init(void)
{
	int rc;

	slurm_mutex_lock(&pend_jobs_lock);
	rc = _load_pending_jobs();
	slurm_mutex_unlock(&pend_jobs_lock);

	return rc;
}

int fini(void)
{
	xfree(log_url);
	xfree(save_state_file);
	xfree(index_type);
	return SLURM_SUCCESS;
}

/*
 * The remainder of this file implements the standard SLURM job completion
 * logging API.
 */
extern int slurm_jobcomp_set_location(char *location)
{
	int rc = SLURM_SUCCESS;
	CURL *curl_handle;
	CURLcode res;

	if (location == NULL) {
		debug("JobCompLoc parameter not configured");
		return SLURM_ERROR;
	}

	log_url = xstrdup(location);

	curl_global_init(CURL_GLOBAL_ALL);
	curl_handle = curl_easy_init();
	if (curl_handle) {
		curl_easy_setopt(curl_handle, CURLOPT_URL, log_url);
		curl_easy_setopt(curl_handle, CURLOPT_NOBODY, 1);
		res = curl_easy_perform(curl_handle);
		if (res != CURLE_OK) {
			debug("Could not connect to: %s", log_url);
			rc = SLURM_ERROR;
		}
		curl_easy_cleanup(curl_handle);
	}
	curl_global_cleanup();

	if (rc == SLURM_SUCCESS && pend_jobs.nelems > 0) {
		if (_index_retry() == SLURM_ERROR) {
			debug("Could not index all jobcomp saved data");
		}
	}

	return rc;
}

extern int slurm_jobcomp_log_record(struct job_record *job_ptr)
{
	int nwritten, nparents, B_SIZE = 1024, rc = SLURM_SUCCESS;
	char usr_str[32], grp_str[32], start_str[32], end_str[32], lim_str[32],
	    submit_str[32], select_buf[128], *script, *cluster, *qos,
	    *state_string, *work_dir, *orig_dependency, *exc_nodes, *script_str,
	    *resv_name, *std_err, *std_in, *std_out, *parent_accounts;
	char **acc_aux;
	time_t elapsed_time, submit_time, eligible_time;
	size_t offset = 0, tot_size, wrote;
	enum job_states job_state;
	uint32_t time_limit, num_tasks;
	uint16_t ntasks_per_node, cpus_per_task;

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

	char *buffer = xmalloc(B_SIZE);

	nwritten = snprintf(buffer, B_SIZE, JOBCOMP_DATA_FORMAT,
			    (unsigned long) job_ptr->job_id, usr_str,
			    (unsigned long) job_ptr->user_id, grp_str,
			    (unsigned long) job_ptr->group_id, start_str,
			    end_str, (long) elapsed_time,
			    job_ptr->partition, job_ptr->alloc_node,
			    job_ptr->nodes, (unsigned long) job_ptr->total_cpus,
			    (unsigned long) job_ptr->total_nodes,
			    (unsigned long) job_ptr->derived_ec,
			    (unsigned long) job_ptr->exit_code, state_string);

	if (nwritten >= B_SIZE) {
		B_SIZE += nwritten + 1;
		xrealloc(buffer, B_SIZE);

		nwritten = snprintf(buffer, B_SIZE, JOBCOMP_DATA_FORMAT,
				    (unsigned long) job_ptr->job_id, usr_str,
				    (unsigned long) job_ptr->user_id, grp_str,
				    (unsigned long) job_ptr->group_id,
				    start_str, end_str, (long) elapsed_time,
				    job_ptr->partition, job_ptr->alloc_node,
				    job_ptr->nodes,
				    (unsigned long) job_ptr->total_cpus,
				    (unsigned long) job_ptr->total_nodes,
				    (unsigned long) job_ptr->derived_ec,
				    (unsigned long) job_ptr->exit_code,
				    state_string);

		if (nwritten >= B_SIZE) {
			debug("Job completion data truncated and lost");
			rc = SLURM_ERROR;
		}
	}

	int tmp_size = 256;
	char *tmp = xmalloc(tmp_size * sizeof(char));

	sprintf(tmp, ",\"cpu_hours\":%.6f",
		((float) elapsed_time * (float) job_ptr->total_cpus) /
		(float) 3600);
	xstrcat(buffer, tmp);

	if (job_ptr->details && (job_ptr->details->submit_time != NO_VAL)) {
		submit_time = job_ptr->details->submit_time;
		_make_time_str(&submit_time, submit_str, sizeof(submit_str));
		sprintf(tmp, ",\"@submit\":\"%s\"", submit_str);
		xstrcat(buffer, tmp);
	}

	if (job_ptr->details && (job_ptr->details->begin_time != NO_VAL)) {
		eligible_time =
		    job_ptr->start_time - job_ptr->details->begin_time;
		sprintf(tmp, ",\"eligible_time\":%lu", eligible_time);
		xstrcat(buffer, tmp);
	}

	if (job_ptr->details
	    && (job_ptr->details->work_dir != NULL
		&& strlen(job_ptr->details->work_dir) > 0)) {
		work_dir = job_ptr->details->work_dir;
		if ((strlen(work_dir)+14+1) > tmp_size) {
			tmp_size = strlen(work_dir) + 14 + 1;
			xrealloc(tmp, sizeof(char) * tmp_size);
		}		
		sprintf(tmp, ",\"work_dir\":\"%s\"", work_dir);
		xstrcat(buffer, tmp);
	}

	if (job_ptr->details
	    && (job_ptr->details->std_err != NULL
		&& strlen(job_ptr->details->std_err) > 0)) {
		std_err = job_ptr->details->std_err;
		if ((strlen(std_err)+13+1) > tmp_size) {
			tmp_size = strlen(std_err) + 13 + 1;
			xrealloc(tmp, sizeof(char) * tmp_size);
		}		
		sprintf(tmp, ",\"std_err\":\"%s\"", std_err);
		xstrcat(buffer, tmp);
	}

	if (job_ptr->details
	    && (job_ptr->details->std_in != NULL
		&& strlen(job_ptr->details->std_in) > 0)) {
		std_in = job_ptr->details->std_in;
		if ((strlen(std_in)+12+1) > tmp_size) {
			tmp_size = strlen(std_in) + 12 + 1;
			xrealloc(tmp, sizeof(char) * tmp_size);
		}		
		sprintf(tmp, ",\"std_in\":\"%s\"", std_in);
		xstrcat(buffer, tmp);
	}

	if (job_ptr->details
	    && (job_ptr->details->std_out != NULL
		&& strlen(job_ptr->details->std_out) > 0)) {
		std_out = job_ptr->details->std_out;
		if ((strlen(std_out)+13+1) > tmp_size) {
			tmp_size = strlen(std_out) + 13 + 1;
			xrealloc(tmp, sizeof(char) * tmp_size);
		}		
		sprintf(tmp, ",\"std_out\":\"%s\"", std_out);
		xstrcat(buffer, tmp);
	}

	if (job_ptr->assoc_ptr != NULL) {
		cluster =
		    ((slurmdb_association_rec_t *) job_ptr->assoc_ptr)->cluster;
		sprintf(tmp, ",\"cluster\":\"%s\"", cluster);
		xstrcat(buffer, tmp);
	}

	if (job_ptr->qos_ptr != NULL) {
		slurmdb_qos_rec_t *assoc =
		    (slurmdb_qos_rec_t *) job_ptr->qos_ptr;
		qos = assoc->name;
		sprintf(tmp, ",\"qos\":\"%s\"", qos);
		xstrcat(buffer, tmp);
	}

	if (job_ptr->details && (job_ptr->details->num_tasks != NO_VAL)) {
		num_tasks = job_ptr->details->num_tasks;
		sprintf(tmp, ",\"ntasks\":%hu", num_tasks);
		xstrcat(buffer, tmp);
	}

	if (job_ptr->details && (job_ptr->details->ntasks_per_node != NO_VAL)) {
		ntasks_per_node = job_ptr->details->ntasks_per_node;
		sprintf(tmp, ",\"ntasks_per_node\":%hu", ntasks_per_node);
		xstrcat(buffer, tmp);
	}

	if (job_ptr->details && (job_ptr->details->cpus_per_task != NO_VAL)) {
		cpus_per_task = job_ptr->details->cpus_per_task;
		sprintf(tmp, ",\"cpus_per_task\":%hu", cpus_per_task);
		xstrcat(buffer, tmp);
	}

	if (job_ptr->details
	    && (job_ptr->details->orig_dependency != NULL
		&& strlen(job_ptr->details->orig_dependency) > 0)) {
		orig_dependency = job_ptr->details->orig_dependency;
		if ((strlen(orig_dependency)+21+1) > tmp_size) {
			tmp_size = strlen(orig_dependency) + 21 + 1;
			xrealloc(tmp, sizeof(char) * tmp_size);
		}		
		sprintf(tmp, ",\"orig_dependency\":\"%s\"", orig_dependency);
		xstrcat(buffer, tmp);
	}

	if (job_ptr->details
	    && (job_ptr->details->exc_nodes != NULL
		&& strlen(job_ptr->details->exc_nodes) > 0)) {
		exc_nodes = job_ptr->details->exc_nodes;
		if ((strlen(exc_nodes)+20+1) > tmp_size) {
			tmp_size = strlen(exc_nodes) + 20 + 1;
			xrealloc(tmp, sizeof(char) * tmp_size);
		}		
		sprintf(tmp, ",\"excluded_nodes\":\"%s\"", exc_nodes);
		xstrcat(buffer, tmp);
	}

	if (time_limit != INFINITE) {
		sprintf(tmp, ",\"time_limit\":%lu", time_limit * 60);
		xstrcat(buffer, tmp);
	}

	if (job_ptr->resv_name != NULL && strlen(job_ptr->resv_name) > 0) {
		if ((strlen(job_ptr->resv_name)+22+1) > tmp_size) {
			tmp_size = strlen(job_ptr->resv_name) + 22 + 1;
			xrealloc(tmp, sizeof(char) * tmp_size);
		}		
		sprintf(tmp, ",\"reservation_name\":\"%s\"",
			job_ptr->resv_name);
		xstrcat(buffer, tmp);
	}

	if (job_ptr->gres_req != NULL && strlen(job_ptr->gres_req) > 0) {
		if ((strlen(job_ptr->gres_req)+14+1) > tmp_size) {
			tmp_size = strlen(job_ptr->gres_req) + 14 + 1;
			xrealloc(tmp, sizeof(char) * tmp_size);
		}		
		sprintf(tmp, ",\"gres_req\":\"%s\"", job_ptr->gres_req);
		xstrcat(buffer, tmp);
	}

	if (job_ptr->gres_alloc != NULL && strlen(job_ptr->gres_alloc) > 0) {
		if ((strlen(job_ptr->gres_alloc)+16+1) > tmp_size) {
			tmp_size = strlen(job_ptr->gres_alloc) + 16 + 1;
			xrealloc(tmp, sizeof(char) * tmp_size);
		}		
		sprintf(tmp, ",\"gres_alloc\":\"%s\"", job_ptr->gres_alloc);
		xstrcat(buffer, tmp);
	}

	if (job_ptr->account != NULL && strlen(job_ptr->account) > 0) {
		sprintf(tmp, ",\"account\":\"%s\"", job_ptr->account);
		xstrcat(buffer, tmp);
	}

	script = get_job_script(job_ptr);
	if (script != NULL && strlen(script) > 0) {
		script_str = _json_escape(script);
		if ((strlen(script_str)+12+1) > tmp_size) {
			tmp_size = strlen(script_str) + 12 + 1;
			xrealloc(tmp, sizeof(char) * strlen(script_str) + 1 + 12);
		}
		sprintf(tmp, ",\"script\":\"%s\"", script_str);
		xstrcat(buffer, tmp);
		xfree(script_str);
		xfree(script);
	}

	if (job_ptr->assoc_ptr) {
		slurmdb_association_rec_t assoc_rec, *assoc_ptr;

		parent_accounts = NULL;
		acc_aux = NULL;
		nparents = 0;

		memset(&assoc_rec, 0, sizeof(slurmdb_association_rec_t));
		assoc_rec.cluster = xstrdup(cluster);
		assoc_rec.id =
		    ((slurmdb_association_rec_t *) job_ptr->assoc_ptr)->
		    parent_id;

		do {
			assoc_mgr_fill_in_assoc(acct_db_conn, &assoc_rec,
						accounting_enforce, &assoc_ptr,
						false);
			xrealloc(acc_aux, sizeof(char *) * (nparents + 1));
			acc_aux[nparents] = xstrdup(assoc_ptr->acct);
			nparents++;
			assoc_rec.id = assoc_ptr->parent_id;
			xfree(assoc_rec.cluster);
			memset(&assoc_rec, 0,
			       sizeof(slurmdb_association_rec_t));
			assoc_rec.cluster = xstrdup(cluster);
			assoc_rec.id = assoc_ptr->parent_id;

		} while (xstrcmp(assoc_ptr->acct, "root") != 0);

		int i;
		for (i = nparents - 1; i >= 0; i--) {
			xstrcat(parent_accounts, "/");
			xstrcat(parent_accounts, acc_aux[i]);
			xfree(acc_aux[i]);
		}

		
		if ((strlen(parent_accounts)+21+1) > tmp_size) {
			tmp_size = strlen(parent_accounts) + 21 + 1;
			xrealloc(tmp, sizeof(char) * tmp_size);
		}		
		sprintf(tmp, ",\"parent_accounts\":\"%s\"", parent_accounts);
		xstrcat(buffer, tmp);
		xfree(acc_aux);
		xfree(assoc_rec.cluster);
		xfree(parent_accounts);
	}

	xstrcat(buffer, "}");

	if (rc == SLURM_SUCCESS) {
		if (_index_job(buffer) == SLURM_ERROR) {
			slurm_mutex_lock(&pend_jobs_lock);
			_push_pending_job(buffer);
			slurm_mutex_unlock(&pend_jobs_lock);
			rc = _save_state();
		} else {
			rc = _index_retry();
		}
	}

	xfree(tmp);
	xfree(buffer);

	return rc;
}

extern int slurm_jobcomp_get_errno(void)
{
	return plugin_errno;
}

extern char *slurm_jobcomp_strerror(int errnum)
{
	char *res = _lookup_slurm_api_errtab(errnum);
	return (res ? res : strerror(errnum));
}

/*
 * get info from the database
 * in/out job_list List of job_rec_t *
 * note List needs to be freed when called
 */
extern List slurm_jobcomp_get_jobs(slurmdb_job_cond_t * job_cond)
{
	info("This function is not implemented.");
	return NULL;
}

/*
 * expire old info from the database
 */
extern int slurm_jobcomp_archive(slurmdb_archive_cond_t * arch_cond)
{
	info("This function is not implemented.");
	return SLURM_SUCCESS;
}
