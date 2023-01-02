/*****************************************************************************\
 *  alloc.c - Slurm data parser allocators for objects
 *****************************************************************************
 *  Copyright (C) 2022 SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
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

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "alloc.h"

extern void *create_parser_list_obj(const parser_t *const parser, ssize_t *size)
{
	void *obj;

	xassert(size);
	xassert(*size <= 0);

	*size = parser->size;

	xassert((*size > 0) && (*size < NO_VAL));

	obj = xmalloc(*size);

	log_flag(DATA, "created %zd byte %s object at 0x%" PRIxPTR, *size,
		 parser->obj_type_string, (uintptr_t) obj);

	return obj;
}

extern void *create_assoc_rec_obj(const parser_t *const parser, ssize_t *size)
{
	slurmdb_assoc_rec_t *assoc;

	xassert(size);
	xassert(!*size);
	xassert((parser->type == DATA_PARSER_ASSOC_SHORT) ||
		(parser->type == DATA_PARSER_ASSOC));

	*size = sizeof(*assoc);
	assoc = xmalloc(*size);
	slurmdb_init_assoc_rec(assoc, false);

	xassert(xsize(assoc) == *size);
	xassert(*size == parser->size);

	log_flag(DATA, "created %zd byte %s object at 0x%" PRIxPTR, *size,
		 parser->obj_type_string, (uintptr_t) assoc);

	return assoc;
}

extern void *create_job_rec_obj(const parser_t *const parser, ssize_t *size)
{
	slurmdb_job_rec_t *job;

	xassert(size);
	xassert(!*size);
	xassert(parser->type == DATA_PARSER_JOB);

	*size = sizeof(slurmdb_job_rec_t);
	job = slurmdb_create_job_rec();

	xassert(xsize(job) == *size);
	xassert(*size == parser->size);

	log_flag(DATA, "created %zd byte %s object at 0x%" PRIxPTR, *size,
		 parser->obj_type_string, (uintptr_t) job);

	return job;
}

extern void *create_step_rec_obj(const parser_t *const parser, ssize_t *size)
{
	slurmdb_step_rec_t *step;

	xassert(size);
	xassert(!*size);
	xassert(parser->type == DATA_PARSER_STEP);

	*size = sizeof(slurmdb_step_rec_t);
	step = slurmdb_create_step_rec();

	xassert(xsize(step) == *size);
	xassert(*size == parser->size);

	log_flag(DATA, "created %zd byte %s object at 0x%" PRIxPTR, *size,
		 parser->obj_type_string, (uintptr_t) step);

	return step;
}

extern void *create_cluster_rec_obj(const parser_t *const parser, ssize_t *size)
{
	slurmdb_cluster_rec_t *cluster;

	xassert(size);
	xassert(!*size);
	xassert(parser->type == DATA_PARSER_CLUSTER_REC);

	*size = sizeof(*cluster);
	cluster = xmalloc(*size);
	slurmdb_init_cluster_rec(cluster, false);

	xassert(*size == parser->size);

	log_flag(DATA, "created %zd byte %s object at 0x%" PRIxPTR, *size,
		 parser->obj_type_string, (uintptr_t) cluster);

	return cluster;
}

extern void *create_qos_rec_obj(const parser_t *const parser, ssize_t *size)
{
	slurmdb_qos_rec_t *qos;

	xassert(size);
	xassert(!*size);
	xassert(parser->type == DATA_PARSER_QOS);

	*size = sizeof(*qos);
	qos = xmalloc(*size);
	slurmdb_init_qos_rec(qos, false, NO_VAL);

	/*
	 * Clear the QOS_FLAG_NOTSET by slurmdb_init_qos_rec() so that
	 * flag updates won't be ignored.
	 */
	qos->flags = 0;

	/* force to off instead of NO_VAL */
	qos->preempt_mode = PREEMPT_MODE_OFF;

	xassert(*size == parser->size);

	log_flag(DATA, "created %zd byte %s object at 0x%" PRIxPTR, *size,
		 parser->obj_type_string, (uintptr_t) qos);

	return qos;
}

extern void *create_user_rec_obj(const parser_t *const parser, ssize_t *size)
{
	slurmdb_user_rec_t *user;

	xassert(size);
	xassert(!*size);
	xassert(parser->type == DATA_PARSER_USER);

	*size = sizeof(*user);
	user = xmalloc(*size);
	user->assoc_list = list_create(slurmdb_destroy_assoc_rec);
	user->coord_accts = list_create(slurmdb_destroy_coord_rec);

	xassert(*size == parser->size);

	log_flag(DATA, "created %zd byte %s object at 0x%" PRIxPTR, *size,
		 parser->obj_type_string, (uintptr_t) user);

	return user;
}

extern void *create_wckey_rec_obj(const parser_t *const parser, ssize_t *size)
{
	slurmdb_wckey_rec_t *wckey;

	xassert(size);
	xassert(!*size);
	xassert(parser->type == DATA_PARSER_WCKEY);

	*size = sizeof(*wckey);
	wckey = xmalloc(*size);
	slurmdb_init_wckey_rec(wckey, false);
	wckey->accounting_list = list_create(slurmdb_destroy_account_rec);

	xassert(*size == parser->size);

	log_flag(DATA, "created %zd byte %s object at 0x%" PRIxPTR, *size,
		 parser->obj_type_string, (uintptr_t) wckey);

	return wckey;
}

extern void *create_job_desc_msg_obj(const parser_t *const parser,
				     ssize_t *size)
{
	job_desc_msg_t *job;

	xassert(size);
	xassert(!*size);
	xassert(parser->type == DATA_PARSER_JOB_DESC_MSG);

	*size = sizeof(*job);
	job = xmalloc(*size);
	slurm_init_job_desc_msg(job);
	xassert(*size == parser->size);

	log_flag(DATA, "created %zd byte %s object at 0x%" PRIxPTR, *size,
		 parser->obj_type_string, (uintptr_t) job);

	return job;
}
