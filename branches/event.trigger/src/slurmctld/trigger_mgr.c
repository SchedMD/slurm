/*****************************************************************************\
 *  trigger_mgr.c - Event trigger management
 *****************************************************************************
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
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

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif

#include "src/slurmctld/trigger_mgr.h"

#define _DEBUG 1

static char *_res_type(uint8_t  res_type)
{
	if      (res_type == TRIGGER_RES_TYPE_JOB)
		return "job";
	else if (res_type == TRIGGER_RES_TYPE_NODE)
		return "node";
	else
		return "unknown";
}

static char *_trig_type(uint8_t  trig_type)
{
	if      (trig_type == TRIGGER_TYPE_UP)
		return "up";
	else if (trig_type == TRIGGER_TYPE_DOWN)
		return "down";
	else if (trig_type == TRIGGER_TYPE_TIME)
		return "time";
	else if (trig_type == TRIGGER_TYPE_FINI)
		return "fini";
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

static void _dump_trigger_msg(char *header, trigger_info_msg_t *msg)
{
#if _DEBUG
	int i;

	info(header);
	if ((msg == NULL) || (msg->record_count == 0)) {
		info("Trigger has no entries");
		return;
	}

	info("INDEX TRIG_ID RES_TYPE RES_ID TRIG_TYPE OFFSET UID PROGRAM");
	for (i=0; i<msg->record_count; i++) {
		info("Trigger[%d] %u %s %s %s %d %u %s", i,
			msg->trigger_array[i].trig_id,
			_res_type(msg->trigger_array[i].res_type),
			msg->trigger_array[i].res_id,
			_trig_type(msg->trigger_array[i].trig_type),
			_trig_offset(msg->trigger_array[i].offset),
			msg->trigger_array[i].user_id,
			msg->trigger_array[i].program);
	}
#endif
}

extern int trigger_clear(uid_t uid, trigger_info_msg_t *msg)
{
	_dump_trigger_msg("trigger_clear", msg);
	return SLURM_SUCCESS;
}

extern int trigger_get(uid_t uid, slurm_fd conn_fd)
{
	_dump_trigger_msg("trigger_get", NULL);
	return SLURM_SUCCESS;
}

extern int trigger_set(uid_t uid, trigger_info_msg_t *msg)
{
	int i;

	for (i=0; i<msg->record_count; i++)
		msg->trigger_array[i].user_id = (uint32_t) uid)

	_dump_trigger_msg("trigger_set", msg);
	return SLURM_SUCCESS;
}
