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

#include <errno.h>
#include <stdlib.h>

#include "src/common/list.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/trigger_mgr.h"

#define _DEBUG 1

List trigger_list;
uint32_t next_trigger_id = 1;
static pthread_mutex_t trigger_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct trig_mgr_info {
	uint32_t trig_id;	/* trigger ID */
	uint8_t  res_type;	/* TRIGGER_RES_TYPE_* */
	char *   res_id;	/* node name or job_id (string) */
	uint32_t job_id;	/* job ID (if applicable) */
	uint8_t  trig_type;	/* TRIGGER_TYPE_* */
	uint16_t offset;	/* seconds from trigger, 0x8000 origin */
	uint32_t user_id;	/* user requesting trigger */
	char *   program;	/* program to execute */
	uint8_t  state;		/* 0=pending, 1=pulled, 2=completed */
} trig_mgr_info_t;

/* Prototype for ListDelF */
void _trig_del(void *x) {
	trig_mgr_info_t * tmp = (trig_mgr_info_t *) x;
	xfree(tmp->res_id);
	xfree(tmp->program);
	xfree(tmp);
}

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
		info("trigger[%d] %u %s %s %s %d %u %s", i,
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
		if (job_id == 0)
			goto fini;
	} else if (trig_in->trig_id == 0)
		goto fini;

	/* now look for a valid request, matching uid */
	trig_iter = list_iterator_create(trigger_list);
	while ((trig_test = list_next(trig_iter))) {
		if ((trig_test->user_id != (uint32_t) uid)
		&&  (uid != 0))
			continue;
		if (trig_in->trig_id
		&&  (trig_in->trig_id != trig_test->trig_id))
			continue;
		if (job_id
		&& (job_id != trig_test->job_id))
			continue;
		list_delete(trig_iter);
		rc = SLURM_SUCCESS;
	}
	list_iterator_destroy(trig_iter);

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
		/* Note: All filtering currently done by strigger */
		trig_out->trig_id   = trig_in->trig_id;
		trig_out->res_type  = trig_in->res_type;
		trig_out->res_id    = xstrdup(trig_in->res_id);
		trig_out->trig_type = trig_in->trig_type;
		trig_out->offset    = trig_in->offset;
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

extern int trigger_set(uid_t uid, trigger_info_msg_t *msg)
{
	int i;
	trig_mgr_info_t * trig_add;

	slurm_mutex_lock(&trigger_mutex);
	if (trigger_list == NULL)
		trigger_list = list_create(_trig_del);

	for (i=0; i<msg->record_count; i++) {
		trig_add = xmalloc(sizeof(trig_mgr_info_t));
		msg->trigger_array[i].trig_id = next_trigger_id;
		trig_add->trig_id = next_trigger_id;
		next_trigger_id++;
		trig_add->res_type = msg->trigger_array[i].res_type;
		if (trig_add->res_type == TRIGGER_RES_TYPE_JOB) {
			trig_add->job_id = (uint32_t) atol(
				msg->trigger_array[i].res_id);
		}
		/* move don't copy "res_id" */
		trig_add->res_id = msg->trigger_array[i].res_id;
		trig_add->trig_type = msg->trigger_array[i].trig_type;
		trig_add->offset = msg->trigger_array[i].offset;
		trig_add->user_id = (uint32_t) uid;
		/* move don't copy "program" */
		trig_add->program = msg->trigger_array[i].program;
		list_append(trigger_list, trig_add);
	}
	_dump_trigger_msg("trigger_set", msg);
	/* Relocated pointers, clear now to avoid duplicate free */
	for (i=0; i<msg->record_count; i++) {
		msg->trigger_array[i].res_id = NULL;
		msg->trigger_array[i].program = NULL;
	}

	slurm_mutex_unlock(&trigger_mutex);
	return SLURM_SUCCESS;
}

extern void trigger_node_down(char *node_name)
{
	/* FIXME */
}

extern void trigger_node_up(char *node_name)
{
	/* FIXME */
}

extern void trigger_job_fini(uint32_t job_id)
{
	ListIterator trig_iter;
	trig_mgr_info_t *trig_test;

	if (job_id == 0) {
		error("trigger_job_fini: job_id=0");
		return;
	}

	slurm_mutex_lock(&trigger_mutex);
	if (trigger_list == NULL)
		trigger_list = list_create(_trig_del);

	trig_iter = list_iterator_create(trigger_list);
	while ((trig_test = list_next(trig_iter))) {
		if ((job_id != trig_test->job_id)
		||  (trig_test->state != 0))
			continue;
		trig_test->state = 1;
#if _DEBUG
		info("trigger[%d] for job %u fini pulled",
			trig_test->trig_id, job_id);
#endif
	}
	list_iterator_destroy(trig_iter);

	slurm_mutex_unlock(&trigger_mutex);
}

extern void trigger_state_save(void)
{
	/* FIXME */
}

extern void trigger_state_restore(void)
{
	/* FIXME */
}

extern void trigger_process(void)
{
	/* FIXME */
}
