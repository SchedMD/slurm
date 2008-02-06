/*****************************************************************************\
 *  read_config.c - functions for reading slurmdbd.conf
 *****************************************************************************
 *  Copyright (C) 2003-2008 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-226842.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <slurm/slurm_errno.h>

#include "src/common/macros.h"
#include "src/common/log.h"
#include "src/common/parse_config.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmdbd/read_config.h"

/* Global variables */
pthread_mutex_t conf_mutex = PTHREAD_MUTEX_INITIALIZER;
slurm_dbd_conf_t *slurmdbd_conf = NULL;

/* Local functions */
static void _clear_slurmdbd_conf(void);
static char * _get_conf_path(void);

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
		xfree(slurmdbd_conf->log_file);
		xfree(slurmdbd_conf->pid_file);
		xfree(slurmdbd_conf->state_save_dir);
		xfree(slurmdbd_conf->storage_password);
		xfree(slurmdbd_conf->storage_user);
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
		{"DebugLevel", S_P_UINT16},
		{"LogFile", S_P_STRING},
		{"PidFile", S_P_STRING},
		{"StateSaveDir", S_P_STRING},
		{"StoragePassword", S_P_STRING},
		{"StorageUser", S_P_STRING},
		{NULL} };
	s_p_hashtbl_t *tbl;
	char *conf_path;
	struct stat buf;

	/* Set initial values */
	slurm_mutex_lock(&conf_mutex);
	if (slurmdbd_conf == NULL)
		slurmdbd_conf = xmalloc(sizeof(slurm_dbd_conf_t));
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

		s_p_get_uint16(&slurmdbd_conf->debug_level,
				"DebugLevel", tbl);
		s_p_get_string(&slurmdbd_conf->log_file,
				"LogFile", tbl);
		s_p_get_string(&slurmdbd_conf->pid_file,
				"PidFile", tbl);
		s_p_get_string(&slurmdbd_conf->state_save_dir,
				"StateSaveDir", tbl);
		s_p_get_string(&slurmdbd_conf->storage_password,
				"StoragePassword", tbl);
		s_p_get_string(&slurmdbd_conf->storage_user,
				"StorageUser", tbl);

		s_p_hashtbl_destroy(tbl);
	}

	xfree(conf_path);
	if (slurmdbd_conf->pid_file == NULL)
		slurmdbd_conf->pid_file = xstrdup(DEFAULT_SLURMDBD_PIDFILE);
	if (slurmdbd_conf->state_save_dir == NULL)
		slurmdbd_conf->state_save_dir = xstrdup(DEFAULT_STATE_SAVE_DIR);

	slurm_mutex_unlock(&conf_mutex);
	return SLURM_SUCCESS;
}

/* Log the current configuration using verbose() */
extern void log_config(void)
{
	verbose("DebugLevel        = %u", slurmdbd_conf->debug_level);
	verbose("LogFile           = %s", slurmdbd_conf->log_file);
	verbose("PidFile           = %s", slurmdbd_conf->pid_file);
	verbose("StateSaveDir      = %s", slurmdbd_conf->state_save_dir);
	verbose("StoragePassword   = %s", slurmdbd_conf->storage_password);
	verbose("StorageUser       = %s", slurmdbd_conf->storage_user);
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
