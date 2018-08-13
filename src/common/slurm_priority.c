/*****************************************************************************\
 *  slurm_priority.c - Define priority plugin functions
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include "src/common/slurm_priority.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/xstring.h"

typedef struct slurm_priority_ops {
	uint32_t (*set)            (uint32_t last_prio,
				    struct job_record *job_ptr);
	void     (*reconfig)       (bool assoc_clear);
	void     (*set_assoc_usage)(slurmdb_assoc_rec_t *assoc);
	double   (*calc_fs_factor) (long double usage_efctv,
				    long double shares_norm);
	List	 (*get_priority_factors)
	(priority_factors_request_msg_t *req_msg, uid_t uid);
	void     (*job_end)        (struct job_record *job_ptr);
} slurm_priority_ops_t;

/*
 * Must be synchronized with slurm_priority_ops_t above.
 */
static const char *syms[] = {
	"priority_p_set",
	"priority_p_reconfig",
	"priority_p_set_assoc_usage",
	"priority_p_calc_fs_factor",
	"priority_p_get_priority_factors_list",
	"priority_p_job_end",
};

static slurm_priority_ops_t ops;
static plugin_context_t *g_priority_context = NULL;
static pthread_mutex_t g_priority_context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

extern int priority_sort_part_tier(void *x, void *y)
{
	struct part_record *parta = *(struct part_record **) x;
	struct part_record *partb = *(struct part_record **) y;

	if (parta->priority_tier > partb->priority_tier)
		return -1;
	if (parta->priority_tier < partb->priority_tier)
		return 1;

	return 0;
}

/*
 * Initialize context for priority plugin
 */
extern int slurm_priority_init(void)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "priority";
	char *type = NULL;

	if (init_run && g_priority_context)
		return retval;

	slurm_mutex_lock(&g_priority_context_lock);

	if (g_priority_context)
		goto done;

	type = slurm_get_priority_type();

	g_priority_context = plugin_context_create(
		plugin_type, type, (void **)&ops, syms, sizeof(syms));

	if (!g_priority_context) {
		error("cannot create %s context for %s", plugin_type, type);
		retval = SLURM_ERROR;
		goto done;
	}
	init_run = true;

done:
	slurm_mutex_unlock(&g_priority_context_lock);
	xfree(type);
	return retval;
}

extern int slurm_priority_fini(void)
{
	int rc;

	if (!g_priority_context)
		return SLURM_SUCCESS;

	init_run = false;
	rc = plugin_context_destroy(g_priority_context);
	g_priority_context = NULL;
	return rc;
}

extern uint32_t priority_g_set(uint32_t last_prio, struct job_record *job_ptr)
{
	if (slurm_priority_init() < 0)
		return 0;

	return (*(ops.set))(last_prio, job_ptr);
}

extern void priority_g_reconfig(bool assoc_clear)
{
	if (slurm_priority_init() < 0)
		return;

	(*(ops.reconfig))(assoc_clear);

	return;
}

extern void priority_g_set_assoc_usage(slurmdb_assoc_rec_t *assoc)
{
	if (slurm_priority_init() < 0)
		return;

	(*(ops.set_assoc_usage))(assoc);
	return;
}

extern double priority_g_calc_fs_factor(long double usage_efctv,
					long double shares_norm)
{
	if (slurm_priority_init() < 0)
		return 0.0;

	return (*(ops.calc_fs_factor))
		(usage_efctv, shares_norm);
}

extern List priority_g_get_priority_factors_list(
	priority_factors_request_msg_t *req_msg, uid_t uid)
{
	if (slurm_priority_init() < 0)
		return NULL;

	return (*(ops.get_priority_factors))(req_msg, uid);
}

extern void priority_g_job_end(struct job_record *job_ptr)
{
	if (slurm_priority_init() < 0)
		return;

	(*(ops.job_end))(job_ptr);
}
