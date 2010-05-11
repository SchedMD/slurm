/*****************************************************************************\
 *  node_select.c - node selection plugin wrapper.
 *
 *  NOTE: The node selection plugin itself is intimately tied to slurmctld
 *  functions and data structures. Some related functions (e.g. data structure
 *  un/packing, environment variable setting) are required by most SLURM
 *  commands. Since some of these commands must be executed on the BlueGene
 *  front-end nodes, the functions they require are here rather than within
 *  the plugin. This is because functions required by the plugin can not be
 *  resolved on the front-end nodes, so we can't load the plugins there.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <pthread.h>

#include "src/common/list.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"
#include "src/common/node_select.h"

/* Define select_jobinfo_t below to avoid including extraneous slurm headers */
#ifndef __select_jobinfo_t_defined
#  define  __select_jobinfo_t_defined
typedef struct select_jobinfo select_jobinfo_t;     /* opaque data type */
typedef struct select_nodeinfo select_nodeinfo_t;     /* opaque data type */
#endif

/*
 * Local data
 */

typedef struct slurm_select_ops {
	uint32_t	(*plugin_id);
	int		(*state_save)	       (char *dir_name);
	int	       	(*state_restore)       (char *dir_name);
	int		(*job_init)	       (List job_list);
	int 		(*node_init)	       (struct node_record *node_ptr,
					        int node_cnt);
	int 		(*block_init)	       (List block_list);
	int		(*job_test)	       (struct job_record *job_ptr,
						bitstr_t *bitmap,
						uint32_t min_nodes,
						uint32_t max_nodes,
						uint32_t req_nodes,
						uint16_t mode,
						List preeemptee_candidates,
						List *preemptee_job_list);
	int		(*job_begin)	       (struct job_record *job_ptr);
	int		(*job_ready)	       (struct job_record *job_ptr);
	int		(*job_resized)	       (struct job_record *job_ptr,
						struct node_record *node_ptr);
	int		(*job_fini)	       (struct job_record *job_ptr);
	int		(*job_suspend)	       (struct job_record *job_ptr);
	int		(*job_resume)	       (struct job_record *job_ptr);
	int		(*pack_select_info)    (time_t last_query_time,
						Buf *buffer_ptr,
						uint16_t protocol_version);
        int	        (*nodeinfo_pack)       (select_nodeinfo_t *nodeinfo,
						Buf buffer,
						uint16_t protocol_version);
        int	        (*nodeinfo_unpack)     (void **nodeinfo,
						Buf buffer,
						uint16_t protocol_version);
	select_nodeinfo_t *(*nodeinfo_alloc)   (uint32_t size);
	int	        (*nodeinfo_free)       (select_nodeinfo_t *nodeinfo);
	int             (*nodeinfo_set_all)    (time_t last_query_time);
	int             (*nodeinfo_set)        (struct job_record *job_ptr);
	int             (*nodeinfo_get)        (select_nodeinfo_t *nodeinfo,
						enum
						select_nodedata_type dinfo,
						enum node_states state,
						void *data);
	select_jobinfo_t *(*jobinfo_alloc)     ();
	int             (*jobinfo_free)        (select_jobinfo_t *jobinfo);
	int             (*jobinfo_set)         (select_jobinfo_t *jobinfo,
						enum
						select_jobdata_type data_type,
						void *data);
	int             (*jobinfo_get)         (select_jobinfo_t *jobinfo,
						enum
						select_jobdata_type data_type,
						void *data);
	select_jobinfo_t *(*jobinfo_copy)        (select_jobinfo_t *jobinfo);
	int             (*jobinfo_pack)        (select_jobinfo_t *jobinfo,
						Buf buffer,
						uint16_t protocol_version);
	int             (*jobinfo_unpack)      (void **jobinfo_pptr,
						Buf buffer,
						uint16_t protocol_version);
	char *          (*jobinfo_sprint)      (select_jobinfo_t *jobinfo,
						char *buf, size_t size,
						int mode);
	char *          (*jobinfo_xstrdup)     (select_jobinfo_t *jobinfo,
						int mode);
        int             (*update_block)        (update_block_msg_t
						*block_desc_ptr);
        int             (*update_sub_node)     (update_block_msg_t
						*block_desc_ptr);
	int             (*get_info_from_plugin)(enum
						select_plugindata_info dinfo,
						struct job_record *job_ptr,
						void *data);
	int		(*update_node_config)  (int index);
	int             (*update_node_state)   (int index, uint16_t state);
	int             (*alter_node_cnt)      (enum select_node_cnt type,
						void *data);
	int		(*reconfigure)         (void);
} slurm_select_ops_t;

typedef struct slurm_select_context {
	char	       	*select_type;
	plugrack_t     	plugin_list;
	plugin_handle_t	cur_plugin;
	int		select_errno;
	slurm_select_ops_t ops;
} slurm_select_context_t;

static int select_context_cnt = -1;
static int select_context_default = -1;
/* If there is a new select plugin, list it here */
static char *select_plugin_list = "bluegene,cons_res,linear";
static slurm_select_context_t * select_context = NULL;
static pthread_mutex_t		select_context_lock =
	PTHREAD_MUTEX_INITIALIZER;

#ifdef HAVE_CRAY_XT		/* node selection specific logic */
#  define JOBINFO_MAGIC 0x8cb3
struct select_jobinfo {
	uint16_t magic;		/* magic number */
	char *reservation_id;	/* BASIL reservation ID */
};
#endif	/* HAVE_CRAY_XT */

/*
 * Local functions
 */
static int _select_context_destroy(slurm_select_context_t *c);

/*
 * Locate and load the appropriate plugin
 */
static int _select_get_ops(char *select_type, slurm_select_context_t *c)
{
	/*
	 * Must be synchronized with slurm_select_ops_t above.
	 */
	static const char *syms[] = {
		"plugin_id",
		"select_p_state_save",
		"select_p_state_restore",
		"select_p_job_init",
		"select_p_node_init",
		"select_p_block_init",
		"select_p_job_test",
		"select_p_job_begin",
		"select_p_job_ready",
		"select_p_job_resized",
		"select_p_job_fini",
		"select_p_job_suspend",
		"select_p_job_resume",
		"select_p_pack_select_info",
                "select_p_select_nodeinfo_pack",
                "select_p_select_nodeinfo_unpack",
 		"select_p_select_nodeinfo_alloc",
		"select_p_select_nodeinfo_free",
  		"select_p_select_nodeinfo_set_all",
  		"select_p_select_nodeinfo_set",
		"select_p_select_nodeinfo_get",
		"select_p_select_jobinfo_alloc",
  		"select_p_select_jobinfo_free",
   		"select_p_select_jobinfo_set",
   		"select_p_select_jobinfo_get",
   		"select_p_select_jobinfo_copy",
  		"select_p_select_jobinfo_pack",
  		"select_p_select_jobinfo_unpack",
  		"select_p_select_jobinfo_sprint",
  		"select_p_select_jobinfo_xstrdup",
    		"select_p_update_block",
		"select_p_update_sub_node",
		"select_p_get_info_from_plugin",
		"select_p_update_node_config",
		"select_p_update_node_state",
		"select_p_alter_node_cnt",
		"select_p_reconfigure",
	};
	int n_syms = sizeof( syms ) / sizeof( char * );

	c->select_type	= xstrdup(select_type);
	c->plugin_list	= NULL;
	c->cur_plugin	= PLUGIN_INVALID_HANDLE;
	c->select_errno	= SLURM_SUCCESS;

	/* Find the correct plugin. */
        c->cur_plugin = plugin_load_and_link(c->select_type, n_syms, syms,
					     (void **) &c->ops);
        if ( c->cur_plugin != PLUGIN_INVALID_HANDLE )
        	return SLURM_SUCCESS;

	error("Couldn't find the specified plugin name for %s "
	      "looking at all files",
	      c->select_type);

	/* Get plugin list. */
	if ( c->plugin_list == NULL ) {
		char *plugin_dir;
		c->plugin_list = plugrack_create();
		if ( c->plugin_list == NULL ) {
			error( "cannot create plugin manager" );
			return SLURM_ERROR;
		}
		plugrack_set_major_type( c->plugin_list, "select" );
		plugrack_set_paranoia( c->plugin_list,
				       PLUGRACK_PARANOIA_NONE,
				       0 );
		plugin_dir = slurm_get_plugin_dir();
		plugrack_read_dir( c->plugin_list, plugin_dir );
		xfree(plugin_dir);
	}

	c->cur_plugin = plugrack_use_by_type( c->plugin_list, c->select_type );
	if ( c->cur_plugin == PLUGIN_INVALID_HANDLE ) {
		error( "cannot find node selection plugin for %s",
		       c->select_type );
		return SLURM_ERROR;
	}

	/* Dereference the API. */
	if ( plugin_get_syms( c->cur_plugin,
			      n_syms,
			      syms,
			      (void **) &c->ops ) < n_syms ) {
		error( "incomplete node selection plugin detected" );
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

/*
 * Destroy a node selection context
 */
static int _select_context_destroy( slurm_select_context_t *c )
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

	xfree(c->select_type);

	return rc;
}

static void _free_block_info(block_info_t *block_info)
{
	if(block_info) {
		xfree(block_info->bg_block_id);
		xfree(block_info->blrtsimage);
		xfree(block_info->bp_inx);
		xfree(block_info->ionodes);
		xfree(block_info->ionode_inx);
		xfree(block_info->linuximage);
		xfree(block_info->mloaderimage);
		xfree(block_info->nodes);
		xfree(block_info->owner_name);
		xfree(block_info->ramdiskimage);
	}
}

/* NOTE: The matching pack functions are directly in the select/bluegene
 * plugin. The unpack functions can not be there since the plugin is
 * dependent upon libraries which do not exist on the BlueGene front-end
 * nodes. */
static int _unpack_block_info(block_info_t *block_info, Buf buffer,
			      uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	char *bp_inx_str = NULL;

	if(protocol_version >= SLURM_2_1_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&block_info->bg_block_id,
				       &uint32_tmp, buffer);
#ifdef HAVE_BGL
		safe_unpackstr_xmalloc(&block_info->blrtsimage,
				       &uint32_tmp, buffer);
#endif
		safe_unpackstr_xmalloc(&bp_inx_str, &uint32_tmp, buffer);
		if (bp_inx_str == NULL) {
			block_info->bp_inx = bitfmt2int("");
		} else {
			block_info->bp_inx = bitfmt2int(bp_inx_str);
			xfree(bp_inx_str);
		}
		safe_unpack16(&block_info->conn_type, buffer);
		safe_unpackstr_xmalloc(&(block_info->ionodes),
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&bp_inx_str, &uint32_tmp, buffer);
		if (bp_inx_str == NULL) {
			block_info->ionode_inx = bitfmt2int("");
		} else {
			block_info->ionode_inx = bitfmt2int(bp_inx_str);
			xfree(bp_inx_str);
		}
		safe_unpack32(&block_info->job_running, buffer);
		safe_unpackstr_xmalloc(&block_info->linuximage,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&block_info->mloaderimage,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&(block_info->nodes), &uint32_tmp,
				       buffer);
		safe_unpack32(&block_info->node_cnt, buffer);
#ifdef HAVE_BGL
		safe_unpack16(&block_info->node_use, buffer);
#endif
		safe_unpackstr_xmalloc(&block_info->owner_name,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&block_info->ramdiskimage,
				       &uint32_tmp, buffer);
		safe_unpack16(&block_info->state, buffer);
	}
	return SLURM_SUCCESS;

unpack_error:
	error("_unpack_node_info: error unpacking here");
	_free_block_info(block_info);
	return SLURM_ERROR;
}

extern int node_select_free_block_info(block_info_t *block_info)
{
	if(block_info) {
		_free_block_info(block_info);
		xfree(block_info);
	}
	return SLURM_SUCCESS;
}

extern void node_select_pack_block_info(block_info_t *block_info, Buf buffer,
					uint16_t protocol_version)
{
	if(protocol_version >= SLURM_2_1_PROTOCOL_VERSION) {
		if(!block_info) {
			packnull(buffer);
#ifdef HAVE_BGL
			packnull(buffer);
#endif
			pack16((uint16_t)NO_VAL, buffer);
			packnull(buffer);

			packnull(buffer);
			packnull(buffer);

			pack32(NO_VAL, buffer);

			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
			pack32(NO_VAL, buffer);
#ifdef HAVE_BGL
			pack16((uint16_t)NO_VAL, buffer);
#endif

			packnull(buffer);
			packnull(buffer);
			pack16((uint16_t)NO_VAL, buffer);
		} else {
			packstr(block_info->bg_block_id, buffer);
#ifdef HAVE_BGL
			packstr(block_info->blrtsimage, buffer);
#endif

			if(block_info->bp_inx) {
				char *bitfmt = inx2bitfmt(block_info->bp_inx);
				packstr(bitfmt, buffer);
				xfree(bitfmt);
			} else
				packnull(buffer);

			pack16(block_info->conn_type, buffer);

			packstr(block_info->ionodes, buffer);

			if(block_info->ionode_inx) {
				char *bitfmt =
					inx2bitfmt(block_info->ionode_inx);
				packstr(bitfmt, buffer);
				xfree(bitfmt);
			} else
				packnull(buffer);

			pack32(block_info->job_running, buffer);

			packstr(block_info->linuximage, buffer);
			packstr(block_info->mloaderimage, buffer);
			packstr(block_info->nodes, buffer);
			pack32(block_info->node_cnt, buffer);
#ifdef HAVE_BGL
			pack16(block_info->node_use, buffer);
#endif
			packstr(block_info->owner_name, buffer);
			packstr(block_info->ramdiskimage, buffer);
			pack16(block_info->state, buffer);
		}
	}
}

extern int node_select_unpack_block_info(block_info_t **block_info, Buf buffer,
					 uint16_t protocol_version)
{
        int rc = SLURM_SUCCESS;
	block_info_t *bg_rec = xmalloc(sizeof(block_info_t));

	if((rc = _unpack_block_info(bg_rec, buffer, protocol_version))
	   != SLURM_SUCCESS)
		xfree(bg_rec);
	else
		*block_info = bg_rec;
	return rc;
}

extern int node_select_block_info_msg_free (
	block_info_msg_t **block_info_msg_pptr)
{
	block_info_msg_t *block_info_msg = NULL;

	if (block_info_msg_pptr == NULL)
		return EINVAL;

	block_info_msg = *block_info_msg_pptr;
	if (block_info_msg->block_array) {
		int i;
		for(i=0; i<block_info_msg->record_count; i++)
			_free_block_info(
				&(block_info_msg->block_array[i]));
		xfree(block_info_msg->block_array);
	}
	xfree(block_info_msg);

	*block_info_msg_pptr = NULL;
	return SLURM_SUCCESS;
}

/* Unpack node select info from a buffer */
extern int node_select_block_info_msg_unpack(
	block_info_msg_t **block_info_msg_pptr, Buf buffer,
	uint16_t protocol_version)
{
	int i;
	block_info_msg_t *buf;

	buf = xmalloc(sizeof(block_info_msg_t));
	if(protocol_version >= SLURM_2_1_PROTOCOL_VERSION) {
		safe_unpack32(&(buf->record_count), buffer);
		safe_unpack_time(&(buf->last_update), buffer);
		buf->block_array = xmalloc(sizeof(block_info_t) *
					   buf->record_count);
		for(i=0; i<buf->record_count; i++) {
			if (_unpack_block_info(&(buf->block_array[i]), buffer,
					       protocol_version))
				goto unpack_error;
		}
	}
	*block_info_msg_pptr = buf;
	return SLURM_SUCCESS;

unpack_error:
	node_select_block_info_msg_free(&buf);
	*block_info_msg_pptr = NULL;
	return SLURM_ERROR;
}


/*
 * Initialize context for node selection plugin
 */
extern int slurm_select_init(void)
{
	int retval = SLURM_SUCCESS;
	char *select_type = NULL;
	char *last = NULL, *names, *one_name, *full_name;
	int i, j, rc;

	slurm_mutex_lock( &select_context_lock );

	if ( select_context )
		goto done;

	select_type = slurm_get_select_type();

#ifdef HAVE_XCPU
	if(strcasecmp(select_type, "select/linear")) {
		error("%s is incompatible with XCPU use", select_type);
		fatal("Use SelectType=select/linear");
	}
#endif

#ifdef HAVE_BG
	if(strcasecmp(select_type, "select/bluegene")) {
		error("%s is incompatible with BlueGene", select_type);
		fatal("Use SelectType=select/bluegene");
	}
#endif
	select_context_cnt = 0;
	names = xstrdup(select_plugin_list);
	one_name = strtok_r(names, ",", &last);
	while (one_name) {
		full_name = xstrdup_printf("select/%s", one_name);
		for (i=0; i<select_context_cnt; i++) {
			if (!strcmp(full_name, select_context[i].select_type))
				break;
		}
		if (i < select_context_cnt) {
			error("Duplicate plugin %s ignored",
			      select_context[i].select_type);
		} else {
			xrealloc(select_context,
				 (sizeof(slurm_select_context_t) *
				 (select_context_cnt + 1)));
			rc = _select_get_ops(
				full_name, select_context + select_context_cnt);
			if (rc != SLURM_SUCCESS)
				break;
			/* set the default */
			if (!strcmp(full_name, select_type))
				select_context_default = select_context_cnt;
			select_context_cnt++;
		}
		xfree(full_name);
		one_name = strtok_r(NULL, ",", &last);
	}
	xfree(names);
	if(select_context_default == -1)
		fatal("Can't find plugin for %s", select_type);

	/* Insure that plugin_id is valid and unique */
	for (i=0; i<select_context_cnt; i++) {
		for (j=i+1; j<select_context_cnt; j++) {
			if (*(select_context[i].ops.plugin_id) !=
			    *(select_context[j].ops.plugin_id))
				continue;
			fatal("SelectPlugins: Duplicate plugin_id %u for "
			      "%s and %s",
			      *(select_context[i].ops.plugin_id),
			      select_context[i].select_type,
			      select_context[j].select_type);
		}
		if (*(select_context[i].ops.plugin_id) < 100) {
			fatal("SelectPlugins: Invalid plugin_id %u (<100) %s",
			      *(select_context[i].ops.plugin_id),
			      select_context[i].select_type);
		}

	}

done:
	slurm_mutex_unlock( &select_context_lock );
	xfree(select_type);
	return retval;
}

extern int slurm_select_fini(void)
{
	int rc = SLURM_SUCCESS, i, j;

	slurm_mutex_lock(&select_context_lock);
	if (!select_context)
		goto fini;

	for (i=0; i<select_context_cnt; i++) {
		j = _select_context_destroy(select_context);
		if (j != SLURM_SUCCESS)
			rc = j;
	}
	xfree(select_context);
	select_context_cnt = -1;

fini:	slurm_mutex_unlock(&select_context_lock);
	return rc;
}

/*
 * Save any global state information
 * IN dir_name - directory into which the data can be stored
 */
extern int select_g_state_save(char *dir_name)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(select_context[select_context_default].ops.state_save))
		(dir_name);
}

/*
 * Initialize context for node selection plugin and
 * restore any global state information
 * IN dir_name - directory from which the data can be restored
 */
extern int select_g_state_restore(char *dir_name)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(select_context[select_context_default].ops.state_restore))
		(dir_name);
}

/*
 * Note the initialization of job records, issued upon restart of
 * slurmctld and used to synchronize any job state.
 */
extern int select_g_job_init(List job_list)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(select_context[select_context_default].ops.job_init))
		(job_list);
}

/*
 * Note re/initialization of node record data structure
 * IN node_ptr - current node data
 * IN node_count - number of node entries
 */
extern int select_g_node_init(struct node_record *node_ptr, int node_cnt)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(select_context[select_context_default].ops.node_init))
		(node_ptr, node_cnt);
}


/*
 * Note re/initialization of block record data structure
 * IN block_list - list of partition records
 */
extern int select_g_block_init(List block_list)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(select_context[select_context_default].ops.block_init))
		(block_list);
}

/*
 * Select the "best" nodes for given job from those available
 * IN/OUT job_ptr - pointer to job being considered for initiation,
 *                  set's start_time when job expected to start
 * IN/OUT bitmap - map of nodes being considered for allocation on input,
 *                 map of nodes actually to be assigned on output
 * IN min_nodes - minimum number of nodes to allocate to job
 * IN max_nodes - maximum number of nodes to allocate to job
 * IN req_nodes - requested (or desired) count of nodes
 * IN mode - SELECT_MODE_RUN_NOW: try to schedule job now
 *           SELECT_MODE_TEST_ONLY: test if job can ever run
 *           SELECT_MODE_WILL_RUN: determine when and where job can run
 * IN preemptee_candidates - List of pointers to jobs which can bee preempted
 * IN/OUT preemptee_job_list - Pointer to list of job pointers. These are the
 *		jobs to be preempted to initiate the pending job. Not set
 *		if mode=SELECT_MODE_TEST_ONLY or input pointer is NULL.
 *		Existing list is appended to.
 * RET zero on success, EINVAL otherwise
 */
extern int select_g_job_test(struct job_record *job_ptr, bitstr_t *bitmap,
			     uint32_t min_nodes, uint32_t max_nodes,
			     uint32_t req_nodes, uint16_t mode,
			     List preemptee_candidates,
			     List *preemptee_job_list)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(select_context[select_context_default].ops.job_test))
		(job_ptr, bitmap,
		 min_nodes, max_nodes,
		 req_nodes, mode,
		 preemptee_candidates,
		 preemptee_job_list);
}

/*
 * Note initiation of job is about to begin. Called immediately
 * after select_g_job_test(). Executed from slurmctld.
 * IN job_ptr - pointer to job being initiated
 */
extern int select_g_job_begin(struct job_record *job_ptr)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(select_context[select_context_default].ops.job_begin))
		(job_ptr);
}

/*
 * determine if job is ready to execute per the node select plugin
 * IN job_ptr - pointer to job being tested
 * RET: -2 fatal error, -1 try again, 1 if ready to execute,
 *	0 not ready to execute
 */
extern int select_g_job_ready(struct job_record *job_ptr)
{
	if (slurm_select_init() < 0)
		return -1;

	return (*(select_context[select_context_default].ops.job_ready))
		(job_ptr);
}

/*
 * Modify internal data structures for a job that has changed size
 *	Only support jobs shrinking now.
 * RET: 0 or an error code
 */
extern int select_g_job_resized(struct job_record *job_ptr,
				struct node_record *node_ptr)
{
	if (slurm_select_init() < 0)
		return -1;

	return (*(select_context[select_context_default].ops.job_resized))
		(job_ptr, node_ptr);
}

/*
 * Note termination of job is starting. Executed from slurmctld.
 * IN job_ptr - pointer to job being terminated
 */
extern int select_g_job_fini(struct job_record *job_ptr)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(select_context[select_context_default].ops.job_fini))
		(job_ptr);
}

/*
 * Suspend a job. Executed from slurmctld.
 * IN job_ptr - pointer to job being suspended
 * RET SLURM_SUCCESS or error code
 */
extern int select_g_job_suspend(struct job_record *job_ptr)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(select_context[select_context_default].ops.job_suspend))
		(job_ptr);
}

/*
 * Resume a job. Executed from slurmctld.
 * IN job_ptr - pointer to job being resumed
 * RET SLURM_SUCCESS or error code
 */
extern int select_g_job_resume(struct job_record *job_ptr)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(select_context[select_context_default].ops.job_resume))
		(job_ptr);
}

extern int select_g_pack_select_info(time_t last_query_time, Buf *buffer,
				     uint16_t protocol_version)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(select_context[select_context_default].ops.pack_select_info))
		(last_query_time, buffer, protocol_version);
}

extern int select_g_select_nodeinfo_pack(dynamic_plugin_data_t *nodeinfo,
					 Buf buffer,
					 uint16_t protocol_version)
{
	void *data = NULL;

	if (slurm_select_init() < 0)
		return SLURM_ERROR;
	if(protocol_version >= SLURM_2_2_PROTOCOL_VERSION)
		pack32(*(select_context[select_context_default].ops.plugin_id),
		       buffer);
	if(nodeinfo)
		data = nodeinfo->data;
	return (*(select_context[select_context_default].ops.
		  nodeinfo_pack))(nodeinfo->data, buffer, protocol_version);
}

extern int select_g_select_nodeinfo_unpack(dynamic_plugin_data_t **nodeinfo,
					   Buf buffer,
					   uint16_t protocol_version)
{
	dynamic_plugin_data_t *nodeinfo_ptr = NULL;

	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	nodeinfo_ptr = xmalloc(sizeof(dynamic_plugin_data_t));
	*nodeinfo = nodeinfo_ptr;

	if(protocol_version >= SLURM_2_2_PROTOCOL_VERSION) {
		int i;
		uint32_t plugin_id;
		safe_unpack32(&plugin_id, buffer);
		for (i=0; i<select_context_cnt; i++)
			if(*(select_context[i].ops.plugin_id) == plugin_id) {
				nodeinfo_ptr->plugin_id = i;
				break;
			}
		if (i >= select_context_cnt) {
			error("we don't have this plugin type %u", plugin_id);
			goto unpack_error;
		}
	} else
		nodeinfo_ptr->plugin_id = select_context_default;

	return (*(select_context[nodeinfo_ptr->plugin_id].ops.nodeinfo_unpack))
		(&nodeinfo_ptr->data, buffer, protocol_version);
unpack_error:
	error("select_g_select_nodeinfo_unpack: unpack error");
	return SLURM_ERROR;
}

extern dynamic_plugin_data_t *select_g_select_nodeinfo_alloc(uint32_t size)
{
	dynamic_plugin_data_t *nodeinfo_ptr = NULL;

	if (slurm_select_init() < 0)
		return NULL;
	nodeinfo_ptr = xmalloc(sizeof(dynamic_plugin_data_t));
	nodeinfo_ptr->plugin_id = select_context_default;
	nodeinfo_ptr->data = (*(select_context[select_context_default].ops.
				nodeinfo_alloc))(size);
	return nodeinfo_ptr;
}

extern int select_g_select_nodeinfo_free(dynamic_plugin_data_t *nodeinfo)
{
	int rc = SLURM_SUCCESS;

	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	if(nodeinfo) {
		if(nodeinfo->data)
			rc = (*(select_context[nodeinfo->plugin_id].ops.
				nodeinfo_free))(nodeinfo->data);
		xfree(nodeinfo);
	}
	return rc;
}

extern int select_g_select_nodeinfo_set_all(time_t last_query_time)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(select_context[select_context_default].ops.nodeinfo_set_all))
		(last_query_time);
}

extern int select_g_select_nodeinfo_set(struct job_record *job_ptr)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(select_context[select_context_default].ops.nodeinfo_set))
		(job_ptr);
}

extern int select_g_select_nodeinfo_get(dynamic_plugin_data_t *nodeinfo,
					enum select_nodedata_type dinfo,
					enum node_states state,
					void *data)
{
	void *nodedata = NULL;
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	if(nodeinfo)
		nodedata = nodeinfo->data;
	return (*(select_context[nodeinfo->plugin_id].ops.nodeinfo_get))
		(nodedata, dinfo, state, data);
}

/* OK since the Cray XT could be done with either linear or cons_res
 * select plugin just wrap these functions.  I don't like it either,
 * but that is where we stand right now.
 */
#ifdef HAVE_CRAY_XT

/* allocate storage for a select job credential
 * RET jobinfo - storage for a select job credential
 * NOTE: storage must be freed using select_g_select_jobinfo_free
 */
extern dynamic_plugin_data_t *select_g_select_jobinfo_alloc ()
{
	dynamic_plugin_data_t *jobinfo = xmalloc(sizeof(dynamic_plugin_data_t));
	struct select_jobinfo *data;

	jobinfo->magic = JOBINFO_MAGIC;

	data = xmalloc(sizeof(struct select_jobinfo));
	data->magic = JOBINFO_MAGIC;
	jobinfo->data = data;

	return jobinfo;
}

/* free storage previously allocated for a select job credential
 * IN jobinfo  - the select job credential to be freed
 */
extern int select_g_select_jobinfo_free  (dynamic_plugin_data_t *jobinfo)
{
	int rc = SLURM_SUCCESS;
	struct select_jobinfo *data;

	xassert(jobinfo != NULL);

	data = (struct select_jobinfo *)jobinfo->data;

	if (data == NULL)	/* never set, treat as not an error */
		;
	else if (data->magic != JOBINFO_MAGIC) {
		error("select_g_select_jobinfo_free: jobinfo magic bad");
		rc = EINVAL;
	} else {
		data->magic = 0;
		xfree(data->reservation_id);
		xfree(data);
	}
	xfree(jobinfo);
	return rc;
}

/* fill in a previously allocated select job credential
 * IN/OUT jobinfo  - updated select job credential
 * IN data_type - type of data to enter into job credential
 * IN data - the data to enter into job credential
 */
extern int select_g_select_jobinfo_set (dynamic_plugin_data_t *jobinfo,
					enum select_jobdata_type data_type,
					void *data)
{
	int rc = SLURM_SUCCESS;
	char *tmp_char = (char *) data;
	struct select_jobinfo *data;

	xassert(jobinfo != NULL);

	data = (struct select_jobinfo *)jobinfo->data;

	if (data == NULL) {
		error("select_g_select_jobinfo_set: jobinfo not set");
		return SLURM_ERROR;
	}
	if (data->magic != JOBINFO_MAGIC) {
		error("select_g_select_jobinfo_set: jobinfo magic bad");
		return SLURM_ERROR;
	}

	switch (data_type) {
	case SELECT_JOBDATA_RESV_ID:
		/* we xfree() any preset value to avoid a memory leak */
		xfree(data->reservation_id);
		if (tmp_char)
			data->reservation_id = xstrdup(tmp_char);
		break;
	default:
		debug("select_g_select_jobinfo_set data_type %d invalid",
		      data_type);
	}

	return rc;
}

/* get data from a select job credential
 * IN jobinfo  - updated select job credential
 * IN data_type - type of data to enter into job credential
 * OUT data - the data to get from job credential, caller must xfree
 *	data for data_tyep == SELECT_DATA_BLOCK_ID
 */
extern int select_g_select_jobinfo_get (dynamic_plugin_data_t *jobinfo,
					enum select_jobdata_type data_type,
					void *data)
{
	int rc = SLURM_SUCCESS;
	char **tmp_char = (char **) data;
	struct select_jobinfo *data;

	xassert(jobinfo != NULL);

	data = (struct select_jobinfo *)jobinfo->data;

	if (data == NULL) {
		error("select_g_select_jobinfo_get: jobinfo not set");
		return SLURM_ERROR;
	}
	if (data->magic != JOBINFO_MAGIC) {
		error("select_g_select_jobinfo_get: jobinfo magic bad");
		return SLURM_ERROR;
	}

	switch (data_type) {
	case SELECT_DATA_RESV_ID:
		if ((data->reservation_id == NULL) ||
		    (data->reservation_id[0] == '\0'))
			*tmp_char = NULL;
		else
			*tmp_char = xstrdup(data->reservation_id);
		break;
	default:
		/* There is some use of BlueGene specific params that
		 * are not supported on the Cray, but requested on
		 * all systems */
		debug2("select_g_select_jobinfo_get data_type %d invalid",
		       data_type);
		return SLURM_ERROR;
	}

	return rc;
}

/* copy a select job credential
 * IN jobinfo - the select job credential to be copied
 * RET        - the copy or NULL on failure
 * NOTE: returned value must be freed using select_g_select_jobinfo_free
 */
extern dynamic_plugin_data_t *select_g_select_jobinfo_copy(
	dynamic_plugin_data_t *jobinfo)
{
	dynamic_plugin_data_t *rc = NULL;
	struct select_jobinfo *data, *data2;

	xassert(jobinfo != NULL);

	data = (struct select_jobinfo *)jobinfo->data;

	if (data == NULL)
		;
	else if (data->magic != JOBINFO_MAGIC)
		error("select_g_select_jobinfo_copy: jobinfo magic bad");
	else {
		rc = xmalloc(sizeof(dynamic_plugin_data_t));
		data2 = xmalloc(sizeof(struct select_jobinfo));
		data2->magic = JOBINFO_MAGIC;
		data2->reservation_id = xstrdup(data->reservation_id);
		rc->data = data2;
	}

	return rc;
}

/* pack a select job credential into a buffer in machine independent form
 * IN jobinfo  - the select job credential to be saved
 * OUT buffer  - buffer with select credential appended
 * RET         - slurm error code
 */
extern int  select_g_select_jobinfo_pack(dynamic_plugin_data_t *jobinfo,
					 Buf buffer,
					 uint16_t protocol_version)
{
	struct select_jobinfo *data;
	xassert(jobinfo != NULL);
	data = (struct select_jobinfo *)jobinfo->data;

	if (data) {
		/* NOTE: If new elements are added here, make sure to
		 * add equivalant pack of zeros below for NULL pointer */
		packstr(data->reservation_id, buffer, protocol_version);
	} else {
		packnull(buffer); //reservation_id
	}

	return SLURM_SUCCESS;
}

/* unpack a select job credential from a buffer
 * OUT jobinfo - the select job credential read
 * IN  buffer  - buffer with select credential read from current pointer loc
 * RET         - slurm error code
 * NOTE: returned value must be freed using select_g_select_jobinfo_free
 */
extern int select_g_select_jobinfo_unpack(dynamic_plugin_data_t **jobinfo_pptr,
					  Buf buffer,
					  uint16_t protocol_version)
{
	uint32_t uint32_tmp;

	dynamic_plugin_data_t *jobinfo = xmalloc(sizeof(dynamic_plugin_data_t));
	struct select_jobinfo *data = xmalloc(sizeof(struct select_jobinfo));
	*jobinfo_pptr = jobinfo;
	jobinfo->data = data;

	data->magic = JOBINFO_MAGIC;
	safe_unpackstr_xmalloc(&(data->reservation_id), &uint32_tmp, buffer,
		protocol_version);

	return SLURM_SUCCESS;

unpack_error:
	select_g_select_jobinfo_free(jobinfo);
	*jobinfo_pptr = NULL;

	return SLURM_ERROR;
}

/* write select job credential to a string
 * IN jobinfo - a select job credential
 * OUT buf    - location to write job credential contents
 * IN size    - byte size of buf
 * IN mode    - print mode, see enum select_print_mode
 * RET        - the string, same as buf
 */
extern char *select_g_select_jobinfo_sprint(dynamic_plugin_data_t *jobinfo,
					    char *buf, size_t size, int mode)
{
	struct select_jobinfo *data;
	xassert(jobinfo != NULL);
	data = (struct select_jobinfo *)jobinfo->data;

	if (buf == NULL) {
		error("select_g_select_jobinfo_sprint: buf is null");
		return NULL;
	}

	if ((mode != SELECT_PRINT_DATA) &&
	    data && (data->magic != JOBINFO_MAGIC)) {
		error("select_g_select_jobinfo_sprint: jobinfo magic bad");
		return NULL;
	}

	if (data == NULL) {
		if (mode != SELECT_PRINT_HEAD) {
			error("select_g_select_jobinfo_sprint: jobinfo bad");
			return NULL;
		}
	}

	switch (mode) {
	case SELECT_PRINT_HEAD:
		snprintf(buf, size,
			 "RESV_ID");
		break;
	case SELECT_PRINT_DATA:
		snprintf(buf, size,
			 "%7s",
			 data->reservation_id);
		break;
	case SELECT_PRINT_MIXED:
		snprintf(buf, size,
			 "Resv_ID=%s",
			 data->reservation_id);
		break;
	case SELECT_PRINT_RESV_ID:
		snprintf(buf, size, "%s", data->reservation_id);
		break;
	default:
		/* likely a BlueGene specific mode */
		error("select_g_select_jobinfo_sprint: bad mode %d", mode);
		if (size > 0)
			buf[0] = '\0';
	}

	return buf;
}

/* write select job info to a string
 * IN jobinfo - a select job credential
 * IN mode    - print mode, see enum select_print_mode
 * RET        - char * containing string of request
 */
extern char *select_g_select_jobinfo_xstrdup(
	dynamic_plugin_data_t *jobinfo, int mode)
{
	char *buf = NULL;
	struct select_jobinfo *data;

	xassert(jobinfo != NULL);
	data = (struct select_jobinfo *)jobinfo->data;

	if ((mode != SELECT_PRINT_DATA) &&
	    data && (data->magic != JOBINFO_MAGIC)) {
		error("select_g_select_jobinfo_xstrdup: jobinfo magic bad");
		return NULL;
	}

	if (data == NULL) {
		if (mode != SELECT_PRINT_HEAD) {
			error("select_g_select_jobinfo_xstrdup: jobinfo bad");
			return NULL;
		}
	}

	switch (mode) {
	case SELECT_PRINT_HEAD:
		xstrcat(buf,
			"RESV_ID");
		break;
	case SELECT_PRINT_DATA:
		xstrfmtcat(buf,
			   "%7s",
			   data->reservation_id);
		break;
	case SELECT_PRINT_MIXED:
		xstrfmtcat(buf,
			   "Resv_ID=%s",
			   data->reservation_id);
		break;
	case SELECT_PRINT_RESV_ID:
		xstrfmtcat(buf, "%s", data->reservation_id);
		break;
	default:
		error("select_g_select_jobinfo_xstrdup: bad mode %d", mode);
	}

	return buf;
}

#else /* HAVE_CRAY_XT */

extern dynamic_plugin_data_t *select_g_select_jobinfo_alloc()
{
	dynamic_plugin_data_t *jobinfo_ptr = NULL;
	if (slurm_select_init() < 0)
		return NULL;
	jobinfo_ptr = xmalloc(sizeof(dynamic_plugin_data_t));
	jobinfo_ptr->plugin_id = select_context_default;
	jobinfo_ptr->data =  (*(select_context[select_context_default].ops.
				jobinfo_alloc))();
	return jobinfo_ptr;
}

/* free storage previously allocated for a select job credential
 * IN jobinfo  - the select job credential to be freed
 */
extern int select_g_select_jobinfo_free(dynamic_plugin_data_t *jobinfo)
{
	int rc = SLURM_SUCCESS;

	if (slurm_select_init() < 0)
		return SLURM_ERROR;
	if(jobinfo) {
		if(jobinfo->data)
			rc = (*(select_context[jobinfo->plugin_id].ops.
				jobinfo_free))(jobinfo->data);
		xfree(jobinfo);
	}
	return rc;
}

extern int select_g_select_jobinfo_set(dynamic_plugin_data_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data)
{
	void *jobdata = NULL;

	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	if(jobinfo)
		jobdata = jobinfo->data;
	return (*(select_context[jobinfo->plugin_id].ops.jobinfo_set))
		(jobdata, data_type, data);
}

/* get data from a select job credential
 * IN jobinfo  - updated select job credential
 * IN data_type - type of data to enter into job credential
 * IN/OUT data - the data to enter into job credential
 */
extern int select_g_select_jobinfo_get(dynamic_plugin_data_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data)
{
	void *jobdata = NULL;

	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	if(jobinfo)
		jobdata = jobinfo->data;
	return (*(select_context[jobinfo->plugin_id].ops.jobinfo_get))
		(jobdata, data_type, data);
}

/* copy a select job credential
 * IN jobinfo - the select job credential to be copied
 * RET        - the copy or NULL on failure
 * NOTE: returned value must be freed using select_g_free_jobinfo
 */
extern dynamic_plugin_data_t *select_g_select_jobinfo_copy(
	dynamic_plugin_data_t *jobinfo)
{
	dynamic_plugin_data_t *jobinfo_ptr = NULL;
	if (slurm_select_init() < 0)
		return NULL;

	jobinfo_ptr = xmalloc(sizeof(dynamic_plugin_data_t));
	if(jobinfo) {
		jobinfo_ptr->plugin_id = jobinfo->plugin_id;
		jobinfo_ptr->data = (*(select_context[jobinfo->plugin_id].ops.
				       jobinfo_copy))(jobinfo->data);
	} else
		jobinfo_ptr->plugin_id = select_context_default;

	return jobinfo_ptr;
}

/* pack a select job credential into a buffer in machine independent form
 * IN jobinfo  - the select job credential to be saved
 * OUT buffer  - buffer with select credential appended
 * RET         - slurm error code
 */
extern int select_g_select_jobinfo_pack(dynamic_plugin_data_t *jobinfo,
					Buf buffer,
					uint16_t protocol_version)
{
	void *data = NULL;
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	if(protocol_version >= SLURM_2_2_PROTOCOL_VERSION)
		pack32(*(select_context[select_context_default].ops.plugin_id),
		       buffer);
	if(jobinfo)
		data = jobinfo->data;
	return (*(select_context[select_context_default].ops.
		  jobinfo_pack))(data, buffer, protocol_version);
}

/* unpack a select job credential from a buffer
 * OUT jobinfo - the select job credential read
 * IN  buffer  - buffer with select credential read from current pointer loc
 * RET         - slurm error code
 * NOTE: returned value must be freed using select_g_free_jobinfo
 */
extern int select_g_select_jobinfo_unpack(
	dynamic_plugin_data_t **jobinfo, Buf buffer, uint16_t protocol_version)
{
	dynamic_plugin_data_t *jobinfo_ptr = NULL;

	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	jobinfo_ptr = xmalloc(sizeof(dynamic_plugin_data_t));
	*jobinfo = jobinfo_ptr;

	if(protocol_version >= SLURM_2_2_PROTOCOL_VERSION) {
		int i;
		uint32_t plugin_id;
		safe_unpack32(&plugin_id, buffer);
		for (i=0; i<select_context_cnt; i++)
			if(*(select_context[i].ops.plugin_id) == plugin_id) {
				jobinfo_ptr->plugin_id = i;
				break;
			}
		if (i >= select_context_cnt) {
			error("we don't have this plugin type %u", plugin_id);
			goto unpack_error;
		}
	} else
		jobinfo_ptr->plugin_id = select_context_default;

	return (*(select_context[jobinfo_ptr->plugin_id].ops.jobinfo_unpack))
		(&jobinfo_ptr->data, buffer, protocol_version);
unpack_error:
	select_g_select_jobinfo_free(jobinfo_ptr);
	*jobinfo = NULL;
	error("select_g_select_jobinfo_unpack: unpack error");
	return SLURM_ERROR;
}

/* write select job credential to a string
 * IN jobinfo - a select job credential
 * OUT buf    - location to write job credential contents
 * IN size    - byte size of buf
 * IN mode    - print mode, see enum select_print_mode
 * RET        - the string, same as buf
 */
extern char *select_g_select_jobinfo_sprint(dynamic_plugin_data_t *jobinfo,
					    char *buf, size_t size, int mode)
{
	void *data = NULL;
	if (slurm_select_init() < 0)
		return NULL;

	if(jobinfo)
		data = jobinfo->data;
	return (*(select_context[select_context_default].ops.
		  jobinfo_sprint))
		(data, buf, size, mode);
}
/* write select job info to a string
 * IN jobinfo - a select job credential
 * IN mode    - print mode, see enum select_print_mode
 * RET        - char * containing string of request
 */
extern char *select_g_select_jobinfo_xstrdup(
	dynamic_plugin_data_t *jobinfo, int mode)
{
	void *data = NULL;
	if (slurm_select_init() < 0)
		return NULL;

	if(jobinfo)
		data = jobinfo->data;
	return (*(select_context[select_context_default].ops.
		  jobinfo_xstrdup))(data, mode);
}

#endif	/* HAVE_CRAY_XT */

/*
 * Update specific block (usually something has gone wrong)
 * IN block_desc_ptr - information about the block
 */
extern int select_g_update_block (update_block_msg_t *block_desc_ptr)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(select_context[select_context_default].ops.
		  update_block))(block_desc_ptr);
}

/*
 * Update specific sub nodes (usually something has gone wrong)
 * IN block_desc_ptr - information about the block
 */
extern int select_g_update_sub_node (update_block_msg_t *block_desc_ptr)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(select_context[select_context_default].ops.
		  update_sub_node))(block_desc_ptr);
}

/*
 * Get select data from a plugin
 * IN dinfo     - type of data to get from the node record
 *                (see enum select_plugindata_info)
 * IN/OUT data  - the data to get from node record
 */
extern int select_g_get_info_from_plugin (enum select_plugindata_info dinfo,
					  struct job_record *job_ptr,
					  void *data)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(select_context[select_context_default].ops.
		  get_info_from_plugin))(dinfo, job_ptr, data);
}

/*
 * Updated a node configuration. This happens when a node registers with
 *     more resources than originally configured (e.g. memory).
 * IN index  - index into the node record list
 * RETURN SLURM_SUCCESS on success || SLURM_ERROR else wise
 */
extern int select_g_update_node_config (int index)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(select_context[select_context_default].ops.
		  update_node_config))(index);
}

/*
 * Updated a node state in the plugin, this should happen when a node is
 * drained or put into a down state then changed back.
 * IN index  - index into the node record list
 * IN state  - state to update to
 * RETURN SLURM_SUCCESS on success || SLURM_ERROR else wise
 */
extern int select_g_update_node_state (int index, uint16_t state)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(select_context[select_context_default].ops.update_node_state))
		(index, state);
}

/*
 * Alter the node count for a job given the type of system we are on
 * IN/OUT job_desc  - current job desc
 */
extern int select_g_alter_node_cnt (enum select_node_cnt type, void *data)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	if (type == SELECT_GET_NODE_SCALING) {
		/* default to one, so most plugins don't have to */
		uint32_t *nodes = (uint32_t *)data;
		*nodes = 1;
	}
	return (*(select_context[select_context_default].ops.alter_node_cnt))
		(type, data);
}

/*
 * Note reconfiguration or change in partition configuration
 */
extern int select_g_reconfigure (void)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(select_context[select_context_default].ops.reconfigure))();
}


