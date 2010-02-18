/*****************************************************************************\
 *  src/common/switch.c - Generic switch (interconnect) for slurm
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/switch.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * WARNING:  Do not change the order of these fields or add additional
 * fields at the beginning of the structure.  If you do, job completion
 * logging plugins will stop working.  If you need to add fields, add them
 * at the end of the structure.
 */
typedef struct slurm_switch_ops {
	int          (*state_save)        ( char *dir_name );
	int          (*state_restore)     ( char *dir_name, bool recover );

	int          (*alloc_jobinfo)     ( switch_jobinfo_t **jobinfo );
	int          (*build_jobinfo)     ( switch_jobinfo_t *jobinfo,
						char *nodelist,
						uint16_t *tasks_per_node,
						int cyclic_alloc,
						char *network);
	switch_jobinfo_t *(*copy_jobinfo)  ( switch_jobinfo_t *jobinfo );
	void         (*free_jobinfo)      ( switch_jobinfo_t *jobinfo );
	int          (*pack_jobinfo)      ( switch_jobinfo_t *jobinfo,
						Buf buffer );
	int          (*unpack_jobinfo)    ( switch_jobinfo_t *jobinfo,
						Buf buffer );
	int          (*get_jobinfo)       ( switch_jobinfo_t *switch_job,
						int key, void *data);
	void         (*print_jobinfo)     ( FILE *fp,
						switch_jobinfo_t *jobinfo );
	char *       (*string_jobinfo)    ( switch_jobinfo_t *jobinfo,
						char *buf, size_t size);
	int          (*node_init)         ( void );
	int          (*node_fini)         ( void );
	int          (*job_preinit)       ( switch_jobinfo_t *jobinfo );
	int          (*job_init)          ( switch_jobinfo_t *jobinfo,
						uid_t uid );
	int          (*job_fini)          ( switch_jobinfo_t *jobinfo );
	int          (*job_postfini)      ( switch_jobinfo_t *jobinfo,
						uid_t pgid,
						uint32_t job_id,
						uint32_t step_id );
	int          (*job_attach)        ( switch_jobinfo_t *jobinfo,
						char ***env, uint32_t nodeid,
						uint32_t procid, uint32_t nnodes,
						uint32_t nprocs, uint32_t rank);
	char *	     (*switch_strerror)   ( int errnum );
	int          (*switch_errno)      ( void );
	int          (*clear_node)        ( void );
	int          (*alloc_nodeinfo)    ( switch_node_info_t **nodeinfo );
	int          (*build_nodeinfo)    ( switch_node_info_t *nodeinfo );
	int          (*pack_nodeinfo)     ( switch_node_info_t *nodeinfo,
						Buf buffer );
	int          (*unpack_nodeinfo)   ( switch_node_info_t *nodeinfo,
						Buf buffer );
	int          (*free_nodeinfo)     ( switch_node_info_t **nodeinfo );
	char *       (*sprintf_nodeinfo)  ( switch_node_info_t *nodeinfo,
						char *buf, size_t size );
	int          (*step_complete)     ( switch_jobinfo_t *jobinfo,
						char *nodelist );
	int          (*step_part_comp)    ( switch_jobinfo_t *jobinfo,
						char *nodelist );
	bool         (*part_comp)         ( void );
	int          (*step_allocated)    ( switch_jobinfo_t *jobinfo,
					        char *nodelist );
	int          (*state_clear)       ( void );
	int          (*slurmctld_init)    ( void );
	int          (*slurmd_init)       ( void );
	int          (*slurmd_step_init)  ( void );
} slurm_switch_ops_t;

struct slurm_switch_context {
	char *			switch_type;
	plugrack_t              plugin_list;
	plugin_handle_t         cur_plugin;
	int                     switch_errno;
	slurm_switch_ops_t	ops;
};

static slurm_switch_context_t *g_context = NULL;
static pthread_mutex_t      context_lock = PTHREAD_MUTEX_INITIALIZER;


static slurm_switch_context_t *
_slurm_switch_context_create(const char *switch_type)
{
	slurm_switch_context_t *c;

	if ( switch_type == NULL ) {
		debug3( "_slurm_switch_context_create: no switch type" );
		return NULL;
	}

	c = xmalloc( sizeof( struct slurm_switch_context ) );

	c->switch_errno = SLURM_SUCCESS;

	/* Copy the job completion authentication type. */
	c->switch_type = xstrdup( switch_type );
	if (c->switch_type == NULL ) {
		debug3( "can't make local copy of switch type" );
		xfree( c );
		return NULL;
	}

	/* Plugin rack is demand-loaded on first reference. */
	c->plugin_list = NULL;
	c->cur_plugin = PLUGIN_INVALID_HANDLE;

	return c;
}

static int
_slurm_switch_context_destroy( slurm_switch_context_t *c )
{
	int rc = SLURM_SUCCESS;
	/*
	 * Must check return code here because plugins might still
	 * be loaded and active.
	 */
	if ( c->plugin_list ) {
		if ( plugrack_destroy( c->plugin_list ) != SLURM_SUCCESS ) {
			rc = SLURM_ERROR;
		}
	} else {
		plugin_unload(c->cur_plugin);
	}

	xfree( c->switch_type );
	xfree( c );

	return rc;
}

/*
 * Resolve the operations from the plugin.
 */
static slurm_switch_ops_t *
_slurm_switch_get_ops( slurm_switch_context_t *c )
{
	/*
	 * These strings must be kept in the same order as the fields
	 * declared for slurm_switch_ops_t.
	 */
	static const char *syms[] = {
		"switch_p_libstate_save",
		"switch_p_libstate_restore",
		"switch_p_alloc_jobinfo",
		"switch_p_build_jobinfo",
		"switch_p_copy_jobinfo",
		"switch_p_free_jobinfo",
		"switch_p_pack_jobinfo",
		"switch_p_unpack_jobinfo",
		"switch_p_get_jobinfo",
		"switch_p_print_jobinfo",
		"switch_p_sprint_jobinfo",
		"switch_p_node_init",
		"switch_p_node_fini",
		"switch_p_job_preinit",
		"switch_p_job_init",
		"switch_p_job_fini",
		"switch_p_job_postfini",
		"switch_p_job_attach",
		"switch_p_strerror",
		"switch_p_get_errno",
		"switch_p_clear_node_state",
		"switch_p_alloc_node_info",
		"switch_p_build_node_info",
		"switch_p_pack_node_info",
		"switch_p_unpack_node_info",
		"switch_p_free_node_info",
		"switch_p_sprintf_node_info",
		"switch_p_job_step_complete",
		"switch_p_job_step_part_comp",
		"switch_p_part_comp",
		"switch_p_job_step_allocated",
		"switch_p_libstate_clear",
		"switch_p_slurmctld_init",
		"switch_p_slurmd_init",
		"switch_p_slurmd_step_init"
	};
	int n_syms = sizeof( syms ) / sizeof( char * );

	/* Find the correct plugin. */
        c->cur_plugin = plugin_load_and_link(c->switch_type, n_syms, syms,
					     (void **) &c->ops);
        if ( c->cur_plugin != PLUGIN_INVALID_HANDLE )
        	return &c->ops;

	error("Couldn't find the specified plugin name for %s "
	      "looking at all files",
	      c->switch_type);

	/* Get the plugin list, if needed. */
	if ( c->plugin_list == NULL ) {
		char *plugin_dir;
		c->plugin_list = plugrack_create();
		if ( c->plugin_list == NULL ) {
			verbose( "Unable to create a plugin manager" );
			return NULL;
		}

		plugrack_set_major_type( c->plugin_list, "switch" );
		plugrack_set_paranoia( c->plugin_list,
				PLUGRACK_PARANOIA_NONE,
				 0 );
		plugin_dir = slurm_get_plugin_dir();
		plugrack_read_dir( c->plugin_list, plugin_dir );
		xfree(plugin_dir);
	}

	/* Find the correct plugin. */
	c->cur_plugin =
		plugrack_use_by_type( c->plugin_list, c->switch_type );
	if ( c->cur_plugin == PLUGIN_INVALID_HANDLE ) {
		verbose( "can't find a plugin for type %s", c->switch_type );
		return NULL;
	}

	/* Dereference the API. */
	if ( plugin_get_syms( c->cur_plugin,
				n_syms,
				syms,
				(void **) &c->ops ) < n_syms ) {
		verbose( "incomplete switch plugin detected" );
		return NULL;
	}

	return &c->ops;
}

extern int switch_init( void )
{
	int retval = SLURM_SUCCESS;
	char *switch_type = NULL;

	slurm_mutex_lock( &context_lock );

	if ( g_context )
		goto done;

	switch_type = slurm_get_switch_type();
	g_context = _slurm_switch_context_create( switch_type );
	if ( g_context == NULL ) {
		error( "cannot create a context for %s", switch_type );
		retval = SLURM_ERROR;
		goto done;
	}

	if ( _slurm_switch_get_ops( g_context ) == NULL ) {
		error( "cannot resolve plugin operations for %s", switch_type );
		_slurm_switch_context_destroy( g_context );
		g_context = NULL;
		retval = SLURM_ERROR;
	}

      done:
	slurm_mutex_unlock( &context_lock );
	xfree(switch_type);
	return retval;
}

extern int switch_fini(void)
{
	int rc;

	if (!g_context)
		return SLURM_SUCCESS;

	rc = _slurm_switch_context_destroy(g_context);
	return rc;
}

extern int  switch_save(char *dir_name)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.state_save))( dir_name );
}

extern int  switch_restore(char *dir_name, bool recover)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.state_restore))( dir_name, recover );
}

extern int  switch_clear(void)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.state_clear))( );
}

extern int  switch_alloc_jobinfo(switch_jobinfo_t **jobinfo)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.alloc_jobinfo))( jobinfo );
}

extern int  switch_build_jobinfo(switch_jobinfo_t *jobinfo,
		char *nodelist, uint16_t *tasks_per_node,
		int cyclic_alloc, char *network)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.build_jobinfo))( jobinfo, nodelist,
			tasks_per_node, cyclic_alloc, network );
}

extern switch_jobinfo_t *switch_copy_jobinfo(switch_jobinfo_t *jobinfo)
{
	if ( switch_init() < 0 )
		return NULL;

	return (*(g_context->ops.copy_jobinfo))( jobinfo );
}

extern void switch_free_jobinfo(switch_jobinfo_t *jobinfo)
{
	if ( switch_init() < 0 )
		return;

	(*(g_context->ops.free_jobinfo))( jobinfo );
}

extern int switch_pack_jobinfo(switch_jobinfo_t *jobinfo, Buf buffer)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.pack_jobinfo))( jobinfo, buffer );
}

extern int switch_unpack_jobinfo(switch_jobinfo_t *jobinfo, Buf buffer)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.unpack_jobinfo))( jobinfo, buffer );
}

extern int  switch_g_get_jobinfo(switch_jobinfo_t *jobinfo,
	int data_type, void *data)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.get_jobinfo))( jobinfo, data_type, data);
}

extern void switch_print_jobinfo(FILE *fp, switch_jobinfo_t *jobinfo)
{
	if ( switch_init() < 0 )
		return;

	(*(g_context->ops.print_jobinfo)) (fp, jobinfo);
}

extern char *switch_sprint_jobinfo( switch_jobinfo_t *jobinfo,
	       char *buf, size_t size)
{
	if ( switch_init() < 0 )
		return NULL;

	return (*(g_context->ops.string_jobinfo)) (jobinfo, buf, size);
}

extern int interconnect_node_init(void)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.node_init)) ();
}

extern int interconnect_node_fini(void)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.node_fini)) ();
}

extern int interconnect_preinit(switch_jobinfo_t *jobinfo)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.job_preinit)) (jobinfo);
}

extern int interconnect_init(switch_jobinfo_t *jobinfo, uid_t uid)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.job_init)) (jobinfo, uid);
}

extern int interconnect_fini(switch_jobinfo_t *jobinfo)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.job_fini)) (jobinfo);
}

extern int interconnect_postfini(switch_jobinfo_t *jobinfo, uid_t pgid,
				uint32_t job_id, uint32_t step_id )
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.job_postfini)) (jobinfo, pgid,
		job_id, step_id);
}

extern int interconnect_attach(switch_jobinfo_t *jobinfo, char ***env,
		uint32_t nodeid, uint32_t procid, uint32_t nnodes,
		uint32_t nprocs, uint32_t gid)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.job_attach)) (jobinfo, env,
		nodeid, procid, nnodes, nprocs, gid);
}

extern int switch_get_errno(void)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.switch_errno))( );
}

extern char *switch_strerror(int errnum)
{
	if ( switch_init() < 0 )
		return NULL;

	return (*(g_context->ops.switch_strerror))( errnum );
}


/*
 * node switch state monitoring functions
 * required for IBM Federation switch
 */
extern int switch_g_clear_node_state(void)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.clear_node))();
}

extern int switch_g_alloc_node_info(switch_node_info_t **switch_node)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.alloc_nodeinfo))( switch_node );
}

extern int switch_g_build_node_info(switch_node_info_t *switch_node)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.build_nodeinfo))( switch_node );
}

extern int switch_g_pack_node_info(switch_node_info_t *switch_node,
	Buf buffer)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.pack_nodeinfo))( switch_node, buffer );
}

extern int switch_g_unpack_node_info(switch_node_info_t *switch_node,
	Buf buffer)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.unpack_nodeinfo))( switch_node, buffer );
}

extern int switch_g_free_node_info(switch_node_info_t **switch_node)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.free_nodeinfo))( switch_node );
}

extern char*switch_g_sprintf_node_info(switch_node_info_t *switch_node,
	char *buf, size_t size)
{
	if ( switch_init() < 0 )
		return NULL;

	return (*(g_context->ops.sprintf_nodeinfo))( switch_node, buf, size );
}

extern int switch_g_job_step_complete(switch_jobinfo_t *jobinfo,
	char *nodelist)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.step_complete))( jobinfo, nodelist );
}

extern int switch_g_job_step_part_comp(switch_jobinfo_t *jobinfo,
	char *nodelist)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.step_part_comp))( jobinfo, nodelist );
}

extern bool switch_g_part_comp(void)
{
	if ( switch_init() < 0 )
		return false;

	return (*(g_context->ops.part_comp))( );
}


extern int switch_g_job_step_allocated(switch_jobinfo_t *jobinfo,
	char *nodelist)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.step_allocated))( jobinfo, nodelist );
}

extern int switch_g_slurmctld_init(void)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.slurmctld_init)) ();
}

extern int switch_g_slurmd_init(void)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.slurmd_init)) ();
}

extern int switch_g_slurmd_step_init(void)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(g_context->ops.slurmd_step_init)) ();
}
