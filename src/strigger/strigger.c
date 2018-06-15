/*****************************************************************************\
 *  strigger.c - Manage slurm event triggers
 *****************************************************************************
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) 2010-2016 SchedMD LLC.
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

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"
#include "slurm/slurm.h"

#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/strigger/strigger.h"

static int   _clear_trigger(void);
static int   _get_trigger(void);
static int   _set_trigger(void);
static char *_trig_flags(uint16_t flags);
static int   _trig_offset(uint16_t offset);
static char *_trig_user(uint32_t user_id);

int main(int argc, char **argv)
{
	int rc = 0;
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	log_init("strigger", opts, SYSLOG_FACILITY_DAEMON, NULL);

	slurm_conf_init(NULL);
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

	slurm_init_trigger_msg(&ti);
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
	else if (params.user_id != NO_VAL)
		verbose("triggers for user %u cleared", ti.user_id);
	else
		verbose("trigger %u cleared", ti.trig_id);
	return 0;
}

static int _set_trigger(void)
{
	trigger_info_t ti;
	char tmp_c[128];

	slurm_init_trigger_msg (&ti);
	if (params.job_id) {
		ti.res_type = TRIGGER_RES_TYPE_JOB;
		snprintf(tmp_c, sizeof(tmp_c), "%u", params.job_id);
		ti.res_id = tmp_c;
		if (params.job_fini)
			ti.trig_type |= TRIGGER_TYPE_FINI;
		if (params.time_limit)
			ti.trig_type |= TRIGGER_TYPE_TIME;
	} else if (params.front_end) {
		ti.res_type = TRIGGER_RES_TYPE_FRONT_END;
	} else if (params.burst_buffer) {
		ti.res_type = TRIGGER_RES_TYPE_OTHER;
	} else {
		ti.res_type = TRIGGER_RES_TYPE_NODE;
		if (params.node_id)
			ti.res_id = params.node_id;
		else
			ti.res_id = "*";
	}
	if (params.burst_buffer)
		ti.trig_type |= TRIGGER_TYPE_BURST_BUFFER;
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
	if (params.pri_ctld_fail) {
		ti.trig_type |= TRIGGER_TYPE_PRI_CTLD_FAIL;
		ti.res_type = TRIGGER_RES_TYPE_SLURMCTLD;
	}
	if (params.pri_ctld_res_op) {
		ti.trig_type |= TRIGGER_TYPE_PRI_CTLD_RES_OP;
		ti.res_type = TRIGGER_RES_TYPE_SLURMCTLD;
	}
	if (params.pri_ctld_res_ctrl) {
		ti.trig_type |=  TRIGGER_TYPE_PRI_CTLD_RES_CTRL;
		ti.res_type = TRIGGER_RES_TYPE_SLURMCTLD;
	}
	if (params.pri_ctld_acct_buffer_full) {
		ti.trig_type |= TRIGGER_TYPE_PRI_CTLD_ACCT_FULL;
		ti.res_type = TRIGGER_RES_TYPE_SLURMCTLD;
	}
	if (params.bu_ctld_fail) {
		ti.trig_type |= TRIGGER_TYPE_BU_CTLD_FAIL;
		ti.res_type = TRIGGER_RES_TYPE_SLURMCTLD;
	}
	if (params.bu_ctld_res_op) {
		ti.trig_type |= TRIGGER_TYPE_BU_CTLD_RES_OP;
		ti.res_type = TRIGGER_RES_TYPE_SLURMCTLD;
	}
	if (params.bu_ctld_as_ctrl) {
		ti.trig_type |= TRIGGER_TYPE_BU_CTLD_AS_CTRL;
		ti.res_type = TRIGGER_RES_TYPE_SLURMCTLD;
	}
	if (params.pri_dbd_fail) {
		ti.trig_type |= TRIGGER_TYPE_PRI_DBD_FAIL;
		ti.res_type = TRIGGER_RES_TYPE_SLURMDBD;
	}
	if (params.pri_dbd_res_op) {
		ti.trig_type |= TRIGGER_TYPE_PRI_DBD_RES_OP;
		ti.res_type = TRIGGER_RES_TYPE_SLURMDBD;
	}
	if (params.pri_db_fail) {
		ti.trig_type |= TRIGGER_TYPE_PRI_DB_FAIL;
		ti.res_type = TRIGGER_RES_TYPE_DATABASE;
	}
	if (params.pri_db_res_op) {
		ti.trig_type |= TRIGGER_TYPE_PRI_DB_RES_OP;
		ti.res_type = TRIGGER_RES_TYPE_DATABASE;
	}

	ti.flags   = params.flags;
	ti.offset  = params.offset + 0x8000;
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
		if (params.burst_buffer) {
			if (trig_msg->trigger_array[i].trig_type
					!= TRIGGER_TYPE_BURST_BUFFER)
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
			if (((trig_msg->trigger_array[i].res_type
					!= TRIGGER_RES_TYPE_NODE) &&
			     (trig_msg->trigger_array[i].res_type
					!= TRIGGER_RES_TYPE_FRONT_END)) ||
			    (trig_msg->trigger_array[i].trig_type
					!= TRIGGER_TYPE_DOWN))
				continue;
		}
		if (params.node_drained) {
			if ((trig_msg->trigger_array[i].res_type
					!= TRIGGER_RES_TYPE_NODE) ||
			    (trig_msg->trigger_array[i].trig_type
					!= TRIGGER_TYPE_DRAINED))
				continue;
		}
		if (params.node_fail) {
			if ((trig_msg->trigger_array[i].res_type
					!= TRIGGER_RES_TYPE_NODE) ||
			    (trig_msg->trigger_array[i].trig_type
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
					!= TRIGGER_RES_TYPE_NODE) ||
			    (trig_msg->trigger_array[i].trig_type
					!= TRIGGER_TYPE_IDLE))
				continue;
		}
		if (params.node_up) {
			if (((trig_msg->trigger_array[i].res_type
					!= TRIGGER_RES_TYPE_NODE) &&
			     (trig_msg->trigger_array[i].res_type
					!= TRIGGER_RES_TYPE_FRONT_END)) ||
			    (trig_msg->trigger_array[i].trig_type
					!= TRIGGER_TYPE_UP))
				continue;
		}
		if (params.time_limit) {
			if ((trig_msg->trigger_array[i].res_type
					!= TRIGGER_RES_TYPE_JOB) ||
			    (trig_msg->trigger_array[i].trig_type
					!= TRIGGER_TYPE_TIME))
				continue;
		}
		if (params.trigger_id) {
			if (params.trigger_id !=
			    trig_msg->trigger_array[i].trig_id)
				continue;
		}
		if (params.user_id != NO_VAL) {
			if (params.user_id !=
			    trig_msg->trigger_array[i].user_id)
				continue;
		}
		if (params.pri_ctld_fail) {
			if ((trig_msg->trigger_array[i].res_type !=
			     TRIGGER_RES_TYPE_SLURMCTLD) ||
			    (trig_msg->trigger_array[i].trig_type !=
			     TRIGGER_TYPE_PRI_CTLD_FAIL))
				continue;
		}
		if (params.pri_ctld_res_op) {
			if ((trig_msg->trigger_array[i].res_type !=
			     TRIGGER_RES_TYPE_SLURMCTLD) ||
			    (trig_msg->trigger_array[i].trig_type !=
			     TRIGGER_TYPE_PRI_CTLD_RES_OP))
				continue;
		}
		if (params.pri_ctld_res_ctrl) {
			if ((trig_msg->trigger_array[i].res_type !=
			     TRIGGER_RES_TYPE_SLURMCTLD) ||
			    (trig_msg->trigger_array[i].trig_type !=
			     TRIGGER_TYPE_PRI_CTLD_RES_CTRL))
				continue;
		}
		if (params.pri_ctld_acct_buffer_full) {
			if ((trig_msg->trigger_array[i].res_type !=
			     TRIGGER_RES_TYPE_SLURMCTLD) ||
			    (trig_msg->trigger_array[i].trig_type !=
			     TRIGGER_TYPE_PRI_CTLD_ACCT_FULL))
				continue;
		}
		if (params.bu_ctld_fail) {
			if ((trig_msg->trigger_array[i].res_type !=
			     TRIGGER_RES_TYPE_SLURMCTLD) ||
			    (trig_msg->trigger_array[i].trig_type !=
			     TRIGGER_TYPE_BU_CTLD_FAIL))
				continue;
		}
		if (params.bu_ctld_res_op) {
			if ((trig_msg->trigger_array[i].res_type !=
			     TRIGGER_RES_TYPE_SLURMCTLD) ||
			    (trig_msg->trigger_array[i].trig_type !=
			     TRIGGER_TYPE_BU_CTLD_RES_OP))
				continue;
		}
		if (params.bu_ctld_as_ctrl) {
			if ((trig_msg->trigger_array[i].res_type !=
			     TRIGGER_RES_TYPE_SLURMCTLD) ||
			    (trig_msg->trigger_array[i].trig_type !=
			     TRIGGER_TYPE_BU_CTLD_AS_CTRL))
				continue;
		}
		if (params.pri_dbd_fail) {
			if ((trig_msg->trigger_array[i].res_type !=
			     TRIGGER_RES_TYPE_SLURMDBD) ||
			    (trig_msg->trigger_array[i].trig_type !=
			     TRIGGER_TYPE_PRI_DBD_FAIL))
				continue;
		}
		if (params.pri_dbd_res_op) {
			if ((trig_msg->trigger_array[i].res_type !=
			     TRIGGER_RES_TYPE_SLURMDBD) ||
			    (trig_msg->trigger_array[i].trig_type !=
			     TRIGGER_TYPE_PRI_DBD_RES_OP))
				continue;
		}
		if (params.pri_db_fail) {
			if ((trig_msg->trigger_array[i].res_type !=
			     TRIGGER_RES_TYPE_DATABASE) ||
			    (trig_msg->trigger_array[i].trig_type !=
			     TRIGGER_TYPE_PRI_DB_FAIL))
				continue;
		}
		if (params.pri_db_res_op) {
			if ((trig_msg->trigger_array[i].res_type !=
			     TRIGGER_RES_TYPE_DATABASE) ||
			    (trig_msg->trigger_array[i].trig_type !=
			     TRIGGER_TYPE_PRI_DB_RES_OP))
				continue;
		}

		if ((line_no == 0) && !params.no_header) {
			/*      7777777 999999999 7777777 */
			printf("TRIG_ID RES_TYPE   RES_ID "

			/*      35353535353535353535353535353535353 */
			       "TYPE                                "

			/*      666666 88888888 55555 xxxxxxx */
			       "OFFSET USER     FLAGS PROGRAM\n");
		}
		line_no++;

		printf("%7u %-9s %7s %-35s %6d %-8s %-5s %s\n",
			trig_msg->trigger_array[i].trig_id,
			trigger_res_type(trig_msg->trigger_array[i].res_type),
			trig_msg->trigger_array[i].res_id,
			trigger_type(trig_msg->trigger_array[i].trig_type),
			_trig_offset(trig_msg->trigger_array[i].offset),
			_trig_user(trig_msg->trigger_array[i].user_id),
			_trig_flags(trig_msg->trigger_array[i].flags),
			trig_msg->trigger_array[i].program);
	}

	slurm_free_trigger_msg(trig_msg);
	return 0;
}

static char *_trig_flags(uint16_t flags)
{
	if (flags & TRIGGER_FLAG_PERM)
		return "PERM";
	return "";
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
