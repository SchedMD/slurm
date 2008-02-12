/*****************************************************************************\
 *  jobacct_storage_slurmdbd.c - SlurmDBD slurm job accounting plugin.
 *****************************************************************************
 *  Copyright (C) 2002-2008 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Aubke <da@llnl.gov>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <slurm/slurm_errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/pack.h"
#include "src/common/slurmdbd_defs.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmd/slurmd/slurmd.h"

static uint16_t slurmdbd_port;

static char * _get_conf_path(void);
static int    _read_slurmdbd_conf(void);

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
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum versions for their plugins as the job accounting API 
 * matures.
 */
const char plugin_name[] = "Job accounting storage SLURMDBD plugin";
const char plugin_type[] = "jobacct_storage/slurmdbd";
const uint32_t plugin_version = 100;

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	static int first = 1;
	if(first) {
		/* since this can be loaded from many different places
		   only tell us once. */
		verbose("%s loaded", plugin_name);
		first = 0;
		_read_slurmdbd_conf();
	} else {
		debug4("%s loaded", plugin_name);
	}

	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	return SLURM_SUCCESS;
}

/* Read the slurmdbd.conf file to get the DbdPort value */
static int _read_slurmdbd_conf(void)
{
	s_p_options_t options[] = {
		{"DbdPort", S_P_UINT16},
		{"DebugLevel", S_P_UINT16},
		{"LogFile", S_P_STRING},
		{"PidFile", S_P_STRING},
		{"StoragePassword", S_P_STRING},
		{"StorageUser", S_P_STRING},
		{NULL} };
	s_p_hashtbl_t *tbl;
	char *conf_path;
	struct stat buf;

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

		if (!s_p_get_uint16(&slurmdbd_port, "DbdPort", tbl))
			slurmdbd_port = SLURMDBD_PORT;

		s_p_hashtbl_destroy(tbl);
	}

	xfree(conf_path);

	return SLURM_SUCCESS;
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

static int _send_msg(Buf buffer)
{
	return SLURM_SUCCESS;
}

/* 
 * Initialize the storage make sure tables are created and in working
 * order
 */
extern int jobacct_storage_p_init(char *location)
{
	return SLURM_SUCCESS;
}

/*
 * finish up storage connection
 */
extern int jobacct_storage_p_fini()
{
	return SLURM_SUCCESS;
}

/* 
 * load into the storage the start of a job
 */
extern int jobacct_storage_p_job_start(struct job_record *job_ptr)
{
	int rc;
	dbd_job_start_msg_t msg;
	Buf buffer = init_buf(1024);

	msg.job_id  = job_ptr->job_id;
	dbd_pack_job_start_msg(&msg, buffer);
	rc = _send_msg(buffer);
	free_buf(buffer);
	return rc;
}

/* 
 * load into the storage the end of a job
 */
extern int jobacct_storage_p_job_complete(struct job_record *job_ptr)
{
	int rc;
	dbd_job_comp_msg_t msg;
	Buf buffer = init_buf(1024);

	msg.job_id  = job_ptr->job_id;
	dbd_pack_job_complete_msg(&msg, buffer);
	rc = _send_msg(buffer);
	free_buf(buffer);
	return rc;
}

/* 
 * load into the storage the start of a job step
 */
extern int jobacct_storage_p_step_start(struct step_record *step_ptr)
{
	int rc;
	dbd_step_start_msg_t msg;
	Buf buffer = init_buf(1024);

	msg.job_id  = step_ptr->job_ptr->job_id;
	msg.step_id = step_ptr->step_id;
	dbd_pack_step_start_msg(&msg, buffer);
	rc = _send_msg(buffer);
	free_buf(buffer);
	return rc;
}

/* 
 * load into the storage the end of a job step
 */
extern int jobacct_storage_p_step_complete(struct step_record *step_ptr)
{
	int rc;
	dbd_step_comp_msg_t msg;
	Buf buffer = init_buf(1024);

	msg.job_id  = step_ptr->job_ptr->job_id;
	msg.step_id = step_ptr->step_id;
	dbd_pack_step_complete_msg(&msg, buffer);
	rc = _send_msg(buffer);
	free_buf(buffer);
	return rc;
}

/* 
 * load into the storage a suspention of a job
 */
extern int jobacct_storage_p_suspend(struct job_record *job_ptr)
{
	int rc;
	dbd_job_suspend_msg_t msg;
	Buf buffer = init_buf(1024);

	msg.job_id = job_ptr->job_id;
	dbd_pack_job_suspend_msg(&msg, buffer);
	rc = _send_msg(buffer);
	free_buf(buffer);
	return rc;
}

/* 
 * get info from the storage 
 * returns List of job_rec_t *
 * note List needs to be freed when called
 */
extern void jobacct_storage_p_get_jobs(List job_list,
					List selected_steps,
					List selected_parts,
					void *params)
{
	dbd_get_jobs_msg_t msg;
	Buf buffer = init_buf(1024);

	msg.job_id = NO_VAL;
	dbd_pack_get_jobs_msg(&msg, buffer);
	_send_msg(buffer);
	free_buf(buffer);
	return;
}

/* 
 * expire old info from the storage 
 */
extern void jobacct_storage_p_archive(List selected_parts,
				       void *params)
{
	return;
}
