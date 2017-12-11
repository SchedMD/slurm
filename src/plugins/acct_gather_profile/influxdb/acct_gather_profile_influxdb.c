/*****************************************************************************\
 *  acct_gather_profile_influxdb.c - slurm accounting plugin for
 *                               influxdb profiling.
 *****************************************************************************
 *  Author: Carlos Fenoy Garcia
 *  Copyright (C) 2016 F. Hoffmann - La Roche
 *
 *  Based on the HDF5 profiling plugin and Elasticsearch job completion plugin
 *  
 *  Portions Copyright (C) 2013 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *
 *  Portions Copyright (C) 2013 SchedMD LLC.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
 \*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <math.h>
#include <curl/curl.h>

#include "src/common/slurm_xlator.h"
#include "src/common/fd.h"
#include "src/common/slurm_acct_gather_profile.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_time.h"
#include "src/common/macros.h"
#include "src/slurmd/common/proctrack.h"

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
 * the plugin (e.g., "jobacct" for SLURM job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load job completion logging plugins if the plugin_type string has a
 * prefix of "jobacct/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "AcctGatherProfile influxdb plugin";
const char plugin_type[] = "acct_gather_profile/influxdb";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

typedef struct {
	char *host;
	char *database;
	uint32_t def;
} slurm_influxdb_conf_t;

typedef struct {
	char ** names;
	uint32_t *types;
	size_t size;
	char * name;
} table_t;

static slurm_influxdb_conf_t influxdb_conf;
static uint64_t debug_flags = 0;
static uint32_t g_profile_running = ACCT_GATHER_PROFILE_NOT_SET;
static stepd_step_rec_t *g_job = NULL;

static char *datastr;
static int datastrlen;

static table_t *tables = NULL;
static size_t   tables_max_len = 0;
static size_t   tables_cur_len = 0;

static void _reset_slurm_profile_conf(void)
{
	xfree(influxdb_conf.host);
	influxdb_conf.def = ACCT_GATHER_PROFILE_ALL;
}

static uint32_t _determine_profile(void)
{
	uint32_t profile;

	xassert(g_job);

	if (g_profile_running != ACCT_GATHER_PROFILE_NOT_SET)
		profile = g_profile_running;
	else if (g_job->profile >= ACCT_GATHER_PROFILE_NONE)
		profile = g_job->profile;
	else
		profile = influxdb_conf.def;

	return profile;
}

static bool _run_in_daemon(void)
{
	static bool set = false;
	static bool run = false;

	if (!set) {
		set = 1;
		run = run_in_daemon("slurmstepd");
	}

	return run;
}

/* Type for handling HTTP responses */
struct http_response {
	char *message;
	size_t size;
};

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
static int _send_data(const char *data)
{
	if(data!=NULL && datastrlen+strlen(data) <= BUF_SIZE){
		xstrcat(datastr,data);
		datastrlen += strlen(data);
		return SLURM_SUCCESS;
	}

	CURL *curl_handle = NULL;
	CURLcode res;
	struct http_response chunk;
	int rc = SLURM_SUCCESS;
	static int error_cnt = 0;

	if (curl_global_init(CURL_GLOBAL_ALL) != 0) {
		debug("curl_global_init: %m");
		rc = SLURM_ERROR;
	} else if ((curl_handle = curl_easy_init()) == NULL) {
		debug("curl_easy_init: %m");
		rc = SLURM_ERROR;
	}

	if (curl_handle) {
		char *url = xstrdup(influxdb_conf.host);
		xstrfmtcat(url, "/write?db=%s&rp=default&precision=s",
				influxdb_conf.database);

		chunk.message = xmalloc(1);
		chunk.size = 0;

		curl_easy_setopt(curl_handle, CURLOPT_URL, url);
		curl_easy_setopt(curl_handle, CURLOPT_POST, 1);
		curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, datastr);
		curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE,
				strlen(datastr));
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
					(void)  strtok(NULL, " ");
					token = strtok(NULL, " ");
				}
				else if ((xstrcmp(token, "400") == 0)) {
					debug("400: Bad Request");
					rc = SLURM_ERROR;
				}
				else if ((xstrcmp(token, "404") == 0)) {
					debug("404: Unacceptable request");
					rc = SLURM_ERROR;
				}
				else if ((xstrcmp(token, "500") == 0)) {
					debug("500: Internal Server Error");
					rc = SLURM_ERROR;
				}
				else if (xstrcmp(token, "204") != 0) {
					debug("HTTP status code %s received "
							"from %s", token, url);
					debug3("HTTP Response:\n%s", response);
					rc = SLURM_ERROR;
				} else {
					debug("Data written");
				}
				xfree(response);
			}
		}
		xfree(chunk.message);
		curl_easy_cleanup(curl_handle);
		xfree(url);
	}
	curl_global_cleanup();

	if (rc == SLURM_ERROR) {
		if (((error_cnt++) % 100) == 0) {
			/* Periodically log errors */
			info("%s: Unable to push data, some data may be lost."
				"Error count: %d ",plugin_type, error_cnt);
		}
	}

	if (data!=NULL) {
		strcpy(datastr,data);
		datastrlen = strlen(data);
	}else{
		strcpy(datastr,"");
		datastrlen = 0;
	}
	return rc;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	if (!_run_in_daemon())
		return SLURM_SUCCESS;

	debug_flags = slurm_get_debug_flags();

	datastr = xmalloc(BUF_SIZE);
	return SLURM_SUCCESS;
}

extern int fini(void)
{
	xfree(tables);
	xfree(datastr);
	xfree(influxdb_conf.host);
	xfree(influxdb_conf.database);
	return SLURM_SUCCESS;
}

extern void acct_gather_profile_p_conf_options(s_p_options_t **full_options,
		int *full_options_cnt)
{
	s_p_options_t options[] = {
		{"ProfileInfluxDBHost", S_P_STRING},
		{"ProfileInfluxDBDatabase", S_P_STRING},
		{"ProfileInfluxDBDefault", S_P_STRING},
		{NULL} };

	transfer_s_p_options(full_options, options, full_options_cnt);
	return;
}

extern void acct_gather_profile_p_conf_set(s_p_hashtbl_t *tbl)
{
	char *tmp = NULL;
	_reset_slurm_profile_conf();
	if (tbl) {
		s_p_get_string(&influxdb_conf.host, "ProfileInfluxDBHost", tbl);
		if (s_p_get_string(&tmp, "ProfileInfluxDBDefault", tbl)) {
			influxdb_conf.def = 
				acct_gather_profile_from_string(tmp);
			if (influxdb_conf.def == ACCT_GATHER_PROFILE_NOT_SET) {
				fatal("ProfileInfluxDBDefault can not be "
					"set to %s, please specify a valid "
					"option", tmp);
			}
			xfree(tmp);
		}
		s_p_get_string(&influxdb_conf.database, 
				"ProfileInfluxDBDatabase", tbl);
	}

	if (!influxdb_conf.host)
		fatal("No ProfileInfluxDBHost in your acct_gather.conf file.  "
				"This is required to use the %s plugin", 
				plugin_type);
	if (!influxdb_conf.database)
		fatal("No ProfileInfluxDBDatabase in your acct_gather.conf "
				"file.  This is required to use the %s plugin",
			       	plugin_type);

	debug("%s loaded", plugin_name);
}

extern void acct_gather_profile_p_get(enum acct_gather_profile_info info_type,
		void *data)
{
	uint32_t *uint32 = (uint32_t *) data;
	char **tmp_char = (char **) data;

	switch (info_type) {
		case ACCT_GATHER_PROFILE_DIR:
			*tmp_char = xstrdup(influxdb_conf.host);
			break;
		case ACCT_GATHER_PROFILE_DEFAULT:
			*uint32 = influxdb_conf.def;
			break;
		case ACCT_GATHER_PROFILE_RUNNING:
			*uint32 = g_profile_running;
			break;
		default:
			debug2("acct_gather_profile_p_get info_type %d invalid",
					info_type);
	}
}

extern int acct_gather_profile_p_node_step_start(stepd_step_rec_t* job)
{
	int rc = SLURM_SUCCESS;

	char *profile_str;

	xassert(_run_in_daemon());

	g_job = job;
	profile_str = acct_gather_profile_to_string(g_job->profile);
	info("PROFILE: option --profile=%s", profile_str);
	g_profile_running = _determine_profile();
	return rc;
}

extern int acct_gather_profile_p_child_forked(void)
{
	return SLURM_SUCCESS;
}

extern int acct_gather_profile_p_node_step_end(void)
{
	int rc = SLURM_SUCCESS;

	xassert(_run_in_daemon());


	return rc;
}

extern int acct_gather_profile_p_task_start(uint32_t taskid)
{
	int rc = SLURM_SUCCESS;

	info("PROFILE: task_start with %d prof",g_profile_running);
	xassert(_run_in_daemon());
	xassert(g_job);

	xassert(g_profile_running != ACCT_GATHER_PROFILE_NOT_SET);

	if (g_profile_running <= ACCT_GATHER_PROFILE_NONE)
		return rc;

	if (debug_flags & DEBUG_FLAG_PROFILE)
		info("PROFILE: task_start");

	return rc;
}

extern int acct_gather_profile_p_task_end(pid_t taskpid)
{
	if (debug_flags & DEBUG_FLAG_PROFILE)
		info("PROFILE: task_end");
	DEF_TIMERS;
	START_TIMER;
	_send_data(NULL);
	END_TIMER;
	debug("PROFILE: task_end took %s",TIME_STR);
	return SLURM_SUCCESS;
}

extern int acct_gather_profile_p_create_group(const char* name)
{
	return 0;
}

extern int acct_gather_profile_p_create_dataset(
		const char* name, int parent, 
		acct_gather_profile_dataset_t *dataset)
{
	table_t * table;
	acct_gather_profile_dataset_t *dataset_loc = dataset;

	if (g_profile_running <= ACCT_GATHER_PROFILE_NONE)
		return SLURM_ERROR;

	debug("acct_gather_profile_p_create_dataset %s", name);

	// compute the size of the type needed to create the table 
	if (tables_cur_len == tables_max_len) {
		if (tables_max_len == 0)
			++tables_max_len;
		tables_max_len *= 2;
		tables = xrealloc(tables, tables_max_len * sizeof(table_t));
	}
	table = &(tables[tables_cur_len]);
	table->name = xstrdup(name);
	table->size = 0;
	while (dataset_loc && (dataset_loc->type != PROFILE_FIELD_NOT_SET)) {
		table->names = xrealloc(table->names, 
				(table->size+1) * sizeof(char *));
		table->types = xrealloc(table->types, 
				(table->size+1) * sizeof(char *));
		(table->names)[table->size] = xstrdup(dataset_loc->name);
		switch (dataset_loc->type) {
			case PROFILE_FIELD_UINT64:
				table->types[table->size] = 
					PROFILE_FIELD_UINT64;
				break;
			case PROFILE_FIELD_DOUBLE:
				table->types[table->size] = 
					PROFILE_FIELD_DOUBLE;
				break;
			case PROFILE_FIELD_NOT_SET:
				break;
		}
		table->size++;
		dataset_loc++;
	}
	++tables_cur_len;
	return tables_cur_len - 1;
}

union data_t{
	uint64_t u;
	double	 d;
};

extern int acct_gather_profile_p_add_sample_data(int table_id, void *data,
		time_t sample_time)
{



	table_t *table = &tables[table_id];

	int i = 0;
	char *str = NULL;
	for(;i<table->size;i++){
		switch (table->types[i]) {
			case PROFILE_FIELD_UINT64:
				xstrfmtcat(str,"%s,job=%d,step=%d,task=%s,"
						"host=%s value=%"PRIu64" "
						"%"PRIu64"\n",
						table->names[i],g_job->jobid,
						g_job->stepid,table->name,
						g_job->node_name,
						((union data_t*)data)[i].u,
						sample_time);
				break;
			case PROFILE_FIELD_DOUBLE:
				xstrfmtcat(str,"%s,job=%d,step=%d,task=%s,"
						"host=%s value=%.2f %"PRIu64""
						"\n",table->names[i], 
						g_job->jobid,g_job->stepid,
						table->name,g_job->node_name,
						((union data_t*)data)[i].d,
						sample_time);
				break;
			case PROFILE_FIELD_NOT_SET:
				break;
		}
	}

	DEF_TIMERS;
	START_TIMER;
	_send_data(str);
	END_TIMER;
	xfree(str);
	debug("PROFILE: took %s",TIME_STR);
	debug("PROFILE: data sent");

	return SLURM_SUCCESS;
}

extern void acct_gather_profile_p_conf_values(List *data)
{
	config_key_pair_t *key_pair;

	xassert(*data);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ProfileInfluxDBHost");
	key_pair->value = xstrdup(influxdb_conf.host);
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ProfileInfluxDBDatabase");
	key_pair->value = xstrdup(influxdb_conf.database);
	list_append(*data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ProfileInfluxDBDefault");
	key_pair->value = 
		xstrdup(acct_gather_profile_to_string(influxdb_conf.def));
	list_append(*data, key_pair);
	return;

}

extern bool acct_gather_profile_p_is_active(uint32_t type)
{
	if (g_profile_running <= ACCT_GATHER_PROFILE_NONE)
		return false;
	return (type == ACCT_GATHER_PROFILE_NOT_SET)
		|| (g_profile_running & type);
}
