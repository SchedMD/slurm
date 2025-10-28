/*****************************************************************************\
 *  src/common/switch.c - Generic switch (switch_g) for slurm
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/interfaces/switch.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/* opaque type */
typedef struct switch_stepinfo switch_stepinfo_t;

typedef struct slurm_switch_ops {
	uint32_t     (*plugin_id);
	int          (*state_save)        ( void );
	int          (*state_restore)     ( bool recover );
	void         (*jobinfo_pack)      ( void *switch_jobinfo,
					    buf_t *buffer,
					    uint16_t protocol_version );
	int          (*jobinfo_unpack)    ( void **switch_jobinfo,
					    buf_t *buffer,
					    uint16_t protocol_version );
	void (*jobinfo_free)(void *switch_jobinfo);
	int          (*stepinfo_build)    ( switch_stepinfo_t **stepinfo,
					    void *switch_jobinfo,
					    step_record_t *step_ptr );
	void         (*stepinfo_duplicate)( switch_stepinfo_t *source,
					    switch_stepinfo_t **dest);
	void         (*stepinfo_free)     ( switch_stepinfo_t *stepinfo );
	void         (*stepinfo_pack)     ( switch_stepinfo_t *stepinfo,
					    buf_t *buffer,
					    uint16_t protocol_version );
	int          (*stepinfo_unpack)   ( switch_stepinfo_t **stepinfo,
					    buf_t *buffer,
					    uint16_t protocol_version );
	bool (*setup_special_steps)(void);
	int          (*job_preinit)       ( stepd_step_rec_t *step );
	int          (*job_init)          ( stepd_step_rec_t *step );
	int          (*job_postfini)      ( stepd_step_rec_t *step);
	int          (*job_attach)        ( switch_stepinfo_t *stepinfo,
					    char ***env, uint32_t nodeid,
					    uint32_t procid, uint32_t nnodes,
					    uint32_t nprocs, uint32_t rank);
	int          (*step_complete)     ( switch_stepinfo_t *stepinfo,
					    char *nodelist );
	int (*job_start)(job_record_t *job_ptr, bool test_only);
	void         (*job_complete)      ( job_record_t *job_ptr );
	int          (*fs_init)           ( stepd_step_rec_t *step );
	void	     (*extern_step_fini)  ( uint32_t job_id);
} slurm_switch_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for slurm_switch_ops_t.
 */
static const char *syms[] = {
	"plugin_id",
	"switch_p_save",
	"switch_p_restore",
	"switch_p_jobinfo_pack",
	"switch_p_jobinfo_unpack",
	"switch_p_jobinfo_free",
	"switch_p_stepinfo_build",
	"switch_p_stepinfo_duplicate",
	"switch_p_stepinfo_free",
	"switch_p_stepinfo_pack",
	"switch_p_stepinfo_unpack",
	"switch_p_setup_special_steps",
	"switch_p_job_preinit",
	"switch_p_job_init",
	"switch_p_job_postfini",
	"switch_p_job_attach",
	"switch_p_job_step_complete",
	"switch_p_job_start",
	"switch_p_job_complete",
	"switch_p_fs_init",
	"switch_p_extern_step_fini",
};

static slurm_switch_ops_t  *ops            = NULL;
static plugin_context_t   **switch_context = NULL;
static pthread_mutex_t      context_lock = PTHREAD_MUTEX_INITIALIZER;

static int switch_context_cnt     = -1;
static int switch_context_default = -1;

typedef struct _plugin_args {
	char *plugin_type;
	char *default_plugin;
} _plugin_args_t;

static int _load_plugins(void *x, void *arg)
{
	char *plugin_name     = (char *)x;
	_plugin_args_t *pargs = (_plugin_args_t *)arg;

	switch_context[switch_context_cnt] =
		plugin_context_create(pargs->plugin_type, plugin_name,
				      (void **)&ops[switch_context_cnt], syms,
				      sizeof(syms));

	if (switch_context[switch_context_cnt]) {
		/* set the default */
		if (!xstrcmp(plugin_name, pargs->default_plugin))
			switch_context_default = switch_context_cnt;
		switch_context_cnt++;
	}

	return 0;
}

static dynamic_plugin_data_t *_create_dynamic_plugin_data(uint32_t plugin_id)
{
	dynamic_plugin_data_t *stepinfo_ptr = NULL;

	stepinfo_ptr = xmalloc(sizeof(dynamic_plugin_data_t));
	stepinfo_ptr->plugin_id = plugin_id;

	return stepinfo_ptr;
}

extern int switch_g_init(bool only_default)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "switch";
	int i, j, plugin_cnt;
	list_t *plugin_names = NULL;
	_plugin_args_t plugin_args = {0};

	slurm_mutex_lock( &context_lock );

	if (switch_context_cnt >= 0)
		goto done;

	switch_context_cnt = 0;

	if (!slurm_conf.switch_type)
		goto done;

	plugin_args.plugin_type    = plugin_type;
	plugin_args.default_plugin = slurm_conf.switch_type;

	if (only_default) {
		plugin_names = list_create(xfree_ptr);
		list_append(plugin_names, xstrdup(slurm_conf.switch_type));
	} else {
		plugin_names = plugin_get_plugins_of_type(plugin_type);
	}
	if (plugin_names && (plugin_cnt = list_count(plugin_names))) {
		ops = xcalloc(plugin_cnt, sizeof(slurm_switch_ops_t));
		switch_context = xcalloc(plugin_cnt,
					 sizeof(plugin_context_t *));

		list_for_each(plugin_names, _load_plugins, &plugin_args);
	}


	if (switch_context_default == -1)
		fatal("Can't find plugin for %s", slurm_conf.switch_type);

	/* Ensure that plugin_id is valid and unique */
	for (i = 0; i < switch_context_cnt; i++) {
		for (j = i+1; j < switch_context_cnt; j++) {
			if (*(ops[i].plugin_id) !=
			    *(ops[j].plugin_id))
				continue;
			fatal("switchPlugins: Duplicate plugin_id %u for "
			      "%s and %s",
			      *(ops[i].plugin_id),
			      switch_context[i]->type,
			      switch_context[j]->type);
		}
		if (*(ops[i].plugin_id) < 100) {
			fatal("switchPlugins: Invalid plugin_id %u (<100) %s",
			      *(ops[i].plugin_id),
			      switch_context[i]->type);
		}
	}

done:
	slurm_mutex_unlock( &context_lock );
	FREE_NULL_LIST(plugin_names);

	return retval;
}

extern int switch_g_fini(void)
{
	int rc = SLURM_SUCCESS, i;

	slurm_mutex_lock( &context_lock );
	if (!switch_context)
		goto fini;

	for (i = 0; i < switch_context_cnt; i++) {
		rc |= plugin_context_destroy(switch_context[i]);
	}
	xfree(switch_context);
	xfree(ops);
	switch_context_cnt = -1;
fini:
	slurm_mutex_unlock( &context_lock );
	return rc;
}

extern int switch_g_save(void)
{
	xassert(switch_context_cnt >= 0);

	if (!switch_context_cnt)
		return SLURM_SUCCESS;

	return (*(ops[switch_context_default].state_save))();
}

extern int switch_g_restore(bool recover)
{
	xassert(switch_context_cnt >= 0);

	if (!switch_context_cnt)
		return SLURM_SUCCESS;

	return (*(ops[switch_context_default].state_restore))(recover);
}

/*
 * These are designed so that the payload will be skipped if the plugin
 * is unavailable.
 */
extern void switch_g_jobinfo_pack(void *switch_jobinfo, buf_t *buffer,
				  uint16_t protocol_version)
{
	dynamic_plugin_data_t plugin_data = {
		.data = switch_jobinfo,
	};

	if (!switch_context_cnt) {
		dynamic_plugin_data_pack(NULL, NULL, buffer, protocol_version);
		return;
	}

	plugin_data.plugin_id = *ops[switch_context_default].plugin_id;

	dynamic_plugin_data_pack(&plugin_data,
				 *(ops[switch_context_default].jobinfo_pack),
				 buffer, protocol_version);
}

static dynamic_plugin_data_unpack_func _get_unpack_func(
	uint32_t plugin_id)
{
	if (plugin_id != *(ops[switch_context_default].plugin_id))
		return NULL;

	return *(ops[switch_context_default].jobinfo_unpack);
}

extern int switch_g_jobinfo_unpack(void **switch_jobinfo, buf_t *buffer,
				   uint16_t protocol_version)
{
	dynamic_plugin_data_t *plugin_data = NULL;
	int rc;

	if (!switch_context_cnt) {
		return dynamic_plugin_data_unpack(NULL, NULL, buffer,
						  protocol_version);
	}

	rc = dynamic_plugin_data_unpack(&plugin_data, _get_unpack_func, buffer,
					protocol_version);

	if ((rc == SLURM_SUCCESS) && plugin_data) {
		*switch_jobinfo = plugin_data->data;
	}

	xfree(plugin_data);

	return rc;
}

/* Free switch_jobinfo struct when switch_g_job_complete can't be used */
extern void switch_g_jobinfo_free(job_record_t *job_ptr)
{
	xassert(switch_context_cnt >= 0);
	if (!switch_context_cnt)
		return;

	(*(ops[switch_context_default].jobinfo_free))(job_ptr);
}

extern int switch_g_stepinfo_build(dynamic_plugin_data_t **stepinfo,
				   void *switch_jobinfo,
				   step_record_t *step_ptr)
{
	void **stepinfo_data = NULL;
	uint32_t plugin_id = switch_context_default;

	xassert(switch_context_cnt >= 0);

	if (!switch_context_cnt)
		return SLURM_SUCCESS;

	*stepinfo = _create_dynamic_plugin_data(plugin_id);
	stepinfo_data = &(*stepinfo)->data;

	return (*(ops[plugin_id].stepinfo_build))(
		(switch_stepinfo_t **) stepinfo_data, switch_jobinfo, step_ptr);
}

extern void switch_g_stepinfo_duplicate(dynamic_plugin_data_t *source,
					dynamic_plugin_data_t **dest)
{
	dynamic_plugin_data_t *dest_ptr = NULL;
	uint32_t plugin_id = source->plugin_id;

	xassert(switch_context_cnt >= 0);

	if (!switch_context_cnt)
		return;

	dest_ptr = _create_dynamic_plugin_data(plugin_id);
	*dest = dest_ptr;

	(*(ops[plugin_id].stepinfo_duplicate))
		(source->data, (switch_stepinfo_t **) &dest_ptr->data);
}

extern void switch_g_stepinfo_free(dynamic_plugin_data_t *stepinfo)
{
	if (!switch_context_cnt)
		return;

	if (stepinfo) {
		if (stepinfo->data)
			(*(ops[stepinfo->plugin_id].stepinfo_free))
				(stepinfo->data);
		xfree(stepinfo);
	}
}

extern void switch_g_stepinfo_pack(dynamic_plugin_data_t *stepinfo,
				   buf_t *buffer, uint16_t protocol_version)
{
	void *data = NULL;
	uint32_t length_position = 0, start = 0, end = 0, plugin_id;

	xassert(switch_context_cnt >= 0);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		length_position = get_buf_offset(buffer);
		pack32(0, buffer);
		start = get_buf_offset(buffer);

		if (!switch_context_cnt) {
			return;
		}

		if (stepinfo) {
			data = stepinfo->data;
			plugin_id = stepinfo->plugin_id;
		} else
			plugin_id = switch_context_default;

		pack32(*(ops[plugin_id].plugin_id), buffer);

		(*(ops[plugin_id].stepinfo_pack))(
			data, buffer, protocol_version);

		end = get_buf_offset(buffer);
		set_buf_offset(buffer, length_position);
		pack32(end - start, buffer);
		set_buf_offset(buffer, end);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		return;
	}
}

extern int switch_g_stepinfo_unpack(dynamic_plugin_data_t **stepinfo,
				    buf_t *buffer, uint16_t protocol_version)
{
	int i;
	uint32_t length = 0, switch_stepinfo_end = 0, plugin_id;
	dynamic_plugin_data_t *stepinfo_ptr = NULL;

	xassert(switch_context_cnt >= 0);

	if (protocol_version < SLURM_MIN_PROTOCOL_VERSION)
		goto unpack_error;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&length, buffer);
		switch_stepinfo_end = get_buf_offset(buffer) + length;
		if (!(running_in_slurmstepd() || running_in_slurmctld()) ||
		    !length || !switch_context_cnt)
			goto skip_buf;

		if (remaining_buf(buffer) < length)
			return SLURM_ERROR;

		stepinfo_ptr = xmalloc(sizeof(dynamic_plugin_data_t));
		*stepinfo = stepinfo_ptr;

		safe_unpack32(&plugin_id, buffer);
		for (i = 0; i < switch_context_cnt; i++) {
			if (*(ops[i].plugin_id) == plugin_id) {
				stepinfo_ptr->plugin_id = i;
				break;
			}
		}

		if (i >= switch_context_cnt) {
			/*
			 * We were sent a plugin that we don't know how to
			 * handle so skip it if possible.
			 */
			debug("we don't have switch plugin type %u", plugin_id);
			goto skip_buf;
		}

		if ((*(ops[stepinfo_ptr->plugin_id].stepinfo_unpack))
		    ((switch_stepinfo_t **) &stepinfo_ptr->data, buffer,
		     protocol_version))
			goto unpack_error;
	}

	/*
	 * Free nodeinfo_ptr if it is different from local cluster as it is not
	 * relevant to this cluster.
	 */
	if ((stepinfo_ptr->plugin_id != switch_context_default) &&
	    running_in_slurmctld()) {
		switch_g_stepinfo_free(stepinfo_ptr);
		*stepinfo = _create_dynamic_plugin_data(switch_context_default);
	}

	return SLURM_SUCCESS;

skip_buf:
	if (length) {
		debug("%s: skipping switch_stepinfo data (%u)",
		      __func__, length);
		set_buf_offset(buffer, switch_stepinfo_end);
	}
	return SLURM_SUCCESS;

unpack_error:
	switch_g_stepinfo_free(stepinfo_ptr);
	*stepinfo = NULL;
	error("%s: unpack error", __func__);
	return SLURM_ERROR;
}

extern bool switch_g_setup_special_steps(void)
{
	xassert(switch_context_cnt >= 0);

	if (!switch_context_cnt)
		return false;

	return (*(ops[switch_context_default].setup_special_steps))();
}

extern int switch_g_job_preinit(stepd_step_rec_t *step)
{
	xassert(switch_context_cnt >= 0);

	if (!switch_context_cnt)
		return SLURM_SUCCESS;

	return (*(ops[switch_context_default].job_preinit))(step);
}

extern int switch_g_job_init(stepd_step_rec_t *step)
{
	xassert(switch_context_cnt >= 0);

	if (!switch_context_cnt)
		return SLURM_SUCCESS;

	return (*(ops[switch_context_default].job_init))(step);
}

extern int switch_g_job_postfini(stepd_step_rec_t *step)
{
	xassert(switch_context_cnt >= 0);

	if (!switch_context_cnt)
		return SLURM_SUCCESS;

	return (*(ops[switch_context_default].job_postfini))(step);
}

extern int switch_g_job_attach(dynamic_plugin_data_t *stepinfo, char ***env,
			       uint32_t nodeid, uint32_t procid,
			       uint32_t nnodes, uint32_t nprocs, uint32_t gid)
{
	void *data = NULL;
	uint32_t plugin_id;

	xassert(switch_context_cnt >= 0);

	if (!switch_context_cnt)
		return SLURM_SUCCESS;

	if (stepinfo) {
		data = stepinfo->data;
		plugin_id = stepinfo->plugin_id;
	} else
		plugin_id = switch_context_default;

	return (*(ops[plugin_id].job_attach))
		(data, env, nodeid, procid, nnodes, nprocs, gid);
}

extern int switch_g_job_step_complete(dynamic_plugin_data_t *stepinfo,
				      char *nodelist)
{
	void *data = NULL;
	uint32_t plugin_id;

	xassert(switch_context_cnt >= 0);

	if (!switch_context_cnt)
		return SLURM_SUCCESS;

	if (stepinfo) {
		data = stepinfo->data;
		plugin_id = stepinfo->plugin_id;
	} else
		plugin_id = switch_context_default;

	return (*(ops[plugin_id].step_complete))(data, nodelist);
}

extern int switch_g_job_start(job_record_t *job_ptr, bool test_only)
{
	xassert(switch_context_cnt >= 0);

	if (!switch_context_cnt)
		return SLURM_SUCCESS;

	return (*(ops[switch_context_default].job_start))(job_ptr, test_only);
}

extern void switch_g_job_complete(job_record_t *job_ptr)
{
	xassert(switch_context_cnt >= 0);

	if (!switch_context_cnt)
		return;

	(*(ops[switch_context_default].job_complete))(job_ptr);
}

extern int switch_g_fs_init(stepd_step_rec_t *step)
{
	xassert(switch_context_cnt >= 0);

	if (!switch_context_cnt)
		return SLURM_SUCCESS;

	return (*(ops[switch_context_default].fs_init))(step);
}

extern void switch_g_extern_step_fini(uint32_t job_id)
{
	xassert(switch_context_cnt >= 0);

	if (!switch_context_cnt)
		return;

	(*(ops[switch_context_default].extern_step_fini))(job_id);
}
