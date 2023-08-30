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

#include <dirent.h>

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/interfaces/switch.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

typedef struct slurm_switch_ops {
	uint32_t     (*plugin_id);
	int          (*state_save)        ( char *dir_name );
	int          (*state_restore)     ( char *dir_name, bool recover );

	int          (*alloc_jobinfo)     ( switch_jobinfo_t **jobinfo,
					    uint32_t job_id, uint32_t step_id );
	int          (*build_jobinfo)     ( switch_jobinfo_t *jobinfo,
					    slurm_step_layout_t *step_layout,
					    step_record_t *step_ptr );
	int          (*duplicate_jobinfo) ( switch_jobinfo_t *source,
					    switch_jobinfo_t **dest);
	void         (*free_jobinfo)      ( switch_jobinfo_t *jobinfo );
	int          (*pack_jobinfo)      ( switch_jobinfo_t *jobinfo,
					    buf_t *buffer,
					    uint16_t protocol_version );
	int          (*unpack_jobinfo)    ( switch_jobinfo_t **jobinfo,
					    buf_t *buffer,
					    uint16_t protocol_version );
	int          (*get_jobinfo)       ( switch_jobinfo_t *switch_job,
					    int key, void *data);
	int          (*job_preinit)       ( stepd_step_rec_t *step );
	int          (*job_init)          ( stepd_step_rec_t *step );
	int          (*job_suspend_test)  ( switch_jobinfo_t *jobinfo );
	void         (*job_suspend_info_get)( switch_jobinfo_t *jobinfo,
					      void *suspend_info );
	void         (*job_suspend_info_pack)( void *suspend_info,
					       buf_t *buffer,
					       uint16_t protocol_version );
	int          (*job_suspend_info_unpack)( void **suspend_info,
						 buf_t *buffer,
						 uint16_t protocol_version );
	void         (*job_suspend_info_free)( void *suspend_info );
	int          (*job_suspend)       ( void *suspend_info,
					    int max_wait );
	int          (*job_resume)        ( void *suspend_info,
					    int max_wait );
	int          (*job_fini)          ( switch_jobinfo_t *jobinfo );
	int          (*job_postfini)      ( stepd_step_rec_t *step);
	int          (*job_attach)        ( switch_jobinfo_t *jobinfo,
					    char ***env, uint32_t nodeid,
					    uint32_t procid, uint32_t nnodes,
					    uint32_t nprocs, uint32_t rank);
	int          (*step_complete)     ( switch_jobinfo_t *jobinfo,
					    char *nodelist );
	int          (*step_allocated)    ( switch_jobinfo_t *jobinfo,
					    char *nodelist );
	int          (*state_clear)       ( void );
	int          (*reconfig)          ( void );
	int          (*job_step_pre_suspend)( stepd_step_rec_t *step );
	int          (*job_step_post_suspend)( stepd_step_rec_t *step );
	int          (*job_step_pre_resume)( stepd_step_rec_t *step );
	int          (*job_step_post_resume)( stepd_step_rec_t *step );
	void         (*job_complete)      ( uint32_t job_id );
} slurm_switch_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for slurm_switch_ops_t.
 */
static const char *syms[] = {
	"plugin_id",
	"switch_p_libstate_save",
	"switch_p_libstate_restore",
	"switch_p_alloc_jobinfo",
	"switch_p_build_jobinfo",
	"switch_p_duplicate_jobinfo",
	"switch_p_free_jobinfo",
	"switch_p_pack_jobinfo",
	"switch_p_unpack_jobinfo",
	"switch_p_get_jobinfo",
	"switch_p_job_preinit",
	"switch_p_job_init",
	"switch_p_job_suspend_test",
	"switch_p_job_suspend_info_get",
	"switch_p_job_suspend_info_pack",
	"switch_p_job_suspend_info_unpack",
	"switch_p_job_suspend_info_free",
	"switch_p_job_suspend",
	"switch_p_job_resume",
	"switch_p_job_fini",
	"switch_p_job_postfini",
	"switch_p_job_attach",
	"switch_p_job_step_complete",
	"switch_p_job_step_allocated",
	"switch_p_libstate_clear",
	"switch_p_reconfig",
	"switch_p_job_step_pre_suspend",
	"switch_p_job_step_post_suspend",
	"switch_p_job_step_pre_resume",
	"switch_p_job_step_post_resume",
	"switch_p_job_complete",
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
	dynamic_plugin_data_t *jobinfo_ptr = NULL;

	jobinfo_ptr = xmalloc(sizeof(dynamic_plugin_data_t));
	jobinfo_ptr->plugin_id = plugin_id;

	return jobinfo_ptr;
}

extern int switch_init(bool only_default)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "switch";
	int i, j, plugin_cnt;
	List plugin_names = NULL;
	_plugin_args_t plugin_args = {0};

	slurm_mutex_lock( &context_lock );

	if ( switch_context )
		goto done;

	switch_context_cnt = 0;

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

extern int switch_fini(void)
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

extern int  switch_g_reconfig(void)
{
	xassert(switch_context);

	return (*(ops[switch_context_default].reconfig))( );
}

extern int  switch_g_save(char *dir_name)
{
	xassert(switch_context);

	return (*(ops[switch_context_default].state_save))( dir_name );
}

extern int  switch_g_restore(char *dir_name, bool recover)
{
	xassert(switch_context);

	return (*(ops[switch_context_default].state_restore))
		(dir_name, recover);
}

extern int  switch_g_clear(void)
{
	xassert(switch_context);

	return (*(ops[switch_context_default].state_clear))( );
}

extern int  switch_g_alloc_jobinfo(dynamic_plugin_data_t **jobinfo,
				   uint32_t job_id, uint32_t step_id)
{
	dynamic_plugin_data_t *jobinfo_ptr = NULL;

	xassert(switch_context);

	jobinfo_ptr = _create_dynamic_plugin_data(switch_context_default);
	*jobinfo    = jobinfo_ptr;

	return (*(ops[jobinfo_ptr->plugin_id].alloc_jobinfo))
		((switch_jobinfo_t **)&jobinfo_ptr->data, job_id, step_id);
}

extern int switch_g_build_jobinfo(dynamic_plugin_data_t *jobinfo,
				  slurm_step_layout_t *step_layout,
				  step_record_t *step_ptr)
{
	void *data = NULL;
	uint32_t plugin_id;

	xassert(switch_context);

	if (jobinfo) {
		data      = jobinfo->data;
		plugin_id = jobinfo->plugin_id;
	} else
		plugin_id = switch_context_default;

	return (*(ops[plugin_id].build_jobinfo))(data, step_layout, step_ptr);
}

extern int  switch_g_duplicate_jobinfo(dynamic_plugin_data_t *source,
				       dynamic_plugin_data_t **dest)
{
	dynamic_plugin_data_t *dest_ptr = NULL;
	uint32_t plugin_id = source->plugin_id;

	xassert(switch_context);

	dest_ptr = _create_dynamic_plugin_data(plugin_id);
	*dest = dest_ptr;

	return (*(ops[plugin_id].duplicate_jobinfo))(
		source->data, (switch_jobinfo_t **)&dest_ptr->data);
}

extern void switch_g_free_jobinfo(dynamic_plugin_data_t *jobinfo)
{
	xassert(switch_context);

	if (jobinfo) {
		if (jobinfo->data)
			(*(ops[jobinfo->plugin_id].free_jobinfo))
				(jobinfo->data);
		xfree(jobinfo);
	}
}

extern int switch_g_pack_jobinfo(dynamic_plugin_data_t *jobinfo, buf_t *buffer,
				 uint16_t protocol_version)
{
	void *data = NULL;
	uint32_t plugin_id;

	xassert(switch_context);

	if (jobinfo) {
		data      = jobinfo->data;
		plugin_id = jobinfo->plugin_id;
	} else
		plugin_id = switch_context_default;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(*(ops[plugin_id].plugin_id), buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		return SLURM_ERROR;
	}

	return (*(ops[plugin_id].pack_jobinfo))(data, buffer, protocol_version);
}

extern int switch_g_unpack_jobinfo(dynamic_plugin_data_t **jobinfo,
				   buf_t *buffer, uint16_t protocol_version)
{
	dynamic_plugin_data_t *jobinfo_ptr = NULL;

	xassert(switch_context);

	jobinfo_ptr = xmalloc(sizeof(dynamic_plugin_data_t));
	*jobinfo = jobinfo_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		int i;
		uint32_t plugin_id;
		safe_unpack32(&plugin_id, buffer);
		for (i = 0; i < switch_context_cnt; i++) {
			if (*(ops[i].plugin_id) == plugin_id) {
				jobinfo_ptr->plugin_id = i;
				break;
			}
		}
		if (i >= switch_context_cnt) {
			error("we don't have switch plugin type %u", plugin_id);
			goto unpack_error;
		}
	} else
		goto unpack_error;

	if  ((*(ops[jobinfo_ptr->plugin_id].unpack_jobinfo))
	     ((switch_jobinfo_t **)&jobinfo_ptr->data, buffer,
	      protocol_version))
		goto unpack_error;

	/*
	 * Free nodeinfo_ptr if it is different from local cluster as it is not
	 * relevant to this cluster.
	 */
	if ((jobinfo_ptr->plugin_id != switch_context_default) &&
	    running_in_slurmctld()) {
		switch_g_free_jobinfo(jobinfo_ptr);
		*jobinfo = _create_dynamic_plugin_data(switch_context_default);
	}


	return SLURM_SUCCESS;

unpack_error:
	switch_g_free_jobinfo(jobinfo_ptr);
	*jobinfo = NULL;
	error("%s: unpack error", __func__);
	return SLURM_ERROR;
}

extern int  switch_g_get_jobinfo(dynamic_plugin_data_t *jobinfo,
				 int data_type, void *data)
{
	void *jobdata = NULL;
	uint32_t plugin_id;

	xassert(switch_context);

	if (jobinfo) {
		jobdata   = jobinfo->data;
		plugin_id = jobinfo->plugin_id;
	} else
		plugin_id = switch_context_default;

	return (*(ops[plugin_id].get_jobinfo))(jobdata, data_type, data);
}

extern int switch_g_job_preinit(stepd_step_rec_t *step)
{
	xassert(switch_context);

	return (*(ops[switch_context_default].job_preinit))(step);
}

extern int switch_g_job_init(stepd_step_rec_t *step)
{
	xassert(switch_context);

	return (*(ops[switch_context_default].job_init))(step);
}

extern int switch_g_job_suspend_test(dynamic_plugin_data_t *jobinfo)
{
	void *data = NULL;
	uint32_t plugin_id;

	xassert(switch_context);

	if (jobinfo) {
		data      = jobinfo->data;
		plugin_id = jobinfo->plugin_id;
	} else
		plugin_id = switch_context_default;

	return (*(ops[plugin_id].job_suspend_test)) (data);
}

extern void switch_g_job_suspend_info_get(dynamic_plugin_data_t *jobinfo,
					  void **suspend_info)
{
	void *data = NULL;
	uint32_t plugin_id;

	xassert(switch_context);

	if (jobinfo) {
		data      = jobinfo->data;
		plugin_id = jobinfo->plugin_id;
	} else
		plugin_id = switch_context_default;

	(*(ops[plugin_id].job_suspend_info_get)) (data, suspend_info);
}

extern void switch_g_job_suspend_info_pack(void *suspend_info, buf_t *buffer,
					   uint16_t protocol_version)
{
	xassert(switch_context);

	(*(ops[switch_context_default].job_suspend_info_pack))
		(suspend_info, buffer, protocol_version);
}

extern int switch_g_job_suspend_info_unpack(void **suspend_info, buf_t *buffer,
					    uint16_t protocol_version)
{
	xassert(switch_context);

	return (*(ops[switch_context_default].job_suspend_info_unpack))
		(suspend_info, buffer, protocol_version);
}

extern void switch_g_job_suspend_info_free(void *suspend_info)
{
	xassert(switch_context);

	(*(ops[switch_context_default].job_suspend_info_free)) (suspend_info);
}

extern int switch_g_job_suspend(void *suspend_info, int max_wait)
{
	xassert(switch_context);

	return (*(ops[switch_context_default].job_suspend))
		(suspend_info, max_wait);
}

extern int switch_g_job_resume(void *suspend_info, int max_wait)
{
	xassert(switch_context);

	return (*(ops[switch_context_default].job_resume))
		(suspend_info, max_wait);
}

extern int switch_g_job_fini(dynamic_plugin_data_t *jobinfo)
{
	void *data = NULL;
	uint32_t plugin_id;

	xassert(switch_context);

	if (jobinfo) {
		data      = jobinfo->data;
		plugin_id = jobinfo->plugin_id;
	} else
		plugin_id = switch_context_default;

	return (*(ops[plugin_id].job_fini)) (data);
}

extern int switch_g_job_postfini(stepd_step_rec_t *step)
{
	xassert(switch_context);

	return (*(ops[switch_context_default].job_postfini))(step);
}

extern int switch_g_job_attach(dynamic_plugin_data_t *jobinfo, char ***env,
			       uint32_t nodeid, uint32_t procid,
			       uint32_t nnodes, uint32_t nprocs, uint32_t gid)
{
	void *data = NULL;
	uint32_t plugin_id;

	xassert(switch_context);

	if (jobinfo) {
		data      = jobinfo->data;
		plugin_id = jobinfo->plugin_id;
	} else
		plugin_id = switch_context_default;

	return (*(ops[plugin_id].job_attach))
		(data, env, nodeid, procid, nnodes, nprocs, gid);
}

extern int switch_g_job_step_complete(dynamic_plugin_data_t *jobinfo,
				      char *nodelist)
{
	void *data = NULL;
	uint32_t plugin_id;

	xassert(switch_context);

	if (jobinfo) {
		data      = jobinfo->data;
		plugin_id = jobinfo->plugin_id;
	} else
		plugin_id = switch_context_default;

	return (*(ops[plugin_id].step_complete))(data, nodelist);
}

extern int switch_g_job_step_allocated(dynamic_plugin_data_t *jobinfo,
				       char *nodelist)
{
	void *data = NULL;
	uint32_t plugin_id;

	xassert(switch_context);

	if (jobinfo) {
		data      = jobinfo->data;
		plugin_id = jobinfo->plugin_id;
	} else
		plugin_id = switch_context_default;

	return (*(ops[plugin_id].step_allocated))(data, nodelist);
}

extern int switch_g_job_step_pre_suspend(stepd_step_rec_t *step)
{
	xassert(switch_context);

	return (*(ops[switch_context_default].job_step_pre_suspend))(step);
}

extern int switch_g_job_step_post_suspend(stepd_step_rec_t *step)
{
	xassert(switch_context);

	return (*(ops[switch_context_default].job_step_post_suspend))(step);
}

extern int switch_g_job_step_pre_resume(stepd_step_rec_t *step)
{
	xassert(switch_context);

	return (*(ops[switch_context_default].job_step_pre_resume))(step);
}

extern int switch_g_job_step_post_resume(stepd_step_rec_t *step)
{
	xassert(switch_context);

	return (*(ops[switch_context_default].job_step_post_resume))(step);
}

extern void switch_g_job_complete(uint32_t job_id)
{
	xassert(switch_context);

	(*(ops[switch_context_default].job_complete))(job_id);
}
