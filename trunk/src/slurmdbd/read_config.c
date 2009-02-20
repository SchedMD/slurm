/*****************************************************************************\
 *  read_config.c - functions for reading slurmdbd.conf
 *****************************************************************************
 *  Copyright (C) 2003-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <slurm/slurm_errno.h>

#include "src/common/log.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/parse_config.h"
#include "src/common/parse_time.h"
#include "src/common/read_config.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmdbd/read_config.h"

/* Global variables */
pthread_mutex_t conf_mutex = PTHREAD_MUTEX_INITIALIZER;
//slurm_dbd_conf_t *slurmdbd_conf = NULL;

/* Local functions */
static void _clear_slurmdbd_conf(void);
static char * _get_conf_path(void);

static time_t boot_time;

/*
 * free_slurmdbd_conf - free storage associated with the global variable 
 *	slurmdbd_conf
 */
extern void free_slurmdbd_conf(void)
{
	slurm_mutex_lock(&conf_mutex);
	_clear_slurmdbd_conf();
	xfree(slurmdbd_conf);
	slurm_mutex_unlock(&conf_mutex);
}

static void _clear_slurmdbd_conf(void)
{
	if (slurmdbd_conf) {
		xfree(slurmdbd_conf->archive_dir);
		slurmdbd_conf->archive_jobs = 0;
		xfree(slurmdbd_conf->archive_script);
		slurmdbd_conf->archive_steps = 0;
		xfree(slurmdbd_conf->auth_info);
		xfree(slurmdbd_conf->auth_type);
		xfree(slurmdbd_conf->dbd_addr);
		xfree(slurmdbd_conf->dbd_host);
		slurmdbd_conf->dbd_port = 0;
		slurmdbd_conf->debug_level = 0;
		xfree(slurmdbd_conf->default_qos);
		slurmdbd_conf->job_purge = 0;
		xfree(slurmdbd_conf->log_file);
		xfree(slurmdbd_conf->pid_file);
		xfree(slurmdbd_conf->plugindir);
		slurmdbd_conf->private_data = 0;
		slurmdbd_conf->slurm_user_id = NO_VAL;
		xfree(slurmdbd_conf->slurm_user_name);
		slurmdbd_conf->step_purge = 0;
		xfree(slurmdbd_conf->storage_host);
		xfree(slurmdbd_conf->storage_loc);
		xfree(slurmdbd_conf->storage_pass);
		slurmdbd_conf->storage_port = 0;
		xfree(slurmdbd_conf->storage_type);
		xfree(slurmdbd_conf->storage_user);
		slurmdbd_conf->track_wckey = 0;
	}
}

/*
 * read_slurmdbd_conf - load the SlurmDBD configuration from the slurmdbd.conf  
 *	file. Store result into global variable slurmdbd_conf. 
 *	This function can be called more than once.
 * RET SLURM_SUCCESS if no error, otherwise an error code
 */
extern int read_slurmdbd_conf(void)
{
	s_p_options_t options[] = {
		{"ArchiveDir", S_P_STRING},
		{"ArchiveJobs", S_P_BOOLEAN},
		{"ArchiveScript", S_P_STRING},
		{"ArchiveSteps", S_P_BOOLEAN},
		{"AuthInfo", S_P_STRING},
		{"AuthType", S_P_STRING},
		{"DbdAddr", S_P_STRING},
		{"DbdHost", S_P_STRING},
		{"DbdPort", S_P_UINT16},
		{"DebugLevel", S_P_UINT16},
		{"DefaultQOS", S_P_STRING},
		{"JobPurge", S_P_UINT16},
		{"LogFile", S_P_STRING},
		{"MessageTimeout", S_P_UINT16},
		{"PidFile", S_P_STRING},
		{"PluginDir", S_P_STRING},
		{"PrivateData", S_P_STRING},
		{"SlurmUser", S_P_STRING},
		{"StepPurge", S_P_UINT16},
		{"StorageHost", S_P_STRING},
		{"StorageLoc", S_P_STRING},
		{"StoragePass", S_P_STRING},
		{"StoragePort", S_P_UINT16},
		{"StorageType", S_P_STRING},
		{"StorageUser", S_P_STRING},
		{"TrackWCKey", S_P_BOOLEAN},
		{NULL} };
	s_p_hashtbl_t *tbl = NULL;
	char *conf_path = NULL;
	char *temp_str = NULL;
	struct stat buf;

	/* Set initial values */
	slurm_mutex_lock(&conf_mutex);
	if (slurmdbd_conf == NULL) {
		slurmdbd_conf = xmalloc(sizeof(slurm_dbd_conf_t));
		boot_time = time(NULL);
	}
	slurmdbd_conf->debug_level = LOG_LEVEL_INFO;
	_clear_slurmdbd_conf();

	/* Get the slurmdbd.conf path and validate the file */
	conf_path = _get_conf_path();
	if ((conf_path == NULL) || (stat(conf_path, &buf) == -1)) {
		info("No slurmdbd.conf file (%s)", conf_path);
	} else {
		debug("Reading slurmdbd.conf file %s", conf_path);

		tbl = s_p_hashtbl_create(options);
		if (s_p_parse_file(tbl, conf_path) == SLURM_ERROR) {
			fatal("Could not open/read/parse slurmdbd.conf file %s",
		 	     conf_path);
		}

		if(!s_p_get_string(&slurmdbd_conf->archive_dir, "ArchiveDir",
				   tbl))
			slurmdbd_conf->archive_dir =
				xstrdup(DEFAULT_SLURMDBD_ARCHIVE_DIR);
		s_p_get_boolean((bool *)&slurmdbd_conf->archive_jobs,
				"ArchiveJobs", tbl);
		s_p_get_string(&slurmdbd_conf->archive_script, "ArchiveScript",
			       tbl);
		s_p_get_boolean((bool *)&slurmdbd_conf->archive_steps,
				"ArchiveSteps", tbl);
		s_p_get_string(&slurmdbd_conf->auth_info, "AuthInfo", tbl);
		s_p_get_string(&slurmdbd_conf->auth_type, "AuthType", tbl);
		s_p_get_string(&slurmdbd_conf->dbd_host, "DbdHost", tbl);
		s_p_get_string(&slurmdbd_conf->dbd_addr, "DbdAddr", tbl);
		s_p_get_uint16(&slurmdbd_conf->dbd_port, "DbdPort", tbl);
		s_p_get_uint16(&slurmdbd_conf->debug_level, "DebugLevel", tbl);
		s_p_get_string(&slurmdbd_conf->default_qos, "DefaultQOS", tbl);
		s_p_get_uint16(&slurmdbd_conf->job_purge, "JobPurge", tbl);
		s_p_get_string(&slurmdbd_conf->log_file, "LogFile", tbl);
		if (!s_p_get_uint16(&slurmdbd_conf->msg_timeout,
				    "MessageTimeout", tbl))
			slurmdbd_conf->msg_timeout = DEFAULT_MSG_TIMEOUT;
		else if (slurmdbd_conf->msg_timeout > 100) {
			info("WARNING: MessageTimeout is too high for "
			     "effective fault-tolerance");
		}
		s_p_get_string(&slurmdbd_conf->pid_file, "PidFile", tbl);
		s_p_get_string(&slurmdbd_conf->plugindir, "PluginDir", tbl);
		if (s_p_get_string(&temp_str, "PrivateData", tbl)) {
			if (strstr(temp_str, "job"))
				slurmdbd_conf->private_data 
					|= PRIVATE_DATA_JOBS;
			if (strstr(temp_str, "node"))
				slurmdbd_conf->private_data 
					|= PRIVATE_DATA_NODES;
			if (strstr(temp_str, "partition"))
				slurmdbd_conf->private_data 
					|= PRIVATE_DATA_PARTITIONS;
			if (strstr(temp_str, "usage"))
				slurmdbd_conf->private_data
					|= PRIVATE_DATA_USAGE;
			if (strstr(temp_str, "users"))
				slurmdbd_conf->private_data 
					|= PRIVATE_DATA_USERS;
			if (strstr(temp_str, "accounts"))
				slurmdbd_conf->private_data 
					|= PRIVATE_DATA_ACCOUNTS;
			if (strstr(temp_str, "all"))
				slurmdbd_conf->private_data = 0xffff;
			xfree(temp_str);
		}

		s_p_get_string(&slurmdbd_conf->slurm_user_name, "SlurmUser",
			       tbl);
		s_p_get_uint16(&slurmdbd_conf->step_purge, "StepPurge", tbl);

		s_p_get_string(&slurmdbd_conf->storage_host,
				"StorageHost", tbl);
		s_p_get_string(&slurmdbd_conf->storage_loc,
				"StorageLoc", tbl);
		s_p_get_string(&slurmdbd_conf->storage_pass,
				"StoragePass", tbl);
		s_p_get_uint16(&slurmdbd_conf->storage_port,
			       "StoragePort", tbl);
		s_p_get_string(&slurmdbd_conf->storage_type,
			       "StorageType", tbl);
		s_p_get_string(&slurmdbd_conf->storage_user,
				"StorageUser", tbl);

		if(!s_p_get_boolean((bool *)&slurmdbd_conf->track_wckey, 
				    "TrackWCKey", tbl))
			slurmdbd_conf->track_wckey = false;

		s_p_hashtbl_destroy(tbl);
	}

	xfree(conf_path);
	if (slurmdbd_conf->auth_type == NULL)
		slurmdbd_conf->auth_type = xstrdup(DEFAULT_SLURMDBD_AUTHTYPE);
	if (slurmdbd_conf->dbd_host == NULL) {
		error("slurmdbd.conf lacks DbdHost parameter, "
		      "using 'localhost'");
		slurmdbd_conf->dbd_host = xstrdup("localhost");
	}
	if (slurmdbd_conf->dbd_addr == NULL)
		slurmdbd_conf->dbd_addr = xstrdup(slurmdbd_conf->dbd_host);
	if (slurmdbd_conf->pid_file == NULL)
		slurmdbd_conf->pid_file = xstrdup(DEFAULT_SLURMDBD_PIDFILE);
	if (slurmdbd_conf->dbd_port == 0)
		slurmdbd_conf->dbd_port = SLURMDBD_PORT;
	if(slurmdbd_conf->plugindir == NULL)
		slurmdbd_conf->plugindir = xstrdup(default_plugin_path);
	if (slurmdbd_conf->slurm_user_name) {
		uid_t pw_uid = uid_from_string(slurmdbd_conf->slurm_user_name);
		if (pw_uid == (uid_t) -1) {
			fatal("Invalid user for SlurmUser %s, ignored",
			      slurmdbd_conf->slurm_user_name);
		} else
			slurmdbd_conf->slurm_user_id = pw_uid;
	} else {
		slurmdbd_conf->slurm_user_name = xstrdup("root");
		slurmdbd_conf->slurm_user_id = 0;
	}
	if (slurmdbd_conf->storage_type == NULL)
		fatal("StorageType must be specified");
	
	if (slurmdbd_conf->archive_dir) {
		if(stat(slurmdbd_conf->archive_dir, &buf) < 0) 
			fatal("Failed to stat the archive directory %s: %m",
			      slurmdbd_conf->archive_dir);
		if (!(buf.st_mode & S_IFDIR)) 
			fatal("rchive directory %s isn't a directory",
			      slurmdbd_conf->archive_script);
		
		if (access(slurmdbd_conf->archive_dir, W_OK) < 0) 
			fatal("rchive directory %s is not writable",
			      slurmdbd_conf->archive_dir);
	}

	if (slurmdbd_conf->archive_script) {
		if(stat(slurmdbd_conf->archive_script, &buf) < 0) 
			fatal("Failed to stat the archive script %s: %m",
			      slurmdbd_conf->archive_dir);

		if (!(buf.st_mode & S_IFREG)) 
			fatal("archive script %s isn't a regular file",
			      slurmdbd_conf->archive_script);
		
		if (access(slurmdbd_conf->archive_script, X_OK) < 0) 
			fatal("archive script %s is not executable",
			      slurmdbd_conf->archive_script);
	}
		
	slurm_mutex_unlock(&conf_mutex);
	return SLURM_SUCCESS;
}

/* Log the current configuration using verbose() */
extern void log_config(void)
{
	char tmp_str[128];

	debug2("ArchiveDir        = %s", slurmdbd_conf->archive_dir);
	debug2("ArchiveScript     = %s", slurmdbd_conf->archive_script);
	debug2("AuthInfo          = %s", slurmdbd_conf->auth_info);
	debug2("AuthType          = %s", slurmdbd_conf->auth_type);
	debug2("DbdAddr           = %s", slurmdbd_conf->dbd_addr);
	debug2("DbdHost           = %s", slurmdbd_conf->dbd_host);
	debug2("DbdPort           = %u", slurmdbd_conf->dbd_port);
	debug2("DebugLevel        = %u", slurmdbd_conf->debug_level);
	debug2("DefaultQOS        = %s", slurmdbd_conf->default_qos);

	if(slurmdbd_conf->job_purge)
		debug2("JobPurge          = %u months",
		       slurmdbd_conf->job_purge);
	else
		debug2("JobPurge          = NONE");
		
	debug2("LogFile           = %s", slurmdbd_conf->log_file);
	debug2("MessageTimeout    = %u", slurmdbd_conf->msg_timeout);
	debug2("PidFile           = %s", slurmdbd_conf->pid_file);
	debug2("PluginDir         = %s", slurmdbd_conf->plugindir);

	private_data_string(slurmdbd_conf->private_data,
			    tmp_str, sizeof(tmp_str));

	debug2("PrivateData       = %s", tmp_str);
	debug2("SlurmUser         = %s(%u)", 
		slurmdbd_conf->slurm_user_name, slurmdbd_conf->slurm_user_id);

	if(slurmdbd_conf->step_purge)
		debug2("StepPurge         = %u months", 
		       slurmdbd_conf->step_purge); 
	else
		debug2("StepPurge         = NONE"); 
		
	debug2("StorageHost       = %s", slurmdbd_conf->storage_host);
	debug2("StorageLoc        = %s", slurmdbd_conf->storage_loc);
	debug2("StoragePass       = %s", slurmdbd_conf->storage_pass);
	debug2("StoragePort       = %u", slurmdbd_conf->storage_port);
	debug2("StorageType       = %s", slurmdbd_conf->storage_type);
	debug2("StorageUser       = %s", slurmdbd_conf->storage_user);
	debug2("TrackWCKey        = %u", slurmdbd_conf->track_wckey);
}

/* Return the DbdPort value */
extern uint16_t get_dbd_port(void)
{
	uint16_t port;

	slurm_mutex_lock(&conf_mutex);
	port = slurmdbd_conf->dbd_port;
	slurm_mutex_unlock(&conf_mutex);
	return port;
}

extern void slurmdbd_conf_lock(void)
{
	slurm_mutex_lock(&conf_mutex);
}

extern void slurmdbd_conf_unlock(void)
{
	slurm_mutex_unlock(&conf_mutex);
}


/* Return the pathname of the slurmdbd.conf file.
 * xfree() the value returned */
static char * _get_conf_path(void)
{
	char *val = getenv("SLURM_CONF");
	char *path = NULL;
	int i;

	if (!val)
		val = default_slurm_config_file;

	/* Replace file name on end of path */
	i = strlen(val) + 15;
	path = xmalloc(i);
	strcpy(path, val);
	val = strrchr(path, (int)'/');
	if (val)	/* absolute path */
		val++;
	else		/* not absolute path */
		val = path;
	strcpy(val, "slurmdbd.conf");

	return path;
}

/* Dump the configuration in name,value pairs for output to 
 *	"sacctmgr show config", caller must call list_destroy() */
extern List dump_config(void)
{
	config_key_pair_t *key_pair;
	List my_list = list_create(destroy_acct_config_rec);

	if (!my_list)
		fatal("malloc failure on list_create");

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ArchiveDir");
	key_pair->value = xstrdup(slurmdbd_conf->archive_dir);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ArchiveScript");
	key_pair->value = xstrdup(slurmdbd_conf->archive_script);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AuthInfo");
	key_pair->value = xstrdup(slurmdbd_conf->auth_info);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AuthType");
	key_pair->value = xstrdup(slurmdbd_conf->auth_type);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("BOOT_TIME");
	key_pair->value = xmalloc(128);
	slurm_make_time_str ((time_t *)&boot_time, key_pair->value, 128);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("DbdAddr");
	key_pair->value = xstrdup(slurmdbd_conf->dbd_addr);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("DbdHost");
	key_pair->value = xstrdup(slurmdbd_conf->dbd_host);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("DbdPort");
	key_pair->value = xmalloc(32);
	snprintf(key_pair->value, 32, "%u", slurmdbd_conf->dbd_port);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("DebugLevel");
	key_pair->value = xmalloc(32);
	snprintf(key_pair->value, 32, "%u", slurmdbd_conf->debug_level);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("DefaultQOS");
	key_pair->value = xstrdup(slurmdbd_conf->default_qos);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("JobPurge");
	if(slurmdbd_conf->job_purge) {
		key_pair->value = xmalloc(32);
		snprintf(key_pair->value, 32, "%u months", 
			 slurmdbd_conf->job_purge);
	} else
		key_pair->value = xstrdup("NONE");
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("LogFile");
	key_pair->value = xstrdup(slurmdbd_conf->log_file);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MessageTimeout");
	key_pair->value = xmalloc(32);
	snprintf(key_pair->value, 32, "%u secs", slurmdbd_conf->msg_timeout);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("PidFile");
	key_pair->value = xstrdup(slurmdbd_conf->pid_file);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("PluginDir");
	key_pair->value = xstrdup(slurmdbd_conf->plugindir);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("PrivateData");
	key_pair->value = xmalloc(128);
	private_data_string(slurmdbd_conf->private_data,
			    key_pair->value, 128);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SLURMDBD_CONF");
	key_pair->value = _get_conf_path();
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SLURMDBD_VERSION");
	key_pair->value = xstrdup(SLURM_VERSION);
	list_append(my_list, key_pair);


	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SlurmUser");
	key_pair->value = xmalloc(128);
	snprintf(key_pair->value, 128, "%s(%u)",
		 slurmdbd_conf->slurm_user_name, slurmdbd_conf->slurm_user_id);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("StepPurge");
	if(slurmdbd_conf->job_purge) {
		key_pair->value = xmalloc(32);
		snprintf(key_pair->value, 32, "%u months", 
			 slurmdbd_conf->step_purge);
	} else
		key_pair->value = xstrdup("NONE");
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("StorageHost");
	key_pair->value = xstrdup(slurmdbd_conf->storage_host);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("StorageLoc");
	key_pair->value = xstrdup(slurmdbd_conf->storage_loc);
	list_append(my_list, key_pair);

	/* StoragePass should NOT be passed due to security reasons */

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("StoragePort");
	key_pair->value = xmalloc(32);
	snprintf(key_pair->value, 32, "%u", slurmdbd_conf->storage_port);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("StorageType");
	key_pair->value = xstrdup(slurmdbd_conf->storage_type);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("StorageUser");
	key_pair->value = xstrdup(slurmdbd_conf->storage_user);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("TrackWCKey");
	key_pair->value = xmalloc(32);
	snprintf(key_pair->value, 32, "%u", slurmdbd_conf->track_wckey);
	list_append(my_list, key_pair);

	return my_list;
}
