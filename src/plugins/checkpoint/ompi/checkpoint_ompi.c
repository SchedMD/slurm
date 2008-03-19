/*****************************************************************************\
 *  checkpoint_ompi.c - OpenMPI slurm checkpoint plugin.
 *****************************************************************************
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  LLNL-CODE-402394.
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
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/common/pack.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/srun_comm.h"

struct check_job_info {
	uint16_t disabled;	/* counter, checkpointable only if zero */
	uint16_t reply_cnt;
	uint16_t wait_time;
	time_t   time_stamp;	/* begin or end checkpoint time */
	uint32_t error_code;
	char    *error_msg;
};

static int _ckpt_step(struct step_record * step_ptr, uint16_t wait, int vacate);

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
 * the plugin (e.g., "checkpoint" for SLURM checkpoint) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load checkpoint plugins if the plugin_type string has a 
 * prefix of "checkpoint/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum versions for their plugins as the checkpoint API matures.
 */
const char plugin_name[]       	= "OpenMPI checkpoint plugin";
const char plugin_type[]       	= "checkpoint/ompi";
const uint32_t plugin_version	= 90;

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	/* We can add a pthread here to handle timeout of pending checkpoint
	 * requests. If a CHECK_VACATE request, we can just abort the job.
	 * see checkpoint_aix.c for an example of how to do this. */
	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	return SLURM_SUCCESS;
}

/*
 * The remainder of this file implements the standard SLURM checkpoint API.
 */

extern int slurm_ckpt_op ( uint16_t op, uint16_t data,
		struct step_record * step_ptr, time_t * event_time,
		uint32_t *error_code, char **error_msg )
{
	int rc = SLURM_SUCCESS;
	struct check_job_info *check_ptr;

	xassert(step_ptr);
	check_ptr = (struct check_job_info *) step_ptr->check_job;
	xassert(check_ptr);

	switch (op) {
		case CHECK_ABLE:
			if (check_ptr->disabled)
				rc = ESLURM_DISABLED;
			else {
				if ((check_ptr->reply_cnt < 1) && event_time) {
					/* Return time of last event */
					*event_time = check_ptr->time_stamp;
				}
				rc = SLURM_SUCCESS;
			}
			break;
		case CHECK_DISABLE:
			check_ptr->disabled++;
			break;
		case CHECK_ENABLE:
			check_ptr->disabled--;
			break;
		case CHECK_CREATE:
			check_ptr->time_stamp = time(NULL);
			check_ptr->reply_cnt = 0;
			check_ptr->error_code = 0;
			xfree(check_ptr->error_msg);
			rc = _ckpt_step(step_ptr, data, 0);
			break;
		case CHECK_VACATE:
			check_ptr->time_stamp = time(NULL);
			check_ptr->reply_cnt = 0;
			check_ptr->error_code = 0;
			xfree(check_ptr->error_msg);
			rc = _ckpt_step(step_ptr, data, 1);
			break;
		case CHECK_RESTART:
			/* Lots of work is required in Slurm to restart a
			 * checkpointed job. For now the user can submit a
			 * new job and execute "ompi_restart <snapshot>" */
			rc = ESLURM_NOT_SUPPORTED;
			break;
		case CHECK_ERROR:
			xassert(error_code);
			xassert(error_msg);
			*error_code = check_ptr->error_code;
			xfree(*error_msg);
			*error_msg = xstrdup(check_ptr->error_msg);
			break;
		default:
			error("Invalid checkpoint operation: %d", op);
			rc = EINVAL;
	}

	return rc;
}

extern int slurm_ckpt_comp (struct step_record * step_ptr, time_t event_time,
		uint32_t error_code, char *error_msg)
{
/* FIXME: How do we tell when checkpoint completes? 
 * Add another RPC from srun to slurmctld?
 * Where is this called from? */
	struct check_job_info *check_ptr;
	time_t now;
	long delay;

	xassert(step_ptr);
	check_ptr = (struct check_job_info *) step_ptr->check_job;
	xassert(check_ptr);

	/* We ignore event_time here, just key off reply_cnt */
	if (check_ptr->reply_cnt)
		return ESLURM_ALREADY_DONE;

	if (error_code > check_ptr->error_code) {
		info("slurm_ckpt_comp for step %u.%u error %u: %s", 
			step_ptr->job_ptr->job_id, step_ptr->step_id,
			error_code, error_msg);
		check_ptr->error_code = error_code;
		xfree(check_ptr->error_msg);
		check_ptr->error_msg = xstrdup(error_msg);
		return SLURM_SUCCESS;
	}

	now = time(NULL);
	delay = difftime(now, check_ptr->time_stamp);
	info("slurm_ckpt_comp for step %u.%u in %ld secs: %s",
		step_ptr->job_ptr->job_id, step_ptr->step_id,
		delay, error_msg);
	check_ptr->error_code = error_code;
	xfree(check_ptr->error_msg);
	check_ptr->error_msg = xstrdup(error_msg);
	check_ptr->reply_cnt++;
	check_ptr->time_stamp = now;

	return SLURM_SUCCESS; 
}

extern int slurm_ckpt_alloc_job(check_jobinfo_t *jobinfo)
{
	*jobinfo = (check_jobinfo_t) xmalloc(sizeof(struct check_job_info));
	return SLURM_SUCCESS;
}

extern int slurm_ckpt_free_job(check_jobinfo_t jobinfo)
{
	xfree(jobinfo);
	return SLURM_SUCCESS;
}

extern int slurm_ckpt_pack_job(check_jobinfo_t jobinfo, Buf buffer)
{
	struct check_job_info *check_ptr = 
		(struct check_job_info *)jobinfo;
 
	pack16(check_ptr->disabled, buffer);
	pack16(check_ptr->reply_cnt, buffer);
	pack16(check_ptr->wait_time, buffer);

	pack32(check_ptr->error_code, buffer);
	packstr(check_ptr->error_msg, buffer);
	pack_time(check_ptr->time_stamp, buffer);

	return SLURM_SUCCESS;
}

extern int slurm_ckpt_unpack_job(check_jobinfo_t jobinfo, Buf buffer)
{
	uint32_t uint32_tmp;
	struct check_job_info *check_ptr =
		(struct check_job_info *)jobinfo;

	safe_unpack16(&check_ptr->disabled, buffer);
	safe_unpack16(&check_ptr->reply_cnt, buffer);
	safe_unpack16(&check_ptr->wait_time, buffer);

	safe_unpack32(&check_ptr->error_code, buffer);
	safe_unpackstr_xmalloc(&check_ptr->error_msg, &uint32_tmp, buffer);
	safe_unpack_time(&check_ptr->time_stamp, buffer);
	
	return SLURM_SUCCESS; 

    unpack_error:
	xfree(check_ptr->error_msg);
	return SLURM_ERROR;
}

static int _ckpt_step(struct step_record * step_ptr, uint16_t wait, int vacate)
{
	struct check_job_info *check_ptr;
	struct job_record *job_ptr;
	char *argv[3];

	xassert(step_ptr);
	check_ptr = (struct check_job_info *) step_ptr->check_job;
	xassert(check_ptr);
	job_ptr = step_ptr->job_ptr;
	xassert(job_ptr);

	if (IS_JOB_FINISHED(job_ptr))
		return ESLURM_ALREADY_DONE;

	if (check_ptr->disabled)
		return ESLURM_DISABLED;

	argv[0] = "ompi-checkpoint";
	if (vacate) {
		argv[1] = "--term";
		argv[2] = NULL;
	} else
		argv[1] = NULL;
	srun_exec(step_ptr, argv);
	check_ptr->time_stamp = time(NULL);
	check_ptr->wait_time  = wait;
	info("checkpoint requested for job %u.%u", 
		job_ptr->job_id, step_ptr->step_id);
	return SLURM_SUCCESS;
}

extern int slurm_ckpt_task_comp ( struct step_record * step_ptr, uint32_t task_id,
				  time_t event_time, uint32_t error_code, char *error_msg )
{
	return SLURM_SUCCESS;
}

