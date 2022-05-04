/*****************************************************************************\
 *  read_config.c - functions for reading slurmdbd.conf
 *****************************************************************************
 *  Copyright (C) 2003-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

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
#include "src/common/slurmdb_defs.h"
#include "src/slurmdbd/read_config.h"

/* Global variables */
pthread_mutex_t conf_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Local functions */
static void _clear_slurmdbd_conf(void);

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
	free_slurm_conf(&slurm_conf, 0);

	if (slurmdbd_conf) {
		xfree(slurmdbd_conf->archive_dir);
		xfree(slurmdbd_conf->archive_script);
		slurmdbd_conf->commit_delay = 0;
		xfree(slurmdbd_conf->dbd_addr);
		xfree(slurmdbd_conf->dbd_backup);
		xfree(slurmdbd_conf->dbd_host);
		slurmdbd_conf->dbd_port = 0;
		slurmdbd_conf->debug_level = LOG_LEVEL_INFO;
		xfree(slurmdbd_conf->default_qos);
		xfree(slurmdbd_conf->log_file);
		slurmdbd_conf->syslog_debug = LOG_LEVEL_END;
		xfree(slurmdbd_conf->parameters);
		xfree(slurmdbd_conf->pid_file);
		slurmdbd_conf->purge_event = 0;
		slurmdbd_conf->purge_job = 0;
		slurmdbd_conf->purge_resv = 0;
		slurmdbd_conf->purge_step = 0;
		slurmdbd_conf->purge_suspend = 0;
		slurmdbd_conf->purge_txn = 0;
		slurmdbd_conf->purge_usage = 0;
		xfree(slurmdbd_conf->storage_loc);
		slurmdbd_conf->track_wckey = 0;
		slurmdbd_conf->track_ctld = 0;
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
		{"ArchiveEvents", S_P_BOOLEAN},
		{"ArchiveJobs", S_P_BOOLEAN},
		{"ArchiveResvs", S_P_BOOLEAN},
		{"ArchiveScript", S_P_STRING},
		{"ArchiveSteps", S_P_BOOLEAN},
		{"ArchiveSuspend", S_P_BOOLEAN},
		{"ArchiveTXN", S_P_BOOLEAN},
		{"ArchiveUsage", S_P_BOOLEAN},
		{"AuthAltTypes", S_P_STRING},
		{"AuthAltParameters", S_P_STRING},
		{"AuthInfo", S_P_STRING},
		{"AuthType", S_P_STRING},
		{"CommitDelay", S_P_UINT16},
		{"CommunicationParameters", S_P_STRING},
		{"DbdAddr", S_P_STRING},
		{"DbdBackupHost", S_P_STRING},
		{"DbdHost", S_P_STRING},
		{"DbdPort", S_P_UINT16},
		{"DebugFlags", S_P_STRING},
		{"DebugLevel", S_P_STRING},
		{"DebugLevelSyslog", S_P_STRING},
		{"DefaultQOS", S_P_STRING},
		{"JobPurge", S_P_UINT32},
		{"LogFile", S_P_STRING},
		{"LogTimeFormat", S_P_STRING},
		{"MaxQueryTimeRange", S_P_STRING},
		{"MessageTimeout", S_P_UINT16},
		{"Parameters", S_P_STRING},
		{"PidFile", S_P_STRING},
		{"PluginDir", S_P_STRING},
		{"PrivateData", S_P_STRING},
		{"PurgeEventAfter", S_P_STRING},
		{"PurgeJobAfter", S_P_STRING},
		{"PurgeResvAfter", S_P_STRING},
		{"PurgeStepAfter", S_P_STRING},
		{"PurgeSuspendAfter", S_P_STRING},
		{"PurgeTXNAfter", S_P_STRING},
		{"PurgeUsageAfter", S_P_STRING},
		{"PurgeEventMonths", S_P_UINT32},
		{"PurgeJobMonths", S_P_UINT32},
		{"PurgeStepMonths", S_P_UINT32},
		{"PurgeSuspendMonths", S_P_UINT32},
		{"PurgeTXNMonths", S_P_UINT32},
		{"PurgeUsageMonths", S_P_UINT32},
		{"SlurmUser", S_P_STRING},
		{"StepPurge", S_P_UINT32},
		{"StorageBackupHost", S_P_STRING},
		{"StorageHost", S_P_STRING},
		{"StorageLoc", S_P_STRING},
		{"StorageParameters", S_P_STRING},
		{"StoragePass", S_P_STRING},
		{"StoragePort", S_P_UINT16},
		{"StorageType", S_P_STRING},
		{"StorageUser", S_P_STRING},
		{"TCPTimeout", S_P_UINT16},
		{"TrackWCKey", S_P_BOOLEAN},
		{"TrackSlurmctldDown", S_P_BOOLEAN},
		{NULL} };
	s_p_hashtbl_t *tbl = NULL;
	char *conf_path = NULL;
	char *temp_str = NULL;
	struct stat buf;

	/* Set initial values */
	slurm_mutex_lock(&conf_mutex);
	if (slurmdbd_conf == NULL) {
		slurmdbd_conf = xmalloc(sizeof(*slurmdbd_conf));
		boot_time = time(NULL);
	}
	_clear_slurmdbd_conf();

	/* Get the slurmdbd.conf path and validate the file */
	conf_path = get_extra_conf_path("slurmdbd.conf");
	if ((conf_path == NULL) || (stat(conf_path, &buf) == -1)) {
		info("No slurmdbd.conf file (%s)", conf_path);
	} else {
		bool a_events = false, a_jobs = false, a_resv = false;
		bool a_steps = false, a_suspend = false, a_txn = false;
		bool a_usage = false;
		uid_t conf_path_uid;
		debug3("Checking slurmdbd.conf file:%s access permissions",
		       conf_path);
		if ((buf.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO)) != 0600)
			fatal("slurmdbd.conf file %s should be 600 is %o accessible for group or others",
			      conf_path,
			      buf.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO));

		debug("Reading slurmdbd.conf file %s", conf_path);

		tbl = s_p_hashtbl_create(options);
		if (s_p_parse_file(tbl, NULL, conf_path, false)
		    == SLURM_ERROR) {
			fatal("Could not open/read/parse slurmdbd.conf file %s",
			      conf_path);
		}
		conf_path_uid = buf.st_uid;


		if (!s_p_get_string(&slurmdbd_conf->archive_dir, "ArchiveDir",
				    tbl))
			slurmdbd_conf->archive_dir =
				xstrdup(DEFAULT_SLURMDBD_ARCHIVE_DIR);
		s_p_get_boolean(&a_events, "ArchiveEvents", tbl);
		s_p_get_boolean(&a_jobs, "ArchiveJobs", tbl);
		s_p_get_boolean(&a_resv, "ArchiveResvs", tbl);
		s_p_get_string(&slurmdbd_conf->archive_script, "ArchiveScript",
			       tbl);
		s_p_get_boolean(&a_steps, "ArchiveSteps", tbl);
		s_p_get_boolean(&a_suspend, "ArchiveSuspend", tbl);
		s_p_get_boolean(&a_txn, "ArchiveTXN", tbl);
		s_p_get_boolean(&a_usage, "ArchiveUsage", tbl);
		s_p_get_string(&slurm_conf.authalttypes, "AuthAltTypes", tbl);
		s_p_get_string(&slurm_conf.authalt_params, "AuthAltParameters",
			       tbl);
		s_p_get_string(&slurm_conf.authinfo, "AuthInfo", tbl);
		s_p_get_string(&slurm_conf.authtype, "AuthType", tbl);
		s_p_get_uint16(&slurmdbd_conf->commit_delay,
			       "CommitDelay", tbl);
		s_p_get_string(&slurm_conf.comm_params,
			       "CommunicationParameters", tbl);

		/*
		 * IPv4 on by default, can be disabled.
		 * IPv6 off by default, can be turned on.
		 */
		slurm_conf.conf_flags |= CTL_CONF_IPV4_ENABLED;
		if (xstrcasestr(slurm_conf.comm_params, "EnableIPv6"))
			slurm_conf.conf_flags |= CTL_CONF_IPV6_ENABLED;
		if (xstrcasestr(slurm_conf.comm_params, "DisableIPv4"))
			slurm_conf.conf_flags &= ~CTL_CONF_IPV4_ENABLED;

		if (!(slurm_conf.conf_flags & CTL_CONF_IPV4_ENABLED) &&
		    !(slurm_conf.conf_flags & CTL_CONF_IPV6_ENABLED))
			fatal("Both IPv4 and IPv6 support disabled, cannot communicate");

		s_p_get_string(&slurmdbd_conf->dbd_backup,
			       "DbdBackupHost", tbl);
		s_p_get_string(&slurmdbd_conf->dbd_host, "DbdHost", tbl);
		s_p_get_string(&slurmdbd_conf->dbd_addr, "DbdAddr", tbl);
		s_p_get_uint16(&slurmdbd_conf->dbd_port, "DbdPort", tbl);

		if (s_p_get_string(&temp_str, "DebugFlags", tbl)) {
			if (debug_str2flags(temp_str, &slurm_conf.debug_flags)
			    != SLURM_SUCCESS)
				fatal("DebugFlags invalid: %s", temp_str);
			xfree(temp_str);
		} else	/* Default: no DebugFlags */
			slurm_conf.debug_flags = 0;

		if (s_p_get_string(&temp_str, "DebugLevel", tbl)) {
			slurmdbd_conf->debug_level = log_string2num(temp_str);
			if (slurmdbd_conf->debug_level == NO_VAL16)
				fatal("Invalid DebugLevel %s", temp_str);
			xfree(temp_str);
		}

		s_p_get_string(&slurmdbd_conf->default_qos, "DefaultQOS", tbl);
		if (s_p_get_uint32(&slurmdbd_conf->purge_job,
				   "JobPurge", tbl)) {
			if (!slurmdbd_conf->purge_job)
				slurmdbd_conf->purge_job = NO_VAL;
			else
				slurmdbd_conf->purge_job |=
					SLURMDB_PURGE_MONTHS;
		}

		s_p_get_string(&slurmdbd_conf->log_file, "LogFile", tbl);

		if (s_p_get_string(&temp_str, "DebugLevelSyslog", tbl)) {
			slurmdbd_conf->syslog_debug = log_string2num(temp_str);
			if (slurmdbd_conf->syslog_debug == NO_VAL16)
				fatal("Invalid DebugLevelSyslog %s", temp_str);
			xfree(temp_str);
		}

		if (s_p_get_string(&temp_str, "LogTimeFormat", tbl)) {
			if (xstrcasestr(temp_str, "iso8601_ms"))
				slurm_conf.log_fmt = LOG_FMT_ISO8601_MS;
			else if (xstrcasestr(temp_str, "iso8601"))
				slurm_conf.log_fmt = LOG_FMT_ISO8601;
			else if (xstrcasestr(temp_str, "rfc5424_ms"))
				slurm_conf.log_fmt = LOG_FMT_RFC5424_MS;
			else if (xstrcasestr(temp_str, "rfc5424"))
				slurm_conf.log_fmt = LOG_FMT_RFC5424;
			else if (xstrcasestr(temp_str, "clock"))
				slurm_conf.log_fmt = LOG_FMT_CLOCK;
			else if (xstrcasestr(temp_str, "short"))
				slurm_conf.log_fmt = LOG_FMT_SHORT;
			else if (xstrcasestr(temp_str, "thread_id"))
				slurm_conf.log_fmt = LOG_FMT_THREAD_ID;
			xfree(temp_str);
		} else
			slurm_conf.log_fmt = LOG_FMT_ISO8601_MS;

		if (s_p_get_string(&temp_str, "MaxQueryTimeRange", tbl)) {
			slurmdbd_conf->max_time_range = time_str2secs(temp_str);
			xfree(temp_str);
		} else {
			slurmdbd_conf->max_time_range = INFINITE;
		}

		if (!s_p_get_uint16(&slurm_conf.msg_timeout, "MessageTimeout",
		                    tbl))
			slurm_conf.msg_timeout = DEFAULT_MSG_TIMEOUT;
		else if (slurm_conf.msg_timeout > 100)
			info("WARNING: MessageTimeout is too high for effective fault-tolerance");

		s_p_get_string(&slurmdbd_conf->parameters, "Parameters", tbl);
		if (slurmdbd_conf->parameters) {
			if (xstrcasestr(slurmdbd_conf->parameters,
					"PreserveCaseUser"))
				slurmdbd_conf->persist_conn_rc_flags |=
					PERSIST_FLAG_P_USER_CASE;
		}

		s_p_get_string(&slurmdbd_conf->pid_file, "PidFile", tbl);
		s_p_get_string(&slurm_conf.plugindir, "PluginDir", tbl);

		slurm_conf.private_data = 0; /* default visible to all */
		if (s_p_get_string(&temp_str, "PrivateData", tbl)) {
			if (xstrcasestr(temp_str, "account"))
				slurm_conf.private_data
					|= PRIVATE_DATA_ACCOUNTS;
			if (xstrcasestr(temp_str, "job"))
				slurm_conf.private_data
					|= PRIVATE_DATA_JOBS;
			if (xstrcasestr(temp_str, "event"))
				slurm_conf.private_data
					|= PRIVATE_DATA_EVENTS;
			if (xstrcasestr(temp_str, "node"))
				slurm_conf.private_data
					|= PRIVATE_DATA_NODES;
			if (xstrcasestr(temp_str, "partition"))
				slurm_conf.private_data
					|= PRIVATE_DATA_PARTITIONS;
			if (xstrcasestr(temp_str, "reservation"))
				slurm_conf.private_data
					|= PRIVATE_DATA_RESERVATIONS;
			if (xstrcasestr(temp_str, "usage"))
				slurm_conf.private_data
					|= PRIVATE_DATA_USAGE;
			if (xstrcasestr(temp_str, "user"))
				slurm_conf.private_data
					|= PRIVATE_DATA_USERS;
			if (xstrcasestr(temp_str, "all"))
				slurm_conf.private_data = 0xffff;
			xfree(temp_str);
		}
		if (s_p_get_string(&temp_str, "PurgeEventAfter", tbl)) {
			/* slurmdb_parse_purge will set SLURMDB_PURGE_FLAGS */
			if ((slurmdbd_conf->purge_event =
			     slurmdb_parse_purge(temp_str)) == NO_VAL) {
				fatal("Bad value \"%s\" for PurgeEventAfter",
				      temp_str);
			}
			xfree(temp_str);
		}
		if (s_p_get_string(&temp_str, "PurgeJobAfter", tbl)) {
			/* slurmdb_parse_purge will set SLURMDB_PURGE_FLAGS */
  			if ((slurmdbd_conf->purge_job =
			     slurmdb_parse_purge(temp_str)) == NO_VAL) {
				fatal("Bad value \"%s\" for PurgeJobAfter",
				      temp_str);
			}
			xfree(temp_str);
		}
		if (s_p_get_string(&temp_str, "PurgeResvAfter", tbl)) {
			/* slurmdb_parse_purge will set SLURMDB_PURGE_FLAGS */
			if ((slurmdbd_conf->purge_resv =
			     slurmdb_parse_purge(temp_str)) == NO_VAL) {
				fatal("Bad value \"%s\" for PurgeResvAfter",
				      temp_str);
			}
			xfree(temp_str);
		}
		if (s_p_get_string(&temp_str, "PurgeStepAfter", tbl)) {
			/* slurmdb_parse_purge will set SLURMDB_PURGE_FLAGS */
  			if ((slurmdbd_conf->purge_step =
			     slurmdb_parse_purge(temp_str)) == NO_VAL) {
				fatal("Bad value \"%s\" for PurgeStepAfter",
				      temp_str);
			}
			xfree(temp_str);
		}
		if (s_p_get_string(&temp_str, "PurgeSuspendAfter", tbl)) {
			/* slurmdb_parse_purge will set SLURMDB_PURGE_FLAGS */
 			if ((slurmdbd_conf->purge_suspend =
			     slurmdb_parse_purge(temp_str)) == NO_VAL) {
				fatal("Bad value \"%s\" for PurgeSuspendAfter",
				      temp_str);
			}
			xfree(temp_str);
		}
		if (s_p_get_string(&temp_str, "PurgeTXNAfter", tbl)) {
			/* slurmdb_parse_purge will set SLURMDB_PURGE_FLAGS */
 			if ((slurmdbd_conf->purge_txn =
			     slurmdb_parse_purge(temp_str)) == NO_VAL) {
				fatal("Bad value \"%s\" for PurgeTXNAfter",
				      temp_str);
			}
			xfree(temp_str);
		}
		if (s_p_get_string(&temp_str, "PurgeUsageAfter", tbl)) {
			/* slurmdb_parse_purge will set SLURMDB_PURGE_FLAGS */
			if ((slurmdbd_conf->purge_usage =
			     slurmdb_parse_purge(temp_str)) == NO_VAL) {
				fatal("Bad value \"%s\" for PurgeUsageAfter",
				      temp_str);
			}
			xfree(temp_str);
		}
		if (s_p_get_uint32(&slurmdbd_conf->purge_event,
				   "PurgeEventMonths", tbl)) {
			if (!slurmdbd_conf->purge_event)
				slurmdbd_conf->purge_event = NO_VAL;
			else
				slurmdbd_conf->purge_event |=
					SLURMDB_PURGE_MONTHS;
		}

		if (s_p_get_uint32(&slurmdbd_conf->purge_job,
				   "PurgeJobMonths", tbl)) {
			if (!slurmdbd_conf->purge_job)
				slurmdbd_conf->purge_job = NO_VAL;
			else
				slurmdbd_conf->purge_job |=
					SLURMDB_PURGE_MONTHS;
		}

		if (s_p_get_uint32(&slurmdbd_conf->purge_step,
				   "PurgeStepMonths", tbl)) {
			if (!slurmdbd_conf->purge_step)
				slurmdbd_conf->purge_step = NO_VAL;
			else
				slurmdbd_conf->purge_step |=
					SLURMDB_PURGE_MONTHS;
		}

		if (s_p_get_uint32(&slurmdbd_conf->purge_suspend,
				   "PurgeSuspendMonths", tbl)) {
			if (!slurmdbd_conf->purge_suspend)
				slurmdbd_conf->purge_suspend = NO_VAL;
			else
				slurmdbd_conf->purge_suspend
					|= SLURMDB_PURGE_MONTHS;
		}

		if (s_p_get_uint32(&slurmdbd_conf->purge_txn,
				   "PurgeTXNMonths", tbl)) {
			if (!slurmdbd_conf->purge_txn)
				slurmdbd_conf->purge_txn = NO_VAL;
			else
				slurmdbd_conf->purge_txn
					|= SLURMDB_PURGE_MONTHS;
		}

		if (s_p_get_uint32(&slurmdbd_conf->purge_usage,
				   "PurgeUsageMonths", tbl)) {
			if (!slurmdbd_conf->purge_usage)
				slurmdbd_conf->purge_usage = NO_VAL;
			else
				slurmdbd_conf->purge_usage
					|= SLURMDB_PURGE_MONTHS;
		}

		s_p_get_string(&slurm_conf.slurm_user_name, "SlurmUser", tbl);

		if (slurm_conf.slurm_user_name) {
			struct passwd *conf_owner;
			if ((conf_owner = getpwuid(conf_path_uid))) {
				if (xstrcmp(conf_owner->pw_name,
					    slurm_conf.slurm_user_name)) {
					fatal("slurmdbd.conf not owned by SlurmUser %s!=%s",
					      conf_owner->pw_name,
					      slurm_conf.slurm_user_name);
				}
			} else
				fatal("No user entry for uid(%d) owning slurmdbd.conf file %s found",
				      conf_path_uid,
				      conf_path);
		}

		if (s_p_get_uint32(&slurmdbd_conf->purge_step,
				   "StepPurge", tbl)) {
			if (!slurmdbd_conf->purge_step)
				slurmdbd_conf->purge_step = NO_VAL;
			else
				slurmdbd_conf->purge_step |=
					SLURMDB_PURGE_MONTHS;
		}

		s_p_get_string(&slurm_conf.accounting_storage_backup_host,
			       "StorageBackupHost", tbl);
		s_p_get_string(&slurm_conf.accounting_storage_host,
			       "StorageHost", tbl);
		s_p_get_string(&slurmdbd_conf->storage_loc,
			       "StorageLoc", tbl);
		s_p_get_string(&slurm_conf.accounting_storage_params,
			       "StorageParameters", tbl);
		s_p_get_string(&slurm_conf.accounting_storage_pass,
			       "StoragePass", tbl);
		s_p_get_uint16(&slurm_conf.accounting_storage_port,
		               "StoragePort", tbl);
		s_p_get_string(&slurm_conf.accounting_storage_type,
		               "StorageType", tbl);
		s_p_get_string(&slurm_conf.accounting_storage_user,
			       "StorageUser", tbl);

		if (!s_p_get_uint16(&slurm_conf.tcp_timeout, "TCPTimeout", tbl))
			slurm_conf.tcp_timeout = DEFAULT_TCP_TIMEOUT;

		if (!s_p_get_boolean((bool *)&slurmdbd_conf->track_wckey,
				     "TrackWCKey", tbl))
			slurmdbd_conf->track_wckey = false;

		if (!s_p_get_boolean((bool *)&slurmdbd_conf->track_ctld,
				     "TrackSlurmctldDown", tbl))
			slurmdbd_conf->track_ctld = false;

		if (a_events && slurmdbd_conf->purge_event)
			slurmdbd_conf->purge_event |= SLURMDB_PURGE_ARCHIVE;
		if (a_jobs && slurmdbd_conf->purge_job)
			slurmdbd_conf->purge_job |= SLURMDB_PURGE_ARCHIVE;
		if (a_resv && slurmdbd_conf->purge_resv)
			slurmdbd_conf->purge_resv |= SLURMDB_PURGE_ARCHIVE;
		if (a_steps && slurmdbd_conf->purge_step)
			slurmdbd_conf->purge_step |= SLURMDB_PURGE_ARCHIVE;
		if (a_suspend && slurmdbd_conf->purge_suspend)
			slurmdbd_conf->purge_suspend |= SLURMDB_PURGE_ARCHIVE;
		if (a_txn && slurmdbd_conf->purge_txn)
			slurmdbd_conf->purge_txn |= SLURMDB_PURGE_ARCHIVE;
		if (a_usage && slurmdbd_conf->purge_usage)
			slurmdbd_conf->purge_usage |= SLURMDB_PURGE_ARCHIVE;

		s_p_hashtbl_destroy(tbl);
	}

	xfree(conf_path);
	if (!slurm_conf.authtype)
		slurm_conf.authtype = xstrdup(DEFAULT_SLURMDBD_AUTHTYPE);
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
	if (!slurm_conf.plugindir)
		slurm_conf.plugindir = xstrdup(default_plugin_path);
	if (slurm_conf.slurm_user_name) {
		if (uid_from_string(slurm_conf.slurm_user_name,
		                    &slurm_conf.slurm_user_id) < 0)
			fatal("Invalid user for SlurmUser %s, ignored",
			      slurm_conf.slurm_user_name);
	} else {
		slurm_conf.slurm_user_name = xstrdup("root");
		slurm_conf.slurm_user_id = 0;
	}

	if (!slurm_conf.accounting_storage_type)
		fatal("StorageType must be specified");
	if (!xstrcmp(slurm_conf.accounting_storage_type,
	             "accounting_storage/slurmdbd")) {
		fatal("StorageType=%s is invalid in slurmdbd.conf",
		      slurm_conf.accounting_storage_type);
	}

	if (!slurm_conf.accounting_storage_host)
		slurm_conf.accounting_storage_host =
			xstrdup(DEFAULT_STORAGE_HOST);

	if (!slurm_conf.accounting_storage_user)
		slurm_conf.accounting_storage_user = xstrdup(getlogin());

	if (!xstrcmp(slurm_conf.accounting_storage_type,
	             "accounting_storage/mysql")) {
		if (!slurm_conf.accounting_storage_port)
			slurm_conf.accounting_storage_port =
				DEFAULT_MYSQL_PORT;
		if (!slurmdbd_conf->storage_loc)
			slurmdbd_conf->storage_loc =
				xstrdup(DEFAULT_ACCOUNTING_DB);
	} else {
		if (!slurm_conf.accounting_storage_port)
			slurm_conf.accounting_storage_port =
				DEFAULT_STORAGE_PORT;
		if (!slurmdbd_conf->storage_loc)
			slurmdbd_conf->storage_loc =
				xstrdup(DEFAULT_STORAGE_LOC);
	}

	if (slurmdbd_conf->archive_dir) {
		if (stat(slurmdbd_conf->archive_dir, &buf) < 0)
			fatal("Failed to stat the archive directory %s: %m",
			      slurmdbd_conf->archive_dir);
		if (!(buf.st_mode & S_IFDIR))
			fatal("archive directory %s isn't a directory",
			      slurmdbd_conf->archive_dir);

		if (access(slurmdbd_conf->archive_dir, W_OK) < 0)
			fatal("archive directory %s is not writable",
			      slurmdbd_conf->archive_dir);
	}

	if (slurmdbd_conf->archive_script) {
		if (stat(slurmdbd_conf->archive_script, &buf) < 0)
			fatal("Failed to stat the archive script %s: %m",
			      slurmdbd_conf->archive_dir);

		if (!(buf.st_mode & S_IFREG))
			fatal("archive script %s isn't a regular file",
			      slurmdbd_conf->archive_script);

		if (access(slurmdbd_conf->archive_script, X_OK) < 0)
			fatal("archive script %s is not executable",
			      slurmdbd_conf->archive_script);
	}

	if (!slurmdbd_conf->purge_event)
		slurmdbd_conf->purge_event = NO_VAL;
	if (!slurmdbd_conf->purge_job)
		slurmdbd_conf->purge_job = NO_VAL;
	if (!slurmdbd_conf->purge_resv)
		slurmdbd_conf->purge_resv = NO_VAL;
	if (!slurmdbd_conf->purge_step)
		slurmdbd_conf->purge_step = NO_VAL;
	if (!slurmdbd_conf->purge_suspend)
		slurmdbd_conf->purge_suspend = NO_VAL;
	if (!slurmdbd_conf->purge_txn)
		slurmdbd_conf->purge_txn = NO_VAL;
	if (!slurmdbd_conf->purge_usage)
		slurmdbd_conf->purge_usage = NO_VAL;

	slurm_conf.last_update = time(NULL);
	slurm_mutex_unlock(&conf_mutex);
	return SLURM_SUCCESS;
}

/* Log the current configuration using verbose() */
extern void log_config(void)
{
	char tmp_str[128];
	char *tmp_ptr = NULL;

	debug2("ArchiveDir        = %s", slurmdbd_conf->archive_dir);
	debug2("ArchiveScript     = %s", slurmdbd_conf->archive_script);
	debug2("AuthAltTypes      = %s", slurm_conf.authalttypes);
	debug2("AuthAltParameters = %s", slurm_conf.authalt_params);
	debug2("AuthInfo          = %s", slurm_conf.authinfo);
	debug2("AuthType          = %s", slurm_conf.authtype);
	debug2("CommitDelay       = %u", slurmdbd_conf->commit_delay);
	debug2("CommunicationParameters	= %s", slurm_conf.comm_params);
	debug2("DbdAddr           = %s", slurmdbd_conf->dbd_addr);
	debug2("DbdBackupHost     = %s", slurmdbd_conf->dbd_backup);
	debug2("DbdHost           = %s", slurmdbd_conf->dbd_host);
	debug2("DbdPort           = %u", slurmdbd_conf->dbd_port);
	tmp_ptr = debug_flags2str(slurm_conf.debug_flags);
	debug2("DebugFlags        = %s", tmp_ptr);
	xfree(tmp_ptr);
	debug2("DebugLevel        = %u", slurmdbd_conf->debug_level);
	debug2("DebugLevelSyslog  = %u", slurmdbd_conf->syslog_debug);
	debug2("DefaultQOS        = %s", slurmdbd_conf->default_qos);

	debug2("LogFile           = %s", slurmdbd_conf->log_file);
	debug2("MessageTimeout    = %u", slurm_conf.msg_timeout);
	debug2("Parameters        = %s", slurmdbd_conf->parameters);
	debug2("PidFile           = %s", slurmdbd_conf->pid_file);
	debug2("PluginDir         = %s", slurm_conf.plugindir);

	private_data_string(slurm_conf.private_data,
			    tmp_str, sizeof(tmp_str));
	debug2("PrivateData       = %s", tmp_str);

	slurmdb_purge_string(slurmdbd_conf->purge_event,
			     tmp_str, sizeof(tmp_str), 1);
	debug2("PurgeEventAfter   = %s", tmp_str);

	slurmdb_purge_string(slurmdbd_conf->purge_job,
			     tmp_str, sizeof(tmp_str), 1);
	debug2("PurgeJobAfter     = %s", tmp_str);

	slurmdb_purge_string(slurmdbd_conf->purge_resv,
			     tmp_str, sizeof(tmp_str), 1);
	debug2("PurgeResvAfter    = %s", tmp_str);

	slurmdb_purge_string(slurmdbd_conf->purge_step,
			     tmp_str, sizeof(tmp_str), 1);
	debug2("PurgeStepAfter    = %s", tmp_str);

	slurmdb_purge_string(slurmdbd_conf->purge_suspend,
			     tmp_str, sizeof(tmp_str), 1);
	debug2("PurgeSuspendAfter = %s", tmp_str);

	slurmdb_purge_string(slurmdbd_conf->purge_txn,
			     tmp_str, sizeof(tmp_str), 1);
	debug2("PurgeTXNAfter = %s", tmp_str);

	slurmdb_purge_string(slurmdbd_conf->purge_usage,
			     tmp_str, sizeof(tmp_str), 1);
	debug2("PurgeUsageAfter = %s", tmp_str);

	debug2("SlurmUser         = %s(%u)",
	       slurm_conf.slurm_user_name, slurm_conf.slurm_user_id);

	debug2("StorageBackupHost = %s",
	       slurm_conf.accounting_storage_backup_host);
	debug2("StorageHost       = %s",
	       slurm_conf.accounting_storage_host);
	debug2("StorageLoc        = %s", slurmdbd_conf->storage_loc);
	debug2("StorageParameters = %s", slurm_conf.accounting_storage_params);
	/* debug2("StoragePass       = %s",
	       slurm_conf.accounting_storage_pass); */
	debug2("StoragePort       = %u", slurm_conf.accounting_storage_port);
	debug2("StorageType       = %s", slurm_conf.accounting_storage_type);
	debug2("StorageUser       = %s", slurm_conf.accounting_storage_user);

	debug2("TCPTimeout        = %u", slurm_conf.tcp_timeout);

	debug2("TrackWCKey        = %u", slurmdbd_conf->track_wckey);
	debug2("TrackSlurmctldDown= %u", slurmdbd_conf->track_ctld);
}

/* Dump the configuration in name,value pairs for output to
 *	"statsmgr show config", caller must call list_destroy() */
extern List dump_config(void)
{
	config_key_pair_t *key_pair;
	char time_str[32];
	List my_list = list_create(destroy_config_key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ArchiveDir");
	key_pair->value = xstrdup(slurmdbd_conf->archive_dir);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ArchiveEvents");
	key_pair->value = xstrdup(
		SLURMDB_PURGE_ARCHIVE_SET(
			slurmdbd_conf->purge_event) ? "Yes" : "No");
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ArchiveJobs");
	key_pair->value = xstrdup(
		SLURMDB_PURGE_ARCHIVE_SET(
			slurmdbd_conf->purge_job) ? "Yes" : "No");
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ArchiveResvs");
	key_pair->value = xstrdup(
		SLURMDB_PURGE_ARCHIVE_SET(
			slurmdbd_conf->purge_resv) ? "Yes" : "No");
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ArchiveScript");
	key_pair->value = xstrdup(slurmdbd_conf->archive_script);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ArchiveSteps");
	key_pair->value = xstrdup(
		SLURMDB_PURGE_ARCHIVE_SET(
			slurmdbd_conf->purge_step) ? "Yes" : "No");
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ArchiveSuspend");
	key_pair->value = xstrdup(
		SLURMDB_PURGE_ARCHIVE_SET(
			slurmdbd_conf->purge_suspend) ? "Yes" : "No");
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ArchiveTXN");
	key_pair->value = xstrdup(
		SLURMDB_PURGE_ARCHIVE_SET(
			slurmdbd_conf->purge_txn) ? "Yes" : "No");
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ArchiveUsage");
	key_pair->value = xstrdup(
		SLURMDB_PURGE_ARCHIVE_SET(
			slurmdbd_conf->purge_usage) ? "Yes" : "No");
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AuthAltTypes");
	key_pair->value = xstrdup(slurm_conf.authalttypes);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AuthAltParameters");
	key_pair->value = xstrdup(slurm_conf.authalt_params);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AuthInfo");
	key_pair->value = xstrdup(slurm_conf.authinfo);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AuthType");
	key_pair->value = xstrdup(slurm_conf.authtype);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("BOOT_TIME");
	key_pair->value = xmalloc(128);
	slurm_make_time_str ((time_t *)&boot_time, key_pair->value, 128);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("CommitDelay");
	key_pair->value = xstrdup(slurmdbd_conf->commit_delay ? "Yes" : "No");
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("CommunicationParameters");
	key_pair->value = xstrdup(slurm_conf.comm_params);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("DbdAddr");
	key_pair->value = xstrdup(slurmdbd_conf->dbd_addr);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("DbdBackupHost");
	key_pair->value = xstrdup(slurmdbd_conf->dbd_backup);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("DbdHost");
	key_pair->value = xstrdup(slurmdbd_conf->dbd_host);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("DbdPort");
	key_pair->value = xstrdup_printf("%u", slurmdbd_conf->dbd_port);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("DebugFlags");
	key_pair->value = debug_flags2str(slurm_conf.debug_flags);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("DebugLevel");
	key_pair->value = xstrdup(log_num2string(slurmdbd_conf->debug_level));
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("DebugLevelSyslog");
	key_pair->value = xstrdup(log_num2string(slurmdbd_conf->syslog_debug));
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("DefaultQOS");
	key_pair->value = xstrdup(slurmdbd_conf->default_qos);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("LogFile");
	key_pair->value = xstrdup(slurmdbd_conf->log_file);
	list_append(my_list, key_pair);

	secs2time_str(slurmdbd_conf->max_time_range, time_str,
		      sizeof(time_str));
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MaxQueryTimeRange");
	key_pair->value = xstrdup_printf("%s", time_str);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MessageTimeout");
	key_pair->value = xstrdup_printf("%u secs", slurm_conf.msg_timeout);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("Parameters");
	key_pair->value = xstrdup(slurmdbd_conf->parameters);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("PidFile");
	key_pair->value = xstrdup(slurmdbd_conf->pid_file);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("PluginDir");
	key_pair->value = xstrdup(slurm_conf.plugindir);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("PrivateData");
	key_pair->value = xmalloc(128);
	private_data_string(slurm_conf.private_data, key_pair->value, 128);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("PurgeEventAfter");
	if (slurmdbd_conf->purge_event != NO_VAL) {
		key_pair->value = xmalloc(32);
		slurmdb_purge_string(slurmdbd_conf->purge_event,
				     key_pair->value, 32, 1);
	} else
		key_pair->value = xstrdup("NONE");
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("PurgeJobAfter");
	if (slurmdbd_conf->purge_job != NO_VAL) {
		key_pair->value = xmalloc(32);
		slurmdb_purge_string(slurmdbd_conf->purge_job,
				     key_pair->value, 32, 1);
	} else
		key_pair->value = xstrdup("NONE");
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("PurgeResvAfter");
	if (slurmdbd_conf->purge_resv != NO_VAL) {
		key_pair->value = xmalloc(32);
		slurmdb_purge_string(slurmdbd_conf->purge_resv,
				     key_pair->value, 32, 1);
	} else
		key_pair->value = xstrdup("NONE");
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("PurgeStepAfter");
	if (slurmdbd_conf->purge_step != NO_VAL) {
		key_pair->value = xmalloc(32);
		slurmdb_purge_string(slurmdbd_conf->purge_step,
				     key_pair->value, 32, 1);
	} else
		key_pair->value = xstrdup("NONE");
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("PurgeSuspendAfter");
	if (slurmdbd_conf->purge_suspend != NO_VAL) {
		key_pair->value = xmalloc(32);
		slurmdb_purge_string(slurmdbd_conf->purge_suspend,
				     key_pair->value, 32, 1);
	} else
		key_pair->value = xstrdup("NONE");
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("PurgeTXNAfter");
	if (slurmdbd_conf->purge_txn != NO_VAL) {
		key_pair->value = xmalloc(32);
		slurmdb_purge_string(slurmdbd_conf->purge_txn,
				     key_pair->value, 32, 1);
	} else
		key_pair->value = xstrdup("NONE");
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("PurgeUsageAfter");
	if (slurmdbd_conf->purge_usage != NO_VAL) {
		key_pair->value = xmalloc(32);
		slurmdb_purge_string(slurmdbd_conf->purge_usage,
				     key_pair->value, 32, 1);
	} else
		key_pair->value = xstrdup("NONE");
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SLURMDBD_CONF");
	key_pair->value = get_extra_conf_path("slurmdbd.conf");
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SLURMDBD_VERSION");
	key_pair->value = xstrdup(SLURM_VERSION_STRING);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SlurmUser");
	key_pair->value = xstrdup_printf("%s(%u)",
	                                 slurm_conf.slurm_user_name,
	                                 slurm_conf.slurm_user_id);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("StorageBackupHost");
	key_pair->value = xstrdup(slurm_conf.accounting_storage_backup_host);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("StorageHost");
	key_pair->value = xstrdup(slurm_conf.accounting_storage_host);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("StorageLoc");
	key_pair->value = xstrdup(slurmdbd_conf->storage_loc);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("StorageParameters");
	key_pair->value = xstrdup(slurm_conf.accounting_storage_params);
	list_append(my_list, key_pair);

	/* StoragePass should NOT be passed due to security reasons */

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("StoragePort");
	key_pair->value = xstrdup_printf("%u",
	                                 slurm_conf.accounting_storage_port);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("StorageType");
	key_pair->value = xstrdup(slurm_conf.accounting_storage_type);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("StorageUser");
	key_pair->value = xstrdup(slurm_conf.accounting_storage_user);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("TCPTimeout");
	key_pair->value = xstrdup_printf("%u secs", slurm_conf.tcp_timeout);
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("TrackWCKey");
	key_pair->value = xstrdup(slurmdbd_conf->track_wckey ? "Yes" : "No");
	list_append(my_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("TrackSlurmctldDown");
	key_pair->value = xstrdup(slurmdbd_conf->track_ctld ? "Yes" : "No");
	list_append(my_list, key_pair);

	return my_list;
}
