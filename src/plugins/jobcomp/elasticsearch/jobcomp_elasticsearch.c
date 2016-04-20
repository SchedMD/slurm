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

#define USE_ISO8601 1
#define MAX_STR_LEN 10240	/* 10 KB */
#define MAX_JOBS 1000000

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
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

#define INDEX_RETRY_INTERVAL 30
#define JOBCOMP_DATA_FORMAT "{\"jobid\":%lu,\"username\":\"%s\","	\
	"\"user_id\":%lu,\"groupname\":\"%s\",\"group_id\":%lu,"	\
	"\"@start\":\"%s\",\"@end\":\"%s\",\"elapsed\":%ld,"		\
	"\"partition\":\"%s\",\"alloc_node\":\"%s\","			\
	"\"nodes\":\"%s\",\"total_cpus\":%lu,\"total_nodes\":%lu,"	\
	"\"derived_exitcode\":%lu,\"exitcode\":%lu,\"state\":\"%s\""

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
int accounting_enforce __attribute__((weak_import)) = 0;
void *acct_db_conn  __attribute__((weak_import)) = NULL;
#else
int accounting_enforce = 0;
void *acct_db_conn = NULL;
#endif

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

struct job_node {
	time_t last_index_retry;
	char * serialized_job;
};

char *save_state_file = "elasticsearch_state";
char *index_type = "/slurm/jobcomp";
char *log_url = NULL;

static pthread_mutex_t save_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t pend_jobs_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t job_handler_thread;
static List jobslist = NULL;
static bool thread_shutdown = false;

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
static uint32_t _read_file(const char *file, char **data)
{
	uint32_t data_size = 0;
	int data_allocated, data_read, fd, fsize = 0;
	struct stat f_stat;

	fd = open(file, O_RDONLY);
	if (fd < 0) {
		if (slurm_get_debug_flags() & DEBUG_FLAG_ESEARCH)
			info("%s: Could not open state file %s", plugin_type,
			     file);
		return data_size;
	}
	if (fstat(fd, &f_stat)) {
		if (slurm_get_debug_flags() & DEBUG_FLAG_ESEARCH)
			info("%s: Could not stat state file %s", plugin_type,
			     file);
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
		error("%s: Could not read entire jobcomp state file %s (%d of"
		      " %d)", plugin_type, file, data_size, fsize);
	}
	return data_size;
}

/* Load jobcomp data from save state file */
static int _load_pending_jobs(void)
{
	int i, rc = SLURM_SUCCESS;
	char *saved_data = NULL, *state_file = NULL, *job_data = NULL;
	uint32_t data_size, job_cnt = 0, tmp32 = 0;
	Buf buffer;
	struct job_node *jnode;

	state_file = slurm_get_state_save_location();
	if (state_file == NULL) {
		error("%s: Could not retrieve StateSaveLocation from conf",
		      plugin_type);
		return SLURM_ERROR;
	}

	if (state_file[strlen(state_file) - 1] != '/')
		xstrcat(state_file, "/");
	xstrcat(state_file, save_state_file);

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
		if (slurm_get_debug_flags() & DEBUG_FLAG_ESEARCH)
			info("%s: Loaded %u jobs from state file", plugin_type,
			     job_cnt);
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
	int rc = SLURM_SUCCESS;

	if (log_url == NULL) {
		error("%s: JobCompLoc parameter not configured", plugin_type);
		return SLURM_ERROR;
	}

	if (curl_global_init(CURL_GLOBAL_ALL) != 0) {
		error("%s: curl_global_init: %m", plugin_type);
		rc = SLURM_ERROR;
	} else if ((curl_handle = curl_easy_init()) == NULL) {
		error("%s: curl_easy_init: %m", plugin_type);
		rc = SLURM_ERROR;
	}

	if (curl_handle) {
		char *url = xstrdup(log_url);
		xstrcat(url, index_type);

		chunk.message = xmalloc(1);
		chunk.size = 0;

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
			if (slurm_get_debug_flags() & DEBUG_FLAG_ESEARCH)
				info("%s: Could not connect to: %s , reason: %s"
				     , plugin_type, url,
				     curl_easy_strerror(res));
			rc = SLURM_ERROR;
		} else {
			char *token, *response;
			response = xstrdup(chunk.message);
			token = strtok(chunk.message, " ");
			if (token == NULL) {
				error("%s: Could not receive the HTTP response"
				      " status code from %s", plugin_type, 
					url);
				rc = SLURM_ERROR;
			} else {
				token = strtok(NULL, " ");
				if ((xstrcmp(token, "100") == 0)) {
					(void)  strtok(NULL, " ");
					token = strtok(NULL, " ");
				}
				if ((xstrcmp(token, "200") != 0)
				    && (xstrcmp(token, "201") != 0)) {
					if (slurm_get_debug_flags() &
					    DEBUG_FLAG_ESEARCH) {
						info("%s: HTTP status code %s "
						     "received from %s",
						     plugin_type, token, url);
						info("%s: HTTP response:\n%s",
						     plugin_type, response);
					}
					rc = SLURM_ERROR;
				} else {
					token = strtok((char *)jobcomp, ",");
					(void)  strtok(token, ":");
					token = strtok(NULL, ":");
					if (slurm_get_debug_flags() &
					    DEBUG_FLAG_ESEARCH) {
						info("%s: Job with jobid %s "
 						     "indexed into "
						     "elasticsearch",
						     plugin_type, token);
					}
				}
				xfree(response);
			}
		}
		xfree(chunk.message);
		curl_easy_cleanup(curl_handle);
		xfree(url);
	}
	curl_global_cleanup();

	return rc;
}

/* Escape characters according to RFC7159 */
static char *_json_escape(const char *str)
{
	char *ret = NULL;
	int i, o, len;

	len = strlen(str) * 2 + 128;
	ret = xmalloc(len);
	for (i = 0, o = 0; str[i]; ++i) {
		if (o >= MAX_STR_LEN) {
			break;
		} else if ((o + 8) >= len) {
			len *= 2;
			ret = xrealloc(ret, len);
		}
		switch (str[i]) {
		case '\\':
			ret[o++] = '\\';
			ret[o++] = '\\';
			break;
		case '"':
			ret[o++] = '\\';
			ret[o++] = '\"';
			break;
		case '\n':
			ret[o++] = '\\';
			ret[o++] = 'n';
			break;
		case '\b':
			ret[o++] = '\\';
			ret[o++] = 'b';
			break;
		case '\f':
			ret[o++] = '\\';
			ret[o++] = 'f';
			break;
		case '\r':
			ret[o++] = '\\';
			ret[o++] = 'r';
			break;
		case '\t':
			ret[o++] = '\\';
			ret[o++] = 't';
			break;
		case '<':
			ret[o++] = '\\';
			ret[o++] = 'u';
			ret[o++] = '0';
			ret[o++] = '0';
			ret[o++] = '3';
			ret[o++] = 'C';
			break;
		case '/':
			ret[o++] = '\\';
			ret[o++] = '/';
			break;
		default:
			ret[o++] = str[i];
		}
	}
	return ret;
}

/* Saves the state of all jobcomp data for further indexing retries */
static int _save_state(void)
{
	int fd, rc = SLURM_SUCCESS;
	char *state_file, *new_file, *old_file;
	ListIterator iter;
	static int high_buffer_size = (1024 * 1024);
	Buf buffer = init_buf(high_buffer_size);
	uint32_t job_cnt;
	struct job_node *jnode;

	job_cnt = list_count(jobslist);
	pack32(job_cnt, buffer);
	iter = list_iterator_create(jobslist);
	while ((jnode = (struct job_node *)list_next(iter))) {
		packstr(jnode->serialized_job, buffer);
	}
	list_iterator_destroy(iter);

	state_file = slurm_get_state_save_location();
	if (state_file == NULL || state_file[0] == '\0') {
		error("%s: Could not retrieve StateSaveLocation from conf",
		      plugin_type);
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
		error("%s: Can't save jobcomp state, open file %s error %m",
		      plugin_type, new_file);
		rc = SLURM_ERROR;
	} else {
		int pos = 0, nwrite, amount, rc2;
		char *data;
		fd_set_close_on_exec(fd);
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

	slurm_gmtime_r(time, &time_tm);
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

extern int slurm_jobcomp_log_record(struct job_record *job_ptr)
{
	int nwritten, B_SIZE = 1024;
	char usr_str[32], grp_str[32], start_str[32], end_str[32];
	char submit_str[32], *cluster = NULL, *qos, *state_string;
	time_t elapsed_time, submit_time, eligible_time;
	enum job_states job_state;
	uint32_t time_limit;
	uint16_t ntasks_per_node;
	int i;
	char *buffer, tmp_str[256], *script_str, *script;
	struct job_node *jnode;

	if (list_count(jobslist) > MAX_JOBS) {
		error("%s: Limit of %d enqueued jobs in memory waiting to be "
		      "indexed reached. Job %lu discarded", plugin_type,
		      MAX_JOBS, (unsigned long)job_ptr->job_id);
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

	buffer = xmalloc(B_SIZE);

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
		buffer = xrealloc(buffer, B_SIZE);

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
			error("%s: Job completion data truncated and lost",
			      plugin_type);
			return SLURM_ERROR;
		}
	}

	snprintf(tmp_str, sizeof(tmp_str), ",\"cpu_hours\":%.6f",
		 ((float) elapsed_time * (float) job_ptr->total_cpus) /
		  (float) 3600);
	xstrcat(buffer, tmp_str);

	if (job_ptr->array_task_id != NO_VAL) {
		xstrfmtcat(buffer, ",\"array_job_id\":%lu",
			   (unsigned long) job_ptr->array_job_id);
		xstrfmtcat(buffer, ",\"array_task_id\":%lu",
			   (unsigned long) job_ptr->array_task_id);
	}

	if (job_ptr->details && (job_ptr->details->submit_time != NO_VAL)) {
		submit_time = job_ptr->details->submit_time;
		_make_time_str(&submit_time, submit_str, sizeof(submit_str));
		xstrfmtcat(buffer, ",\"@submit\":\"%s\"", submit_str);
	}

	if (job_ptr->details && (job_ptr->details->begin_time != NO_VAL)) {
		eligible_time = job_ptr->start_time -
				job_ptr->details->begin_time;
		xstrfmtcat(buffer, ",\"eligible_time\":%lu", eligible_time);
	}

	if (job_ptr->details
	    && (job_ptr->details->work_dir && job_ptr->details->work_dir[0])) {
		xstrfmtcat(buffer, ",\"work_dir\":\"%s\"",
			   job_ptr->details->work_dir);
	}

	if (job_ptr->details
	    && (job_ptr->details->std_err && job_ptr->details->std_err[0])) {
		xstrfmtcat(buffer, ",\"std_err\":\"%s\"",
			   job_ptr->details->std_err);
	}

	if (job_ptr->details
	    && (job_ptr->details->std_in && job_ptr->details->std_in[0])) {
		xstrfmtcat(buffer, ",\"std_in\":\"%s\"",
			   job_ptr->details->std_in);
	}

	if (job_ptr->details
	    && (job_ptr->details->std_out && job_ptr->details->std_out[0])) {
		xstrfmtcat(buffer, ",\"std_out\":\"%s\"",
			   job_ptr->details->std_out);
	}

	if (job_ptr->assoc_ptr != NULL) {
		cluster = ((slurmdb_assoc_rec_t *) job_ptr->assoc_ptr)->cluster;
		xstrfmtcat(buffer, ",\"cluster\":\"%s\"", cluster);
	}

	if (job_ptr->qos_ptr != NULL) {
		slurmdb_qos_rec_t *assoc =
			(slurmdb_qos_rec_t *) job_ptr->qos_ptr;
		qos = assoc->name;
		xstrfmtcat(buffer, ",\"qos\":\"%s\"", qos);
	}

	if (job_ptr->details && (job_ptr->details->num_tasks != NO_VAL)) {
		xstrfmtcat(buffer, ",\"ntasks\":%hu",
			   job_ptr->details->num_tasks);
	}

	if (job_ptr->details && (job_ptr->details->ntasks_per_node != NO_VAL)) {
		ntasks_per_node = job_ptr->details->ntasks_per_node;
		xstrfmtcat(buffer, ",\"ntasks_per_node\":%hu", ntasks_per_node);
	}

	if (job_ptr->details && (job_ptr->details->cpus_per_task != NO_VAL)) {
		xstrfmtcat(buffer, ",\"cpus_per_task\":%hu",
			   job_ptr->details->cpus_per_task);
	}

	if (job_ptr->details
	    && (job_ptr->details->orig_dependency
		&& job_ptr->details->orig_dependency[0])) {
		xstrfmtcat(buffer, ",\"orig_dependency\":\"%s\"",
			   job_ptr->details->orig_dependency);
	}

	if (job_ptr->details
	    && (job_ptr->details->exc_nodes
		&& job_ptr->details->exc_nodes[0])) {
		xstrfmtcat(buffer, ",\"excluded_nodes\":\"%s\"",
			   job_ptr->details->exc_nodes);
	}

	if (time_limit != INFINITE) {
		xstrfmtcat(buffer, ",\"time_limit\":%lu",
			   (unsigned long) time_limit * 60);
	}

	if (job_ptr->resv_name && job_ptr->resv_name[0]) {
		xstrfmtcat(buffer, ",\"reservation_name\":\"%s\"",
			   job_ptr->resv_name);
	}

	if (job_ptr->gres_req && job_ptr->gres_req[0]) {
		xstrfmtcat(buffer, ",\"gres_req\":\"%s\"", job_ptr->gres_req);
	}

	if (job_ptr->gres_alloc && job_ptr->gres_alloc[0]) {
		xstrfmtcat(buffer, ",\"gres_alloc\":\"%s\"",
			   job_ptr->gres_alloc);
	}

	if (job_ptr->account && job_ptr->account[0]) {
		xstrfmtcat(buffer, ",\"account\":\"%s\"", job_ptr->account);
	}

	script = get_job_script(job_ptr);
	if (script && script[0]) {
		script_str = _json_escape(script);
		xstrfmtcat(buffer, ",\"script\":\"%s\"", script_str);
		xfree(script_str);
	}
	xfree(script);

	if (job_ptr->assoc_ptr) {
		assoc_mgr_lock_t locks = { READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK,
					   NO_LOCK, NO_LOCK, NO_LOCK };
		slurmdb_assoc_rec_t *assoc_ptr = job_ptr->assoc_ptr;
		char *parent_accounts = NULL;
		char **acc_aux = NULL;
		int nparents = 0;

		assoc_mgr_lock(&locks);

		/* Start at the first parent and go up.  When studying
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

		xstrfmtcat(buffer, ",\"parent_accounts\":\"%s\"",
			   parent_accounts);

		xfree(parent_accounts);

		assoc_mgr_unlock(&locks);
	}

	xstrcat(buffer, "}");
	jnode = xmalloc(sizeof(struct job_node));
	jnode->serialized_job = xstrdup(buffer);
	list_enqueue(jobslist, jnode);

	return SLURM_SUCCESS;
}



extern void *_process_jobs(void *x)
{
	ListIterator iter;
	struct job_node *jnode = NULL;
	time_t now;

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
		if ((success_cnt || fail_cnt) &&
		    (slurm_get_debug_flags() & DEBUG_FLAG_ESEARCH)) {
			info("%s: index success:%d fail:%d wait_retry:%d",
			     plugin_type, success_cnt, fail_cnt,
			     wait_retry_cnt);
		}
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
	pthread_attr_t thread_attr;
	int rc = SLURM_SUCCESS;

	jobslist = list_create(_jobslist_del);
	slurm_attr_init(&thread_attr);
	if (pthread_create(&job_handler_thread, &thread_attr,
			   _process_jobs, NULL))
		fatal("pthread_create error %m");
	slurm_mutex_lock(&pend_jobs_lock);
	(void) _load_pending_jobs();
	slurm_mutex_unlock(&pend_jobs_lock);

	return rc;
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
 * The remainder of this file implements the standard SLURM job completion
 * logging API.
 */
extern int slurm_jobcomp_set_location(char *location)
{
	int rc = SLURM_SUCCESS;
	CURL *curl_handle;
	CURLcode res;

	if (location == NULL) {
		error("%s: JobCompLoc parameter not configured", plugin_type);
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
			error("%s: Could not connect to: %s", plugin_type,
			      log_url);
			rc = SLURM_ERROR;
		}
		curl_easy_cleanup(curl_handle);
	}
	curl_global_cleanup();

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
	debug("%s function is not implemented", __func__);
	return NULL;
}

/*
 * expire old info from the database
 */
extern int slurm_jobcomp_archive(slurmdb_archive_cond_t * arch_cond)
{
	debug("%s function is not implemented", __func__);
	return SLURM_SUCCESS;
}
