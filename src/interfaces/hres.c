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
#include <time.h>

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/threadpool.h"
#include "src/common/timers.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/hres.h"

#include "src/slurmctld/licenses.h"
#include "src/slurmctld/locks.h"

/*
 * hres_layers_snapshot(), hres_layer_set_disabled() and hres_job_prep_list()
 * live in the controller's licenses.c, lock_slurmctld() and unlock_slurmctld()
 * in its locks.c. This interface is linked into slurmd/slurmstepd too, where
 * those symbols are absent and the controller paths never run, so reference
 * them weakly.
 */
extern list_t *__attribute__((weak)) hres_layers_snapshot(uint16_t hres_id,
							  bool locked);
extern void __attribute__((weak)) hres_layer_set_disabled(licenses_id_t id,
							  bool disable,
							  bool locked);
extern list_t *__attribute__((weak)) hres_job_prep_list(job_record_t *job_ptr);
extern void __attribute__((weak)) lock_slurmctld(slurmctld_lock_t lock_levels);
extern void __attribute__((weak)) unlock_slurmctld(slurmctld_lock_t
							   lock_levels);

typedef struct slurm_hres_ops {
	uint32_t(*plugin_id);
	char(*plugin_type);
	int (*controller_job_epilog)(job_record_t *job_ptr, hres_ctx_t *ctx,
				     hres_prep_t *prep);
	int (*controller_job_prerun)(job_record_t *job_ptr, hres_ctx_t *ctx,
				     hres_prep_t *prep, char **err_msg);
	int (*controller_stop)(void);
	int (*create_context)(hres_ctx_t *ctx, list_t *layers);
	int (*destroy_context)(hres_ctx_t *ctx);
	int (*maintenance)(hres_ctx_t *ctx, list_t *layers);
	int (*node_job_epilog)(hres_prep_t *prep, hres_job_t *job);
	int (*node_job_prolog)(hres_prep_t *prep, hres_job_t *job);
	int (*node_task_epilog)(hres_prep_t *prep, hres_job_t *job,
				uint32_t node_tid, uint32_t global_tid);
	int (*node_task_prolog)(hres_prep_t *prep, hres_job_t *job,
				uint32_t node_tid, uint32_t global_tid);
	int (*node_user_task_prolog)(hres_prep_t *prep, hres_job_t *job,
				     uint32_t node_tid, uint32_t global_tid);
} slurm_hres_ops_t;

/*
 * Must be synchronized with slurm_hres_ops_t above.
 */
static const char *syms[] = {
	"plugin_id",
	"plugin_type",
	"hres_p_controller_job_epilog",
	"hres_p_controller_job_prerun",
	"hres_p_controller_stop",
	"hres_p_create_context",
	"hres_p_destroy_context",
	"hres_p_maintenance",
	"hres_p_node_job_epilog",
	"hres_p_node_job_prolog",
	"hres_p_node_task_epilog",
	"hres_p_node_task_prolog",
	"hres_p_node_user_task_prolog",
};

typedef struct {
	char **err_msg;
	job_record_t *job_ptr;
	int rc;
} prep_set_ctx_args_t;

typedef struct {
	job_record_t *job_ptr;
	int rc;
} foreach_prep_epilog_t;

typedef struct {
	hres_job_t *job;
	int rc;
} foreach_node_job_t;

typedef struct {
	uint32_t global_tid;
	hres_job_t *job;
	uint32_t node_tid;
	int rc;
} foreach_node_task_t;

static slurm_hres_ops_t *ops = NULL;
static plugin_context_t **g_context = NULL;
static int g_context_num = 0;
static pthread_mutex_t g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static plugin_init_t plugin_inited = PLUGIN_NOT_INITED;
static hres_ctx_t *hctx = NULL;
static int hctx_num = 0;

/* Maintenance thread wakes on this interval to check each context. */
#define HRES_MAINTENANCE_INTERVAL 5

static pthread_t maintenance_tid = 0;
static bool maintenance_running = false;
static pthread_mutex_t maintenance_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t maintenance_cond = PTHREAD_COND_INITIALIZER;

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

/*
 * Index into ops[]/g_context[] of the loaded plugin with the given id, or -1.
 * The node routes a job's callbacks by the id carried in its hres_prep_t.
 */
static int _get_plugin_index(uint32_t plugin_id)
{
	for (int i = 0; i < g_context_num; i++) {
		if (plugin_id == *ops[i].plugin_id)
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

extern void hres_prep_layer_free(void *x)
{
	hres_prep_layer_t *layer = x;

	xfree(layer->layer_name);
	xfree(layer->nodes);
	xfree(layer);
}

extern void hres_prep_free(void *x)
{
	hres_prep_t *prep = x;

	if (!prep)
		return;

	FREE_NULL_LIST(prep->config);
	FREE_NULL_LIST(prep->layers);
	xfree(prep->name);
	xfree(prep->payload);
	FREE_NULL_LIST(prep->plugin_args);
	xfree(prep);
}

static int _foreach_copy_prep_layer(void *x, void *arg)
{
	hres_prep_layer_t *layer = x;
	list_t *dst_list = arg;
	hres_prep_layer_t *dst;

	dst = xmalloc(sizeof(*dst));
	dst->count = layer->count;
	dst->layer_name = xstrdup(layer->layer_name);
	dst->nodes = xstrdup(layer->nodes);
	list_append(dst_list, dst);

	return 0;
}

static list_t *_copy_prep_layers(list_t *src)
{
	list_t *dst;

	if (!src)
		return NULL;

	dst = list_create(hres_prep_layer_free);
	list_for_each_ro(src, _foreach_copy_prep_layer, dst);

	return dst;
}

static int _foreach_copy_prep(void *x, void *arg)
{
	hres_prep_t *prep = x;
	list_t *dst_list = arg;
	hres_prep_t *dst;

	dst = xmalloc(sizeof(*dst));
	dst->config = hres_config_list_copy(prep->config);
	dst->hres_id = prep->hres_id;
	dst->layers = _copy_prep_layers(prep->layers);
	dst->name = xstrdup(prep->name);
	dst->payload_size = prep->payload_size;
	dst->plugin_args = hres_config_list_copy(prep->plugin_args);
	dst->plugin_id = prep->plugin_id;

	if (prep->payload_size) {
		dst->payload = xmalloc(prep->payload_size);
		memcpy(dst->payload, prep->payload, prep->payload_size);
	}

	list_append(dst_list, dst);

	return 0;
}

extern list_t *hres_prep_copy_list(list_t *src)
{
	list_t *dst;

	if (!src)
		return NULL;

	dst = list_create(hres_prep_free);
	list_for_each_ro(src, _foreach_copy_prep, dst);

	return dst;
}

static void _config_pack(void *in, uint16_t protocol_version, buf_t *buffer)
{
	hres_config_t *config = in;

	packstr(config->name, buffer);
	packstr(config->value, buffer);
}

static int _config_unpack(void **object, uint16_t protocol_version,
			  buf_t *buffer)
{
	hres_config_t *config = xmalloc(sizeof(*config));

	*object = config;

	safe_unpackstr(&config->name, buffer);
	safe_unpackstr(&config->value, buffer);

	return SLURM_SUCCESS;

unpack_error:
	hres_config_free(config);
	*object = NULL;
	return SLURM_ERROR;
}

static void _prep_layer_pack(void *in, uint16_t protocol_version, buf_t *buffer)
{
	hres_prep_layer_t *layer = in;

	pack32(layer->count, buffer);
	packstr(layer->layer_name, buffer);
	packstr(layer->nodes, buffer);
}

static int _prep_layer_unpack(void **object, uint16_t protocol_version,
			      buf_t *buffer)
{
	hres_prep_layer_t *layer = xmalloc(sizeof(*layer));

	*object = layer;

	safe_unpack32(&layer->count, buffer);
	safe_unpackstr(&layer->layer_name, buffer);
	safe_unpackstr(&layer->nodes, buffer);

	return SLURM_SUCCESS;

unpack_error:
	hres_prep_layer_free(layer);
	*object = NULL;
	return SLURM_ERROR;
}

static void _hres_prep_pack(void *in, uint16_t protocol_version, buf_t *buffer)
{
	hres_prep_t *prep = in;

	if (protocol_version >= SLURM_26_11_PROTOCOL_VERSION) {
		(void) slurm_pack_list(prep->config, _config_pack, buffer,
				       protocol_version);
		(void) slurm_pack_list(prep->layers, _prep_layer_pack, buffer,
				       protocol_version);
		packstr(prep->name, buffer);
		packmem(prep->payload, prep->payload_size, buffer);
		(void) slurm_pack_list(prep->plugin_args, _config_pack, buffer,
				       protocol_version);
		pack32(prep->plugin_id, buffer);
	} else {
		error("%s: protocol_version %hu not supported", __func__,
		      protocol_version);
	}
}

extern int hres_prep_pack_list(list_t *in, buf_t *buffer,
			       uint16_t protocol_version)
{
	return slurm_pack_list(in, _hres_prep_pack, buffer, protocol_version);
}

static int _hres_prep_unpack(void **object, uint16_t protocol_version,
			     buf_t *buffer)
{
	hres_prep_t *prep = xmalloc(sizeof(*prep));

	*object = prep;
	/* The id is not on the wire, the controller resolves it by name. */
	prep->hres_id = NO_VAL16;

	if (protocol_version >= SLURM_26_11_PROTOCOL_VERSION) {
		if (slurm_unpack_list(&prep->config, _config_unpack,
				      hres_config_free, buffer,
				      protocol_version))
			goto unpack_error;

		if (slurm_unpack_list(&prep->layers, _prep_layer_unpack,
				      hres_prep_layer_free, buffer,
				      protocol_version))
			goto unpack_error;

		safe_unpackstr(&prep->name, buffer);
		safe_unpackmem_xmalloc((char **) &prep->payload,
				       &prep->payload_size, buffer);

		if (slurm_unpack_list(&prep->plugin_args, _config_unpack,
				      hres_config_free, buffer,
				      protocol_version))
			goto unpack_error;

		safe_unpack32(&prep->plugin_id, buffer);
	} else {
		error("%s: protocol_version %hu not supported", __func__,
		      protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	hres_prep_free(prep);
	*object = NULL;
	return SLURM_ERROR;
}

extern int hres_prep_unpack_list(list_t **out, buf_t *buffer,
				 uint16_t protocol_version)
{
	int rc;

	if ((rc = slurm_unpack_list(out, _hres_prep_unpack, hres_prep_free,
				    buffer, protocol_version)))
		FREE_NULL_LIST(*out);

	return rc;
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
	hctx[hctx_num].name = xstrdup(license->name);

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

/* g_context_lock must be held. The maintenance thread must not be running. */
static void _destroy_all_ctx(void)
{
	for (int i = 0; i < hctx_num; i++) {
		(void) (*(ops[hctx[i].idx].destroy_context))(&hctx[i]);
		FREE_NULL_LIST(hctx[i].config);
		xfree(hctx[i].name);
	}

	xfree(hctx);
	hctx_num = 0;
}

/* Apply a plugin's per-layer verdict from a maintenance run to the licenses. */
static int _foreach_apply_layer_state(void *x, void *arg)
{
	hres_layer_t *layer = x;

	/*
	 * FIXME: down should also cancel or requeue the jobs using this layer;
	 * for now it only stops further allocation, like disable_layer.
	 */
	hres_layer_set_disabled(layer->id, layer->down || layer->disable_layer,
				false);

	return 0;
}

/*
 * Periodically invoke each context's plugin maintenance callback.
 */
static void *_maintenance_thread(void *arg)
{
	slurmctld_lock_t config_write_lock = {
		.conf = WRITE_LOCK,
	};

	slurm_mutex_lock(&maintenance_lock);
	while (maintenance_running) {
		time_t now = time(NULL);
		struct timespec ts = { 0 };

		for (int i = 0; i < hctx_num; i++) {
			DEF_TIMERS;
			int rc;
			list_t *layers;

			/* No cycle configured, no periodic maintenance. */
			if (!hctx[i].maintenance_cycle)
				continue;

			if ((now - hctx[i].last_maintenance) <
			    hctx[i].maintenance_cycle)
				continue;

			/*
			 * The callback runs a plugin, so drop maintenance_lock
			 * around it. hctx/ops are stable because a stop joins
			 * this thread before any teardown. license_mutex must
			 * never be taken under maintenance_lock, so snapshot
			 * the layers here too.
			 */
			slurm_mutex_unlock(&maintenance_lock);
			layers = hres_layers_snapshot(hctx[i].hres_id, false);

			START_TIMER;
			/*
			 * The return code is only logged, the plugin disables
			 * or downs layers through the layer list.
			 */
			rc = (*(ops[hctx[i].idx].maintenance))(&hctx[i],
							       layers);
			END_TIMER3(ops[hctx[i].idx].plugin_type,
				   ((int64_t) hctx[i].maintenance_timeout *
				    USEC_IN_SEC));

			/*
			 * Apply the plugin's per-layer disable/down verdict.
			 * Every select_nodes() caller holds the conf read lock,
			 * so a disable cannot land between hres_filter() and
			 * hres_job_resolve().
			 */
			lock_slurmctld(config_write_lock);
			list_for_each_ro(layers, _foreach_apply_layer_state,
					 NULL);
			unlock_slurmctld(config_write_lock);
			FREE_NULL_LIST(layers);

			if (rc)
				error("%s: maintenance for HRES %s returned %s",
				      __func__, hctx[i].name,
				      slurm_strerror(rc));

			slurm_mutex_lock(&maintenance_lock);
			hctx[i].last_maintenance = now;

			if (!maintenance_running)
				break;
		}

		if (!maintenance_running)
			break;

		/* Wait from now, the callbacks may have taken a while. */
		ts.tv_sec = time(NULL) + HRES_MAINTENANCE_INTERVAL;
		slurm_cond_timedwait(&maintenance_cond, &maintenance_lock, &ts);
	}
	slurm_mutex_unlock(&maintenance_lock);

	return NULL;
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

	/*
	 * Already stopped by the caller, a join here needs license_mutex and
	 * the conf lock.
	 */
	hres_g_maintenance_stop();

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

	hres_g_maintenance_stop();
	_destroy_all_ctx();

	slurm_mutex_unlock(&g_context_lock);

	return SLURM_SUCCESS;
}

extern int hres_g_fini(void)
{
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&g_context_lock);

	/*
	 * Stop the thread before tearing down the contexts it reads. Safe to
	 * join while holding g_context_lock: the maintenance thread only takes
	 * maintenance_lock, never g_context_lock.
	 */
	hres_g_maintenance_stop();

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

extern int hres_g_maintenance_start(void)
{
	slurm_mutex_lock(&maintenance_lock);

	if (maintenance_running || (hctx_num <= 0)) {
		slurm_mutex_unlock(&maintenance_lock);
		return SLURM_SUCCESS;
	}

	maintenance_running = true;
	slurm_thread_create(NULL, &maintenance_tid, _maintenance_thread, NULL);

	slurm_mutex_unlock(&maintenance_lock);

	return SLURM_SUCCESS;
}

extern int hres_g_maintenance_stop(void)
{
	slurm_mutex_lock(&maintenance_lock);

	if (!maintenance_running) {
		slurm_mutex_unlock(&maintenance_lock);
		return SLURM_SUCCESS;
	}

	maintenance_running = false;
	slurm_cond_signal(&maintenance_cond);

	slurm_mutex_unlock(&maintenance_lock);

	slurm_thread_join(maintenance_tid);

	return SLURM_SUCCESS;
}

extern int hres_g_controller_stop(void)
{
	int rc = SLURM_SUCCESS;

	hres_g_maintenance_stop();

	/*
	 * Without g_context_lock, the plugins may need it to finish their work.
	 * ops only change in init and fini, on this same thread.
	 */
	for (int i = 0; i < g_context_num; i++) {
		int rc2 = (*(ops[i].controller_stop))();

		if (rc2 != SLURM_SUCCESS) {
			error("%s: %s failed: %s", __func__,
			      ops[i].plugin_type, slurm_strerror(rc2));
			rc = rc2;
		}
	}

	return rc;
}

/*
 * g_context_lock must be held.
 * RET 1 to delete the prep, its HRES has no plugin and its nodes have nothing
 *     to run.
 */
static int _prep_set_ctx(void *x, void *arg)
{
	hres_prep_t *prep = x;
	prep_set_ctx_args_t *args = arg;

	for (int i = 0; i < hctx_num; i++) {
		if (hctx[i].hres_id != prep->hres_id)
			continue;

		/* hctx[] is rebuilt on reconfigure, the prep is kept. */
		prep->config = hres_config_list_copy(hctx[i].config);
		prep->plugin_id = *ops[hctx[i].idx].plugin_id;

		/* The plugin fills in prep->payload for the nodes. */
		if (args->rc == SLURM_SUCCESS)
			args->rc = (*(ops[hctx[i].idx].controller_job_prerun))(
				args->job_ptr, &hctx[i], prep, args->err_msg);
		return 0;
	}

	return 1;
}

extern int hres_g_controller_job_prerun(job_record_t *job_ptr, char **err_msg)
{
	prep_set_ctx_args_t args = {
		.err_msg = err_msg,
		.job_ptr = job_ptr,
		.rc = SLURM_SUCCESS,
	};

	/* Describe the job's HRES as they are now, for the whole job. */
	FREE_NULL_LIST(job_ptr->hres_prep);
	job_ptr->hres_prep = hres_job_prep_list(job_ptr);

	if (!job_ptr->hres_prep)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&g_context_lock);
	list_delete_all(job_ptr->hres_prep, _prep_set_ctx, &args);
	slurm_mutex_unlock(&g_context_lock);

	if (!list_count(job_ptr->hres_prep))
		FREE_NULL_LIST(job_ptr->hres_prep);

	return args.rc;
}

/* g_context_lock must be held. RET context serving the prep or NULL. */
static hres_ctx_t *_find_ctx_by_prep(hres_prep_t *prep)
{
	for (int i = 0; i < hctx_num; i++) {
		if ((prep->hres_id == NO_VAL16) &&
		    !xstrcmp(prep->name, hctx[i].name)) {
			/* Restored from state, where the id is not packed. */
			prep->hres_id = hctx[i].hres_id;
		}

		if (prep->hres_id == hctx[i].hres_id)
			return &hctx[i];
	}

	return NULL;
}

/* g_context_lock must be held. */
static int _foreach_prep_epilog(void *x, void *arg)
{
	hres_prep_t *prep = x;
	foreach_prep_epilog_t *args = arg;
	hres_ctx_t *ctx = _find_ctx_by_prep(prep);
	int rc;

	if (!ctx) {
		debug("%pJ HRES %s has no plugin, skipping its epilog",
		      args->job_ptr, prep->name);
		return 0;
	}

	rc = (*(ops[ctx->idx].controller_job_epilog))(args->job_ptr, ctx, prep);
	if (rc != SLURM_SUCCESS) {
		error("%pJ HRES %s epilog failed: %s", args->job_ptr,
		      ctx->name, slurm_strerror(rc));
		args->rc = SLURM_ERROR;
	}

	return 0;
}

extern int hres_g_controller_job_epilog(job_record_t *job_ptr)
{
	foreach_prep_epilog_t args = {
		.job_ptr = job_ptr,
		.rc = SLURM_SUCCESS,
	};

	if (!job_ptr->hres_prep)
		return SLURM_SUCCESS;

	/* Reverse order of the pre-runs, the list is not used afterwards. */
	list_flip(job_ptr->hres_prep);

	slurm_mutex_lock(&g_context_lock);
	list_for_each(job_ptr->hres_prep, _foreach_prep_epilog, &args);
	slurm_mutex_unlock(&g_context_lock);

	return args.rc;
}

/*
 * How much a callback return code matters. Draining the node outranks
 * returning the job to the queue, which outranks success.
 */
static int _rc_severity(int rc)
{
	if (rc == SLURM_SUCCESS)
		return 0;
	if (rc == ESLURM_HRES_REMOTE_FAIL)
		return 1;

	return 2;
}

/* The more significant of two callback return codes. */
static int _worst_rc(int rc1, int rc2)
{
	if (_rc_severity(rc2) > _rc_severity(rc1))
		return rc2;

	return rc1;
}

/* g_context_lock must be held. */
static int _foreach_node_prolog(void *x, void *arg)
{
	hres_prep_t *prep = x;
	foreach_node_job_t *args = arg;
	int idx = _get_plugin_index(prep->plugin_id);
	int rc;

	if (idx < 0) {
		error("%s: no plugin id %u loaded for HRES %s", __func__,
		      prep->plugin_id, prep->name);
		args->rc = _worst_rc(args->rc, SLURM_ERROR);
		return 0;
	}

	/* Every prolog runs, even after a failure. */
	if (!(rc = (*(ops[idx].node_job_prolog))(prep, args->job)))
		return 0;

	error("%s: HRES %s prolog failed: %s", __func__, prep->name,
	      slurm_strerror(rc));

	args->rc = _worst_rc(args->rc, rc);

	return 0;
}

extern int hres_g_node_job_prolog(list_t *prep_list, hres_job_t *job)
{
	foreach_node_job_t args = {
		.job = job,
	};

	if (!prep_list)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&g_context_lock);
	list_for_each_ro(prep_list, _foreach_node_prolog, &args);
	slurm_mutex_unlock(&g_context_lock);

	return args.rc;
}

/* g_context_lock must be held. */
static int _foreach_node_epilog(void *x, void *arg)
{
	hres_prep_t *prep = x;
	foreach_node_job_t *args = arg;
	int idx = _get_plugin_index(prep->plugin_id);
	int rc;

	if (idx < 0) {
		error("%s: no plugin id %u loaded for HRES %s", __func__,
		      prep->plugin_id, prep->name);
		args->rc = _worst_rc(args->rc, SLURM_ERROR);
		return 0;
	}

	/* Every epilog runs, even after a failure. */
	if (!(rc = (*(ops[idx].node_job_epilog))(prep, args->job)))
		return 0;

	error("%s: HRES %s epilog failed: %s", __func__, prep->name,
	      slurm_strerror(rc));

	args->rc = _worst_rc(args->rc, rc);

	return 0;
}

extern int hres_g_node_job_epilog(list_t *prep_list, hres_job_t *job)
{
	foreach_node_job_t args = {
		.job = job,
	};
	list_t *reversed;

	if (!prep_list)
		return SLURM_SUCCESS;

	/* Epilogs run in the reverse order of the prologs. */
	reversed = list_shallow_copy(prep_list);
	list_flip(reversed);

	slurm_mutex_lock(&g_context_lock);
	list_for_each_ro(reversed, _foreach_node_epilog, &args);
	slurm_mutex_unlock(&g_context_lock);

	FREE_NULL_LIST(reversed);

	return args.rc;
}

static int _foreach_node_task_prolog(void *x, void *arg)
{
	hres_prep_t *prep = x;
	foreach_node_task_t *args = arg;
	int idx = _get_plugin_index(prep->plugin_id);
	int rc;

	if (idx < 0) {
		error("%s: no plugin id %u loaded for HRES %s", __func__,
		      prep->plugin_id, prep->name);
		args->rc = _worst_rc(args->rc, SLURM_ERROR);
		return 0;
	}

	/* Every prolog runs, even after a failure. */
	if (!(rc = (*(ops[idx].node_task_prolog))(prep, args->job,
						  args->node_tid,
						  args->global_tid)))
		return 0;

	error("%s: HRES %s task %u prolog failed: %s", __func__, prep->name,
	      args->global_tid, slurm_strerror(rc));

	args->rc = _worst_rc(args->rc, rc);

	return 0;
}

extern int hres_g_node_task_prolog(list_t *prep_list, hres_job_t *job,
				   uint32_t node_tid, uint32_t global_tid)
{
	foreach_node_task_t args = {
		.global_tid = global_tid,
		.job = job,
		.node_tid = node_tid,
	};

	if (!prep_list)
		return SLURM_SUCCESS;

	/*
	 * No g_context_lock, this runs in the task's forked child, where a
	 * mutex held by another thread at fork() is never released.
	 */
	list_for_each_ro(prep_list, _foreach_node_task_prolog, &args);

	return args.rc;
}

static int _foreach_node_user_task_prolog(void *x, void *arg)
{
	hres_prep_t *prep = x;
	foreach_node_task_t *args = arg;
	int idx = _get_plugin_index(prep->plugin_id);
	int rc;

	if (idx < 0) {
		error("%s: no plugin id %u loaded for HRES %s", __func__,
		      prep->plugin_id, prep->name);
		args->rc = _worst_rc(args->rc, SLURM_ERROR);
		return 0;
	}

	/* Every prolog runs, even after a failure. */
	if (!(rc = (*(ops[idx].node_user_task_prolog))(prep, args->job,
						       args->node_tid,
						       args->global_tid)))
		return 0;

	error("%s: HRES %s task %u user prolog failed: %s", __func__,
	      prep->name, args->global_tid, slurm_strerror(rc));

	args->rc = _worst_rc(args->rc, rc);

	return 0;
}

extern int hres_g_node_user_task_prolog(list_t *prep_list, hres_job_t *job,
					uint32_t node_tid, uint32_t global_tid)
{
	foreach_node_task_t args = {
		.global_tid = global_tid,
		.job = job,
		.node_tid = node_tid,
	};

	if (!prep_list)
		return SLURM_SUCCESS;

	/*
	 * No g_context_lock, this runs in the task's forked child, where a
	 * mutex held by another thread at fork() is never released.
	 */
	list_for_each_ro(prep_list, _foreach_node_user_task_prolog, &args);

	return args.rc;
}

static int _foreach_node_task_epilog(void *x, void *arg)
{
	hres_prep_t *prep = x;
	foreach_node_task_t *args = arg;
	int idx = _get_plugin_index(prep->plugin_id);
	int rc;

	if (idx < 0) {
		error("%s: no plugin id %u loaded for HRES %s", __func__,
		      prep->plugin_id, prep->name);
		args->rc = _worst_rc(args->rc, SLURM_ERROR);
		return 0;
	}

	/* Every epilog runs, even after a failure. */
	if (!(rc = (*(ops[idx].node_task_epilog))(prep, args->job,
						  args->node_tid,
						  args->global_tid)))
		return 0;

	error("%s: HRES %s task %u epilog failed: %s", __func__, prep->name,
	      args->global_tid, slurm_strerror(rc));

	args->rc = _worst_rc(args->rc, rc);

	return 0;
}

extern int hres_g_node_task_epilog(list_t *prep_list, hres_job_t *job,
				   uint32_t node_tid, uint32_t global_tid)
{
	foreach_node_task_t args = {
		.global_tid = global_tid,
		.job = job,
		.node_tid = node_tid,
	};
	list_t *reversed;

	if (!prep_list)
		return SLURM_SUCCESS;

	/* Epilogs run in the reverse order of the prologs. */
	reversed = list_shallow_copy(prep_list);
	list_flip(reversed);

	slurm_mutex_lock(&g_context_lock);
	list_for_each_ro(reversed, _foreach_node_task_epilog, &args);
	slurm_mutex_unlock(&g_context_lock);

	FREE_NULL_LIST(reversed);

	return args.rc;
}
