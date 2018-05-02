/*****************************************************************************\
 *  trigger_mgr.c - Event trigger management
 *****************************************************************************
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010-2016 SchedMD <https://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "src/common/bitstring.h"
#include "src/common/list.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/state_save.h"
#include "src/slurmctld/trigger_mgr.h"

#define MAX_PROG_TIME 300	/* maximum run time for program */

/* Change TRIGGER_STATE_VERSION value when changing the state save format */
#define TRIGGER_STATE_VERSION        "PROTOCOL_VERSION"

List trigger_list;
uint32_t next_trigger_id = 1;
static pthread_mutex_t trigger_mutex = PTHREAD_MUTEX_INITIALIZER;
bitstr_t *trigger_down_front_end_bitmap = NULL;
bitstr_t *trigger_up_front_end_bitmap = NULL;
bitstr_t *trigger_down_nodes_bitmap = NULL;
bitstr_t *trigger_drained_nodes_bitmap = NULL;
bitstr_t *trigger_fail_nodes_bitmap = NULL;
bitstr_t *trigger_up_nodes_bitmap   = NULL;
static bool trigger_bb_error = false;
static bool trigger_block_err = false;
static bool trigger_node_reconfig = false;
static bool trigger_pri_ctld_fail = false;
static bool trigger_pri_ctld_res_op = false;
static bool trigger_pri_ctld_res_ctrl = false;
static bool trigger_pri_ctld_acct_buffer_full = false;
static bool trigger_bu_ctld_fail = false;
static bool trigger_bu_ctld_res_op = false;
static bool trigger_bu_ctld_as_ctrl = false;
static bool trigger_pri_dbd_fail = false;
static bool trigger_pri_dbd_res_op = false;
static bool trigger_pri_db_fail = false;
static bool trigger_pri_db_res_op = false;

/* Current trigger pull states (saved and restored) */
uint8_t ctld_failure = 0;
uint8_t bu_ctld_failure = 0;
uint8_t db_failure = 0;
uint8_t dbd_failure = 0;

typedef struct trig_mgr_info {
	uint32_t child_pid;	/* pid of child process */
	uint16_t flags;		/* TRIGGER_FLAG_* */
	uint32_t trig_id;	/* trigger ID */
	uint16_t res_type;	/* TRIGGER_RES_TYPE_* */
	char *   res_id;	/* node name or job_id (string) */
	bitstr_t *nodes_bitmap;	/* bitmap of requested nodes (if applicable) */
	uint32_t job_id;	/* job ID (if applicable) */
	struct job_record *job_ptr; /* pointer to job record (if applicable) */
	uint32_t trig_type;	/* TRIGGER_TYPE_* */
	time_t   trig_time;	/* offset (pending) or time stamp (complete) */
	uint32_t user_id;	/* user requesting trigger */
	uint32_t group_id;	/* user's group id */
	char *   program;	/* program to execute */
	uint8_t  state;		/* 0=pending, 1=pulled, 2=completed */

	/* The orig_ fields are used to save  and clone the orignal values */
	bitstr_t *orig_bitmap;	/* bitmap of requested nodes (if applicable) */
	char *   orig_res_id;	/* original node name or job_id (string) */
	time_t   orig_time;	/* offset (pending) or time stamp (complete) */
} trig_mgr_info_t;

/* Prototype for ListDelF */
void _trig_del(void *x) {
	trig_mgr_info_t * tmp = (trig_mgr_info_t *) x;
	xfree(tmp->res_id);
	xfree(tmp->orig_res_id);
	xfree(tmp->program);
	FREE_NULL_BITMAP(tmp->nodes_bitmap);
	FREE_NULL_BITMAP(tmp->orig_bitmap);
	xfree(tmp);
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
	int i;

	if ((slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS) == 0)
		return;

	info("%s", header);
	if ((msg == NULL) || (msg->record_count == 0)) {
		info("Trigger has no entries");
		return;
	}

	info("INDEX TRIG_ID RES_TYPE RES_ID TRIG_TYPE OFFSET UID PROGRAM");
	for (i=0; i<msg->record_count; i++) {
		info("trigger[%u] %u %s %s %s %d %u %s", i,
		     msg->trigger_array[i].trig_id,
		     trigger_res_type(msg->trigger_array[i].res_type),
		     msg->trigger_array[i].res_id,
		     trigger_type(msg->trigger_array[i].trig_type),
		     _trig_offset(msg->trigger_array[i].offset),
		     msg->trigger_array[i].user_id,
		     msg->trigger_array[i].program);
	}
}

/* Validate trigger program */
static bool _validate_trigger(trig_mgr_info_t *trig_in)
{
	struct stat buf;
	int i, modes;
	char *program = xstrdup(trig_in->program);

	for (i = 0; program[i]; i++) {
		if (isspace(program[i])) {
			program[i] = '\0';
			break;
		}
	}

	if (stat(program, &buf) != 0) {
		info("trigger program %s not found", trig_in->program);
		xfree(program);
		return false;
	}
	xfree(program);

	if (!S_ISREG(buf.st_mode)) {
		info("trigger program %s not a regular file", trig_in->program);
		return false;
	}
	if (buf.st_uid == trig_in->user_id)
		modes =  (buf.st_mode >> 6) & 07;
	else if (buf.st_gid == trig_in->group_id)
		modes =  (buf.st_mode >> 3) & 07;
	else
		modes = buf.st_mode  & 07;
	if (modes & 01)
		return true;

	info("trigger program %s not executable", trig_in->program);
	return false;
}


extern int trigger_pull(trigger_info_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	ListIterator trig_iter;
	trigger_info_t *trig_in;
	trig_mgr_info_t *trig_test;

	if (trigger_list == NULL) {
		trigger_list = list_create(_trig_del);
	}

	/* validate the request, designated trigger must be set */
	_dump_trigger_msg("trigger_pull", msg);
	if (msg->record_count != 1)
		return ESRCH;
	trig_in = msg->trigger_array;

	if ((trig_in->res_type != TRIGGER_RES_TYPE_SLURMCTLD) &&
	    (trig_in->res_type != TRIGGER_RES_TYPE_SLURMDBD)  &&
	    (trig_in->res_type != TRIGGER_RES_TYPE_DATABASE)) {
		return EINVAL;
	}

	/* now look for a valid request */
	trig_iter = list_iterator_create(trigger_list);
	while ((trig_test = list_next(trig_iter))) {
		if ((trig_test->res_type  == trig_in->res_type) &&
   		    (trig_test->trig_type == trig_in->trig_type)) {
			switch(trig_test->trig_type) {
			case TRIGGER_TYPE_PRI_CTLD_ACCT_FULL:
				trigger_primary_ctld_acct_full();
				break;
			case TRIGGER_TYPE_BU_CTLD_FAIL:
				trigger_backup_ctld_fail();
				break;
			case TRIGGER_TYPE_BU_CTLD_RES_OP:
				trigger_backup_ctld_res_op();
				break;
			case TRIGGER_TYPE_BU_CTLD_AS_CTRL:
				trigger_backup_ctld_as_ctrl();
				break;
			case TRIGGER_TYPE_PRI_DBD_FAIL:
				trigger_primary_dbd_fail();
				break;
			case TRIGGER_TYPE_PRI_DBD_RES_OP:
				trigger_primary_dbd_res_op();
				break;
			case TRIGGER_TYPE_PRI_DB_FAIL:
				trigger_primary_db_fail();
				break;
			case TRIGGER_TYPE_PRI_DB_RES_OP:
				trigger_primary_db_res_op();
				break;
			default:
				error("trigger_pull call has invalid type: %u",
				      trig_test->trig_type);
				rc = EINVAL;
				break;
			}
		}
	}
	list_iterator_destroy(trig_iter);

	return rc;
}

extern int trigger_clear(uid_t uid, trigger_info_msg_t *msg)
{
	int rc = ESRCH;
	ListIterator trig_iter;
	trigger_info_t *trig_in;
	trig_mgr_info_t *trig_test;
	uint32_t job_id = 0;

	slurm_mutex_lock(&trigger_mutex);
	if (trigger_list == NULL)
		trigger_list = list_create(_trig_del);

	/* validate the request, need a job_id and/or trigger_id */
	_dump_trigger_msg("trigger_clear", msg);
	if (msg->record_count != 1)
		goto fini;
	trig_in = msg->trigger_array;
	if (trig_in->res_type == TRIGGER_RES_TYPE_JOB) {
		job_id = (uint32_t) atol(trig_in->res_id);
		if (job_id == 0) {
			rc = ESLURM_INVALID_JOB_ID;
			goto fini;
		}
	} else if ((trig_in->trig_id == 0) && (trig_in->user_id == NO_VAL)) {
		rc = EINVAL;
		goto fini;
	}

	/* now look for a valid request, matching uid */
	trig_iter = list_iterator_create(trigger_list);
	while ((trig_test = list_next(trig_iter))) {
		if (trig_in->trig_id &&
		    (trig_in->trig_id != trig_test->trig_id))
			continue;
		if (job_id && (job_id != trig_test->job_id))
			continue;
		if ((trig_in->user_id != NO_VAL) &&
		    (trig_in->user_id != trig_test->user_id))
			continue;
		if (trig_test->state == 2)	/* wait for proc termination */
			continue;
		if ((trig_test->user_id != (uint32_t) uid) && (uid != 0)) {
			rc = ESLURM_ACCESS_DENIED;
			continue;
		}
		list_delete_item(trig_iter);
		rc = SLURM_SUCCESS;
	}
	list_iterator_destroy(trig_iter);
	schedule_trigger_save();

fini:	slurm_mutex_unlock(&trigger_mutex);
	return rc;
}

extern trigger_info_msg_t * trigger_get(uid_t uid, trigger_info_msg_t *msg)
{
	trigger_info_msg_t *resp_data;
	ListIterator trig_iter;
	trigger_info_t *trig_out;
	trig_mgr_info_t *trig_in;
	int recs_written = 0;

	slurm_mutex_lock(&trigger_mutex);
	if (trigger_list == NULL)
		trigger_list = list_create(_trig_del);

	_dump_trigger_msg("trigger_get", NULL);
	resp_data = xmalloc(sizeof(trigger_info_msg_t));
	resp_data->record_count = list_count(trigger_list);
	resp_data->trigger_array = xmalloc(sizeof(trigger_info_t) *
					   resp_data->record_count);
	trig_iter = list_iterator_create(trigger_list);
	trig_out = resp_data->trigger_array;
	while ((trig_in = list_next(trig_iter))) {
		/* Note: Filtering currently done by strigger */
		if ((trig_in->state >= 1) &&
		    ((trig_out->flags & TRIGGER_FLAG_PERM) == 0))
			continue;	/* no longer pending */
		trig_out->flags     = trig_in->flags;
		trig_out->trig_id   = trig_in->trig_id;
		trig_out->res_type  = trig_in->res_type;
		trig_out->res_id    = xstrdup(trig_in->res_id);
		trig_out->trig_type = trig_in->trig_type;
		trig_out->offset    = trig_in->trig_time;
		trig_out->user_id   = trig_in->user_id;
		trig_out->program   = xstrdup(trig_in->program);
		trig_out++;
		recs_written++;
	}
	list_iterator_destroy(trig_iter);
	slurm_mutex_unlock(&trigger_mutex);
	resp_data->record_count = recs_written;

	_dump_trigger_msg("trigger_got", resp_data);
	return resp_data;
}

static bool _duplicate_trigger(trigger_info_t *trig_desc)
{
	bool found_dup = false;
	ListIterator trig_iter;
	trig_mgr_info_t *trig_rec;

	trig_iter = list_iterator_create(trigger_list);
	while ((trig_rec = list_next(trig_iter))) {
		if ((trig_desc->flags     == trig_rec->flags)      &&
		    (trig_desc->res_type  == trig_rec->res_type)   &&
		    (trig_desc->trig_type == trig_rec->trig_type)  &&
		    (trig_desc->offset    == trig_rec->trig_time)  &&
		    (trig_desc->user_id   == trig_rec->user_id)    &&
		    !xstrcmp(trig_desc->program, trig_rec->program) &&
		    !xstrcmp(trig_desc->res_id, trig_rec->res_id)) {
			found_dup = true;
			break;
		}
	}
	list_iterator_destroy(trig_iter);
	return found_dup;
}

extern int trigger_set(uid_t uid, gid_t gid, trigger_info_msg_t *msg)
{
	int i;
	int rc = SLURM_SUCCESS;
	uint32_t job_id;
	bitstr_t *bitmap = NULL;
	trig_mgr_info_t * trig_add;
	struct job_record *job_ptr;
	/* Read config and job info */
	slurmctld_lock_t job_read_lock =
		{ READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	lock_slurmctld(job_read_lock);
	slurm_mutex_lock(&trigger_mutex);

	if ((slurmctld_conf.slurm_user_id != 0) &&
	    (slurmctld_conf.slurm_user_id != uid)) {
		/* If SlurmUser is not root, then it is unable to set the
		 * appropriate user id and group id for the program to be
		 * launched. To prevent the launched program for an arbitrary
		 * user being executed as user SlurmUser, disable all other
		 * users from setting triggers. */
		info("Attempt to set trigger by uid %u != SlurmUser", uid);
		rc = ESLURM_ACCESS_DENIED;
		goto fini;
	}

	if (trigger_list == NULL) {
		trigger_list = list_create(_trig_del);
	} else if ((uid != 0) &&
		   (list_count(trigger_list) >= slurmctld_conf.max_job_cnt)) {
		rc = EAGAIN;
		goto fini;
	}

	_dump_trigger_msg("trigger_set", msg);
	for (i = 0; i < msg->record_count; i++) {
		if (msg->trigger_array[i].res_type ==
		    TRIGGER_RES_TYPE_JOB) {
			job_id = (uint32_t) atol(
				 msg->trigger_array[i].res_id);
			job_ptr = find_job_record(job_id);
			if (job_ptr == NULL) {
				rc = ESLURM_INVALID_JOB_ID;
				continue;
			}
			if (IS_JOB_FINISHED(job_ptr)) {
				rc = ESLURM_ALREADY_DONE;
				continue;
			}
		} else {
			job_id = 0;
			job_ptr = NULL;
			if ((msg->trigger_array[i].res_id != NULL)   &&
			    (msg->trigger_array[i].res_id[0] != '*') &&
			    (node_name2bitmap(msg->trigger_array[i].res_id,
					      false, &bitmap) != 0)) {
				FREE_NULL_BITMAP(bitmap);
				rc = ESLURM_INVALID_NODE_NAME;
				continue;
			}
		}
		msg->trigger_array[i].user_id = (uint32_t) uid;
		if (_duplicate_trigger(&msg->trigger_array[i])) {
			FREE_NULL_BITMAP(bitmap);
			rc = ESLURM_TRIGGER_DUP;
			continue;
		}
		trig_add = xmalloc(sizeof(trig_mgr_info_t));
		msg->trigger_array[i].trig_id = next_trigger_id;
		trig_add->trig_id = next_trigger_id;
		next_trigger_id++;
		trig_add->flags = msg->trigger_array[i].flags;
		trig_add->res_type = msg->trigger_array[i].res_type;
		if (bitmap) {
			trig_add->nodes_bitmap = bitmap;
			trig_add->orig_bitmap  = bit_copy(bitmap);
		}
		trig_add->job_id = job_id;
		trig_add->job_ptr = job_ptr;
		if (msg->trigger_array[i].res_id) {
			trig_add->res_id = msg->trigger_array[i].res_id;
			trig_add->orig_res_id = xstrdup(trig_add->res_id);
			msg->trigger_array[i].res_id = NULL; /* moved */
		}
		trig_add->trig_type = msg->trigger_array[i].trig_type;
		trig_add->trig_time = msg->trigger_array[i].offset;
		trig_add->orig_time = msg->trigger_array[i].offset;
		trig_add->user_id   = msg->trigger_array[i].user_id;
		trig_add->group_id  = (uint32_t) gid;
		/* move don't copy "program" */
		trig_add->program = msg->trigger_array[i].program;
		msg->trigger_array[i].program = NULL;
		if (!_validate_trigger(trig_add)) {
			rc = ESLURM_ACCESS_DENIED;
			FREE_NULL_BITMAP(trig_add->nodes_bitmap);
			FREE_NULL_BITMAP(trig_add->orig_bitmap);
			xfree(trig_add->program);
			xfree(trig_add->res_id);
			xfree(trig_add);
			continue;
		}
		list_append(trigger_list, trig_add);
		schedule_trigger_save();
	}

fini:	slurm_mutex_unlock(&trigger_mutex);
	unlock_slurmctld(job_read_lock);
	return rc;
}

extern void trigger_front_end_down(front_end_record_t *front_end_ptr)
{
	int inx = front_end_ptr - front_end_nodes;

	slurm_mutex_lock(&trigger_mutex);
	if (trigger_down_front_end_bitmap == NULL)
		trigger_down_front_end_bitmap = bit_alloc(front_end_node_cnt);
	bit_set(trigger_down_front_end_bitmap, inx);
	slurm_mutex_unlock(&trigger_mutex);
}

extern void trigger_front_end_up(front_end_record_t *front_end_ptr)
{
	int inx = front_end_ptr - front_end_nodes;

	slurm_mutex_lock(&trigger_mutex);
	if (trigger_up_front_end_bitmap == NULL)
		trigger_up_front_end_bitmap = bit_alloc(front_end_node_cnt);
	bit_set(trigger_up_front_end_bitmap, inx);
	slurm_mutex_unlock(&trigger_mutex);
}

extern void trigger_node_down(struct node_record *node_ptr)
{
	int inx = node_ptr - node_record_table_ptr;

	slurm_mutex_lock(&trigger_mutex);
	if (trigger_down_nodes_bitmap == NULL)
		trigger_down_nodes_bitmap = bit_alloc(node_record_count);
	bit_set(trigger_down_nodes_bitmap, inx);
	slurm_mutex_unlock(&trigger_mutex);
}

extern void trigger_node_drained(struct node_record *node_ptr)
{
	int inx = node_ptr - node_record_table_ptr;

	slurm_mutex_lock(&trigger_mutex);
	if (trigger_drained_nodes_bitmap == NULL)
		trigger_drained_nodes_bitmap = bit_alloc(node_record_count);
	bit_set(trigger_drained_nodes_bitmap, inx);
	slurm_mutex_unlock(&trigger_mutex);
}

extern void trigger_node_failing(struct node_record *node_ptr)
{
	int inx = node_ptr - node_record_table_ptr;

	slurm_mutex_lock(&trigger_mutex);
	if (trigger_fail_nodes_bitmap == NULL)
		trigger_fail_nodes_bitmap = bit_alloc(node_record_count);
	bit_set(trigger_fail_nodes_bitmap, inx);
	slurm_mutex_unlock(&trigger_mutex);
}


extern void trigger_node_up(struct node_record *node_ptr)
{
	int inx = node_ptr - node_record_table_ptr;

	slurm_mutex_lock(&trigger_mutex);
	if (trigger_up_nodes_bitmap == NULL)
		trigger_up_nodes_bitmap = bit_alloc(node_record_count);
	bit_set(trigger_up_nodes_bitmap, inx);
	slurm_mutex_unlock(&trigger_mutex);
}

extern void trigger_reconfig(void)
{
	slurm_mutex_lock(&trigger_mutex);
	trigger_node_reconfig = true;
	if (trigger_down_front_end_bitmap)
		trigger_down_front_end_bitmap = bit_realloc(
			trigger_down_front_end_bitmap, node_record_count);
	if (trigger_up_front_end_bitmap)
		trigger_up_front_end_bitmap = bit_realloc(
			trigger_up_front_end_bitmap, node_record_count);
	if (trigger_down_nodes_bitmap)
		trigger_down_nodes_bitmap = bit_realloc(
			trigger_down_nodes_bitmap, node_record_count);
	if (trigger_drained_nodes_bitmap)
		trigger_drained_nodes_bitmap = bit_realloc(
			trigger_drained_nodes_bitmap, node_record_count);
	if (trigger_fail_nodes_bitmap)
		trigger_fail_nodes_bitmap = bit_realloc(
			trigger_fail_nodes_bitmap, node_record_count);
	if (trigger_up_nodes_bitmap)
		trigger_up_nodes_bitmap = bit_realloc(
			trigger_up_nodes_bitmap, node_record_count);
	slurm_mutex_unlock(&trigger_mutex);
}
extern void trigger_primary_ctld_fail(void)
{
	slurm_mutex_lock(&trigger_mutex);
	if (ctld_failure != 1) {
		trigger_pri_ctld_fail = true;
		ctld_failure = 1;
	}
	slurm_mutex_unlock(&trigger_mutex);
}

extern void trigger_primary_ctld_res_op(void)
{
	slurm_mutex_lock(&trigger_mutex);
	trigger_pri_ctld_res_op = true;
	ctld_failure = 0;
	slurm_mutex_unlock(&trigger_mutex);
}

extern void trigger_primary_ctld_res_ctrl(void)
{
	slurm_mutex_lock(&trigger_mutex);
	trigger_pri_ctld_res_ctrl = true;
	slurm_mutex_unlock(&trigger_mutex);
}

extern void trigger_primary_ctld_acct_full(void)
{
	slurm_mutex_lock(&trigger_mutex);
	trigger_pri_ctld_acct_buffer_full = true;
	slurm_mutex_unlock(&trigger_mutex);
}

extern void trigger_backup_ctld_fail(void)
{
	slurm_mutex_lock(&trigger_mutex);
	if (bu_ctld_failure != 1) {
		trigger_bu_ctld_fail = true;
		bu_ctld_failure = 1;
	}
	slurm_mutex_unlock(&trigger_mutex);
}

extern void trigger_backup_ctld_res_op(void)
{
	slurm_mutex_lock(&trigger_mutex);
	trigger_bu_ctld_res_op = true;
	bu_ctld_failure = 0;
	slurm_mutex_unlock(&trigger_mutex);
}

extern void trigger_backup_ctld_as_ctrl(void)
{
	slurm_mutex_lock(&trigger_mutex);
	trigger_bu_ctld_as_ctrl = true;
	slurm_mutex_unlock(&trigger_mutex);
}

extern void trigger_primary_dbd_fail(void)
{
	slurm_mutex_lock(&trigger_mutex);
	if (dbd_failure != 1) {
		trigger_pri_dbd_fail = true;
		dbd_failure = 1;
	}
	slurm_mutex_unlock(&trigger_mutex);
}

extern void trigger_primary_dbd_res_op(void)
{
	slurm_mutex_lock(&trigger_mutex);
	trigger_pri_dbd_res_op = true;
	dbd_failure = 0;
	slurm_mutex_unlock(&trigger_mutex);
}

extern void trigger_primary_db_fail(void)
{
	slurm_mutex_lock(&trigger_mutex);
	if (db_failure != 1) {
		trigger_pri_db_fail = true;
		db_failure = 1;
	}
	slurm_mutex_unlock(&trigger_mutex);
}

extern void trigger_primary_db_res_op(void)
{
	slurm_mutex_lock(&trigger_mutex);
		trigger_pri_db_res_op = true;
		db_failure = 0;
	slurm_mutex_unlock(&trigger_mutex);
}

extern void trigger_block_error(void)
{
	slurm_mutex_lock(&trigger_mutex);
	trigger_block_err = true;
	slurm_mutex_unlock(&trigger_mutex);
}

extern void trigger_burst_buffer(void)
{
	slurm_mutex_lock(&trigger_mutex);
	trigger_bb_error = true;
	slurm_mutex_unlock(&trigger_mutex);
}

static void _dump_trigger_state(trig_mgr_info_t *trig_ptr, Buf buffer)
{
	/* write trigger pull state flags */
	pack8(ctld_failure,    buffer);
	pack8(bu_ctld_failure, buffer);
	pack8(dbd_failure,     buffer);
	pack8(db_failure,      buffer);

	pack16   (trig_ptr->flags,     buffer);
	pack32   (trig_ptr->trig_id,   buffer);
	pack16   (trig_ptr->res_type,  buffer);
	packstr  (trig_ptr->orig_res_id, buffer);  /* restores res_id too */
	/* rebuild nodes_bitmap as needed from res_id */
	/* rebuild job_id as needed from res_id */
	/* rebuild job_ptr as needed from res_id */
	pack32   (trig_ptr->trig_type, buffer);
	pack_time(trig_ptr->orig_time, buffer);    /* restores trig_time too */
	pack32   (trig_ptr->user_id,   buffer);
	pack32   (trig_ptr->group_id,  buffer);
	packstr  (trig_ptr->program,   buffer);
	pack8    (trig_ptr->state,     buffer);
}

static int _load_trigger_state(Buf buffer, uint16_t protocol_version)
{
	trig_mgr_info_t *trig_ptr;
	uint32_t str_len;

	trig_ptr = xmalloc(sizeof(trig_mgr_info_t));

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		/* restore trigger pull state flags */
		safe_unpack8(&ctld_failure, buffer);
		safe_unpack8(&bu_ctld_failure, buffer);
		safe_unpack8(&dbd_failure, buffer);
		safe_unpack8(&db_failure, buffer);

		safe_unpack16   (&trig_ptr->flags,     buffer);
		safe_unpack32   (&trig_ptr->trig_id,   buffer);
		safe_unpack16   (&trig_ptr->res_type,  buffer);
		safe_unpackstr_xmalloc(&trig_ptr->res_id, &str_len, buffer);
		/* rebuild nodes_bitmap as needed from res_id */
		/* rebuild job_id as needed from res_id */
		/* rebuild job_ptr as needed from res_id */
		safe_unpack32   (&trig_ptr->trig_type, buffer);
		safe_unpack_time(&trig_ptr->trig_time, buffer);
		safe_unpack32   (&trig_ptr->user_id,   buffer);
		safe_unpack32   (&trig_ptr->group_id,  buffer);
		safe_unpackstr_xmalloc(&trig_ptr->program, &str_len, buffer);
		safe_unpack8    (&trig_ptr->state,     buffer);
	} else {
		error("_load_trigger_state: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}

	if ((trig_ptr->res_type < TRIGGER_RES_TYPE_JOB)  ||
	    (trig_ptr->res_type > TRIGGER_RES_TYPE_OTHER) ||
	    (trig_ptr->state > 2))
		goto unpack_error;
	if (trig_ptr->res_type == TRIGGER_RES_TYPE_JOB) {
		trig_ptr->job_id = (uint32_t) atol(trig_ptr->res_id);
		trig_ptr->job_ptr = find_job_record(trig_ptr->job_id);
		if ((trig_ptr->job_id == 0)     ||
		    (trig_ptr->job_ptr == NULL) ||
		    (IS_JOB_COMPLETED(trig_ptr->job_ptr) &&
		     trig_ptr->state != 2))
			goto unpack_error;
	} else if (trig_ptr->res_type == TRIGGER_RES_TYPE_NODE) {
		trig_ptr->job_id = 0;
		trig_ptr->job_ptr = NULL;
		if ((trig_ptr->res_id != NULL)   &&
		    (trig_ptr->res_id[0] != '*') &&
		    (node_name2bitmap(trig_ptr->res_id, false,
				      &trig_ptr->nodes_bitmap) != 0))
			goto unpack_error;
	}
	if (trig_ptr->nodes_bitmap)
		trig_ptr->orig_bitmap = bit_copy(trig_ptr->nodes_bitmap);
	if (trig_ptr->res_id)
		trig_ptr->orig_res_id = xstrdup(trig_ptr->res_id);
	trig_ptr->orig_time = trig_ptr->trig_time;

	slurm_mutex_lock(&trigger_mutex);
	if (trigger_list == NULL)
		trigger_list = list_create(_trig_del);
	list_append(trigger_list, trig_ptr);
	next_trigger_id = MAX(next_trigger_id, trig_ptr->trig_id + 1);
	slurm_mutex_unlock(&trigger_mutex);

	return SLURM_SUCCESS;

unpack_error:
	error("Incomplete trigger record");
	xfree(trig_ptr->res_id);
	xfree(trig_ptr->program);
	FREE_NULL_BITMAP(trig_ptr->nodes_bitmap);
	xfree(trig_ptr);
	return SLURM_FAILURE;
}
extern int trigger_state_save(void)
{
	/* Save high-water mark to avoid buffer growth with copies */
	static int high_buffer_size = (1024 * 1024);
	int error_code = 0, log_fd;
	char *old_file, *new_file, *reg_file;
	Buf buffer = init_buf(high_buffer_size);
	ListIterator trig_iter;
	trig_mgr_info_t *trig_in;
	/* Locks: Read config */
	slurmctld_lock_t config_read_lock =
		{ READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	/* write header: version, time */
	packstr(TRIGGER_STATE_VERSION, buffer);
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);

	/* write individual trigger records */
	slurm_mutex_lock(&trigger_mutex);
	if (trigger_list == NULL)
		trigger_list = list_create(_trig_del);

	trig_iter = list_iterator_create(trigger_list);
	while ((trig_in = list_next(trig_iter)))
		_dump_trigger_state(trig_in, buffer);
	list_iterator_destroy(trig_iter);
	slurm_mutex_unlock(&trigger_mutex);

	/* write the buffer to file */
	lock_slurmctld(config_read_lock);
	old_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(old_file, "/trigger_state.old");
	reg_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(reg_file, "/trigger_state");
	new_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(new_file, "/trigger_state.new");
	unlock_slurmctld(config_read_lock);

	lock_state_files();
	log_fd = creat(new_file, 0600);
	if (log_fd < 0) {
		error("Can't save state, create file %s error %m",
		      new_file);
		error_code = errno;
	} else {
		int pos = 0, nwrite = get_buf_offset(buffer), amount, rc;
		char *data = (char *)get_buf_data(buffer);
		high_buffer_size = MAX(nwrite, high_buffer_size);
		while (nwrite > 0) {
			amount = write(log_fd, &data[pos], nwrite);
			if ((amount < 0) && (errno != EINTR)) {
				error("Error writing file %s, %m", new_file);
				error_code = errno;
				break;
			}
			nwrite -= amount;
			pos    += amount;
		}

		rc = fsync_and_close(log_fd, "trigger");
		if (rc && !error_code)
			error_code = rc;
	}
	if (error_code) {
		(void) unlink(new_file);
	} else {			/* file shuffle */
		(void) unlink(old_file);
		if (link(reg_file, old_file)) {
			debug4("unable to create link for %s -> %s: %m",
			       reg_file, old_file);
		}
		(void) unlink(reg_file);
		if (link(new_file, reg_file)) {
			debug4("unable to create link for %s -> %s: %m",
			       new_file, reg_file);
		}
		(void) unlink(new_file);
	}
	xfree(old_file);
	xfree(reg_file);
	xfree(new_file);
	unlock_state_files();
	free_buf(buffer);
	return error_code;
}

/* Open the trigger state save file, or backup if necessary.
 * state_file IN - the name of the state save file used
 * RET the file description to read from or error code
 */
static int _open_resv_state_file(char **state_file)
{
	int state_fd;
	struct stat stat_buf;

	*state_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(*state_file, "/trigger_state");
	state_fd = open(*state_file, O_RDONLY);
	if (state_fd < 0) {
		error("Could not open trigger state file %s: %m",
		      *state_file);
	} else if (fstat(state_fd, &stat_buf) < 0) {
		error("Could not stat trigger state file %s: %m",
		      *state_file);
		(void) close(state_fd);
	} else if (stat_buf.st_size < 10) {
		error("Trigger state file %s too small", *state_file);
		(void) close(state_fd);
	} else 	/* Success */
		return state_fd;

	error("NOTE: Trying backup state save file. Triggers may be lost!");
	xstrcat(*state_file, ".old");
	state_fd = open(*state_file, O_RDONLY);
	return state_fd;
}

extern void trigger_state_restore(void)
{
	int data_allocated, data_read = 0;
	uint32_t data_size = 0;
	uint16_t protocol_version = NO_VAL16;
	int state_fd, trigger_cnt = 0;
	char *data = NULL, *state_file;
	Buf buffer;
	time_t buf_time;
	char *ver_str = NULL;
	uint32_t ver_str_len;

	/* read the file */
	lock_state_files();
	state_fd = _open_resv_state_file(&state_file);
	if (state_fd < 0) {
		info("No trigger state file (%s) to recover", state_file);
		xfree(state_file);
		unlock_state_files();
		return;
	} else {
		data_allocated = BUF_SIZE;
		data = xmalloc(data_allocated);
		while (1) {
			data_read = read(state_fd, &data[data_size],
					 BUF_SIZE);
			if (data_read < 0) {
				if (errno == EINTR)
					continue;
				else {
					error("Read error on %s: %m",
					      state_file);
					break;
				}
			} else if (data_read == 0)	/* eof */
				break;
			data_size      += data_read;
			data_allocated += data_read;
			xrealloc(data, data_allocated);
		}
		close(state_fd);
	}
	xfree(state_file);
	unlock_state_files();

	buffer = create_buf(data, data_size);
	safe_unpackstr_xmalloc(&ver_str, &ver_str_len, buffer);
	if (ver_str && !xstrcmp(ver_str, TRIGGER_STATE_VERSION))
		safe_unpack16(&protocol_version, buffer);

	if (protocol_version == NO_VAL16) {
		if (!ignore_state_errors)
			fatal("Can't recover trigger state, data version incompatible, start with '-i' to ignore this");
		error("Can't recover trigger state, data version "
		      "incompatible");
		xfree(ver_str);
		free_buf(buffer);
		return;
	}
	xfree(ver_str);

	safe_unpack_time(&buf_time, buffer);
	if (trigger_list)
		list_flush(trigger_list);
	while (remaining_buf(buffer) > 0) {
		if (_load_trigger_state(buffer, protocol_version) !=
		    SLURM_SUCCESS)
			goto unpack_error;
		trigger_cnt++;
	}
	goto fini;

unpack_error:
	if (!ignore_state_errors)
		fatal("Incomplete trigger data checkpoint file, start with '-i' to ignore this");
	error("Incomplete trigger data checkpoint file");
fini:	verbose("State of %d triggers recovered", trigger_cnt);
	free_buf(buffer);
}

static bool _front_end_job_test(bitstr_t *front_end_bitmap,
				struct job_record *job_ptr)
{
#ifdef HAVE_FRONT_END
	int i;

	if ((front_end_bitmap == NULL) || (job_ptr->batch_host == NULL))
		return false;

	for (i = 0; i < front_end_node_cnt; i++) {
		if (bit_test(front_end_bitmap, i) &&
		    !xstrcmp(front_end_nodes[i].name, job_ptr->batch_host)) {
			return true;
		}
	}
#endif
	return false;
}

/* Test if the event has been triggered, change trigger state as needed */
static void _trigger_job_event(trig_mgr_info_t *trig_in, time_t now)
{
	trig_in->job_ptr = find_job_record(trig_in->job_id);

	if ((trig_in->trig_type & TRIGGER_TYPE_FINI) &&
	    ((trig_in->job_ptr == NULL) ||
	     (IS_JOB_COMPLETED(trig_in->job_ptr)))) {
		trig_in->state = 1;
		trig_in->trig_time = now + (trig_in->trig_time - 0x8000);
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS) {
			info("trigger[%u] event for job %u fini",
			     trig_in->trig_id, trig_in->job_id);
		}
		return;
	}

	if (trig_in->job_ptr == NULL) {
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS) {
			info("trigger[%u] for defunct job %u",
			     trig_in->trig_id, trig_in->job_id);
		}
		trig_in->state = 2;
		trig_in->trig_time = now;
		return;
	}

	if (!IS_JOB_PENDING(trig_in->job_ptr) &&
	    (trig_in->trig_type & TRIGGER_TYPE_TIME)) {
		long rem_time = (trig_in->job_ptr->end_time - now);
		if (rem_time <= (0x8000 - trig_in->trig_time)) {
			trig_in->state = 1;
			trig_in->trig_time = now;
			if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS) {
				info("trigger[%u] for job %u time",
				     trig_in->trig_id, trig_in->job_id);
			}
			return;
		}
	}

	if (trig_in->trig_type & TRIGGER_TYPE_DOWN) {
		if (_front_end_job_test(trigger_down_front_end_bitmap,
					trig_in->job_ptr)) {
			if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS) {
				info("trigger[%u] for job %u down",
				     trig_in->trig_id, trig_in->job_id);
			}
			trig_in->state = 1;
			trig_in->trig_time = now +
					    (trig_in->trig_time - 0x8000);
			return;
		}
	}

	if (trig_in->trig_type & TRIGGER_TYPE_DOWN) {
		if (trigger_down_nodes_bitmap &&
		    bit_overlap(trig_in->job_ptr->node_bitmap,
				trigger_down_nodes_bitmap)) {
			if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS) {
				info("trigger[%u] for job %u down",
				     trig_in->trig_id, trig_in->job_id);
			}
			trig_in->state = 1;
			trig_in->trig_time = now +
					    (trig_in->trig_time - 0x8000);
			return;
		}
	}

	if (trig_in->trig_type & TRIGGER_TYPE_FAIL) {
		if (trigger_fail_nodes_bitmap &&
		    bit_overlap(trig_in->job_ptr->node_bitmap,
				trigger_fail_nodes_bitmap)) {
			if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS) {
				info("trigger[%u] for job %u node fail",
				     trig_in->trig_id, trig_in->job_id);
			}
			trig_in->state = 1;
			trig_in->trig_time = now +
					     (trig_in->trig_time - 0x8000);
			return;
		}
	}

	if (trig_in->trig_type & TRIGGER_TYPE_UP) {
		if (trigger_up_nodes_bitmap &&
		    bit_overlap(trig_in->job_ptr->node_bitmap,
				trigger_up_nodes_bitmap)) {
			trig_in->state = 1;
			trig_in->trig_time = now +
					    (0x8000 - trig_in->trig_time);
			if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS) {
				info("trigger[%u] for job %u up",
				     trig_in->trig_id, trig_in->job_id);
			}
			return;
		}
	}
}


static void _trigger_front_end_event(trig_mgr_info_t *trig_in, time_t now)
{
	int i;

	if ((trig_in->trig_type & TRIGGER_TYPE_DOWN) &&
	    (trigger_down_front_end_bitmap != NULL) &&
	    ((i = bit_ffs(trigger_down_front_end_bitmap)) != -1)) {
		xfree(trig_in->res_id);
		for (i = 0; i < front_end_node_cnt; i++) {
			if (!bit_test(trigger_down_front_end_bitmap, i))
				continue;
			if (trig_in->res_id != NULL)
				xstrcat(trig_in->res_id, ",");
			xstrcat(trig_in->res_id, front_end_nodes[i].name);
		}
		trig_in->state = 1;
		trig_in->trig_time = now + (trig_in->trig_time - 0x8000);
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS) {
			info("trigger[%u] for node %s down",
			     trig_in->trig_id, trig_in->res_id);
		}
		return;
	}

	if ((trig_in->trig_type & TRIGGER_TYPE_UP) &&
	    (trigger_up_front_end_bitmap != NULL) &&
	    ((i = bit_ffs(trigger_up_front_end_bitmap)) != -1)) {
		xfree(trig_in->res_id);
		for (i = 0; i < front_end_node_cnt; i++) {
			if (!bit_test(trigger_up_front_end_bitmap, i))
				continue;
			if (trig_in->res_id != NULL)
				xstrcat(trig_in->res_id, ",");
			xstrcat(trig_in->res_id, front_end_nodes[i].name);
		}
		trig_in->state = 1;
		trig_in->trig_time = now + (trig_in->trig_time - 0x8000);
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS) {
			info("trigger[%u] for node %s up",
			     trig_in->trig_id, trig_in->res_id);
		}
		return;
	}
}

static void _trigger_other_event(trig_mgr_info_t *trig_in, time_t now)
{
	if ((trig_in->trig_type & TRIGGER_TYPE_BURST_BUFFER) &&
	    trigger_bb_error) {
		trig_in->state = 1;
		trig_in->trig_time = now;
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS)
			info("trigger[%u] for burst buffer", trig_in->trig_id);
		return;
	}
}

static void _trigger_node_event(trig_mgr_info_t *trig_in, time_t now)
{
	if ((trig_in->trig_type & TRIGGER_TYPE_BLOCK_ERR) &&
	    trigger_block_err) {
		trig_in->state = 1;
		trig_in->trig_time = now + (trig_in->trig_time - 0x8000);
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS)
			info("trigger[%u] for block_err", trig_in->trig_id);
		return;
	}

	if ((trig_in->trig_type & TRIGGER_TYPE_DOWN) &&
	    trigger_down_nodes_bitmap                &&
	    (bit_ffs(trigger_down_nodes_bitmap) != -1)) {
		if (trig_in->nodes_bitmap == NULL) {	/* all nodes */
			xfree(trig_in->res_id);
			trig_in->res_id = bitmap2node_name(
					  trigger_down_nodes_bitmap);
			trig_in->state = 1;
		} else if (bit_overlap(trig_in->nodes_bitmap,
				       trigger_down_nodes_bitmap)) {
			bit_and(trig_in->nodes_bitmap,
				trigger_down_nodes_bitmap);
			xfree(trig_in->res_id);
			trig_in->res_id = bitmap2node_name(
					 trig_in->nodes_bitmap);
			trig_in->state = 1;
		}
		if (trig_in->state == 1) {
			trig_in->trig_time = now +
					     (trig_in->trig_time - 0x8000);
			if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS) {
				info("trigger[%u] for node %s down",
				     trig_in->trig_id, trig_in->res_id);
			}
			return;
		}
	}

	if ((trig_in->trig_type & TRIGGER_TYPE_DRAINED) &&
	    trigger_drained_nodes_bitmap                &&
	    (bit_ffs(trigger_drained_nodes_bitmap) != -1)) {
		if (trig_in->nodes_bitmap == NULL) {	/* all nodes */
			xfree(trig_in->res_id);
			trig_in->res_id = bitmap2node_name(
					  trigger_drained_nodes_bitmap);
			trig_in->state = 1;
		} else if (bit_overlap(trig_in->nodes_bitmap,
				       trigger_drained_nodes_bitmap)) {
			bit_and(trig_in->nodes_bitmap,
				trigger_drained_nodes_bitmap);
			xfree(trig_in->res_id);
			trig_in->res_id = bitmap2node_name(
					  trig_in->nodes_bitmap);
			trig_in->state = 1;
		}
		if (trig_in->state == 1) {
			trig_in->trig_time = now +
					     (trig_in->trig_time - 0x8000);
			if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS) {
				info("trigger[%u] for node %s drained",
				     trig_in->trig_id, trig_in->res_id);
			}
			return;
		}
	}

	if ((trig_in->trig_type & TRIGGER_TYPE_FAIL) &&
	    trigger_fail_nodes_bitmap                &&
	    (bit_ffs(trigger_fail_nodes_bitmap) != -1)) {
		if (trig_in->nodes_bitmap == NULL) {	/* all nodes */
			xfree(trig_in->res_id);
			trig_in->res_id = bitmap2node_name(
					  trigger_fail_nodes_bitmap);
			trig_in->state = 1;
		} else if (bit_overlap(trig_in->nodes_bitmap,
				       trigger_fail_nodes_bitmap)) {
			bit_and(trig_in->nodes_bitmap,
				trigger_fail_nodes_bitmap);
			xfree(trig_in->res_id);
			trig_in->res_id = bitmap2node_name(
					  trig_in->nodes_bitmap);
			trig_in->state = 1;
		}
		if (trig_in->state == 1) {
			trig_in->trig_time = now +
					     (trig_in->trig_time - 0x8000);
			if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS) {
				info("trigger[%u] for node %s fail",
				     trig_in->trig_id, trig_in->res_id);
			}
			return;
		}
	}

	if (trig_in->trig_type & TRIGGER_TYPE_IDLE) {
		/* We need to determine which (if any) of these
		 * nodes have been idle for at least the offset time */
		time_t min_idle = now - (trig_in->trig_time - 0x8000);
		int i;
		struct node_record *node_ptr = node_record_table_ptr;
		bitstr_t *trigger_idle_node_bitmap;

		trigger_idle_node_bitmap = bit_alloc(node_record_count);
		for (i = 0; i < node_record_count; i++, node_ptr++) {
			if (!IS_NODE_IDLE(node_ptr) ||
			    (node_ptr->last_idle > min_idle))
				continue;
			bit_set(trigger_idle_node_bitmap, i);
		}
		if (trig_in->nodes_bitmap == NULL) {    /* all nodes */
			xfree(trig_in->res_id);
			trig_in->res_id = bitmap2node_name(
					  trigger_idle_node_bitmap);
			trig_in->state = 1;
		} else if (bit_overlap(trig_in->nodes_bitmap,
				       trigger_idle_node_bitmap)) {
			bit_and(trig_in->nodes_bitmap,
				trigger_idle_node_bitmap);
			xfree(trig_in->res_id);
			trig_in->res_id = bitmap2node_name(
					  trig_in->nodes_bitmap);
			trig_in->state = 1;
		}
		FREE_NULL_BITMAP(trigger_idle_node_bitmap);
		if (trig_in->state == 1) {
			trig_in->trig_time = now;
			if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS) {
				info("trigger[%u] for node %s idle",
				     trig_in->trig_id, trig_in->res_id);
			}
			return;
		}
	}

	if ((trig_in->trig_type & TRIGGER_TYPE_UP) &&
	    trigger_up_nodes_bitmap                &&
	    (bit_ffs(trigger_up_nodes_bitmap) != -1)) {
		if (trig_in->nodes_bitmap == NULL) {	/* all nodes */
			xfree(trig_in->res_id);
			trig_in->res_id = bitmap2node_name(
					  trigger_up_nodes_bitmap);
			trig_in->state = 1;
		} else if (bit_overlap(trig_in->nodes_bitmap,
				       trigger_up_nodes_bitmap)) {
			bit_and(trig_in->nodes_bitmap,
				trigger_up_nodes_bitmap);
			xfree(trig_in->res_id);
			trig_in->res_id = bitmap2node_name(
					  trig_in->nodes_bitmap);
			trig_in->state = 1;
		}
		if (trig_in->state == 1) {
			trig_in->trig_time = now +
					     (trig_in->trig_time - 0x8000);
			if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS) {
				info("trigger[%u] for node %s up",
				     trig_in->trig_id, trig_in->res_id);
			}
			return;
		}
	}

	if ((trig_in->trig_type & TRIGGER_TYPE_RECONFIG) &&
	    trigger_node_reconfig) {
		trig_in->state = 1;
		trig_in->trig_time = now + (trig_in->trig_time - 0x8000);
		xfree(trig_in->res_id);
		trig_in->res_id = xstrdup("reconfig");
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS)
			info("trigger[%u] for reconfig", trig_in->trig_id);
		return;
	}
}

static void _trigger_slurmctld_event(trig_mgr_info_t *trig_in, time_t now)
{
	if ((trig_in->trig_type & TRIGGER_TYPE_PRI_CTLD_FAIL) &&
	    trigger_pri_ctld_fail) {
		trig_in->state = 1;
		trig_in->trig_time = now + (trig_in->trig_time - 0x8000);
		xfree(trig_in->res_id);
		trig_in->res_id = xstrdup("primary_slurmctld_failure");
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS) {
			info("trigger[%u] for primary_slurmctld_failure",
			     trig_in->trig_id);
		}
		return;
	}
	if ((trig_in->trig_type & TRIGGER_TYPE_PRI_CTLD_RES_OP) &&
	    trigger_pri_ctld_res_op) {
		trig_in->state = 1;
		trig_in->trig_time = now + (trig_in->trig_time - 0x8000);
		xfree(trig_in->res_id);
		trig_in->res_id =
			xstrdup("primary_slurmctld_resumed_operation");
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS) {
			info("trigger[%u] for primary_slurmctld_resumed_"
			     "operation", trig_in->trig_id);
		}
		return;
	}
	if ((trig_in->trig_type & TRIGGER_TYPE_PRI_CTLD_RES_CTRL) &&
	    trigger_pri_ctld_res_ctrl) {
		trig_in->state = 1;
		trig_in->trig_time = now + (trig_in->trig_time - 0x8000);
		xfree(trig_in->res_id);
		trig_in->res_id = xstrdup("primary_slurmctld_resumed_control");
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS) {
			info("trigger[%u] for primary_slurmctld_resumed_"
			     "control", trig_in->trig_id);
		}
		return;
	}
	if ((trig_in->trig_type & TRIGGER_TYPE_PRI_CTLD_ACCT_FULL) &&
	    trigger_pri_ctld_acct_buffer_full) {
		trig_in->state = 1;
		trig_in->trig_time = now + (trig_in->trig_time - 0x8000);
		xfree(trig_in->res_id);
		trig_in->res_id = xstrdup("primary_slurmctld_acct_buffer_full");
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS) {
			info("trigger[%u] for primary_slurmctld_acct_"
			     "buffer_full", trig_in->trig_id);
		}
		return;
	}
	if ((trig_in->trig_type & TRIGGER_TYPE_BU_CTLD_FAIL) &&
	    trigger_bu_ctld_fail) {
		trig_in->state = 1;
		trig_in->trig_time = now + (trig_in->trig_time - 0x8000);
		xfree(trig_in->res_id);
		trig_in->res_id = xstrdup("backup_slurmctld_failure");
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS) {
			info("trigger[%u] for backup_slurmctld_failure",
			     trig_in->trig_id);
		}
		return;
	}
	if ((trig_in->trig_type & TRIGGER_TYPE_BU_CTLD_RES_OP) &&
	    trigger_bu_ctld_res_op) {
		trig_in->state = 1;
		trig_in->trig_time = now + (trig_in->trig_time - 0x8000);
		xfree(trig_in->res_id);
		trig_in->res_id = xstrdup("backup_slurmctld_resumed_operation");
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS) {
			info("trigger[%u] for backup_slurmctld_resumed_"
			     "operation", trig_in->trig_id);
		}
		return;
	}
	if ((trig_in->trig_type & TRIGGER_TYPE_BU_CTLD_AS_CTRL) &&
	    trigger_bu_ctld_as_ctrl) {
		trig_in->state = 1;
		trig_in->trig_time = now + (trig_in->trig_time - 0x8000);
		xfree(trig_in->res_id);
		trig_in->res_id = xstrdup("backup_slurmctld_assumed_control");
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS) {
			info("trigger[%u] for bu_slurmctld_assumed_control",
			     trig_in->trig_id);
		}
		return;
	}
}

static void _trigger_slurmdbd_event(trig_mgr_info_t *trig_in, time_t now)
{
	if ((trig_in->trig_type & TRIGGER_TYPE_PRI_DBD_FAIL) &&
	    trigger_pri_dbd_fail) {
		trig_in->state = 1;
		trig_in->trig_time = now + (trig_in->trig_time - 0x8000);
		xfree(trig_in->res_id);
		trig_in->res_id = xstrdup("primary_slurmdbd_failure");
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS)
			info("trigger[%u] for primary_slurmcdbd_failure",
			     trig_in->trig_id);
		return;
	}
	if ((trig_in->trig_type & TRIGGER_TYPE_PRI_DBD_RES_OP) &&
	    trigger_pri_dbd_res_op) {
		trig_in->state = 1;
		trig_in->trig_time = now + (trig_in->trig_time - 0x8000);
		xfree(trig_in->res_id);
		trig_in->res_id = xstrdup("primary_slurmdbd_resumed_operation");
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS) {
			info("trigger[%u] for primary_slurmdbd_resumed_"
			     "operation", trig_in->trig_id);
		}
		return;
	}
}

static void _trigger_database_event(trig_mgr_info_t *trig_in, time_t now)
{
	if ((trig_in->trig_type & TRIGGER_TYPE_PRI_DB_FAIL) &&
	    trigger_pri_db_fail) {
		trig_in->state = 1;
		trig_in->trig_time = now + (trig_in->trig_time - 0x8000);
		xfree(trig_in->res_id);
		trig_in->res_id = xstrdup("primary_database_failure");
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS) {
			info("trigger[%u] for primary_database_failure",
			     trig_in->trig_id);
		}
		return;
	}
	if ((trig_in->trig_type & TRIGGER_TYPE_PRI_DB_RES_OP) &&
	    trigger_pri_db_res_op) {
		trig_in->state = 1;
		trig_in->trig_time = now + (trig_in->trig_time - 0x8000);
		xfree(trig_in->res_id);
		trig_in->res_id = xstrdup("primary_database_resumed_operation");
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS) {
			info("trigger[%u] for primary_database_resumed_"
			     "operation", trig_in->trig_id);
		}
		return;
	}
}
/* Ideally we would use the existing proctrack plugin to prevent any
 * processes from escaping our control, but that plugin is tied
 * to various slurmd data structures. We just the process group ID
 * to kill the spawned program after MAX_PROG_TIME. Since triggers are
 * meant primarily for system administrators rather than users, this
 * may be sufficient. */
static void _trigger_run_program(trig_mgr_info_t *trig_in)
{
	char *tmp, *save_ptr = NULL, *tok;
	char *program, *args[64], user_name[1024];
	char *pname, *uname;
	uid_t uid;
	gid_t gid;
	pid_t child_pid;
	int i;

	if (!_validate_trigger(trig_in))
		return;

	tmp = xstrdup(trig_in->program);
	tok = strtok_r(trig_in->program, " ", &save_ptr);
	program = xstrdup(tok);
	pname = strrchr(program, '/');
	if (pname == NULL)
		pname = program;
	else
		pname++;
	args[0] = xstrdup(pname);
	for (i = 1; i < 63; i++) {
		tok = strtok_r(NULL, " ", &save_ptr);
		if (!tok) {
			args[i] = xstrdup(trig_in->res_id);
			break;
		}
		args[i] = xstrdup(tok);
	}
	for (i++; i < 64; i++)
		args[i] = NULL;
	xfree(tmp);

	uid = trig_in->user_id;
	gid = trig_in->group_id;
	uname = uid_to_string(uid);
	snprintf(user_name, sizeof(user_name), "%s", uname);
	xfree(uname);

	child_pid = fork();
	if (child_pid > 0) {
		trig_in->child_pid = child_pid;
	} else if (child_pid == 0) {
		int i;
		bool run_as_self = (uid == slurmctld_conf.slurm_user_id);

		for (i = 0; i < 1024; i++)
			(void) close(i);
		setpgid(0, 0);
		setsid();
		if ((initgroups(user_name, gid) == -1) && !run_as_self) {
			error("trigger: initgroups: %m");
			exit(1);
		}
		if ((setgid(gid) == -1) && !run_as_self){
			error("trigger: setgid: %m");
			exit(1);
		}
		if ((setuid(uid) == -1) && !run_as_self) {
			error("trigger: setuid: %m");
			exit(1);
		}
		execv(program, args);
		exit(1);
	} else {
		error("fork: %m");
	}
	xfree(program);
	for (i = 0; i < 64; i++)
		xfree(args[i]);
}

static void _clear_event_triggers(void)
{
	if (trigger_down_front_end_bitmap) {
		bit_nclear(trigger_down_front_end_bitmap,
			   0, (bit_size(trigger_down_front_end_bitmap) - 1));
	}
	if (trigger_up_front_end_bitmap) {
		bit_nclear(trigger_up_front_end_bitmap,
			   0, (bit_size(trigger_up_front_end_bitmap) - 1));
	}
	if (trigger_down_nodes_bitmap) {
		bit_nclear(trigger_down_nodes_bitmap,
			   0, (bit_size(trigger_down_nodes_bitmap) - 1));
	}
	if (trigger_drained_nodes_bitmap) {
		bit_nclear(trigger_drained_nodes_bitmap,
			   0, (bit_size(trigger_drained_nodes_bitmap) - 1));
	}
	if (trigger_up_nodes_bitmap) {
		bit_nclear(trigger_up_nodes_bitmap,
			   0, (bit_size(trigger_up_nodes_bitmap) - 1));
	}
	trigger_node_reconfig = false;
	trigger_bb_error = false;
	trigger_block_err = false;
	trigger_pri_ctld_fail = false;
	trigger_pri_ctld_res_op = false;
	trigger_pri_ctld_res_ctrl = false;
	trigger_pri_ctld_acct_buffer_full = false;
	trigger_bu_ctld_fail = false;
	trigger_bu_ctld_res_op = false;
	trigger_bu_ctld_as_ctrl = false;
	trigger_pri_dbd_fail = false;
	trigger_pri_dbd_res_op = false;
	trigger_pri_db_fail = false;
	trigger_pri_db_res_op = false;
}

/* Make a copy of a trigger and pre-pend it on our list */
static void _trigger_clone(trig_mgr_info_t *trig_in)
{
	trig_mgr_info_t *trig_add;

	trig_add = xmalloc(sizeof(trig_mgr_info_t));
	trig_add->flags     = trig_in->flags;
	trig_add->trig_id   = trig_in->trig_id;
	trig_add->res_type  = trig_in->res_type;
	if (trig_in->orig_res_id) {
		trig_add->res_id = xstrdup(trig_in->orig_res_id);
		trig_add->orig_res_id = xstrdup(trig_in->orig_res_id);
	}
	if (trig_in->orig_bitmap) {
		trig_add->nodes_bitmap = bit_copy(trig_in->orig_bitmap);
		trig_add->orig_bitmap  = bit_copy(trig_in->orig_bitmap);
	}
	trig_add->job_id    = trig_in->job_id;
	trig_add->job_ptr   = trig_in->job_ptr;
	trig_add->trig_type = trig_in->trig_type;
	trig_add->trig_time = trig_in->orig_time;
	trig_add->orig_time = trig_in->orig_time;
	trig_add->user_id   = trig_in->user_id;
	trig_add->group_id  = trig_in->group_id;
	trig_add->program   = xstrdup(trig_in->program);;
	list_prepend(trigger_list, trig_add);
}

extern void trigger_process(void)
{
	ListIterator trig_iter;
	trig_mgr_info_t *trig_in;
	time_t now = time(NULL);
	bool state_change = false;
	pid_t rc;
	int prog_stat;

	slurm_mutex_lock(&trigger_mutex);
	if (trigger_list == NULL)
		trigger_list = list_create(_trig_del);

	trig_iter = list_iterator_create(trigger_list);
	while ((trig_in = list_next(trig_iter))) {
		if (trig_in->state == 0) {
			if (trig_in->res_type == TRIGGER_RES_TYPE_OTHER)
				_trigger_other_event(trig_in, now);
			else if (trig_in->res_type == TRIGGER_RES_TYPE_JOB)
				_trigger_job_event(trig_in, now);
			else if (trig_in->res_type == TRIGGER_RES_TYPE_NODE)
				_trigger_node_event(trig_in, now);
			else if (trig_in->res_type ==
				 TRIGGER_RES_TYPE_SLURMCTLD)
				_trigger_slurmctld_event(trig_in, now);
			else if (trig_in->res_type ==
				 TRIGGER_RES_TYPE_SLURMDBD)
				_trigger_slurmdbd_event(trig_in, now);
			else if (trig_in->res_type ==
				 TRIGGER_RES_TYPE_DATABASE)
			 	_trigger_database_event(trig_in, now);
			else if (trig_in->res_type ==
				 TRIGGER_RES_TYPE_FRONT_END)
			 	_trigger_front_end_event(trig_in, now);
		}
		if ((trig_in->state == 1) &&
		    (trig_in->trig_time <= now)) {
			if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRIGGERS) {
				info("launching program for trigger[%u]",
				     trig_in->trig_id);
				info("  uid=%u gid=%u program=%s arg=%s",
				     trig_in->user_id, trig_in->group_id,
				     trig_in->program, trig_in->res_id);
			}
			if (trig_in->flags & TRIGGER_FLAG_PERM) {
				_trigger_clone(trig_in);
			}
			trig_in->state = 2;
			trig_in->trig_time = now;
			state_change = true;
			_trigger_run_program(trig_in);
		} else if ((trig_in->state == 2) &&
			   (difftime(now, trig_in->trig_time) >
			    MAX_PROG_TIME)) {
			if (trig_in->child_pid != 0) {
				killpg(trig_in->child_pid, SIGKILL);
				rc = waitpid(trig_in->child_pid, &prog_stat,
					     WNOHANG);
				if ((rc > 0) && prog_stat) {
					info("trigger uid=%u type=%s:%s "
					     "exit=%u:%u",
					     trig_in->user_id,
					     trigger_res_type(trig_in->res_type),
					     trigger_type(trig_in->trig_type),
					     WIFEXITED(prog_stat),
					     WTERMSIG(prog_stat));
				}
				if ((rc == trig_in->child_pid) ||
				    ((rc == -1) && (errno == ECHILD)))
					trig_in->child_pid = 0;
			}

			if (trig_in->child_pid == 0) {
				if (slurmctld_conf.debug_flags &
				    DEBUG_FLAG_TRIGGERS) {
					info("purging trigger[%u]",
					     trig_in->trig_id);
				}
				list_delete_item(trig_iter);
				state_change = true;
			}
		} else if (trig_in->state == 2) {
			/* Elimiate zombie processes right away.
			 * Purge trigger entry above MAX_PROG_TIME later */
			rc = waitpid(trig_in->child_pid, &prog_stat, WNOHANG);
			if ((rc > 0) && prog_stat) {
				info("trigger uid=%u type=%s:%s exit=%u:%u",
				     trig_in->user_id,
				     trigger_res_type(trig_in->res_type),
				     trigger_type(trig_in->trig_type),
				     WIFEXITED(prog_stat),
				     WTERMSIG(prog_stat));
			}
			if ((rc == trig_in->child_pid) ||
			    ((rc == -1) && (errno == ECHILD)))
				trig_in->child_pid = 0;
		}
	}
	list_iterator_destroy(trig_iter);
	_clear_event_triggers();
	slurm_mutex_unlock(&trigger_mutex);
	if (state_change)
		schedule_trigger_save();
}

/* Free all allocated memory */
extern void trigger_fini(void)
{
	FREE_NULL_LIST(trigger_list);
	FREE_NULL_BITMAP(trigger_down_front_end_bitmap);
	FREE_NULL_BITMAP(trigger_up_front_end_bitmap);
	FREE_NULL_BITMAP(trigger_down_nodes_bitmap);
	FREE_NULL_BITMAP(trigger_drained_nodes_bitmap);
	FREE_NULL_BITMAP(trigger_fail_nodes_bitmap);
	FREE_NULL_BITMAP(trigger_up_nodes_bitmap);
}
