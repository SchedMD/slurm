/*****************************************************************************\
 *  core_spec_plugin.c - Core specialization plugin stub.
 *****************************************************************************
 *  Copyright (C) 2013-2014 SchedMD LLC
 *  Written by Morris Jette
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

#include <pthread.h>

#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/core_spec.h"


typedef struct core_spec_ops {
	int	(*core_spec_p_set)	(uint64_t cont_id, uint16_t count);
	int	(*core_spec_p_clear)	(uint64_t cont_id);
	int	(*core_spec_p_suspend)	(uint64_t cont_id, uint16_t count);
	int	(*core_spec_p_resume)	(uint64_t cont_id, uint16_t count);
} core_spec_ops_t;

/*
 * Must be synchronized with core_spec_ops_t above.
 */
static const char *syms[] = {
	"core_spec_p_set",
	"core_spec_p_clear",
	"core_spec_p_suspend",
	"core_spec_p_resume",
};

static core_spec_ops_t		*ops = NULL;
static plugin_context_t		**g_core_spec_context = NULL;
static int			g_core_spec_context_num = -1;
static pthread_mutex_t		g_core_spec_context_lock =
					PTHREAD_MUTEX_INITIALIZER;

/*
 * Initialize the core specialization plugin.
 *
 * RET - slurm error code
 */
extern int core_spec_g_init(void)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "core_spec";
	char *core_spec_plugin_type = NULL;
	char *last = NULL, *core_spec_plugin_list, *core_spec = NULL;

	slurm_mutex_lock(&g_core_spec_context_lock);

	if (g_core_spec_context_num >= 0)
		goto done;

	core_spec_plugin_type = slurm_get_core_spec_plugin();
	g_core_spec_context_num = 0; /* mark it before anything else */
	if ((core_spec_plugin_type == NULL) ||
	    (core_spec_plugin_type[0] == '\0'))
		goto done;

	core_spec_plugin_list = core_spec_plugin_type;
	while ((core_spec =
		strtok_r(core_spec_plugin_list, ",", &last))) {
		xrealloc(ops,
			 sizeof(core_spec_ops_t) *
			 (g_core_spec_context_num + 1));
		xrealloc(g_core_spec_context, (sizeof(plugin_context_t *)
					  * (g_core_spec_context_num + 1)));
		if (xstrncmp(core_spec, "core_spec/", 10) == 0)
			core_spec += 10; /* backward compatibility */
		core_spec = xstrdup_printf("core_spec/%s",
					       core_spec);
		g_core_spec_context[g_core_spec_context_num] =
			plugin_context_create(
				plugin_type, core_spec,
				(void **)&ops[g_core_spec_context_num],
				syms, sizeof(syms));
		if (!g_core_spec_context[g_core_spec_context_num]) {
			error("cannot create %s context for %s",
			      plugin_type, core_spec);
			xfree(core_spec);
			retval = SLURM_ERROR;
			break;
		}

		xfree(core_spec);
		g_core_spec_context_num++;
		core_spec_plugin_list = NULL; /* for next iteration */
	}

 done:
	slurm_mutex_unlock(&g_core_spec_context_lock);
	xfree(core_spec_plugin_type);

	if (retval != SLURM_SUCCESS)
		core_spec_g_fini();

	return retval;
}

/*
 * Terminate the core specialization plugin, free memory.
 *
 * RET - slurm error code
 */
extern int core_spec_g_fini(void)
{
	int i, rc = SLURM_SUCCESS;

	slurm_mutex_lock(&g_core_spec_context_lock);
	if (!g_core_spec_context)
		goto done;

	for (i = 0; i < g_core_spec_context_num; i++) {
		if (g_core_spec_context[i]) {
			if (plugin_context_destroy(g_core_spec_context[i])
			    != SLURM_SUCCESS) {
				rc = SLURM_ERROR;
			}
		}
	}

	xfree(ops);
	xfree(g_core_spec_context);
	g_core_spec_context_num = -1;

done:
	slurm_mutex_unlock(&g_core_spec_context_lock);
	return rc;
}

/*
 * Set the count of specialized cores at job start
 *
 * Return SLURM_SUCCESS on success
 */
extern int core_spec_g_set(uint64_t cont_id, uint16_t core_count)
{
	int i, rc = SLURM_SUCCESS;

	xassert(g_core_spec_context_num >= 0);

	for (i = 0; ((i < g_core_spec_context_num) && (rc == SLURM_SUCCESS));
	     i++) {
		rc = (*(ops[i].core_spec_p_set))(cont_id, core_count);
	}

	return rc;
}

/*
 * Clear specialized cores at job termination
 *
 * Return SLURM_SUCCESS on success
 */
extern int core_spec_g_clear(uint64_t cont_id)
{
	int i, rc = SLURM_SUCCESS;

	xassert(g_core_spec_context_num >= 0);

	for (i = 0; ((i < g_core_spec_context_num) && (rc == SLURM_SUCCESS));
	     i++) {
		rc = (*(ops[i].core_spec_p_clear))(cont_id);
	}

	return rc;
}

/*
 * Reset specialized cores at job suspend
 *
 * Return SLURM_SUCCESS on success
 */
extern int core_spec_g_suspend(uint64_t cont_id, uint16_t count)
{
	int i, rc = SLURM_SUCCESS;

	xassert(g_core_spec_context_num >= 0);

	for (i = 0; ((i < g_core_spec_context_num) && (rc == SLURM_SUCCESS));
	     i++) {
		rc = (*(ops[i].core_spec_p_suspend))(cont_id, count);
	}

	return rc;
}

/*
 * Reset specialized cores at job resume
 *
 * Return SLURM_SUCCESS on success
 */
extern int core_spec_g_resume(uint64_t cont_id, uint16_t count)
{
	int i, rc = SLURM_SUCCESS;

	xassert(g_core_spec_context_num >= 0);

	for (i = 0; ((i < g_core_spec_context_num) && (rc == SLURM_SUCCESS));
	     i++) {
		rc = (*(ops[i].core_spec_p_resume))(cont_id, count);
	}

	return rc;
}
