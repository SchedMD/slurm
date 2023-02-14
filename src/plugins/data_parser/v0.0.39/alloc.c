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

static void *_create_assoc_rec_obj(void)
{
	slurmdb_assoc_rec_t *assoc = xmalloc(sizeof(*assoc));
	slurmdb_init_assoc_rec(assoc, false);
	return assoc;
}

static void *_create_cluster_rec_obj(void)
{
	slurmdb_cluster_rec_t *cluster = xmalloc(sizeof(*cluster));
	slurmdb_init_cluster_rec(cluster, false);
	return cluster;
}

static void *_create_qos_rec_obj(void)
{
	slurmdb_qos_rec_t *qos = xmalloc(sizeof(*qos));

	slurmdb_init_qos_rec(qos, false, NO_VAL);

	/*
	 * Clear the QOS_FLAG_NOTSET by slurmdb_init_qos_rec() so that
	 * flag updates won't be ignored.
	 */
	qos->flags = 0;

	/* force to off instead of NO_VAL */
	qos->preempt_mode = PREEMPT_MODE_OFF;

	return qos;
}

static void *_create_user_rec_obj(void)
{
	slurmdb_user_rec_t *user = xmalloc(sizeof(*user));
	user->assoc_list = list_create(slurmdb_destroy_assoc_rec);
	user->coord_accts = list_create(slurmdb_destroy_coord_rec);
	return user;
}

static void *_create_wckey_rec_obj(void)
{
	slurmdb_wckey_rec_t *wckey = xmalloc(sizeof(*wckey));
	slurmdb_init_wckey_rec(wckey, false);
	wckey->accounting_list = list_create(slurmdb_destroy_account_rec);
	return wckey;
}

static void *_create_job_desc_msg_obj(void)
{
	job_desc_msg_t *job = xmalloc(sizeof(*job));
	slurm_init_job_desc_msg(job);
	return job;
}

typedef void *(*alloc_func_t)(const parser_t *const parser);

#define add(typem, freef, addf)             \
{                                           \
	.type = DATA_PARSER_ ## typem,      \
	.free_func = (ListDelF) freef,      \
	.alloc_func = (alloc_func_t) addf,  \
}
static const struct {
	type_t type;
	/* if NULL then xfree_ptr() is used */
	ListDelF free_func;
	/*
	 * function to create object
	 * RET ptr to obj
	 *
	 * if NULL, then xmalloc() is used
	 */
	alloc_func_t alloc_func;
} types[] = {
	add(ACCOUNTING, slurmdb_destroy_accounting_rec, NULL),
	add(ACCOUNT, slurmdb_destroy_account_rec, NULL),
	add(ASSOC_SHORT, slurmdb_destroy_assoc_rec, _create_assoc_rec_obj),
	add(ASSOC, slurmdb_destroy_assoc_rec, _create_assoc_rec_obj),
	add(CLUSTER_ACCT_REC, slurmdb_destroy_clus_res_rec, NULL),
	add(CLUSTER_REC, slurmdb_destroy_cluster_rec, _create_cluster_rec_obj),
	add(COORD, slurmdb_destroy_coord_rec, NULL),
	add(JOB_DESC_MSG, (ListDelF) slurm_free_job_desc_msg,
	    _create_job_desc_msg_obj),
	add(JOB, slurmdb_destroy_job_rec, slurmdb_create_job_rec),
	add(QOS_ID, NULL, NULL),
	add(QOS_NAME, NULL, NULL),
	add(QOS, slurmdb_destroy_qos_rec, _create_qos_rec_obj),
	add(STRING, NULL, NULL),
	add(STEP, slurmdb_destroy_step_rec, slurmdb_create_step_rec),
	add(TRES, slurmdb_destroy_tres_rec, NULL),
	add(USER, slurmdb_destroy_user_rec, _create_user_rec_obj),
	add(WCKEY, slurmdb_destroy_wckey_rec, _create_wckey_rec_obj),
};
#undef add

extern void *alloc_parser_obj(const parser_t *const parser)
{
	void *obj = NULL;
	xassert(alloc_registered(parser));
	check_parser(parser);

	for (int i = 0; i < ARRAY_SIZE(types); i++) {
		if (types[i].type == parser->type) {
			if (types[i].alloc_func)
				obj = types[i].alloc_func(parser);
			else
				obj = xmalloc(parser->size);
			break;
		}
	}

	log_flag(DATA, "created %zd byte %s object at 0x%"PRIxPTR,
		 xsize(obj), parser->obj_type_string, (uintptr_t) obj);

	return obj;
}

extern void free_parser_obj(const parser_t *const parser, void *ptr)
{
	ListDelF free_func = parser_obj_free_func(parser);

	xassert(alloc_registered(parser));
	check_parser(parser);

	log_flag(DATA, "destroying %zd byte %s object at 0x%"PRIxPTR,
		 xsize(ptr), parser->obj_type_string, (uintptr_t) ptr);

	free_func(ptr);
}

extern bool alloc_registered(const parser_t *const parser)
{
	for (int i = 0; i < ARRAY_SIZE(types); i++)
		if (types[i].type == parser->type)
			return true;

	return false;
}

extern ListDelF parser_obj_free_func(const parser_t *const parser)
{
	for (int i = 0; i < ARRAY_SIZE(types); i++) {
		if (types[i].type == parser->type) {
			if (types[i].free_func)
				return types[i].free_func;
			else
				return xfree_ptr;
		}
	}

	return false;
}
