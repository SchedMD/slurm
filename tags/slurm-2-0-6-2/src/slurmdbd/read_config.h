/*****************************************************************************\
 *  read_config.h - functions and declarations for reading slurmdbd.conf
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

#ifndef _DBD_READ_CONFIG_H
#define _DBD_READ_CONFIG_H

#if HAVE_CONFIG_H
#  include "config.h"
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#else  /* !HAVE_INTTYPES_H */
#  if HAVE_STDINT_H
#    include <stdint.h>
#  endif
#endif  /* HAVE_INTTYPES_H */
#else   /* !HAVE_CONFIG_H */
#include <stdint.h>
#endif  /* HAVE_CONFIG_H */

#include <time.h>
#include "src/common/list.h"

#define DEFAULT_SLURMDBD_AUTHTYPE	"auth/none"
//#define DEFAULT_SLURMDBD_JOB_PURGE	12
#define DEFAULT_SLURMDBD_PIDFILE	"/var/run/slurmdbd.pid"
#define DEFAULT_SLURMDBD_ARCHIVE_DIR	"/tmp"
//#define DEFAULT_SLURMDBD_STEP_PURGE	1

/* SlurmDBD configuration parameters */
typedef struct slurm_dbd_conf {
	time_t		last_update;	/* time slurmdbd.conf read	*/
	uint16_t	archive_events;	/* flag if we are to
					 * archive events */
	uint16_t	archive_jobs;	/* flag if we are to
					 * archive jobs	*/
	char *		archive_dir;    /* location to localy
					 * store data if not
					 * using a script               */
	char *		archive_script;	/* script to archive old data	*/
	uint16_t	archive_steps;	/* flag if we are to
					 * archive steps	        */
	uint16_t	archive_suspend;/* flag if we are to
					 * archive suspend data         */
	char *		auth_info;	/* authentication info		*/
	char *		auth_type;	/* authentication mechanism	*/
	uint16_t        control_timeout;/* how long to wait before
					 * backup takes control         */   
	char *		dbd_addr;	/* network address of Slurm DBD	*/
	char *		dbd_backup;	/* hostname of Slurm DBD backup */
	char *		dbd_host;	/* hostname of Slurm DBD	*/
	uint16_t	dbd_port;	/* port number for RPCs to DBD	*/
	uint16_t	debug_level;	/* Debug level, default=3	*/
	char *   	default_qos;	/* default qos setting when
					 * adding clusters              */
	char *		log_file;	/* Log file			*/
	uint16_t        msg_timeout;    /* message timeout		*/   
	char *		pid_file;	/* where to store current PID	*/
	char *		plugindir;	/* dir to look for plugins	*/
	uint16_t        private_data;   /* restrict information         */
	uint16_t        purge_event;    /* purge events older than
					 * this in months */
	uint16_t	purge_job;	/* purge time for job info	*/ 
	uint16_t	purge_step;	/* purge time for step info	*/
	uint16_t        purge_suspend;  /* purge suspend data older than this
					 * in months */
	uint32_t	slurm_user_id;	/* uid of slurm_user_name	*/
	char *		slurm_user_name;/* user that slurmcdtld runs as	*/
	char *		storage_backup_host;/* backup host where DB is
					     * running */
	char *		storage_host;	/* host where DB is running	*/
	char *		storage_loc;	/* database name		*/
	char *		storage_pass;   /* password for DB write	*/
	uint16_t	storage_port;	/* port DB is listening to	*/
	char *		storage_type;	/* DB to be used for storage	*/
	char *		storage_user;	/* user authorized to write DB	*/
	uint16_t        track_wckey;    /* Whether or not to track wckey*/
} slurm_dbd_conf_t;

extern pthread_mutex_t conf_mutex;
extern slurm_dbd_conf_t *slurmdbd_conf;


/*
 * free_slurmdbd_conf - free storage associated with the global variable 
 *	slurmdbd_conf
 */
extern void free_slurmdbd_conf(void);

/* Return the DbdPort value */
extern uint16_t get_dbd_port(void);

/* lock and unlock the dbd_conf */
extern void slurmdbd_conf_lock(void);
extern void slurmdbd_conf_unlock(void);

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
