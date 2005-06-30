/*****************************************************************************\
 *  checkpoint_aix.c - AIX slurm checkpoint plugin.
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
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

#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/common/log.h"
#include "src/common/pack.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/slurmctld.h"

struct check_job_info {
	uint16_t disabled;
	uint16_t node_cnt;
	uint16_t reply_cnt;
	uint16_t wait_time;
	time_t   time_stamp;	/* begin or end checkpoint time */
	uint32_t error_code;
	char    *error_msg;
};

static void _comp_msg(struct step_record *step_ptr, 
		struct check_job_info *check_ptr);
static int _step_sig(struct step_record * step_ptr, uint16_t wait, int signal);

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
const char plugin_name[]       	= "Checkpoint AIX plugin";
const char plugin_type[]       	= "checkpoint/aix";
const uint32_t plugin_version	= 90;

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
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
				*event_time = check_ptr->time_stamp;
				rc = SLURM_SUCCESS;
			}
			break;
		case CHECK_DISABLE:
			check_ptr->disabled = 1;
			break;
		case CHECK_ENABLE:
			check_ptr->disabled = 0;
			break;
		case CHECK_CREATE:
			check_ptr->time_stamp = time(NULL);
			check_ptr->reply_cnt = 0;
			check_ptr->error_code = 0;
			xfree(check_ptr->error_msg);
#ifdef SIGSOUND
			rc = _step_sig(step_ptr, data, SIGSOUND);
#else
			/* No checkpoint, SIGWINCH for testing purposes */
			info("Checkpoint not supported, sending SIGWINCH");
			rc = _step_sig(step_ptr, data, SIGWINCH);
#endif
			break;
		case CHECK_VACATE:
			check_ptr->time_stamp = time(NULL);
			check_ptr->reply_cnt = 0;
			check_ptr->error_code = 0;
			xfree(check_ptr->error_msg);
#ifdef SIGMIGRATE
			rc = _step_sig(step_ptr, data, SIGMIGRATE);
#else
			/* No checkpoint, kill job now, useful for testing */
			info("Checkpoint not supported, sending SIGTERM");
			rc = _step_sig(step_ptr, data, SIGTERM);
#endif
			break;
		case CHECK_RESTART:
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

extern int slurm_ckpt_comp ( struct step_record * step_ptr, time_t event_time,
		uint32_t error_code, char *error_msg )
{
	struct check_job_info *check_ptr;

	xassert(step_ptr);
	check_ptr = (struct check_job_info *) step_ptr->check_job;
	xassert(check_ptr);

	if (event_time && (event_time != check_ptr->time_stamp))
		return ESLURM_ALREADY_DONE;

	if (error_code > check_ptr->error_code) {
		info("slurm_ckpt_comp error %u: %s", error_code, error_msg);
		check_ptr->error_code = error_code;
		xfree(check_ptr->error_msg);
		check_ptr->error_msg = xstrdup(error_msg);
	}

	if (++check_ptr->reply_cnt == check_ptr->node_cnt) {
		info("Checkpoint complete for job %u.%u",
			step_ptr->job_ptr->job_id, step_ptr->step_id);
		check_ptr->time_stamp = time(NULL);
	}

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
	pack16(check_ptr->node_cnt, buffer);
	pack16(check_ptr->reply_cnt, buffer);
	pack16(check_ptr->wait_time, buffer);

	pack32(check_ptr->error_code, buffer);
	packstr(check_ptr->error_msg, buffer);
	pack_time(check_ptr->time_stamp, buffer);

	return SLURM_SUCCESS;
}

extern int slurm_ckpt_unpack_job(check_jobinfo_t jobinfo, Buf buffer)
{
	uint16_t uint16_tmp;
	struct check_job_info *check_ptr =
		(struct check_job_info *)jobinfo;

	safe_unpack16(&check_ptr->disabled, buffer);
	safe_unpack16(&check_ptr->node_cnt, buffer);
	safe_unpack16(&check_ptr->reply_cnt, buffer);
	safe_unpack16(&check_ptr->wait_time, buffer);

	safe_unpack32(&check_ptr->error_code, buffer);
	safe_unpackstr_xmalloc(&check_ptr->error_msg, &uint16_tmp, buffer);
	safe_unpack_time(&check_ptr->time_stamp, buffer);
	
	return SLURM_SUCCESS; 

    unpack_error:
	xfree(check_ptr->error_msg);
	return SLURM_ERROR;
}

/* Send specified signal only to the process launched on node 0 */
static int _step_sig(struct step_record * step_ptr, uint16_t wait, int signal)
{
	struct check_job_info *check_ptr;
	struct job_record *job_ptr;
	agent_arg_t *agent_args = NULL;
	kill_tasks_msg_t *kill_tasks_msg;
	int i;

	xassert(step_ptr);
	check_ptr = (struct check_job_info *) step_ptr->check_job;
	xassert(check_ptr);
	job_ptr = step_ptr->job_ptr;
	xassert(job_ptr);

	if (IS_JOB_FINISHED(job_ptr))
		return ESLURM_ALREADY_DONE;

	if (check_ptr->disabled)
		return ESLURM_DISABLED;

	check_ptr->node_cnt = 0;	/* re-calculate below */
	for (i = 0; i < node_record_count; i++) {
		if (bit_test(step_ptr->step_node_bitmap, i) == 0)
			continue;
		if (check_ptr->node_cnt++ > 0)
			continue;
		kill_tasks_msg = xmalloc(sizeof(kill_tasks_msg_t));
		kill_tasks_msg->job_id      = step_ptr->job_ptr->job_id;
		kill_tasks_msg->job_step_id = step_ptr->step_id;
		kill_tasks_msg->signal      = signal;
		agent_args = xmalloc(sizeof(agent_arg_t));
		agent_args->msg_type = REQUEST_KILL_TASKS;
		agent_args->retry = 1;
		agent_args->msg_args = kill_tasks_msg;
		agent_args->slurm_addr = xmalloc(sizeof(struct sockaddr_in));
		agent_args->node_names = xmalloc(MAX_NAME_LEN);
		agent_args->slurm_addr[0] = node_record_table_ptr[i].slurm_addr;
		strncpy(&agent_args->node_names[0], 
			node_record_table_ptr[i].name, MAX_NAME_LEN);
		agent_args->node_count++;
	}

	if (agent_args == NULL) {
		error("_step_sig: job %u.%u has no nodes", job_ptr->job_id,
			step_ptr->step_id);
		return ESLURM_INVALID_NODE_NAME;
	}

	agent_queue_request(agent_args);
	check_ptr->time_stamp = time(NULL);
	check_ptr->wait_time = wait;
	info("checkpoint requested for job %u.%u", job_ptr->job_id,
		step_ptr->step_id);
	return SLURM_SUCCESS;
}

static void _comp_msg(struct step_record *step_ptr, 
		struct check_job_info *check_ptr)
{
	long delay = (long) difftime(time(NULL), check_ptr->time_stamp);
	info("checkpoint done for job %u.%u, secs %ld errno %d", 
		step_ptr->job_ptr->job_id, step_ptr->step_id, 
		delay, check_ptr->error_code);
}
