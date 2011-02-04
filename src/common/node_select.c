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
#include <dirent.h>

#include "src/common/list.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"
#include "src/common/node_select.h"

static int select_context_cnt = -1;
static int select_context_default = -1;
/* If there is a new select plugin, list it here */
static slurm_select_context_t * select_context = NULL;
static pthread_mutex_t		select_context_lock =
	PTHREAD_MUTEX_INITIALIZER;

/*
 * Locate and load the appropriate plugin
 */
static int _select_get_ops(char *select_type,
			   slurm_select_context_t *c)
{
	/*
	 * Must be synchronized with slurm_select_ops_t in node_select.h.
	 * Also must be synchronized with the other_plugin.[c|h] in
	 * the select/cray plugin.
	 */
	static const char *syms[] = {
		"plugin_id",
		"select_p_state_save",
		"select_p_state_restore",
		"select_p_job_init",
		"select_p_node_ranking",
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
		"select_p_resv_test",
		"select_p_ba_init",
		"select_p_ba_fini",
		"select_p_ba_get_dims",
		"select_p_ba_reset",
		"select_p_ba_request_apply",
		"select_p_ba_remove_block",
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

	if(errno != EPLUGIN_NOTFOUND) {
		error("Couldn't load specified plugin name for %s: %s",
		      c->select_type, plugin_strerror(errno));
		return SLURM_ERROR;
	}

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

/**
 * delete a block request
 */
extern void destroy_select_ba_request(void *arg)
{
	select_ba_request_t *ba_request = (select_ba_request_t *)arg;
	if (ba_request) {
		xfree(ba_request->save_name);
		if (ba_request->elongate_geos)
			list_destroy(ba_request->elongate_geos);

		xfree(ba_request->blrtsimage);
		xfree(ba_request->linuximage);
		xfree(ba_request->mloaderimage);
		xfree(ba_request->ramdiskimage);

		xfree(ba_request);
	}
}

/**
 * print a block request
 */
extern void print_select_ba_request(select_ba_request_t* ba_request)
{
	int dim;
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();
	uint16_t cluster_dims = slurmdb_setup_cluster_name_dims();

	if (ba_request == NULL){
		error("print_ba_request Error, request is NULL");
		return;
	}
	debug("  ba_request:");
	debug("    geometry:\t");
	for (dim=0; dim<cluster_dims; dim++){
		debug("%d", ba_request->geometry[dim]);
	}
	debug("        size:\t%d", ba_request->size);
	if (cluster_flags & CLUSTER_FLAG_BGQ) {
		for (dim=0; dim<cluster_dims; dim++)
			debug("   conn_type:\t%d", ba_request->conn_type[dim]);
	} else
		debug("   conn_type:\t%d", ba_request->conn_type[0]);

	debug("      rotate:\t%d", ba_request->rotate);
	debug("    elongate:\t%d", ba_request->elongate);
}


/*
 * Initialize context for node selection plugin
 */
extern int slurm_select_init(bool only_default)
{
	int retval = SLURM_SUCCESS;
	char *select_type = NULL;
	int i, j, rc, len;
	DIR *dirp;
	struct dirent *e;
	char *dir_array = NULL, *head = NULL;

	slurm_mutex_lock( &select_context_lock );

	if ( select_context )
		goto done;

	select_type = slurm_get_select_type();
	if (working_cluster_rec) {
		/* just ignore warnings here */
	} else {
#ifdef HAVE_XCPU
		if (strcasecmp(select_type, "select/linear")) {
			error("%s is incompatible with XCPU use", select_type);
			fatal("Use SelectType=select/linear");
		}
#endif

#ifdef HAVE_BG
#  ifdef HAVE_BGQ
		if (strcasecmp(select_type, "select/bgq")) {
			error("%s is incompatible with BlueGene/Q",
			      select_type);
			fatal("Use SelectType=select/bgq");
		}
#  else
		if (strcasecmp(select_type, "select/bluegene")) {
			error("%s is incompatible with BlueGene", select_type);
			fatal("Use SelectType=select/bluegene");
		}
#  endif
#else
		if (!strcasecmp(select_type, "select/bgq")) {
			fatal("Requested SelectType=select/bgq in slurm.conf, "
			      "but not running on a BGQ system.  If looking "
			      "to emulate a BGQ system use "
			      "--enable-bgq-emulation.");
		}

		if (!strcasecmp(select_type, "select/bluegene")) {
			fatal("Requested SelectType=select/bluegene "
			      "in slurm.conf, but not running on a BG[L|P] "
			      "system.  If looking to emulate a BG[L|P] "
			      "system use --enable-bgl-emulation or "
			      "--enable-bgp-emulation repectfully.");
		}
#endif

#ifdef HAVE_NATIVE_CRAY
		if (strcasecmp(select_type, "select/cray")) {
			error("%s is incompatible with Cray", select_type);
			fatal("Use SelectType=select/cray");
		}
#endif
	}

	select_context_cnt = 0;
	if(only_default) {
		select_context = xmalloc(sizeof(slurm_select_context_t));
		rc = _select_get_ops(select_type, select_context);
		if (rc == SLURM_SUCCESS) {
			select_context_default = 0;
			select_context_cnt++;
		}
		goto skip_load_all;
	}

	if(!(dir_array = slurm_get_plugin_dir())) {
		error("plugin_load_and_link: No plugin dir given");
		goto done;
	}

	head = dir_array;
	for (i=0; ; i++) {
		bool got_colon = 0;
		if (dir_array[i] == ':') {
			dir_array[i] = '\0';
			got_colon = 1;
		} else if(dir_array[i] != '\0')
			continue;

		/* Open the directory. */
		if(!(dirp = opendir(head))) {
			error("cannot open plugin directory %s", head);
			goto done;
		}

		while (1) {
			char full_name[128];

			if(!(e = readdir( dirp )))
				break;
			/* Check only files with select_ in them. */
			if (strncmp(e->d_name, "select_", 7))
				continue;

			len = strlen(e->d_name);
#if defined(__CYGWIN__)
			len -= 4;
#else
			len -= 3;
#endif
			/* Check only shared object files */
			if (strcmp(e->d_name+len,
#if defined(__CYGWIN__)
				   ".dll"
#else
				   ".so"
#endif
				    ))
				continue;
			/* add one for the / */
			len++;
			xassert(len<sizeof(full_name));
			snprintf(full_name, len, "select/%s", e->d_name+7);
			for (j=0; j<select_context_cnt; j++) {
				if (!strcmp(full_name,
					    select_context[j].select_type))
					break;
			}
			if (j < select_context_cnt) {
				error("Duplicate plugin %s ignored",
				      select_context[j].select_type);
			} else {
				xrealloc(select_context,
					 (sizeof(slurm_select_context_t) *
					  (select_context_cnt + 1)));

				rc = _select_get_ops(
					full_name,
					select_context + select_context_cnt);

				/* only add the ones this system has */
				if (rc == SLURM_SUCCESS) {
					/* set the default */
					if (!strcmp(full_name, select_type))
						select_context_default =
							select_context_cnt;
					select_context_cnt++;
				}
			}
		}

		closedir(dirp);

		if (got_colon) {
			head = dir_array + i + 1;
		} else
			break;
	}

skip_load_all:
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
	xfree(dir_array);
	return retval;
}

extern int slurm_select_fini(void)
{
	int rc = SLURM_SUCCESS, i, j;

	slurm_mutex_lock(&select_context_lock);
	if (!select_context)
		goto fini;

	for (i=0; i<select_context_cnt; i++) {
		j = _select_context_destroy(select_context + i);
		if (j != SLURM_SUCCESS)
			rc = j;
	}
	xfree(select_context);
	select_context_cnt = -1;

fini:	slurm_mutex_unlock(&select_context_lock);
	return rc;
}

extern int select_get_plugin_id_pos(uint32_t plugin_id)
{
	int i;

	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	for (i=0; i<select_context_cnt; i++) {
		if(*(select_context[i].ops.plugin_id) == plugin_id)
			break;
	}
	if(i >= select_context_cnt)
		return SLURM_ERROR;
	return i;
}

extern int select_get_plugin_id()
{
	if (slurm_select_init(0) < 0)
		return 0;

	return *(select_context[select_context_default].ops.plugin_id);
}

/*
 * Save any global state information
 * IN dir_name - directory into which the data can be stored
 */
extern int select_g_state_save(char *dir_name)
{
	if (slurm_select_init(0) < 0)
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
	if (slurm_select_init(0) < 0)
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
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(select_context[select_context_default].ops.job_init))
		(job_list);
}

/*
 * Assign a 'node_rank' value to each of the node_ptr entries.
 * IN node_ptr - current node data
 * IN node_count - number of node entries
 * Return true if node ranking was performed, false if not.
 */
extern bool select_g_node_ranking(struct node_record *node_ptr, int node_cnt)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(select_context[select_context_default].ops.node_ranking))
		(node_ptr, node_cnt);
}

/*
 * Note re/initialization of node record data structure
 * IN node_ptr - current node data
 * IN node_count - number of node entries
 */
extern int select_g_node_init(struct node_record *node_ptr, int node_cnt)
{
	if (slurm_select_init(0) < 0)
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
	if (slurm_select_init(0) < 0)
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
	if (slurm_select_init(0) < 0)
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
	if (slurm_select_init(0) < 0)
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
	if (slurm_select_init(0) < 0)
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
	if (slurm_select_init(0) < 0)
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
	if (slurm_select_init(0) < 0)
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
	if (slurm_select_init(0) < 0)
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
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(select_context[select_context_default].ops.job_resume))
		(job_ptr);
}

extern int select_g_pack_select_info(time_t last_query_time,
				     uint16_t show_flags, Buf *buffer,
				     uint16_t protocol_version)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(select_context[select_context_default].ops.pack_select_info))
		(last_query_time, show_flags, buffer, protocol_version);
}

extern int select_g_select_nodeinfo_pack(dynamic_plugin_data_t *nodeinfo,
					 Buf buffer,
					 uint16_t protocol_version)
{
	void *data = NULL;
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	if(nodeinfo) {
		data = nodeinfo->data;
		plugin_id = nodeinfo->plugin_id;
	} else
		plugin_id = select_context_default;

	if(protocol_version >= SLURM_2_2_PROTOCOL_VERSION)
		pack32(*(select_context[plugin_id].ops.plugin_id),
		       buffer);

	return (*(select_context[plugin_id].ops.
		  nodeinfo_pack))(data, buffer, protocol_version);
}

extern int select_g_select_nodeinfo_unpack(dynamic_plugin_data_t **nodeinfo,
					   Buf buffer,
					   uint16_t protocol_version)
{
	dynamic_plugin_data_t *nodeinfo_ptr = NULL;

	if (slurm_select_init(0) < 0)
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
		((select_nodeinfo_t **)&nodeinfo_ptr->data, buffer,
		 protocol_version);
unpack_error:
	error("select_g_select_nodeinfo_unpack: unpack error");
	return SLURM_ERROR;
}

extern dynamic_plugin_data_t *select_g_select_nodeinfo_alloc(uint32_t size)
{
	dynamic_plugin_data_t *nodeinfo_ptr = NULL;
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return NULL;

	plugin_id = working_cluster_rec ?
		working_cluster_rec->plugin_id_select : select_context_default;

	nodeinfo_ptr = xmalloc(sizeof(dynamic_plugin_data_t));
	nodeinfo_ptr->plugin_id = plugin_id;
	nodeinfo_ptr->data = (*(select_context[plugin_id].ops.
				nodeinfo_alloc))(size);
	return nodeinfo_ptr;
}

extern int select_g_select_nodeinfo_free(dynamic_plugin_data_t *nodeinfo)
{
	int rc = SLURM_SUCCESS;

	if (slurm_select_init(0) < 0)
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
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(select_context[select_context_default].ops.nodeinfo_set_all))
		(last_query_time);
}

extern int select_g_select_nodeinfo_set(struct job_record *job_ptr)
{
	if (slurm_select_init(0) < 0)
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
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	if(nodeinfo) {
		nodedata = nodeinfo->data;
		plugin_id = nodeinfo->plugin_id;
	} else
		plugin_id = select_context_default;

	return (*(select_context[plugin_id].ops.nodeinfo_get))
		(nodedata, dinfo, state, data);
}

extern dynamic_plugin_data_t *select_g_select_jobinfo_alloc(void)
{
	dynamic_plugin_data_t *jobinfo_ptr = NULL;
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return NULL;

	plugin_id = working_cluster_rec ?
		working_cluster_rec->plugin_id_select : select_context_default;

	jobinfo_ptr = xmalloc(sizeof(dynamic_plugin_data_t));
	jobinfo_ptr->plugin_id = plugin_id;
	jobinfo_ptr->data =  (*(select_context[plugin_id].ops.
				jobinfo_alloc))();
	return jobinfo_ptr;
}

/* free storage previously allocated for a select job credential
 * IN jobinfo  - the select job credential to be freed
 */
extern int select_g_select_jobinfo_free(dynamic_plugin_data_t *jobinfo)
{
	int rc = SLURM_SUCCESS;

	if (slurm_select_init(0) < 0)
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
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	if(jobinfo) {
		jobdata = jobinfo->data;
		plugin_id = jobinfo->plugin_id;
	} else
		plugin_id = select_context_default;

	return (*(select_context[plugin_id].ops.jobinfo_set))
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
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	if(jobinfo) {
		jobdata = jobinfo->data;
		plugin_id = jobinfo->plugin_id;
	} else
		plugin_id = select_context_default;

	return (*(select_context[plugin_id].ops.jobinfo_get))
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
	if (slurm_select_init(0) < 0)
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
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	if(jobinfo) {
		data = jobinfo->data;
		plugin_id = jobinfo->plugin_id;
	} else
		plugin_id = select_context_default;

	if(protocol_version >= SLURM_2_2_PROTOCOL_VERSION)
		pack32(*(select_context[plugin_id].ops.plugin_id),
		       buffer);
	return (*(select_context[plugin_id].ops.
		  jobinfo_pack))(data, buffer, protocol_version);
}

/* unpack a select job credential from a buffer
 * OUT jobinfo - the select job credential read
 * IN  buffer  - buffer with select credential read from current pointer loc
 * RET         - slurm error code
 * NOTE: returned value must be freed using select_g_free_jobinfo
 */
extern int select_g_select_jobinfo_unpack(dynamic_plugin_data_t **jobinfo,
					  Buf buffer,
					  uint16_t protocol_version)
{
	dynamic_plugin_data_t *jobinfo_ptr = NULL;

	if (slurm_select_init(0) < 0)
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
		((select_jobinfo_t **)&jobinfo_ptr->data, buffer,
		 protocol_version);
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
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return NULL;
	if(jobinfo) {
		data = jobinfo->data;
		plugin_id = jobinfo->plugin_id;
	} else
		plugin_id = select_context_default;

	return (*(select_context[plugin_id].ops.
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
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return NULL;

	if(jobinfo) {
		data = jobinfo->data;
		plugin_id = jobinfo->plugin_id;
	} else
		plugin_id = select_context_default;

	return (*(select_context[plugin_id].ops.
		  jobinfo_xstrdup))(data, mode);
}

/*
 * Update specific block (usually something has gone wrong)
 * IN block_desc_ptr - information about the block
 */
extern int select_g_update_block (update_block_msg_t *block_desc_ptr)
{
	if (slurm_select_init(0) < 0)
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
	if (slurm_select_init(0) < 0)
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
	if (slurm_select_init(0) < 0)
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
	if (slurm_select_init(0) < 0)
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
	if (slurm_select_init(0) < 0)
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
	if (slurm_select_init(0) < 0)
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
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(select_context[select_context_default].ops.reconfigure))();
}

/*
 * select_g_resv_test - Identify the nodes which "best" satisfy a reservation
 *	request. "best" is defined as either single set of consecutive nodes
 *	satisfying the request and leaving the minimum number of unused nodes
 *	OR the fewest number of consecutive node sets
 * IN avail_bitmap - nodes available for the reservation
 * IN node_cnt - count of required nodes
 * RET - nodes selected for use by the reservation
 */
extern bitstr_t * select_g_resv_test(bitstr_t *avail_bitmap, uint32_t node_cnt)
{
#if 0
	/* Wait for Danny to checkin select/bgq logic before using new plugin
	 * function calls. The select_p_resv_test() function is currently only
	 * available in select/linear and select/cons_res */
	if (slurm_select_init(0) < 0)
		return NULL;

	return (*(select_context[select_context_default].ops.resv_test))
		(avail_bitmap, node_cnt);
#else
	return bit_pick_cnt(avail_bitmap, node_cnt);
#endif
}

extern void select_g_ba_init(node_info_msg_t *node_info_ptr, bool sanity_check)
{
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return;

	if (working_cluster_rec)
		plugin_id = working_cluster_rec->plugin_id_select;
	else
		plugin_id = select_context_default;

	(*(select_context[plugin_id].ops.ba_init))(node_info_ptr, sanity_check);
}

extern void select_g_ba_fini(void)
{
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return;

	if (working_cluster_rec)
		plugin_id = working_cluster_rec->plugin_id_select;
	else
		plugin_id = select_context_default;

	(*(select_context[plugin_id].ops.ba_fini))();
}

extern int *select_g_ba_get_dims()
{
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return NULL;

	if (working_cluster_rec)
		plugin_id = working_cluster_rec->plugin_id_select;
	else
		plugin_id = select_context_default;

	return (*(select_context[plugin_id].ops.ba_get_dims))();
}

extern void select_g_ba_reset(bool track_down_nodes)
{
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return;

	if (working_cluster_rec)
		plugin_id = working_cluster_rec->plugin_id_select;
	else
		plugin_id = select_context_default;

	(*(select_context[plugin_id].ops.ba_reset))(track_down_nodes);
}

extern int select_g_ba_request_apply(select_ba_request_t *ba_request)
{
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return 0;

	if (working_cluster_rec)
		plugin_id = working_cluster_rec->plugin_id_select;
	else
		plugin_id = select_context_default;

	return (*(select_context[plugin_id].ops.ba_request_apply))(ba_request);
}

extern int select_g_ba_remove_block(List mps, int new_count, bool is_small)
{
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return 0;

	if (working_cluster_rec)
		plugin_id = working_cluster_rec->plugin_id_select;
	else
		plugin_id = select_context_default;

	return (*(select_context[plugin_id].ops.ba_remove_block))
		(mps, new_count, is_small);
}

extern char *select_g_ba_passthroughs_string(uint16_t passthrough)
{
	char *pass = NULL;

	if (passthrough & PASS_FOUND_A)
		xstrcat(pass, "A");
	if (passthrough & PASS_FOUND_X) {
		if (pass)
			xstrcat(pass, ",X");
		else
			xstrcat(pass, "X");
	}
	if (passthrough & PASS_FOUND_Y) {
		if (pass)
			xstrcat(pass, ",Y");
		else
			xstrcat(pass, "Y");
	}
	if (passthrough & PASS_FOUND_Z) {
		if (pass)
			xstrcat(pass, ",Z");
		else
			xstrcat(pass, "Z");
	}

	return pass;
}
