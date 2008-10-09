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
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
#include "src/common/node_select.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

/* Define select_jobinfo_t below to avoid including extraneous slurm headers */
#ifndef __select_jobinfo_t_defined
#  define  __select_jobinfo_t_defined
   typedef struct select_jobinfo *select_jobinfo_t;     /* opaque data type */
#endif

/*
 * Local data
 */

typedef struct slurm_select_ops {
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
						int mode);
	int             (*job_list_test)       (List req_list);
	int		(*job_begin)	       (struct job_record *job_ptr);
	int		(*job_ready)	       (struct job_record *job_ptr);
	int		(*job_fini)	       (struct job_record *job_ptr);
	int		(*job_suspend)	       (struct job_record *job_ptr);
	int		(*job_resume)	       (struct job_record *job_ptr);
	int		(*pack_node_info)      (time_t last_query_time,
						Buf *buffer_ptr);
        int             (*get_select_nodeinfo) (struct node_record *node_ptr,
						enum select_data_info cr_info, 
						void *data);
	int             (*update_nodeinfo)     (struct job_record *job_ptr);
        int             (*update_block)        (update_part_msg_t
						*part_desc_ptr);
        int             (*update_sub_node)     (update_part_msg_t
						*part_desc_ptr);
	int             (*get_info_from_plugin)(enum select_data_info cr_info,
						struct job_record *job_ptr,
						void *data);
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

static slurm_select_context_t * g_select_context = NULL;
static pthread_mutex_t		g_select_context_lock = 
					PTHREAD_MUTEX_INITIALIZER;

#ifdef HAVE_BG			/* node selection specific logic */
#  define JOBINFO_MAGIC 0x83ac
struct select_jobinfo {
	uint16_t start[SYSTEM_DIMENSIONS];	/* start position of block
						 *  e.g. XYZ */
	uint16_t geometry[SYSTEM_DIMENSIONS];	/* node count in various
						 * dimensions, e.g. XYZ */
	uint16_t conn_type;	/* see enum connection_type */
	uint16_t reboot;	/* reboot block before starting job */
	uint16_t rotate;	/* permit geometry rotation if set */
	char *bg_block_id;	/* Blue Gene block ID */
	uint16_t magic;		/* magic number */
	char *nodes;            /* node list given for estimated start */ 
	char *ionodes;          /* for bg to tell which ionodes of a small
				 * block the job is running */ 
	uint32_t node_cnt;      /* how many cnodes in block */ 
	uint16_t altered;       /* see if we have altered this job 
				 * or not yet */
	uint32_t max_procs;	/* maximum processors to use */
	char *blrtsimage;       /* BlrtsImage for this block */
	char *linuximage;       /* LinuxImage for this block */
	char *mloaderimage;     /* mloaderImage for this block */
	char *ramdiskimage;     /* RamDiskImage for this block */
};
#endif

/*
 * Local functions
 */
static slurm_select_ops_t *_select_get_ops(slurm_select_context_t *c);
static slurm_select_context_t *_select_context_create(const char *select_type);
static int _select_context_destroy(slurm_select_context_t *c);

/*
 * Locate and load the appropriate plugin
 */
static slurm_select_ops_t * _select_get_ops(slurm_select_context_t *c)
{
	/*
	 * Must be synchronized with slurm_select_ops_t above.
	 */
	static const char *syms[] = {
		"select_p_state_save",
		"select_p_state_restore",
		"select_p_job_init",
		"select_p_node_init",
		"select_p_block_init",
		"select_p_job_test",
		"select_p_job_list_test",
		"select_p_job_begin",
		"select_p_job_ready",
		"select_p_job_fini",
		"select_p_job_suspend",
		"select_p_job_resume",
		"select_p_pack_node_info",
                "select_p_get_select_nodeinfo",
                "select_p_update_nodeinfo",
		"select_p_update_block",
 		"select_p_update_sub_node",
                "select_p_get_info_from_plugin",
		"select_p_update_node_state",
		"select_p_alter_node_cnt",
		"select_p_reconfigure"
	};
	int n_syms = sizeof( syms ) / sizeof( char * );

	/* Find the correct plugin. */
        c->cur_plugin = plugin_load_and_link(c->select_type, n_syms, syms,
					     (void **) &c->ops);
        if ( c->cur_plugin != PLUGIN_INVALID_HANDLE ) 
        	return &c->ops;

	error("Couldn't find the specified plugin name for %s "
	      "looking at all files",
	      c->select_type);
	
	/* Get plugin list. */
	if ( c->plugin_list == NULL ) {
		char *plugin_dir;
		c->plugin_list = plugrack_create();
		if ( c->plugin_list == NULL ) {
			error( "cannot create plugin manager" );
			return NULL;
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
		return NULL;
	}

	/* Dereference the API. */
	if ( plugin_get_syms( c->cur_plugin,
			      n_syms,
			      syms,
			      (void **) &c->ops ) < n_syms ) {
		error( "incomplete node selection plugin detected" );
		return NULL;
	}

	return &c->ops;
}

/*
 * Create a node selection context
 */
static slurm_select_context_t *_select_context_create(const char *select_type)
{
	slurm_select_context_t *c;

	if ( select_type == NULL ) {
		debug3( "_select_context_create: no uler type" );
		return NULL;
	}

	c = xmalloc( sizeof( slurm_select_context_t ) );
	c->select_type	= xstrdup( select_type );
	c->plugin_list	= NULL;
	c->cur_plugin	= PLUGIN_INVALID_HANDLE;
	c->select_errno	= SLURM_SUCCESS;

	return c;
}

/*
 * Destroy a node selection context
 */
static int _select_context_destroy( slurm_select_context_t *c )
{
	/*
	 * Must check return code here because plugins might still
	 * be loaded and active.
	 */
	if ( c->plugin_list ) {
		if ( plugrack_destroy( c->plugin_list ) != SLURM_SUCCESS ) {
			return SLURM_ERROR;
		}
	} else {
		plugin_unload(c->cur_plugin);
	}

	xfree( c->select_type );
	xfree( c );

	return SLURM_SUCCESS;
}

/*
 * Initialize context for node selection plugin
 */
extern int slurm_select_init(void)
{
	int retval = SLURM_SUCCESS;
	char *select_type = NULL;
	
	slurm_mutex_lock( &g_select_context_lock );

	if ( g_select_context )
		goto done;

	select_type = slurm_get_select_type();
	g_select_context = _select_context_create(select_type);
	if ( g_select_context == NULL ) {
		error( "cannot create node selection context for %s",
			 select_type );
		retval = SLURM_ERROR;
		goto done;
	}

	if ( _select_get_ops( g_select_context ) == NULL ) {
		error( "cannot resolve node selection plugin operations" );
		_select_context_destroy( g_select_context );
		g_select_context = NULL;
		retval = SLURM_ERROR;
	}

 done:
	slurm_mutex_unlock( &g_select_context_lock );
	xfree(select_type);
	return retval;
}

extern int slurm_select_fini(void)
{
	int rc;

	if (!g_select_context)
		return SLURM_SUCCESS;

	rc = _select_context_destroy( g_select_context );
	g_select_context = NULL;
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

	return (*(g_select_context->ops.state_save))(dir_name);
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

	return (*(g_select_context->ops.state_restore))(dir_name);
}

/*
 * Note the initialization of job records, issued upon restart of
 * slurmctld and used to synchronize any job state.
 */
extern int select_g_job_init(List job_list)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(g_select_context->ops.job_init))(job_list);
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

	return (*(g_select_context->ops.node_init))(node_ptr, node_cnt);
}


/*
 * Note re/initialization of block record data structure
 * IN block_list - list of partition records
 */
extern int select_g_block_init(List block_list)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(g_select_context->ops.block_init))(block_list);
}

/* 
 * Get select data from a specific node record
 * IN node_pts  - current node record
 * IN cr_info   - type of data to get from the node record 
 *                (see enum select_data_info)
 * IN/OUT data  - the data to get from node record
 */
extern int select_g_get_select_nodeinfo (struct node_record *node_ptr, 
                                         enum select_data_info cr_info, 
					 void *data)
{
       if (slurm_select_init() < 0)
               return SLURM_ERROR;

       return (*(g_select_context->ops.get_select_nodeinfo))(node_ptr, 
							     cr_info, 
							     data);
}

/* 
 * Update select data for a specific node record for a specific job 
 * IN cr_info   - type of data to update for a given job record 
 *                (see enum select_data_info)
 * IN job_ptr - current job record
 */
extern int select_g_update_nodeinfo (struct job_record *job_ptr)
{
       if (slurm_select_init() < 0)
               return SLURM_ERROR;

       return (*(g_select_context->ops.update_nodeinfo))(job_ptr);
}

/* 
 * Update specific block (usually something has gone wrong)  
 * IN part_desc_ptr - information about the block
 */
extern int select_g_update_block (update_part_msg_t *part_desc_ptr)
{
       if (slurm_select_init() < 0)
               return SLURM_ERROR;

       return (*(g_select_context->ops.update_block))(part_desc_ptr);
}

/* 
 * Update specific sub nodes (usually something has gone wrong)  
 * IN part_desc_ptr - information about the block
 */
extern int select_g_update_sub_node (update_part_msg_t *part_desc_ptr)
{
       if (slurm_select_init() < 0)
               return SLURM_ERROR;

       return (*(g_select_context->ops.update_sub_node))(part_desc_ptr);
}

/* 
 * Get select data from a plugin
 * IN node_pts  - current node record
 * IN cr_info   - type of data to get from the node record 
 *                (see enum select_data_info)
 * IN/OUT data  - the data to get from node record
 */
extern int select_g_get_info_from_plugin (enum select_data_info cr_info, 
					  struct job_record *job_ptr,
					  void *data)
{
       if (slurm_select_init() < 0)
               return SLURM_ERROR;

       return (*(g_select_context->ops.get_info_from_plugin))(cr_info, job_ptr,
       								data);
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

	return (*(g_select_context->ops.update_node_state))(index, state);
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
	return (*(g_select_context->ops.alter_node_cnt))(type, data);
}

/*
 * Note reconfiguration or change in partition configuration
 */
extern int select_g_reconfigure (void)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(g_select_context->ops.reconfigure))();
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
 * RET zero on success, EINVAL otherwise
 */
extern int select_g_job_test(struct job_record *job_ptr, bitstr_t *bitmap,
			     uint32_t min_nodes, uint32_t max_nodes, 
			     uint32_t req_nodes, int mode)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(g_select_context->ops.job_test))(job_ptr, bitmap, 
						   min_nodes, max_nodes, 
						   req_nodes, mode);
}

/*
 * Given a list of select_will_run_t's in
 * accending priority order we will see if we can start and
 * finish all the jobs without increasing the start times of the
 * jobs specified and fill in the est_start of requests with no
 * est_start.  If you are looking to see if one job will ever run
 * then use select_p_job_test instead.
 * IN/OUT req_list - list of select_will_run_t's in asscending
 *	             priority order on success of placement fill in
 *	             est_start of request with time.
 * RET zero on success, EINVAL otherwise
 */
extern int select_g_job_list_test(List req_list) 
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(g_select_context->ops.job_list_test))(req_list);
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

	return (*(g_select_context->ops.job_begin))(job_ptr);
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

	return (*(g_select_context->ops.job_ready))(job_ptr);
}

/*
 * Note termination of job is starting. Executed from slurmctld.
 * IN job_ptr - pointer to job being terminated
 */
extern int select_g_job_fini(struct job_record *job_ptr)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(g_select_context->ops.job_fini))(job_ptr);
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

	return (*(g_select_context->ops.job_suspend))(job_ptr);
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

	return (*(g_select_context->ops.job_resume))(job_ptr);
}

extern int select_g_pack_node_info(time_t last_query_time, Buf *buffer)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;
	
	return (*(g_select_context->ops.pack_node_info))
		(last_query_time, buffer);
}

#ifdef HAVE_BG		/* node selection specific logic */
static void _free_node_info(bg_info_record_t *bg_info_record)
{
	xfree(bg_info_record->nodes);
	xfree(bg_info_record->ionodes);
	xfree(bg_info_record->owner_name);
	xfree(bg_info_record->bg_block_id);
	xfree(bg_info_record->bp_inx);
	xfree(bg_info_record->ionode_inx);
	xfree(bg_info_record->blrtsimage);
	xfree(bg_info_record->linuximage);
	xfree(bg_info_record->mloaderimage);
	xfree(bg_info_record->ramdiskimage);
}

/* NOTE: The matching pack functions are directly in the select/bluegene 
 * plugin. The unpack functions can not be there since the plugin is 
 * dependent upon libraries which do not exist on the BlueGene front-end 
 * nodes. */
static int _unpack_node_info(bg_info_record_t *bg_info_record, Buf buffer)
{
	uint16_t uint16_tmp;
	uint32_t uint32_tmp;
	char *bp_inx_str;
	
	safe_unpackstr_xmalloc(&(bg_info_record->nodes), &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&(bg_info_record->ionodes), &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&bg_info_record->owner_name,
			       &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&bg_info_record->bg_block_id,
			       &uint32_tmp, buffer);
	safe_unpack16(&uint16_tmp, buffer);
	bg_info_record->state     = (int) uint16_tmp;
	safe_unpack16(&uint16_tmp, buffer);
	bg_info_record->conn_type = (int) uint16_tmp;
	safe_unpack16(&uint16_tmp, buffer);
	bg_info_record->node_use = (int) uint16_tmp;
	safe_unpack16(&uint16_tmp, buffer);
	bg_info_record->quarter = (int) uint16_tmp;
	safe_unpack16(&uint16_tmp, buffer);
	bg_info_record->nodecard = (int) uint16_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	bg_info_record->node_cnt = (int) uint32_tmp;
	safe_unpackstr_xmalloc(&bp_inx_str, &uint32_tmp, buffer);
	if (bp_inx_str == NULL) {
		bg_info_record->bp_inx = bitfmt2int("");
	} else {
		bg_info_record->bp_inx = bitfmt2int(bp_inx_str);
		xfree(bp_inx_str);
	}
	safe_unpackstr_xmalloc(&bp_inx_str, &uint32_tmp, buffer);
	if (bp_inx_str == NULL) {
		bg_info_record->ionode_inx = bitfmt2int("");
	} else {
		bg_info_record->ionode_inx = bitfmt2int(bp_inx_str);
		xfree(bp_inx_str);
	}
	safe_unpackstr_xmalloc(&bg_info_record->blrtsimage,   &uint32_tmp, 
			       buffer);
	safe_unpackstr_xmalloc(&bg_info_record->linuximage,   &uint32_tmp, 
			       buffer);
	safe_unpackstr_xmalloc(&bg_info_record->mloaderimage, &uint32_tmp, 
			       buffer);
	safe_unpackstr_xmalloc(&bg_info_record->ramdiskimage, &uint32_tmp, 
			       buffer);
	
	return SLURM_SUCCESS;

unpack_error:
	error("_unpack_node_info: error unpacking here");
	_free_node_info(bg_info_record);
	return SLURM_ERROR;
}

static char *_job_conn_type_string(uint16_t inx)
{
	if (inx == SELECT_TORUS)
		return "torus";
	else if (inx == SELECT_MESH)
		return "mesh";
	else if (inx == SELECT_SMALL)
		return "small";
	else
		return "n/a";
}

static char *_yes_no_string(uint16_t inx)
{
	if (inx == (uint16_t) NO_VAL)
		return "n/a";
	else if (inx)
		return "yes";
	else
		return "no";
}

/* allocate storage for a select job credential
 * OUT jobinfo - storage for a select job credential
 * RET         - slurm error code
 * NOTE: storage must be freed using select_g_free_jobinfo
 */
extern int select_g_alloc_jobinfo (select_jobinfo_t *jobinfo)
{
	int i;
	xassert(jobinfo != NULL);
	
	*jobinfo = xmalloc(sizeof(struct select_jobinfo));
	for (i=0; i<SYSTEM_DIMENSIONS; i++) {
		(*jobinfo)->start[i]    = (uint16_t) NO_VAL;
		(*jobinfo)->geometry[i] = (uint16_t) NO_VAL;
	}
	(*jobinfo)->conn_type = SELECT_NAV;
	(*jobinfo)->reboot = (uint16_t) NO_VAL;
	(*jobinfo)->rotate = (uint16_t) NO_VAL;
	(*jobinfo)->bg_block_id = NULL;
	(*jobinfo)->magic = JOBINFO_MAGIC;
	(*jobinfo)->nodes = NULL;
	(*jobinfo)->ionodes = NULL;
	(*jobinfo)->node_cnt = NO_VAL;
	(*jobinfo)->max_procs =  NO_VAL;
	(*jobinfo)->blrtsimage = NULL;
	(*jobinfo)->linuximage = NULL;
	(*jobinfo)->mloaderimage = NULL;
	(*jobinfo)->ramdiskimage = NULL;

	return SLURM_SUCCESS;
}

/* fill in a previously allocated select job credential
 * IN/OUT jobinfo  - updated select job credential
 * IN data_type - type of data to enter into job credential
 * IN data - the data to enter into job credential
 */
extern int select_g_set_jobinfo (select_jobinfo_t jobinfo,
		enum select_data_type data_type, void *data)
{
	int i, rc = SLURM_SUCCESS;
	uint16_t *uint16 = (uint16_t *) data;
	uint32_t *uint32 = (uint32_t *) data;
	char *tmp_char = (char *) data;

	if (jobinfo == NULL) {
		error("select_g_set_jobinfo: jobinfo not set");
		return SLURM_ERROR;
	}
	if (jobinfo->magic != JOBINFO_MAGIC) {
		error("select_g_set_jobinfo: jobinfo magic bad");
		return SLURM_ERROR;
	}

	switch (data_type) {
	case SELECT_DATA_START:
		for (i=0; i<SYSTEM_DIMENSIONS; i++) 
			jobinfo->start[i] = uint16[i];
		break;
	case SELECT_DATA_GEOMETRY:
		for (i=0; i<SYSTEM_DIMENSIONS; i++) 
			jobinfo->geometry[i] = uint16[i];
		break;
	case SELECT_DATA_REBOOT:
		jobinfo->reboot = *uint16;
		break;
	case SELECT_DATA_ROTATE:
		jobinfo->rotate = *uint16;
		break;
	case SELECT_DATA_CONN_TYPE:
		jobinfo->conn_type = *uint16;
		break;
	case SELECT_DATA_BLOCK_ID:
		/* we xfree() any preset value to avoid a memory leak */
		xfree(jobinfo->bg_block_id);
		jobinfo->bg_block_id = xstrdup(tmp_char);
		break;
	case SELECT_DATA_NODES:
		xfree(jobinfo->nodes);
		jobinfo->nodes = xstrdup(tmp_char);
		break;
	case SELECT_DATA_IONODES:
		xfree(jobinfo->ionodes);
		jobinfo->ionodes = xstrdup(tmp_char);
		break;
	case SELECT_DATA_NODE_CNT:
		jobinfo->node_cnt = *uint32;
		break;
	case SELECT_DATA_ALTERED:
		jobinfo->altered = *uint16;
		break;
	case SELECT_DATA_MAX_PROCS:
		jobinfo->max_procs = *uint32;
		break;
	case SELECT_DATA_BLRTS_IMAGE:
		/* we xfree() any preset value to avoid a memory leak */
		xfree(jobinfo->blrtsimage);
		jobinfo->blrtsimage = xstrdup(tmp_char);
		break;
	case SELECT_DATA_LINUX_IMAGE:
		/* we xfree() any preset value to avoid a memory leak */
		xfree(jobinfo->linuximage);
		jobinfo->linuximage = xstrdup(tmp_char);
		break;
	case SELECT_DATA_MLOADER_IMAGE:
		/* we xfree() any preset value to avoid a memory leak */
		xfree(jobinfo->mloaderimage);
		jobinfo->mloaderimage = xstrdup(tmp_char);
		break;
	case SELECT_DATA_RAMDISK_IMAGE:
		/* we xfree() any preset value to avoid a memory leak */
		xfree(jobinfo->ramdiskimage);
		jobinfo->ramdiskimage = xstrdup(tmp_char);
		break;	
	default:
		debug("select_g_set_jobinfo data_type %d invalid", 
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
extern int select_g_get_jobinfo (select_jobinfo_t jobinfo,
		enum select_data_type data_type, void *data)
{
	int i, rc = SLURM_SUCCESS;
	uint16_t *uint16 = (uint16_t *) data;
	uint32_t *uint32 = (uint32_t *) data;
	char **tmp_char = (char **) data;

	if (jobinfo == NULL) {
		error("select_g_get_jobinfo: jobinfo not set");
		return SLURM_ERROR;
	}
	if (jobinfo->magic != JOBINFO_MAGIC) {
		error("select_g_get_jobinfo: jobinfo magic bad");
		return SLURM_ERROR;
	}

	switch (data_type) {
	case SELECT_DATA_START:
		for (i=0; i<SYSTEM_DIMENSIONS; i++) {
			uint16[i] = jobinfo->start[i];
		}
		break;
	case SELECT_DATA_GEOMETRY:
		for (i=0; i<SYSTEM_DIMENSIONS; i++) {
			uint16[i] = jobinfo->geometry[i];
		}
		break;
	case SELECT_DATA_REBOOT:
		*uint16 = jobinfo->reboot;
		break;
	case SELECT_DATA_ROTATE:
		*uint16 = jobinfo->rotate;
		break;
	case SELECT_DATA_CONN_TYPE:
		*uint16 = jobinfo->conn_type;
		break;
	case SELECT_DATA_BLOCK_ID:
		if ((jobinfo->bg_block_id == NULL)
		    ||  (jobinfo->bg_block_id[0] == '\0'))
			*tmp_char = NULL;
		else
			*tmp_char = xstrdup(jobinfo->bg_block_id);
		break;
	case SELECT_DATA_NODES:
		if ((jobinfo->nodes == NULL)
		    ||  (jobinfo->nodes[0] == '\0'))
			*tmp_char = NULL;
		else
			*tmp_char = xstrdup(jobinfo->nodes);
		break;
	case SELECT_DATA_IONODES:
		if ((jobinfo->ionodes == NULL)
		    ||  (jobinfo->ionodes[0] == '\0'))
			*tmp_char = NULL;
		else
			*tmp_char = xstrdup(jobinfo->ionodes);
		break;
	case SELECT_DATA_NODE_CNT:
		*uint32 = jobinfo->node_cnt;
		break;
	case SELECT_DATA_ALTERED:
		*uint16 = jobinfo->altered;
		break;
	case SELECT_DATA_MAX_PROCS:
		*uint32 = jobinfo->max_procs;
		break;
	case SELECT_DATA_BLRTS_IMAGE:
		if ((jobinfo->blrtsimage == NULL)
		    ||  (jobinfo->blrtsimage[0] == '\0'))
			*tmp_char = NULL;
		else
			*tmp_char = xstrdup(jobinfo->blrtsimage);
		break;
	case SELECT_DATA_LINUX_IMAGE:
		if ((jobinfo->linuximage == NULL)
		    ||  (jobinfo->linuximage[0] == '\0'))
			*tmp_char = NULL;
		else
			*tmp_char = xstrdup(jobinfo->linuximage);
		break;
	case SELECT_DATA_MLOADER_IMAGE:
		if ((jobinfo->mloaderimage == NULL)
		    ||  (jobinfo->mloaderimage[0] == '\0'))
			*tmp_char = NULL;
		else
			*tmp_char = xstrdup(jobinfo->mloaderimage);
		break;
	case SELECT_DATA_RAMDISK_IMAGE:
		if ((jobinfo->ramdiskimage == NULL)
		    ||  (jobinfo->ramdiskimage[0] == '\0'))
			*tmp_char = NULL;
		else
			*tmp_char = xstrdup(jobinfo->ramdiskimage);
		break;
	default:
		debug("select_g_get_jobinfo data_type %d invalid", 
		      data_type);
	}

	return rc;
}

/* copy a select job credential
 * IN jobinfo - the select job credential to be copied
 * RET        - the copy or NULL on failure
 * NOTE: returned value must be freed using select_g_free_jobinfo
 */
extern select_jobinfo_t select_g_copy_jobinfo(select_jobinfo_t jobinfo)
{
	struct select_jobinfo *rc = NULL;
	int i;
		
	if (jobinfo == NULL)
		;
	else if (jobinfo->magic != JOBINFO_MAGIC)
		error("select_g_copy_jobinfo: jobinfo magic bad");
	else {
		rc = xmalloc(sizeof(struct select_jobinfo));
		for (i=0; i<SYSTEM_DIMENSIONS; i++) {
			rc->start[i] = (uint16_t)jobinfo->start[i];
		}
		for (i=0; i<SYSTEM_DIMENSIONS; i++) {
			rc->geometry[i] = (uint16_t)jobinfo->geometry[i];
		}
		rc->conn_type = jobinfo->conn_type;
		rc->reboot = jobinfo->reboot;
		rc->rotate = jobinfo->rotate;
		rc->bg_block_id = xstrdup(jobinfo->bg_block_id);
		rc->magic = JOBINFO_MAGIC;
		rc->nodes = xstrdup(jobinfo->nodes);
		rc->ionodes = xstrdup(jobinfo->ionodes);
		rc->node_cnt = jobinfo->node_cnt;
		rc->altered = jobinfo->altered;
		rc->max_procs = jobinfo->max_procs;
		rc->blrtsimage = xstrdup(jobinfo->blrtsimage);
		rc->linuximage = xstrdup(jobinfo->linuximage);
		rc->mloaderimage = xstrdup(jobinfo->mloaderimage);
		rc->ramdiskimage = xstrdup(jobinfo->ramdiskimage);
	}

	return rc;
}

/* free storage previously allocated for a select job credential
 * IN jobinfo  - the select job credential to be freed
 */
extern int select_g_free_jobinfo  (select_jobinfo_t *jobinfo)
{
	int rc = SLURM_SUCCESS;

	xassert(jobinfo != NULL);
	if (*jobinfo == NULL)	/* never set, treat as not an error */
		;
	else if ((*jobinfo)->magic != JOBINFO_MAGIC) {
		error("select_g_free_jobinfo: jobinfo magic bad");
		rc = EINVAL;
	} else {
		(*jobinfo)->magic = 0;
		xfree((*jobinfo)->bg_block_id);
		xfree((*jobinfo)->nodes);
		xfree((*jobinfo)->ionodes);
		xfree((*jobinfo)->blrtsimage);
		xfree((*jobinfo)->linuximage);
		xfree((*jobinfo)->mloaderimage);
		xfree((*jobinfo)->ramdiskimage);
		xfree(*jobinfo);
	}
	return rc;
}

/* pack a select job credential into a buffer in machine independent form
 * IN jobinfo  - the select job credential to be saved
 * OUT buffer  - buffer with select credential appended
 * RET         - slurm error code
 */
extern int  select_g_pack_jobinfo  (select_jobinfo_t jobinfo, Buf buffer)
{
	int i;

	if (jobinfo) {
		/* NOTE: If new elements are added here, make sure to 
		 * add equivalant pack of zeros below for NULL pointer */
		for (i=0; i<SYSTEM_DIMENSIONS; i++) {
			pack16(jobinfo->start[i], buffer);
			pack16(jobinfo->geometry[i], buffer);
		}
		pack16(jobinfo->conn_type, buffer);
		pack16(jobinfo->reboot, buffer);
		pack16(jobinfo->rotate, buffer);

		pack32(jobinfo->node_cnt, buffer);
		pack32(jobinfo->max_procs, buffer);

		packstr(jobinfo->bg_block_id, buffer);
		packstr(jobinfo->nodes, buffer);
		packstr(jobinfo->ionodes, buffer);
		packstr(jobinfo->blrtsimage, buffer);
		packstr(jobinfo->linuximage, buffer);
		packstr(jobinfo->mloaderimage, buffer);
		packstr(jobinfo->ramdiskimage, buffer);
	} else {
		/* pack space for 3 positions for start and for geo
		 * then 1 for conn_type, reboot, and rotate
		 */
		for (i=0; i<((SYSTEM_DIMENSIONS*2)+3); i++)
			pack16((uint16_t) 0, buffer);

		pack32((uint32_t) 0, buffer); //node_cnt
		pack32((uint32_t) 0, buffer); //max_procs

		packnull(buffer); //bg_block_id
		packnull(buffer); //nodes
		packnull(buffer); //ionodes
		packnull(buffer); //blrts
		packnull(buffer); //linux
		packnull(buffer); //mloader
		packnull(buffer); //ramdisk
	}

	return SLURM_SUCCESS;
}

/* unpack a select job credential from a buffer
 * OUT jobinfo - the select job credential read
 * IN  buffer  - buffer with select credential read from current pointer loc
 * RET         - slurm error code
 * NOTE: returned value must be freed using select_g_free_jobinfo
 */
extern int  select_g_unpack_jobinfo(select_jobinfo_t jobinfo, Buf buffer)
{
	int i;
	uint32_t uint32_tmp;

	for (i=0; i<SYSTEM_DIMENSIONS; i++) {
		safe_unpack16(&(jobinfo->start[i]), buffer);
		safe_unpack16(&(jobinfo->geometry[i]), buffer);
	}
	safe_unpack16(&(jobinfo->conn_type), buffer);
	safe_unpack16(&(jobinfo->reboot), buffer);
	safe_unpack16(&(jobinfo->rotate), buffer);

	safe_unpack32(&(jobinfo->node_cnt), buffer);
	safe_unpack32(&(jobinfo->max_procs), buffer);

	safe_unpackstr_xmalloc(&(jobinfo->bg_block_id),  &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&(jobinfo->nodes),      &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&(jobinfo->ionodes),      &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&(jobinfo->blrtsimage),   &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&(jobinfo->linuximage),   &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&(jobinfo->mloaderimage), &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&(jobinfo->ramdiskimage), &uint32_tmp, buffer);
	
	return SLURM_SUCCESS;

      unpack_error:
	return SLURM_ERROR;
}

/* write select job credential to a string
 * IN jobinfo - a select job credential
 * OUT buf    - location to write job credential contents
 * IN size    - byte size of buf
 * IN mode    - print mode, see enum select_print_mode
 * RET        - the string, same as buf
 */
extern char *select_g_sprint_jobinfo(select_jobinfo_t jobinfo,
				     char *buf, size_t size, int mode)
{
	uint16_t geometry[SYSTEM_DIMENSIONS];
	int i;
	char max_procs_char[8], start_char[32];
	char *tmp_image = "default";
		
	if (buf == NULL) {
		error("select_g_sprint_jobinfo: buf is null");
		return NULL;
	}

	if ((mode != SELECT_PRINT_DATA)
	&& jobinfo && (jobinfo->magic != JOBINFO_MAGIC)) {
		error("select_g_sprint_jobinfo: jobinfo magic bad");
		return NULL;
	}

	if (jobinfo == NULL) {
		if (mode != SELECT_PRINT_HEAD) {
			error("select_g_sprint_jobinfo: jobinfo bad");
			return NULL;
		}
	} else if (jobinfo->geometry[0] == (uint16_t) NO_VAL) {
		for (i=0; i<SYSTEM_DIMENSIONS; i++)
			geometry[i] = 0;
	} else {
		for (i=0; i<SYSTEM_DIMENSIONS; i++)
			geometry[i] = jobinfo->geometry[i];
	}

	switch (mode) {
	case SELECT_PRINT_HEAD:
		snprintf(buf, size,
			 "CONNECT REBOOT ROTATE MAX_PROCS GEOMETRY START BLOCK_ID");
		break;
	case SELECT_PRINT_DATA:
		if (jobinfo->max_procs == NO_VAL)
			sprintf(max_procs_char, "None");
		else
			convert_num_unit((float)jobinfo->max_procs, 
					 max_procs_char, sizeof(max_procs_char),
					 UNIT_NONE);
		if (jobinfo->start[0] == (uint16_t) NO_VAL)
			sprintf(start_char, "None");
		else {
			snprintf(start_char, sizeof(start_char), 
				"%cx%cx%c",
				 alpha_num[jobinfo->start[0]],
				 alpha_num[jobinfo->start[1]],
				 alpha_num[jobinfo->start[2]]);
		} 
		snprintf(buf, size, 
			 "%7.7s %6.6s %6.6s %9s    %cx%cx%c %5s %-16s",
			 _job_conn_type_string(jobinfo->conn_type),
			 _yes_no_string(jobinfo->reboot),
			 _yes_no_string(jobinfo->rotate),
			 max_procs_char,
			 alpha_num[geometry[0]],
			 alpha_num[geometry[1]],
			 alpha_num[geometry[2]],
			 start_char, jobinfo->bg_block_id);
		break;
	case SELECT_PRINT_MIXED:
		if (jobinfo->max_procs == NO_VAL)
			sprintf(max_procs_char, "None");
		else
			convert_num_unit((float)jobinfo->max_procs,
					 max_procs_char, sizeof(max_procs_char),
					 UNIT_NONE);
		if (jobinfo->start[0] == (uint16_t) NO_VAL)
			sprintf(start_char, "None");
		else {
			snprintf(start_char, sizeof(start_char),
				"%cx%cx%c",
				 alpha_num[jobinfo->start[0]],
				 alpha_num[jobinfo->start[1]],
				 alpha_num[jobinfo->start[2]]);
		}
		
		snprintf(buf, size, 
			 "Connection=%s Reboot=%s Rotate=%s MaxProcs=%s "
			 "Geometry=%cx%cx%c Start=%s Block_ID=%s",
			 _job_conn_type_string(jobinfo->conn_type),
			 _yes_no_string(jobinfo->reboot),
			 _yes_no_string(jobinfo->rotate),
			 max_procs_char,
			 alpha_num[geometry[0]],
			 alpha_num[geometry[1]],
			 alpha_num[geometry[2]],
			 start_char, jobinfo->bg_block_id);
		break;
	case SELECT_PRINT_BG_ID:
		snprintf(buf, size, "%s", jobinfo->bg_block_id);
		break;
	case SELECT_PRINT_NODES:
		if(jobinfo->ionodes && jobinfo->ionodes[0]) 
			snprintf(buf, size, "%s[%s]",
				 jobinfo->nodes, jobinfo->ionodes);
		else
			snprintf(buf, size, "%s", jobinfo->nodes);
		break;
	case SELECT_PRINT_CONNECTION:
		snprintf(buf, size, "%s", 
			 _job_conn_type_string(jobinfo->conn_type));
		break;
	case SELECT_PRINT_REBOOT:
		snprintf(buf, size, "%s",
			 _yes_no_string(jobinfo->reboot));
		break;
	case SELECT_PRINT_ROTATE:
		snprintf(buf, size, "%s",
			 _yes_no_string(jobinfo->rotate));
		break;
	case SELECT_PRINT_GEOMETRY:
		snprintf(buf, size, "%cx%cx%c",
			 alpha_num[geometry[0]],
			 alpha_num[geometry[1]],
			 alpha_num[geometry[2]]);
		break;
	case SELECT_PRINT_START:
		if (jobinfo->start[0] == (uint16_t) NO_VAL)
			sprintf(buf, "None");
		else {
			snprintf(buf, size, 
				 "%cx%cx%c",
				 alpha_num[jobinfo->start[0]],
				 alpha_num[jobinfo->start[1]],
				 alpha_num[jobinfo->start[2]]);
		} 
	case SELECT_PRINT_MAX_PROCS:
		if (jobinfo->max_procs == NO_VAL)
			sprintf(max_procs_char, "None");
		else
			convert_num_unit((float)jobinfo->max_procs,
					 max_procs_char, sizeof(max_procs_char),
					 UNIT_NONE);
		
		snprintf(buf, size, "%s", max_procs_char);
		break;
	case SELECT_PRINT_BLRTS_IMAGE:
		if(jobinfo->blrtsimage)
			tmp_image = jobinfo->blrtsimage;
		snprintf(buf, size, "%s", tmp_image);		
		break;
	case SELECT_PRINT_LINUX_IMAGE:
		if(jobinfo->linuximage)
			tmp_image = jobinfo->linuximage;
		snprintf(buf, size, "%s", tmp_image);		
		break;
	case SELECT_PRINT_MLOADER_IMAGE:
		if(jobinfo->mloaderimage)
			tmp_image = jobinfo->mloaderimage;
		snprintf(buf, size, "%s", tmp_image);		
		break;
	case SELECT_PRINT_RAMDISK_IMAGE:
		if(jobinfo->ramdiskimage)
			tmp_image = jobinfo->ramdiskimage;
		snprintf(buf, size, "%s", tmp_image);		
		break;		
	default:
		error("select_g_sprint_jobinfo: bad mode %d", mode);
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
extern char *select_g_xstrdup_jobinfo(select_jobinfo_t jobinfo, int mode)
{
	uint16_t geometry[SYSTEM_DIMENSIONS];
	int i;
	char max_procs_char[8], start_char[32];
	char *tmp_image = "default";
	char *buf = NULL;
		
	if ((mode != SELECT_PRINT_DATA)
	    && jobinfo && (jobinfo->magic != JOBINFO_MAGIC)) {
		error("select_g_xstrdup_jobinfo: jobinfo magic bad");
		return NULL;
	}

	if (jobinfo == NULL) {
		if (mode != SELECT_PRINT_HEAD) {
			error("select_g_xstrdup_jobinfo: jobinfo bad");
			return NULL;
		}
	} else if (jobinfo->geometry[0] == (uint16_t) NO_VAL) {
		for (i=0; i<SYSTEM_DIMENSIONS; i++)
			geometry[i] = 0;
	} else {
		for (i=0; i<SYSTEM_DIMENSIONS; i++)
			geometry[i] = jobinfo->geometry[i];
	}

	switch (mode) {
	case SELECT_PRINT_HEAD:
		xstrcat(buf, 
			"CONNECT REBOOT ROTATE MAX_PROCS "
			"GEOMETRY START BLOCK_ID");
		break;
	case SELECT_PRINT_DATA:
		if (jobinfo->max_procs == NO_VAL)
			sprintf(max_procs_char, "None");
		else
			convert_num_unit((float)jobinfo->max_procs, 
					 max_procs_char, sizeof(max_procs_char),
					 UNIT_NONE);
		if (jobinfo->start[0] == (uint16_t) NO_VAL)
			sprintf(start_char, "None");
		else {
			snprintf(start_char, sizeof(start_char), 
				"%cx%cx%c",
				 alpha_num[jobinfo->start[0]],
				 alpha_num[jobinfo->start[1]],
				 alpha_num[jobinfo->start[2]]);
		} 
		xstrfmtcat(buf, 
			   "%7.7s %6.6s %6.6s %9s    %cx%cx%c %5s %-16s",
			   _job_conn_type_string(jobinfo->conn_type),
			   _yes_no_string(jobinfo->reboot),
			   _yes_no_string(jobinfo->rotate),
			   max_procs_char,
			   alpha_num[geometry[0]],
			   alpha_num[geometry[1]],
			   alpha_num[geometry[2]],
			   start_char, jobinfo->bg_block_id);
		break;
	case SELECT_PRINT_MIXED:
		if (jobinfo->max_procs == NO_VAL)
			sprintf(max_procs_char, "None");
		else
			convert_num_unit((float)jobinfo->max_procs,
					 max_procs_char, sizeof(max_procs_char),
					 UNIT_NONE);
		if (jobinfo->start[0] == (uint16_t) NO_VAL)
			sprintf(start_char, "None");
		else {
			snprintf(start_char, sizeof(start_char),
				"%cx%cx%c",
				 alpha_num[jobinfo->start[0]],
				 alpha_num[jobinfo->start[1]],
				 alpha_num[jobinfo->start[2]]);
		}
		
		xstrfmtcat(buf, 
			 "Connection=%s Reboot=%s Rotate=%s MaxProcs=%s "
			 "Geometry=%cx%cx%c Start=%s Block_ID=%s",
			 _job_conn_type_string(jobinfo->conn_type),
			 _yes_no_string(jobinfo->reboot),
			 _yes_no_string(jobinfo->rotate),
			 max_procs_char,
			 alpha_num[geometry[0]],
			 alpha_num[geometry[1]],
			 alpha_num[geometry[2]],
			 start_char, jobinfo->bg_block_id);
		break;
	case SELECT_PRINT_BG_ID:
		xstrfmtcat(buf, "%s", jobinfo->bg_block_id);
		break;
	case SELECT_PRINT_NODES:
		if(jobinfo->ionodes && jobinfo->ionodes[0]) 
			xstrfmtcat(buf, "%s[%s]",
				 jobinfo->nodes, jobinfo->ionodes);
		else
			xstrfmtcat(buf, "%s", jobinfo->nodes);
		break;
	case SELECT_PRINT_CONNECTION:
		xstrfmtcat(buf, "%s", 
			 _job_conn_type_string(jobinfo->conn_type));
		break;
	case SELECT_PRINT_REBOOT:
		xstrfmtcat(buf, "%s",
			 _yes_no_string(jobinfo->reboot));
		break;
	case SELECT_PRINT_ROTATE:
		xstrfmtcat(buf, "%s",
			 _yes_no_string(jobinfo->rotate));
		break;
	case SELECT_PRINT_GEOMETRY:
		xstrfmtcat(buf, "%cx%cx%c",
			 alpha_num[geometry[0]],
			 alpha_num[geometry[1]],
			 alpha_num[geometry[2]]);
		break;
	case SELECT_PRINT_START:
		if (jobinfo->start[0] == (uint16_t) NO_VAL)
			sprintf(buf, "None");
		else {
			xstrfmtcat(buf, 
				 "%cx%cx%c",
				 alpha_num[jobinfo->start[0]],
				 alpha_num[jobinfo->start[1]],
				 alpha_num[jobinfo->start[2]]);
		} 
	case SELECT_PRINT_MAX_PROCS:
		if (jobinfo->max_procs == NO_VAL)
			sprintf(max_procs_char, "None");
		else
			convert_num_unit((float)jobinfo->max_procs,
					 max_procs_char, sizeof(max_procs_char),
					 UNIT_NONE);
		
		xstrfmtcat(buf, "%s", max_procs_char);
		break;
	case SELECT_PRINT_BLRTS_IMAGE:
		if(jobinfo->blrtsimage)
			tmp_image = jobinfo->blrtsimage;
		xstrfmtcat(buf, "%s", tmp_image);		
		break;
	case SELECT_PRINT_LINUX_IMAGE:
		if(jobinfo->linuximage)
			tmp_image = jobinfo->linuximage;
		xstrfmtcat(buf, "%s", tmp_image);		
		break;
	case SELECT_PRINT_MLOADER_IMAGE:
		if(jobinfo->mloaderimage)
			tmp_image = jobinfo->mloaderimage;
		xstrfmtcat(buf, "%s", tmp_image);		
		break;
	case SELECT_PRINT_RAMDISK_IMAGE:
		if(jobinfo->ramdiskimage)
			tmp_image = jobinfo->ramdiskimage;
		xstrfmtcat(buf, "%s", tmp_image);		
		break;		
	default:
		error("select_g_xstrdup_jobinfo: bad mode %d", mode);
	}
	
	return buf;
}

/* Unpack node select info from a buffer */
extern int select_g_unpack_node_info(node_select_info_msg_t **
		node_select_info_msg_pptr, Buf buffer)
{
	int i, record_count = 0;
	node_select_info_msg_t *buf;

	buf = xmalloc(sizeof(bg_info_record_t));
	safe_unpack32(&(buf->record_count), buffer);
	safe_unpack_time(&(buf->last_update), buffer);
	buf->bg_info_array = xmalloc(sizeof(bg_info_record_t) * 
		buf->record_count);
	record_count = buf->record_count;
	for(i=0; i<record_count; i++) {
		if (_unpack_node_info(&(buf->bg_info_array[i]), buffer)) 
			goto unpack_error;
	}
	*node_select_info_msg_pptr = buf;
	return SLURM_SUCCESS;

unpack_error:
	for(i=0; i<record_count; i++)
		_free_node_info(&(buf->bg_info_array[i]));
	xfree(buf->bg_info_array);
	xfree(buf);
	return SLURM_ERROR;
}

/* Free a node select information buffer */
extern int select_g_free_node_info(node_select_info_msg_t **
		node_select_info_msg_pptr)
{
	int i;
	node_select_info_msg_t *buf;

	if (node_select_info_msg_pptr == NULL)
		return EINVAL;
	buf = *node_select_info_msg_pptr;

	if (buf->bg_info_array == NULL)
		buf->record_count = 0;
	for(i=0; i<buf->record_count; i++)
		_free_node_info(&(buf->bg_info_array[i]));
	xfree(buf->bg_info_array);
	xfree(buf);
	return SLURM_SUCCESS;
}

#else	/* !HAVE_BG */

/* allocate storage for a select job credential
 * OUT jobinfo - storage for a select job credential
 * RET         - slurm error code
 * NOTE: storage must be freed using select_g_free_jobinfo
 */
extern int select_g_alloc_jobinfo (select_jobinfo_t *jobinfo)
{
	return SLURM_SUCCESS;
}

/* fill in a previously allocated select job credential
 * IN/OUT jobinfo  - updated select job credential
 * IN data_type - type of data to enter into job credential
 * IN data - the data to enter into job credential
 */
extern int select_g_set_jobinfo (select_jobinfo_t jobinfo,
		enum select_data_type data_type, void *data)
{
	return SLURM_SUCCESS;
}

/* get data from a select job credential
 * IN jobinfo  - updated select job credential
 * IN data_type - type of data to enter into job credential
 * IN/OUT data - the data to enter into job credential
 */
extern int select_g_get_jobinfo (select_jobinfo_t jobinfo,
		enum select_data_type data_type, void *data)
{
	return SLURM_ERROR;
}

/* copy a select job credential
 * IN jobinfo - the select job credential to be copied
 * RET        - the copy or NULL on failure
 * NOTE: returned value must be freed using select_g_free_jobinfo
 */
extern select_jobinfo_t select_g_copy_jobinfo(select_jobinfo_t jobinfo)
{
	return NULL;
}

/* free storage previously allocated for a select job credential
 * IN jobinfo  - the select job credential to be freed
 */
extern int select_g_free_jobinfo  (select_jobinfo_t *jobinfo)
{
	return SLURM_SUCCESS;
}

/* pack a select job credential into a buffer in machine independent form
 * IN jobinfo  - the select job credential to be saved
 * OUT buffer  - buffer with select credential appended
 * RET         - slurm error code
 */
extern int  select_g_pack_jobinfo  (select_jobinfo_t jobinfo, Buf buffer)
{
	return SLURM_SUCCESS;
}

/* unpack a select job credential from a buffer
 * OUT jobinfo - the select job credential read
 * IN  buffer  - buffer with select credential read from current pointer loc
 * RET         - slurm error code
 * NOTE: returned value must be freed using select_g_free_jobinfo
 */
extern int  select_g_unpack_jobinfo(select_jobinfo_t jobinfo, Buf buffer)
{
	return SLURM_SUCCESS;
}

/* write select job credential to a string
 * IN jobinfo - a select job credential
 * OUT buf    - location to write job credential contents
 * IN size    - byte size of buf
 * IN mode    - print mode, see enum select_print_mode
 * RET        - the string, same as buf
 */
extern char *select_g_sprint_jobinfo(select_jobinfo_t jobinfo,
		char *buf, size_t size, int mode)
{
	if (buf && size) {
		buf[0] = '\0';
		return buf;
	} else
		return NULL;
}
/* write select job info to a string
 * IN jobinfo - a select job credential
 * IN mode    - print mode, see enum select_print_mode
 * RET        - char * containing string of request
 */
extern char *select_g_xstrdup_jobinfo(select_jobinfo_t jobinfo, int mode)
{
	return NULL;
}

extern int select_g_unpack_node_info(node_select_info_msg_t **
		node_select_info_msg_pptr, Buf buffer)
{
	return SLURM_ERROR;
}

extern int select_g_free_node_info(node_select_info_msg_t **
		node_select_info_msg_pptr)
{
	return SLURM_ERROR;
}

#endif
