/*****************************************************************************\
 *  src/common/switch.c - Generic switch (switch_g) for slurm
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

	int          (*alloc_jobinfo)     ( switch_jobinfo_t **jobinfo,
					    uint32_t job_id, uint32_t step_id );
	int          (*build_jobinfo)     ( switch_jobinfo_t *jobinfo,
					    slurm_step_layout_t *step_layout,
					    char *network);
	void         (*free_jobinfo)      ( switch_jobinfo_t *jobinfo );
	int          (*pack_jobinfo)      ( switch_jobinfo_t *jobinfo,
					    Buf buffer,
					    uint16_t protocol_version );
	int          (*unpack_jobinfo)    ( switch_jobinfo_t *jobinfo,
					    Buf buffer,
					    uint16_t protocol_version );
	int          (*get_jobinfo)       ( switch_jobinfo_t *switch_job,
					    int key, void *data);
	void         (*print_jobinfo)     ( FILE *fp,
					    switch_jobinfo_t *jobinfo );
	char *       (*string_jobinfo)    ( switch_jobinfo_t *jobinfo,
					    char *buf, size_t size);
	int          (*node_init)         ( void );
	int          (*node_fini)         ( void );
	int          (*job_preinit)       ( switch_jobinfo_t *jobinfo );
	int          (*job_init)          ( stepd_step_rec_t *job );
	int          (*job_suspend_test)  ( switch_jobinfo_t *jobinfo );
	void         (*job_suspend_info_get)( switch_jobinfo_t *jobinfo,
					      void *suspend_info );
	void         (*job_suspend_info_pack)( void *suspend_info,
					       Buf buffer,
					       uint16_t protocol_version );
	int          (*job_suspend_info_unpack)( void **suspend_info,
						 Buf buffer,
						 uint16_t protocol_version );
	void         (*job_suspend_info_free)( void *suspend_info );
	int          (*job_suspend)       ( void *suspend_info,
					    int max_wait );
	int          (*job_resume)        ( void *suspend_info,
					    int max_wait );
	int          (*job_fini)          ( switch_jobinfo_t *jobinfo );
	int          (*job_postfini)      ( stepd_step_rec_t *job);
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
					    Buf buffer,
					    uint16_t protocol_version );
	int          (*unpack_nodeinfo)   ( switch_node_info_t *nodeinfo,
					    Buf buffer,
					    uint16_t protocol_version );
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
	int          (*reconfig)          ( void );
	int          (*job_step_pre_suspend)( stepd_step_rec_t *job );
	int          (*job_step_post_suspend)( stepd_step_rec_t *job );
	int          (*job_step_pre_resume)( stepd_step_rec_t *job );
	int          (*job_step_post_resume)( stepd_step_rec_t *job );
} slurm_switch_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for slurm_switch_ops_t.
 */
static const char *syms[] = {
	"switch_p_libstate_save",
	"switch_p_libstate_restore",
	"switch_p_alloc_jobinfo",
	"switch_p_build_jobinfo",
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
	"switch_p_slurmd_step_init",
	"switch_p_reconfig",
	"switch_p_job_step_pre_suspend",
	"switch_p_job_step_post_suspend",
	"switch_p_job_step_pre_resume",
	"switch_p_job_step_post_resume",
};

static slurm_switch_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t      context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

extern int switch_init( void )
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "switch";
	char *type = NULL;

	if ( init_run && g_context )
		return retval;

	slurm_mutex_lock( &context_lock );

	if ( g_context )
		goto done;

	type = slurm_get_switch_type();
	g_context = plugin_context_create(
		plugin_type, type, (void **)&ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s", plugin_type, type);
		retval = SLURM_ERROR;
		goto done;
	}
	init_run = true;

done:
	slurm_mutex_unlock( &context_lock );
	xfree(type);
	return retval;
}

extern int switch_fini(void)
{
	int rc;

	if (!g_context)
		return SLURM_SUCCESS;

	init_run = false;
	rc = plugin_context_destroy(g_context);
	return rc;
}

extern int  switch_g_reconfig(void)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.reconfig))( );
}

extern int  switch_g_save(char *dir_name)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.state_save))( dir_name );
}

extern int  switch_g_restore(char *dir_name, bool recover)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.state_restore))( dir_name, recover );
}

extern int  switch_g_clear(void)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.state_clear))( );
}

extern int  switch_g_alloc_jobinfo(switch_jobinfo_t **jobinfo,
				   uint32_t job_id, uint32_t step_id)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.alloc_jobinfo))( jobinfo, job_id, step_id );
}

extern int  switch_g_build_jobinfo(switch_jobinfo_t *jobinfo,
				 slurm_step_layout_t *step_layout,
				 char *network)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.build_jobinfo))( jobinfo, step_layout, network );
}

extern void switch_g_free_jobinfo(switch_jobinfo_t *jobinfo)
{
	if ( switch_init() < 0 )
		return;

	(*(ops.free_jobinfo))( jobinfo );
}

extern int switch_g_pack_jobinfo(switch_jobinfo_t *jobinfo, Buf buffer,
				 uint16_t protocol_version)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.pack_jobinfo))( jobinfo, buffer, protocol_version );
}

extern int switch_g_unpack_jobinfo(switch_jobinfo_t *jobinfo, Buf buffer,
				   uint16_t protocol_version)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.unpack_jobinfo))( jobinfo, buffer, protocol_version );
}

extern int  switch_g_get_jobinfo(switch_jobinfo_t *jobinfo,
				 int data_type, void *data)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.get_jobinfo))( jobinfo, data_type, data);
}

extern void switch_g_print_jobinfo(FILE *fp, switch_jobinfo_t *jobinfo)
{
	if ( switch_init() < 0 )
		return;

	(*(ops.print_jobinfo)) (fp, jobinfo);
}

extern char *switch_g_sprint_jobinfo( switch_jobinfo_t *jobinfo,
				    char *buf, size_t size)
{
	if ( switch_init() < 0 )
		return NULL;

	return (*(ops.string_jobinfo)) (jobinfo, buf, size);
}

extern int switch_g_node_init(void)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.node_init)) ();
}

extern int switch_g_node_fini(void)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.node_fini)) ();
}

extern int switch_g_job_preinit(switch_jobinfo_t *jobinfo)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.job_preinit)) (jobinfo);
}

extern int switch_g_job_init(stepd_step_rec_t *job)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.job_init)) (job);
}

extern int switch_g_job_suspend_test(switch_jobinfo_t *jobinfo)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.job_suspend_test)) (jobinfo);
}

extern void switch_g_job_suspend_info_get(switch_jobinfo_t *jobinfo,
					  void **suspend_info)
{
	if ( switch_init() < 0 )
		return;

	(*(ops.job_suspend_info_get)) (jobinfo, suspend_info);
}

extern void switch_g_job_suspend_info_pack(void *suspend_info, Buf buffer,
					   uint16_t protocol_version)
{
	if ( switch_init() < 0 )
		return;

	(*(ops.job_suspend_info_pack)) (suspend_info, buffer, protocol_version);
}

extern int switch_g_job_suspend_info_unpack(void **suspend_info, Buf buffer,
					    uint16_t protocol_version)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.job_suspend_info_unpack)) (suspend_info, buffer,
						 protocol_version);
}

extern void switch_g_job_suspend_info_free(void *suspend_info)
{
	if ( switch_init() < 0 )
		return;

	(*(ops.job_suspend_info_free)) (suspend_info);
}

extern int switch_g_job_suspend(void *suspend_info, int max_wait)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.job_suspend)) (suspend_info, max_wait);
}

extern int switch_g_job_resume(void *suspend_info, int max_wait)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.job_resume)) (suspend_info, max_wait);
}

extern int switch_g_job_fini(switch_jobinfo_t *jobinfo)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.job_fini)) (jobinfo);
}

extern int switch_g_job_postfini(stepd_step_rec_t *job)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.job_postfini)) (job);
}

extern int switch_g_job_attach(switch_jobinfo_t *jobinfo, char ***env,
			       uint32_t nodeid, uint32_t procid,
			       uint32_t nnodes, uint32_t nprocs, uint32_t gid)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.job_attach)) (jobinfo, env,
				    nodeid, procid, nnodes, nprocs, gid);
}

extern int switch_g_get_errno(void)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.switch_errno))( );
}

extern char *switch_g_strerror(int errnum)
{
	if ( switch_init() < 0 )
		return NULL;

	return (*(ops.switch_strerror))( errnum );
}


/*
 * node switch state monitoring functions
 * required for IBM Federation switch
 */
extern int switch_g_clear_node_state(void)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.clear_node))();
}

extern int switch_g_alloc_node_info(switch_node_info_t **switch_node)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.alloc_nodeinfo))( switch_node );
}

extern int switch_g_build_node_info(switch_node_info_t *switch_node)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.build_nodeinfo))( switch_node );
}

extern int switch_g_pack_node_info(switch_node_info_t *switch_node,
				   Buf buffer, uint16_t protocol_version)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.pack_nodeinfo))(switch_node, buffer, protocol_version);
}

extern int switch_g_unpack_node_info(switch_node_info_t *switch_node,
				     Buf buffer, uint16_t protocol_version)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.unpack_nodeinfo))(switch_node, buffer, protocol_version);
}

extern int switch_g_free_node_info(switch_node_info_t **switch_node)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.free_nodeinfo))( switch_node );
}

extern char*switch_g_sprintf_node_info(switch_node_info_t *switch_node,
				       char *buf, size_t size)
{
	if ( switch_init() < 0 )
		return NULL;

	return (*(ops.sprintf_nodeinfo))( switch_node, buf, size );
}

extern int switch_g_job_step_complete(switch_jobinfo_t *jobinfo,
				      char *nodelist)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.step_complete))( jobinfo, nodelist );
}

extern int switch_g_job_step_part_comp(switch_jobinfo_t *jobinfo,
				       char *nodelist)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.step_part_comp))( jobinfo, nodelist );
}

extern bool switch_g_part_comp(void)
{
	if ( switch_init() < 0 )
		return false;

	return (*(ops.part_comp))( );
}


extern int switch_g_job_step_allocated(switch_jobinfo_t *jobinfo,
				       char *nodelist)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.step_allocated))( jobinfo, nodelist );
}

extern int switch_g_slurmctld_init(void)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.slurmctld_init)) ();
}

extern int switch_g_slurmd_init(void)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.slurmd_init)) ();
}

extern int switch_g_slurmd_step_init(void)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.slurmd_step_init)) ();
}

extern int switch_g_job_step_pre_suspend(stepd_step_rec_t *job)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.job_step_pre_suspend)) ( job );
}

extern int switch_g_job_step_post_suspend(stepd_step_rec_t *job)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.job_step_post_suspend)) ( job );
}

extern int switch_g_job_step_pre_resume(stepd_step_rec_t *job)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.job_step_pre_resume)) ( job );
}

extern int switch_g_job_step_post_resume(stepd_step_rec_t *job)
{
	if ( switch_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.job_step_post_resume)) ( job );
}
