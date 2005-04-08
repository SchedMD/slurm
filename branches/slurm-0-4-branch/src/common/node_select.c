/*****************************************************************************\
 *  node_select.c - node selection plugin wrapper.
 *
 *  NOTE: The node selection plugin itself is intimately tied to 
 *  slurmctld functions and data structures. Some related 
 *  functions (e.g. data structure un/packing, environment 
 *  variable setting) are required by most SLURM commands. 
 *  Rather than creating a new plugin with these commonly 
 *  used functions, they are included within this module.
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
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
	int		(*state_save)		( char *dir_name );
	int	       	(*state_restore)	( char *dir_name );
	int		(*job_init)		( List job_list );
	int 		(*node_init)		( struct node_record *node_ptr,
						  int node_cnt);
	int 		(*part_init)		( List part_list );
	int		(*job_test)		( struct job_record *job_ptr,
						  bitstr_t *bitmap, int min_nodes, 
						  int max_nodes );
	int		(*job_begin)		( struct job_record *job_ptr );
	int		(*job_ready)		( struct job_record *job_ptr );
	int		(*job_fini)		( struct job_record *job_ptr );
	int		(*pack_node_info)	( time_t last_query_time,
						Buf *buffer_ptr);
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

#ifdef HAVE_BGL		/* node selection specific logic */
#  define JOBINFO_MAGIC 0x83ac
    struct select_jobinfo {
	uint16_t geometry[SYSTEM_DIMENSIONS];	/* node count in various
				 * dimensions, e.g. X, Y, and Z */
	uint16_t conn_type;	/* see enum connection_type */
	uint16_t rotate;	/* permit geometry rotation if set */
	uint16_t node_use;	/* see enum node_use_type */
	char *bgl_part_id;	/* Blue Gene partition ID */
	uint16_t magic;		/* magic number */
    };
#endif

/*
 * Local functions
 */
static slurm_select_context_t *	_select_context_create(const char *select_type);
static int 			_select_context_destroy(slurm_select_context_t *c);
static slurm_select_ops_t *	_select_get_ops(slurm_select_context_t *c);

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
		"select_p_part_init",
		"select_p_job_test",
		"select_p_job_begin",
		"select_p_job_ready",
		"select_p_job_fini",
		"select_p_pack_node_info"
	};
	int n_syms = sizeof( syms ) / sizeof( char * );

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
 * Note re/initialization of partition record data structure
 * IN part_list - list of partition records
 */
extern int select_g_part_init(List part_list)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(g_select_context->ops.part_init))(part_list);
}

/*
 * Select the "best" nodes for given job from those available
 * IN job_ptr - pointer to job being considered for initiation
 * IN/OUT bitmap - map of nodes being considered for allocation on input,
 *                 map of nodes actually to be assigned on output
 * IN min_nodes - minimum number of nodes to allocate to job
 * IN max_nodes - maximum number of nodes to allocate to job 
 */
extern int select_g_job_test(struct job_record *job_ptr, bitstr_t *bitmap,
        int min_nodes, int max_nodes)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(g_select_context->ops.job_test))(job_ptr, bitmap, 
		min_nodes, max_nodes);
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
 * RET -1 on error, 1 if ready to execute, 0 otherwise
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

extern int select_g_pack_node_info(time_t last_query_time, Buf *buffer)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(g_select_context->ops.pack_node_info))
		(last_query_time, buffer);
}

#ifdef HAVE_BGL		/* node selection specific logic */
static char *_job_conn_type_string(uint16_t inx)
{
	if (inx == SELECT_TORUS)
		return "torus";
	else if (inx == SELECT_MESH)
		return "mesh";
	else
		return "nav";
}

static char *_job_node_use_string(uint16_t inx)
{
	if (inx == SELECT_COPROCESSOR_MODE)
		return "coprocessor";
	else if (inx == SELECT_VIRTUAL_NODE_MODE)
		return "virtual";
	else
		return "nav";
}


static char *_job_rotate_string(uint16_t inx)
{
	if (inx)
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
	xassert(jobinfo != NULL);

	*jobinfo = xmalloc(sizeof(struct select_jobinfo));
	(*jobinfo)->magic = JOBINFO_MAGIC;
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
	uint16_t *tmp_16 = (uint16_t *) data;
	char * tmp_char = (char *) data;

	if (jobinfo->magic != JOBINFO_MAGIC) {
		error("select_g_set_jobinfo: jobinfo magic bad");
		return SLURM_ERROR;
	}

	switch (data_type) {
		case SELECT_DATA_GEOMETRY:
			for (i=0; i<SYSTEM_DIMENSIONS; i++)
				jobinfo->geometry[i] = tmp_16[i];
			break;
		case SELECT_DATA_ROTATE:
			jobinfo->rotate = *tmp_16;
			break;
		case SELECT_DATA_NODE_USE:
			jobinfo->node_use = *tmp_16;
			break;
		case SELECT_DATA_CONN_TYPE:
			jobinfo->conn_type = *tmp_16;
			break;
		case SELECT_DATA_PART_ID:
			/* we xfree() any preset value to avoid a memory leak */
			xfree(jobinfo->bgl_part_id);
			jobinfo->bgl_part_id = xstrdup(tmp_char);
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
 * IN/OUT data - the data to enter into job credential
 */
extern int select_g_get_jobinfo (select_jobinfo_t jobinfo,
		enum select_data_type data_type, void *data)
{
	int i, rc = SLURM_SUCCESS;
	uint16_t *tmp_16 = (uint16_t *) data;
	char **tmp_char = (char **) data;

	if (jobinfo->magic != JOBINFO_MAGIC) {
		error("select_g_get_jobinfo: jobinfo magic bad");
		return SLURM_ERROR;
	}

	switch (data_type) {
		case SELECT_DATA_GEOMETRY:
			for (i=0; i<SYSTEM_DIMENSIONS; i++)
				tmp_16[i] = jobinfo->geometry[i];
			break;
		case SELECT_DATA_ROTATE:
			*tmp_16 = jobinfo->rotate;
			break;
		case SELECT_DATA_NODE_USE:
			*tmp_16 = jobinfo->node_use;
			break;
		case SELECT_DATA_CONN_TYPE:
			*tmp_16 = jobinfo->conn_type;
			break;
		case SELECT_DATA_PART_ID:
			if ((jobinfo->bgl_part_id == NULL)
			||  (jobinfo->bgl_part_id[0] == '\0'))
				*tmp_char = NULL;
			else
				*tmp_char = xstrdup(jobinfo->bgl_part_id);
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

	if (jobinfo == NULL)
		;
	else if (jobinfo->magic != JOBINFO_MAGIC)
		error("select_g_copy_jobinfo: jobinfo magic bad");
	else {
		int i;
		rc = xmalloc(sizeof(struct select_jobinfo));
		rc->magic = JOBINFO_MAGIC;
		for (i=0; i<SYSTEM_DIMENSIONS; i++)
			rc->geometry[i] = jobinfo->geometry[i];
		rc->rotate = jobinfo->rotate;
		rc->node_use = jobinfo->node_use;
		rc->conn_type = jobinfo->conn_type;
		rc->rotate = jobinfo->rotate;
		rc->bgl_part_id = xstrdup(jobinfo->bgl_part_id);
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
		xfree((*jobinfo)->bgl_part_id);
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
		for (i=0; i<SYSTEM_DIMENSIONS; i++)
			pack16(jobinfo->geometry[i], buffer);		
		pack16(jobinfo->conn_type, buffer);
		pack16(jobinfo->rotate, buffer);
		pack16(jobinfo->node_use, buffer);
		packstr(jobinfo->bgl_part_id, buffer);
	} else {
		for (i=0; i<(SYSTEM_DIMENSIONS+3); i++)
			pack16((uint16_t) 0, buffer);
		packstr(NULL, buffer);
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
	uint16_t uint16_tmp;

	for (i=0; i<SYSTEM_DIMENSIONS; i++)
		safe_unpack16(&(jobinfo->geometry[i]), buffer);
	safe_unpack16(&(jobinfo->conn_type), buffer);
	safe_unpack16(&(jobinfo->rotate), buffer);
	safe_unpack16(&(jobinfo->node_use), buffer);
	safe_unpackstr_xmalloc(&(jobinfo->bgl_part_id), &uint16_tmp, buffer);
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
			 "CONNECT ROTATE NODE_USE GEOMETRY PART_ID");
		break;
	case SELECT_PRINT_DATA:
		snprintf(buf, size, 
			 "%7.7s %6.6s %8.8s    %ux%ux%u %7s",
			 _job_conn_type_string(jobinfo->conn_type),
			 _job_rotate_string(jobinfo->rotate),
			 _job_node_use_string(jobinfo->node_use),
			 geometry[0], geometry[1], geometry[2],
			 jobinfo->bgl_part_id);
		break;
	case SELECT_PRINT_MIXED:
		snprintf(buf, size, 
			 "Connection=%s Rotate=%s NodeUse=%s "
			 "Geometry=%ux%ux%u Part_ID=%s",
			 _job_conn_type_string(jobinfo->conn_type),
			 _job_rotate_string(jobinfo->rotate),
			 _job_node_use_string(jobinfo->node_use),
			 geometry[0], geometry[1], geometry[2],
			 jobinfo->bgl_part_id);
		break;
	case SELECT_PRINT_BGL_ID:
		return jobinfo->bgl_part_id;
		break;
	default:
		error("select_g_sprint_jobinfo: bad mode %d", mode);
		if (size > 0)
			buf[0] = '\0';
	}
	
	return buf;
}

/* NOTE: The matching pack functions are directly in the select/bluegene 
 * plugin. The unpack functions can not be there since the plugin is 
 * dependent upon libraries which do not exist on the BlueGene front-end 
 * nodes. */
static int _unpack_node_info(bgl_info_record_t *bgl_info_record, Buf buffer)
{
	uint16_t uint16_tmp;
	safe_unpackstr_xmalloc(&(bgl_info_record->nodes), &uint16_tmp, buffer);
	safe_unpackstr_xmalloc(&bgl_info_record->owner_name, &uint16_tmp, 
		buffer);
	safe_unpackstr_xmalloc(&bgl_info_record->bgl_part_id, &uint16_tmp, 
		buffer);

	safe_unpack16(&uint16_tmp, buffer);
	bgl_info_record->state     = (int) uint16_tmp;
	safe_unpack16(&uint16_tmp, buffer);
	bgl_info_record->conn_type = (int) uint16_tmp;
	safe_unpack16(&uint16_tmp, buffer);
	bgl_info_record->node_use = (int) uint16_tmp;

	return SLURM_SUCCESS;

unpack_error:
	if(bgl_info_record->nodes)
		xfree(bgl_info_record->nodes);
	if(bgl_info_record->owner_name)
		xfree(bgl_info_record->owner_name);
	if(bgl_info_record->bgl_part_id)
		xfree(bgl_info_record->bgl_part_id);
	return SLURM_ERROR;
}

static void _free_node_info(bgl_info_record_t *bgl_info_record)
{
	if(bgl_info_record->nodes)
		xfree(bgl_info_record->nodes);
	if(bgl_info_record->owner_name)
		xfree(bgl_info_record->owner_name);
	if(bgl_info_record->bgl_part_id)
		xfree(bgl_info_record->bgl_part_id);
}

/* Unpack node select info from a buffer */
extern int select_g_unpack_node_info(node_select_info_msg_t **
		node_select_info_msg_pptr, Buf buffer)
{
	int i, record_count = 0;
	node_select_info_msg_t *buf;

	buf = xmalloc(sizeof(bgl_info_record_t));
	safe_unpack32(&(buf->record_count), buffer);
	safe_unpack_time(&(buf->last_update), buffer);
	buf->bgl_info_array = xmalloc(sizeof(bgl_info_record_t) * 
		buf->record_count);
	record_count = buf->record_count;

	for(i=0; i<record_count; i++) {
		if (_unpack_node_info(&(buf->bgl_info_array[i]), buffer))
			goto unpack_error;
	}
	*node_select_info_msg_pptr = buf;
	return SLURM_SUCCESS;

unpack_error:
	for(i=0; i<record_count; i++)
		_free_node_info(&(buf->bgl_info_array[i]));
	xfree(buf->bgl_info_array);
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

	if (buf->bgl_info_array == NULL)
		buf->record_count = 0;
	for(i=0; i<buf->record_count; i++)
		_free_node_info(&(buf->bgl_info_array[i]));
	xfree(buf);
	return SLURM_SUCCESS;
}

#else	/* !HAVE_BGL */

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
