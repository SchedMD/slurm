/*****************************************************************************\
 *  hres.c - Hierarchical resource (HRES) plugin interface.
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/hres.h"

#include "src/slurmctld/licenses.h"

/*
 * hres_layers_snapshot() and hres_layer_set_disabled() live in the
 * controller's licenses.c. This interface is linked into slurmd/slurmstepd
 * too, where those symbols are absent and the maintenance/create paths never
 * run, so reference them weakly.
 */
extern list_t *__attribute__((weak)) hres_layers_snapshot(uint16_t hres_id,
							  bool locked);
extern void __attribute__((weak)) hres_layer_set_disabled(licenses_id_t id,
							  bool disable,
							  bool locked);

typedef struct slurm_hres_ops {
	uint32_t(*plugin_id);
	char(*plugin_type);
	int (*create_context)(hres_ctx_t *ctx, list_t *layers);
	int (*destroy_context)(hres_ctx_t *ctx);
} slurm_hres_ops_t;

/*
 * Must be synchronized with slurm_hres_ops_t above.
 */
static const char *syms[] = {
	"plugin_id",
	"plugin_type",
	"hres_p_create_context",
	"hres_p_destroy_context",
};

static slurm_hres_ops_t *ops = NULL;
static plugin_context_t **g_context = NULL;
static int g_context_num = 0;
static pthread_mutex_t g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static plugin_init_t plugin_inited = PLUGIN_NOT_INITED;
static hres_ctx_t *hctx = NULL;
static int hctx_num = 0;

/*
 * Index into ops[]/g_context[] of the already loaded plugin of the given type,
 * or -1. Multiple contexts may share one plugin.
 */
static int _find_plugin_index_by_type(char *type)
{
	for (int i = 0; i < g_context_num; i++) {
		if (!xstrcmp(type, ops[i].plugin_type))
			return i;
	}

	return -1;
}

/* Load the plugin implementing the given type once. */
static int _load_plugin_by_type(char *type)
{
	char *plugin_type = "hres";
	int idx = _find_plugin_index_by_type(type);

	if (idx >= 0)
		return idx;

	xrecalloc(ops, g_context_num + 1, sizeof(slurm_hres_ops_t));
	xrecalloc(g_context, g_context_num + 1, sizeof(plugin_context_t *));

	g_context[g_context_num] =
		plugin_context_create(plugin_type, type,
				      (void **) &ops[g_context_num], syms,
				      sizeof(syms));
	if (!g_context[g_context_num]) {
		error("%s: cannot create %s context for %s",
		      __func__, plugin_type, type);
		return -1;
	}

	return g_context_num++;
}

static int _foreach_load_plugin(void *x, void *arg)
{
	char *plugin_name = x;

	_load_plugin_by_type(plugin_name);

	return SLURM_SUCCESS;
}

static int _foreach_load_ctx_plugin(void *x, void *arg)
{
	licenses_t *license = x;

	if (license->hres_rec.plugin_type)
		_load_plugin_by_type(license->hres_rec.plugin_type);

	return SLURM_SUCCESS;
}

/* license_mutex must be held. */
static int _foreach_disable_layer(void *x, void *arg)
{
	hres_layer_t *layer = x;

	hres_layer_set_disabled(layer->id, true, true);

	return 0;
}

static int _foreach_add_ctx(void *x, void *arg)
{
	int idx, rc;
	licenses_t *license = x;
	list_t *layers;

	/* Only records that define a plugin describe an HRES context. */
	if (!license->hres_rec.plugin_type)
		return SLURM_SUCCESS;

	idx = _find_plugin_index_by_type(license->hres_rec.plugin_type);
	if (idx < 0)
		fatal("%s: no %s plugin loaded for HRES %s", __func__,
		      license->hres_rec.plugin_type, license->name);

	xrecalloc(hctx, hctx_num + 1, sizeof(*hctx));

	hctx[hctx_num].config =
		hres_config_list_copy(license->hres_rec.plugin_config);
	hctx[hctx_num].hres_id = license->id.hres_id;
	hctx[hctx_num].idx = idx;
	hctx[hctx_num].maintenance_cycle = license->hres_rec.maintenance_cycle;
	hctx[hctx_num].maintenance_timeout =
		license->hres_rec.maintenance_timeout;

	/* Called under license_mutex from hres_init(), so locked. */
	layers = hres_layers_snapshot(license->id.hres_id, true);
	rc = (*(ops[idx].create_context))(&hctx[hctx_num], layers);

	switch (rc) {
	case SLURM_SUCCESS:
		break;
	case ESLURM_HRES_MAINTENANCE_DOWN:
		/* FIXME: down the nodes or requeue only jobs using this HRES */
	case ESLURM_HRES_MAINTENANCE_DISABLE:
		/* The resource is unusable now, stop allocating against it. */
		list_for_each_ro(layers, _foreach_disable_layer, NULL);
		break;
	default:
		fatal("%s: create_context failed for HRES %s: %s", __func__,
		      license->name, slurm_strerror(rc));
	}
	FREE_NULL_LIST(layers);

	hctx_num++;

	return SLURM_SUCCESS;
}

/* g_context_lock must be held. */
static void _destroy_all_ctx(void)
{
	for (int i = 0; i < hctx_num; i++) {
		(void) (*(ops[hctx[i].idx].destroy_context))(&hctx[i]);
		FREE_NULL_LIST(hctx[i].config);
	}

	xfree(hctx);
	hctx_num = 0;
}

extern int hres_g_init(list_t *hres_recs)
{
	slurm_mutex_lock(&g_context_lock);

	plugin_inited = PLUGIN_INITED;

	if (hres_recs) {
		/* Only load the plugin types the resources reference. */
		list_for_each_ro(hres_recs, _foreach_load_ctx_plugin, NULL);
	} else {
		/* Nothing to inspect, load every installed HRES plugin. */
		list_t *plugin_names = plugin_get_plugins_of_type("hres");

		if (plugin_names) {
			list_for_each(plugin_names, _foreach_load_plugin, NULL);
			FREE_NULL_LIST(plugin_names);
		}
	}

	slurm_mutex_unlock(&g_context_lock);

	return SLURM_SUCCESS;
}

extern int hres_g_create_context(list_t *hres_recs)
{
	slurm_mutex_lock(&g_context_lock);
	xassert(plugin_inited != PLUGIN_NOT_INITED);

	_destroy_all_ctx();

	if (hres_recs)
		list_for_each_ro(hres_recs, _foreach_add_ctx, NULL);

	slurm_mutex_unlock(&g_context_lock);

	return SLURM_SUCCESS;
}

extern int hres_g_destroy_context(void)
{
	slurm_mutex_lock(&g_context_lock);

	/* Nothing was ever loaded when no hierarchical resource is defined. */
	if (!plugin_inited) {
		slurm_mutex_unlock(&g_context_lock);
		return SLURM_SUCCESS;
	}

	_destroy_all_ctx();

	slurm_mutex_unlock(&g_context_lock);

	return SLURM_SUCCESS;
}

extern int hres_g_fini(void)
{
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&g_context_lock);

	_destroy_all_ctx();

	for (int i = 0; i < g_context_num; i++) {
		char *type = xstrdup(g_context[i]->type);
		int rc2 = plugin_context_destroy(g_context[i]);
		if (rc2) {
			debug("%s: %s: %s", __func__, type,
			      slurm_strerror(rc2));
			rc = SLURM_ERROR;
		}
		xfree(type);
	}
	xfree(ops);
	xfree(g_context);
	g_context_num = 0;
	plugin_inited = PLUGIN_NOT_INITED;

	slurm_mutex_unlock(&g_context_lock);

	return rc;
}
