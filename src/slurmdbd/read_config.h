/*****************************************************************************\
 *  read_config.h - functions and declarations for reading slurmdbd.conf
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

#ifndef _DBD_READ_CONFIG_H
#define _DBD_READ_CONFIG_H

#include <inttypes.h>
#include <time.h>
#include <pthread.h>

#include "src/common/list.h"

#define DEFAULT_SLURMDBD_AUTHTYPE	"auth/munge"
//#define DEFAULT_SLURMDBD_JOB_PURGE	12
#define DEFAULT_SLURMDBD_PIDFILE	"/var/run/slurmdbd.pid"
#define DEFAULT_SLURMDBD_ARCHIVE_DIR	"/tmp"
#define DEFAULT_SLURMDBD_KEEPALIVE_INTERVAL 30
#define DEFAULT_SLURMDBD_KEEPALIVE_PROBES 3
#define DEFAULT_SLURMDBD_KEEPALIVE_TIME 30
//#define DEFAULT_SLURMDBD_STEP_PURGE	1

/* Define slurmdbd_conf_t flags */
#define DBD_CONF_FLAG_ALLOW_NO_DEF_ACCT SLURM_BIT(0)

/* SlurmDBD configuration parameters */
typedef struct {
	char *		archive_dir;    /* location to locally store
					 * data if not using a script   */
	char *		archive_script;	/* script to archive old data	*/
	uint16_t        commit_delay;   /* On busy systems delay
					 * commits from slurmctld this
					 * many seconds                 */
	char *		dbd_addr;	/* network address of Slurm DBD	*/
	char *		dbd_backup;	/* hostname of Slurm DBD backup */
	char *		dbd_host;	/* hostname of Slurm DBD	*/
	uint16_t	dbd_port;	/* port number for RPCs to DBD	*/
	uint16_t	debug_level;	/* Debug level, default=3	*/
	char *	 	default_qos;	/* default qos setting when
					 * adding clusters              */
	uint32_t flags;			/* Various flags see DBD_CONF_FLAG_* */
	char *		log_file;	/* Log file			*/
	uint32_t	max_time_range;	/* max time range for user queries */
	char *		parameters;	/* parameters to change behavior with
					 * the slurmdbd directly	*/
	uint16_t        persist_conn_rc_flags; /* flags to be sent back on any
						* persist connection init
						*/
	char *		pid_file;	/* where to store current PID	*/
					/* purge variable format
					 * controlled by PURGE_FLAGS	*/
	uint32_t        purge_event;    /* purge events older than
					 * this in months or days 	*/
	uint32_t	purge_job;	/* purge time for job info	*/
	uint32_t	purge_resv;	/* purge time for reservation info */
	uint32_t	purge_step;	/* purge time for step info	*/
	uint32_t        purge_suspend;  /* purge suspend data older
					 * than this in months or days	*/
	uint32_t        purge_txn;      /* purge transaction data older
					 * than this in months or days	*/
	uint32_t        purge_usage;    /* purge usage data older
					 * than this in months or days	*/
	char *		storage_loc;	/* database name		*/
	uint16_t	syslog_debug;	/* output to both logfile and syslog*/
	uint16_t        track_wckey;    /* Whether or not to track wckey*/
	uint16_t        track_ctld;     /* Whether or not track when a
					 * slurmctld goes down or not   */
} slurmdbd_conf_t;

extern pthread_mutex_t conf_mutex;
extern slurmdbd_conf_t *slurmdbd_conf;


/*
 * free_slurmdbd_conf - free storage associated with the global variable
 *	slurmdbd_conf
 */
extern void free_slurmdbd_conf(void);

/* Log the current configuration using verbose() */
extern void log_config(void);

/*
 * read_slurmdbd_conf - load the SlurmDBD configuration from the slurmdbd.conf
 *	file. This function can be called more than once if so desired.
 * RET SLURM_SUCCESS if no error, otherwise an error code
 */
extern int read_slurmdbd_conf(void);

/* Dump the configuration in name,value pairs for output to
 *	"sacctmgr show config", caller must call list_destroy() */
extern List dump_config(void);

#endif /* !_DBD_READ_CONFIG_H */
