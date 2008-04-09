/*****************************************************************************\
 *  strigger.c - Manage slurm event triggers
 *****************************************************************************
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <slurm/slurm_errno.h>
#include <slurm/slurm.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_STRINGS_H
#  include <strings.h>
#endif

#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/strigger/strigger.h"

static int   _clear_trigger(void);
static int   _get_trigger(void);
static char *_res_type(uint8_t  res_type);
static int   _set_trigger(void);
static int   _trig_offset(uint16_t offset);
static char *_trig_type(uint16_t trig_type);
static char *_trig_user(uint32_t user_id);

int main(int argc, char *argv[])
{
	int rc = 0;
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	log_init("strigger", opts, SYSLOG_FACILITY_DAEMON, NULL);

	parse_command_line(argc, argv);
	if (params.verbose) {
		opts.stderr_level += params.verbose;
		log_alter(opts, SYSLOG_FACILITY_DAEMON, NULL);
	}

	if      (params.mode_set)
		rc = _set_trigger();
	else if (params.mode_get)
		rc = _get_trigger();
	else if (params.mode_clear)
		rc = _clear_trigger();
	else {
		error("Invalid mode");
		rc = 1;
	}

	exit(rc);
}

static int _clear_trigger(void)
{
	trigger_info_t ti;
	char tmp_c[128];

	bzero(&ti, sizeof(trigger_info_t));
	ti.trig_id	= params.trigger_id;
	ti.user_id	= params.user_id;
	if (params.job_id) {
		ti.res_type = TRIGGER_RES_TYPE_JOB;
		snprintf(tmp_c, sizeof(tmp_c), "%u", params.job_id);
		ti.res_id   = tmp_c;
	}
	if (slurm_clear_trigger(&ti)) {
		if (!params.quiet) {
			slurm_perror("slurm_clear_trigger");
			return 1;
		}
		return 0;
	}

	if (params.job_id)
		verbose("triggers for job %s cleared", ti.res_id);
	else if (params.user_id)
		verbose("triggers for user %u cleared", ti.user_id);
	else
		verbose("trigger %u cleared", ti.trig_id);
	return 0;
}

static int _set_trigger(void)
{
	trigger_info_t ti;
	char tmp_c[128];

	bzero(&ti, sizeof(trigger_info_t));
	if (params.job_id) {
		ti.res_type = TRIGGER_RES_TYPE_JOB;
		snprintf(tmp_c, sizeof(tmp_c), "%u", params.job_id);
		ti.res_id = tmp_c;
		if (params.job_fini)
			ti.trig_type |= TRIGGER_TYPE_FINI;
		if (params.time_limit)
			ti.trig_type |= TRIGGER_TYPE_TIME;
	} else {
		ti.res_type = TRIGGER_RES_TYPE_NODE;
		if (params.node_id)
			ti.res_id = params.node_id;
		else
			ti.res_id = "*";
	}
	if (params.block_err)
		ti.trig_type |= TRIGGER_TYPE_BLOCK_ERR;
	if (params.node_down)
		ti.trig_type |= TRIGGER_TYPE_DOWN;
	if (params.node_drained)
		ti.trig_type |= TRIGGER_TYPE_DRAINED;
	if (params.node_fail)
		ti.trig_type |= TRIGGER_TYPE_FAIL;
	if (params.node_idle)
		ti.trig_type |= TRIGGER_TYPE_IDLE;
	if (params.node_up)
		ti.trig_type |= TRIGGER_TYPE_UP;
	if (params.reconfig)
		ti.trig_type |= TRIGGER_TYPE_RECONFIG;

	ti.offset = params.offset + 0x8000;
	ti.program = params.program;

	while (slurm_set_trigger(&ti)) {
		slurm_perror("slurm_set_trigger");
		if (slurm_get_errno() != EAGAIN)
			return 1;
		sleep(5);
	}


	verbose("trigger set");
	return 0;
}

static int _get_trigger(void)
{
	trigger_info_msg_t * trig_msg;
	int line_no = 0, i;

	if (slurm_get_triggers(&trig_msg)) {
		slurm_perror("slurm_get_triggers");
		return 1;
	}
	verbose("Read %u trigger records", trig_msg->record_count);

	for (i=0; i<trig_msg->record_count; i++) {
		/* perform filtering */
		if (params.block_err) {
			if (trig_msg->trigger_array[i].trig_type 
					!= TRIGGER_TYPE_BLOCK_ERR)
				continue;
		}
		if (params.job_fini) {
			if (trig_msg->trigger_array[i].trig_type 
					!= TRIGGER_TYPE_FINI)
				continue;
		}
		if (params.job_id) {
			long jid;
			if (trig_msg->trigger_array[i].res_type 
					!= TRIGGER_RES_TYPE_JOB)
				continue;
			jid = atol(trig_msg->trigger_array[i].res_id);
			if (jid != params.job_id)
				continue;
		}
		if (params.node_down) {
			if ((trig_msg->trigger_array[i].res_type  
					!= TRIGGER_RES_TYPE_NODE)
			||  (trig_msg->trigger_array[i].trig_type 
					!= TRIGGER_TYPE_DOWN))
				continue;
		}
		if (params.node_drained) {
			if ((trig_msg->trigger_array[i].res_type  
					!= TRIGGER_RES_TYPE_NODE)
			||  (trig_msg->trigger_array[i].trig_type 
					!= TRIGGER_TYPE_DRAINED))
				continue;
		}
		if (params.node_fail) {
			if ((trig_msg->trigger_array[i].res_type  
					!= TRIGGER_RES_TYPE_NODE)
			||  (trig_msg->trigger_array[i].trig_type 
					!= TRIGGER_TYPE_FAIL))
				continue;
		}
		if (params.node_id) {
			if (trig_msg->trigger_array[i].res_type  
					!= TRIGGER_RES_TYPE_NODE)
				continue;
		}
		if (params.node_idle) {
			if ((trig_msg->trigger_array[i].res_type
					!= TRIGGER_RES_TYPE_NODE)
			||  (trig_msg->trigger_array[i].trig_type
					!= TRIGGER_TYPE_IDLE))
				continue;
		}
		if (params.node_up) {
			if ((trig_msg->trigger_array[i].res_type 
					!= TRIGGER_RES_TYPE_NODE)
			||  (trig_msg->trigger_array[i].trig_type 
					!= TRIGGER_TYPE_UP))
				continue;
		}
		if (params.time_limit) {
			if ((trig_msg->trigger_array[i].res_type  
					!= TRIGGER_RES_TYPE_JOB)
			||  (trig_msg->trigger_array[i].trig_type 
					!= TRIGGER_TYPE_TIME))
				continue;
		}
		if (params.trigger_id) {
			if (params.trigger_id != 
			    trig_msg->trigger_array[i].trig_id)
				continue;
		}
		if (params.user_id) {
			if (params.user_id !=
			    trig_msg->trigger_array[i].user_id)
				continue;
		}

		if (line_no == 0) {
			/*      7777777 88888888 7777777 999999999 666666 88888888 xxxxxxx */
			printf("TRIG_ID RES_TYPE  RES_ID TYPE      OFFSET USER     PROGRAM\n");
		}
		line_no++;

		printf("%7u %-8s %7s %-9s %6d %-8s %s\n",
			trig_msg->trigger_array[i].trig_id,
			_res_type(trig_msg->trigger_array[i].res_type),
			trig_msg->trigger_array[i].res_id,
			_trig_type(trig_msg->trigger_array[i].trig_type),
			_trig_offset(trig_msg->trigger_array[i].offset),
			_trig_user(trig_msg->trigger_array[i].user_id),
			trig_msg->trigger_array[i].program);
	}

	slurm_free_trigger_msg(trig_msg);
	return 0;
}

static char *_res_type(uint8_t res_type)
{
	if      (res_type == TRIGGER_RES_TYPE_JOB)
		return "job";
	else if (res_type == TRIGGER_RES_TYPE_NODE)
		return "node";
	else
		return "unknown";
}

static char *_trig_type(uint16_t trig_type)
{
	if      (trig_type == TRIGGER_TYPE_UP)
		return "up";
	else if (trig_type == TRIGGER_TYPE_DOWN)
		return "down";
	else if (trig_type == TRIGGER_TYPE_DRAINED)
		return "drained";
	else if (trig_type == TRIGGER_TYPE_FAIL)
		return "fail";
	else if (trig_type == TRIGGER_TYPE_IDLE)
		return "idle";
	else if (trig_type == TRIGGER_TYPE_TIME)
		return "time";
	else if (trig_type == TRIGGER_TYPE_FINI)
		return "fini";
	else if (trig_type == TRIGGER_TYPE_RECONFIG)
		return "reconfig";
	else if (trig_type == TRIGGER_TYPE_BLOCK_ERR)
		return "block_err";
	else
		return "unknown";
}

static int _trig_offset(uint16_t offset)
{
	static int rc;
	rc  = offset;
	rc -= 0x8000;
	return rc;
}

static char *_trig_user(uint32_t user_id)
{
	uid_t uid = user_id;
	struct passwd *pw;

	pw = getpwuid(uid);
	if (pw == NULL)
		return "unknown";
	return pw->pw_name;
}
