/*****************************************************************************\
 *  sched_upcalls.c - functions available to scheduler plugins
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jay Windley <jwindley@lnxi.com>.
 *  UCRL-CODE-217948.
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

#include <string.h>
#include <signal.h>
#include <slurm/slurm.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>

#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"

#include "src/slurmctld/locks.h"
#include "src/slurmctld/sched_plugin.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/state_save.h"

/*
 * Implementation of the opaque list of nodes, jobs, etc., passed down
 * to the scheduler plugin.
 *
 * XXX - Make the accessor name-function mapping part of the object
 * instead of global.
 */



/* ***************************************************************************/
/*  TAG(                          sched_obj_list                         )   */
/*
 * "Base" object for the controller lists passed to plugins.  Contains a
 * destructor appropriate to the data, a count of items in the list, and
 * a cache of intermediate values computed by accessors that they might
 * want back on subsequent invocations.
 */
struct sched_obj_list
{
	int (*destructor)( sched_obj_list_t );
	int32_t count;
	void *data;
	List cache;
};

/* ***************************************************************************/
/*  TAG(                      sched_obj_cache_entry                      )   */
/*
 * Maps a field name to a cached chunk of data in the list.  If an accessor
 * needs to convert the internal SLURM value into one more appropriate for
 * a plugin or anything like that, it can cache the computed value in the
 * obj_list under its field name to satisfy future calls.  The data cached
 * is opaque but must be freeable via xfree().  When the object list is
 * destroyed, the cached data is automatically freed.
 *
 * This type maps field name to data.  The cache in the sched_obj_list
 * is a List of these.
 */
struct sched_obj_cache_entry
{
	int32_t idx;
	char *field;
	void *data;
};

/*
 * These constants give accessors something to point to for customary
 * default or missing values.
 */
static int32_t zero_32 = 0;
static time_t unspecified_time = (time_t) NO_VAL;

static struct job_details *copy_job_details( struct job_details *from );
static void free_job_details( struct job_details *doomed );

/*
 * Forward declarations of accessor functions.  These functions, when
 * passed a reference to an opaque data structure and an index return
 * the appropriate attribute of that object.
 */
static void * sched_get_job_id(		sched_obj_list_t, int32_t, char * );
static void * sched_get_job_name(	sched_obj_list_t, int32_t, char * );
static void * sched_get_job_last_active( sched_obj_list_t, int32_t, char * );
static void * sched_get_job_state(	sched_obj_list_t, int32_t, char * );
static void * sched_get_job_time_limit(	sched_obj_list_t, int32_t, char * );
static void * sched_get_job_num_tasks(	sched_obj_list_t, int32_t, char * );
static void * sched_get_job_submit_time( sched_obj_list_t, int32_t, char * );
static void * sched_get_job_start_time(	sched_obj_list_t, int32_t, char * );
static void * sched_get_job_end_time(	sched_obj_list_t, int32_t, char * );
static void * sched_get_job_user_id(	sched_obj_list_t, int32_t, char * );
static void * sched_get_job_group_name(	sched_obj_list_t, int32_t, char * );
static void * sched_get_job_req_nodes(	sched_obj_list_t, int32_t, char * );
static void * sched_get_job_alloc_nodes( sched_obj_list_t, int32_t, char * );
static void * sched_get_job_min_nodes(	sched_obj_list_t, int32_t, char * );
static void * sched_get_job_partition(	sched_obj_list_t, int32_t, char * );
static void * sched_get_job_min_disk( sched_obj_list_t, int32_t, char * );
static void * sched_get_job_min_memory( sched_obj_list_t, int32_t, char * );
#if 0
static void * sched_get_job_features(	sched_obj_list_t, int32_t, char * );
static void * sched_get_job_priority(	sched_obj_list_t, int32_t, char * );
static void * sched_get_job_work_dir(	sched_obj_list_t, int32_t, char * );
#endif

static void * sched_get_node_name(	sched_obj_list_t, int32_t, char * );
static void * sched_get_node_state(	sched_obj_list_t, int32_t, char * );
static void * sched_get_node_num_cpus( 	sched_obj_list_t, int32_t, char * );
static void * sched_get_node_real_mem( 	sched_obj_list_t, int32_t, char * );
static void * sched_get_node_tmp_disk( 	sched_obj_list_t, int32_t, char * );
static void * sched_get_node_partition(	sched_obj_list_t, int32_t, char * );
static void * sched_get_node_mod_time( 	sched_obj_list_t, int32_t, char * );



/* ************************************************************************ */
/*  TAG(                         sched_get_port                          )  */
/* ************************************************************************ */
const u_int16_t
sched_get_port( void )
{
	u_int16_t port;
	/* Locks: Read config */
	slurmctld_lock_t config_read_lock = { 
		READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	lock_slurmctld(config_read_lock);
	port = slurmctld_conf.schedport;
	unlock_slurmctld(config_read_lock);

	return port;
}

/* ************************************************************************ */
/*  TAG(                         sched_get_auth                          )  */
/* ************************************************************************ */
const char * const
sched_get_auth( void )
{
	static char auth[128];
	/* Locks: Read config */
	slurmctld_lock_t config_read_lock = { 
		READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	lock_slurmctld(config_read_lock);
	strncpy(auth, slurmctld_conf.schedauth, 128);
	if (auth[127] != '\0') {
		auth[127] = '\0';
		error("slurmctld_conf.schedauth truncated");
	}
	unlock_slurmctld(config_read_lock);

	return auth;
}

/* ************************************************************************ */
/*  TAG(                     sched_get_root_filter                       )  */
/* ************************************************************************ */
const u_int16_t
sched_get_root_filter( void )
{
	u_int16_t root_filter;
	/* Locks: Read config */
	slurmctld_lock_t config_read_lock = { 
		READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	lock_slurmctld(config_read_lock);
	root_filter = slurmctld_conf.schedrootfltr;
	unlock_slurmctld(config_read_lock);

	return root_filter;
}

/* ************************************************************************ */
/*  TAG(                     sched_get_obj_count                         )  */
/* ************************************************************************ */
int32_t sched_get_obj_count( sched_obj_list_t list )
{
	xassert( list );
	return( list->count );
}
	
/* ************************************************************************ */
/*  TAG(                     sched_get_accessor                          )  */
/* ************************************************************************ */
sched_accessor_fn_t
sched_get_accessor( char *field )
{
	struct accessor_map_struct {
		char *field_name;
		sched_accessor_fn_t func;
	};
	static struct accessor_map_struct accessor_map[] = {
		{ JOB_FIELD_ID,			sched_get_job_id },
		{ JOB_FIELD_NAME,		sched_get_job_name },
		{ JOB_FIELD_LAST_ACTIVE,       	sched_get_job_last_active },
		{ JOB_FIELD_STATE,		sched_get_job_state },
		{ JOB_FIELD_TIME_LIMIT,		sched_get_job_time_limit },
		{ JOB_FIELD_NUM_TASKS,		sched_get_job_num_tasks },
		{ JOB_FIELD_SUBMIT_TIME,	sched_get_job_submit_time },
		{ JOB_FIELD_START_TIME,		sched_get_job_start_time },
		{ JOB_FIELD_END_TIME,		sched_get_job_end_time },
		{ JOB_FIELD_USER_ID,		sched_get_job_user_id },
		{ JOB_FIELD_GROUP_ID,		sched_get_job_group_name },
		/*  { JOB_FIELD_ALLOC_NODES,	sched_get_job_alloc_nodes },
		 * Wiki specifies the nodes to be allocated in the requested node
		 * field, so that is where we are getting the allocated node 
		 * information from for now.
		 */
		{ JOB_FIELD_ALLOC_NODES,	sched_get_job_req_nodes },
		{ JOB_FIELD_REQ_NODES,		sched_get_job_req_nodes },
		{ JOB_FIELD_MIN_NODES,		sched_get_job_min_nodes },
		{ JOB_FIELD_PARTITION,		sched_get_job_partition },
		{ JOB_FIELD_MIN_DISK,		sched_get_job_min_disk },
		{ JOB_FIELD_MIN_MEMORY,		sched_get_job_min_memory },
#if 0		
		{ JOB_FIELD_FEATURES,		sched_get_job_features },
		{ JOB_FIELD_PRIORITY,		sched_get_job_priority },
		{ JOB_FIELD_WORK_DIR,		sched_get_job_work_dir },
#endif
		{ NODE_FIELD_NAME,     		sched_get_node_name },
		{ NODE_FIELD_STATE,    		sched_get_node_state },
		{ NODE_FIELD_NUM_CPUS,		sched_get_node_num_cpus },
		{ NODE_FIELD_REAL_MEM,		sched_get_node_real_mem },
		{ NODE_FIELD_TMP_DISK,		sched_get_node_tmp_disk },
		{ NODE_FIELD_PARTITION,		sched_get_node_partition },
		{ NODE_FIELD_MOD_TIME,		sched_get_node_mod_time },
		
		{ NULL, NULL }
	};
	struct accessor_map_struct *p;

	for ( p = accessor_map; p->field_name != NULL; ++p ) {
		if ( strcmp( p->field_name, field ) == 0 )
			return p->func;
	}

	return NULL;
}

/* ************************************************************************ */
/*  TAG(                       sched_free_obj_list                       )  */
/* ************************************************************************ */
int
sched_free_obj_list( sched_obj_list_t objlist )
{
	if ( objlist->count > 0 ) {
		(*objlist->destructor)( objlist );
	}
	list_destroy( objlist->cache );
	xfree( objlist );
	return SLURM_SUCCESS;
}

/* ************************************************************** */
/*  TAG(            sched_obj_cache_entry_destructor          )   */
/* ************************************************************** */
/* DESCRIPTION
 * Destructor for an item in the accessor's cache.  This is intended to
 * be called by list_destroy().
 * 
 * ARGUMENTS
 * ent - pointer to the sched_obj_cache_entry to be freed.
 *
 * RETURNS
 * None.
 * 
 **************************************************************** */
static void
sched_obj_cache_entry_destructor( void *ent )
{
	struct sched_obj_cache_entry *e =
		(struct sched_obj_cache_entry *) ent;
	xfree( e->data );
	xfree( e );
}


/* ************************************************************************ */
/*  TAG(                sched_obj_cache_entry_find                       )  */
/* ************************************************************************ */
void *
sched_obj_cache_entry_find( sched_obj_list_t objlist,
			    int32_t idx,
			    char *field )
{
	ListIterator i;
	struct sched_obj_cache_entry *e;

	i = list_iterator_create( objlist->cache );

	while ( ( e = (struct sched_obj_cache_entry *) list_next( i ) ) != NULL ) {
		if ( ( e->idx == idx ) && ( strcmp( e->field, field ) == 0 ) ) {
			list_iterator_destroy( i );
			return e->data;
		}
	}
	list_iterator_destroy( i );

	return NULL;
}


/* ************************************************************************ */
/*  TAG(                     sched_obj_cache_entry_add                   )  */
/* ************************************************************************ */
void
sched_obj_cache_entry_add( sched_obj_list_t objlist,
			   int32_t idx,
			   char *field,
			   void *data )
{
	struct sched_obj_cache_entry *e =
		(struct sched_obj_cache_entry *) xmalloc( sizeof( *e ) );
	e->idx = idx;
	e->field = field;
	e->data = data;
	list_push( objlist->cache, e );
}


/* ************************************************************************ */
/*  TAG(                       sched_free_job_list                       )  */
/* ************************************************************************ */
int
sched_free_job_list( sched_obj_list_t objlist )
{
	int32_t i;
	struct job_record *job;
	
	for ( i = 0, job = (struct job_record *) objlist->data;
	      i < objlist->count;
	      ++i, ++job ) {		
		if ( job->details ) {
			free_job_details( job->details );
		}
	}
	xfree( objlist->data );
	return SLURM_SUCCESS;
}


/* ************************************************************************ */
/*  TAG(                       sched_get_job_list                        )  */
/* ************************************************************************ */
sched_obj_list_t 
sched_get_job_list( void )
{
	ListIterator it;
	struct job_record *to, *from;
	int32_t i;
	sched_obj_list_t objlist;
	/* read lock on job info, no other locks needed */
	slurmctld_lock_t job_read_lock = { 
                NO_LOCK,
		READ_LOCK,
		NO_LOCK,
		NO_LOCK
	};

	/* Allocate the structure. */
	objlist = (sched_obj_list_t )
		xmalloc( sizeof( struct sched_obj_list ) );
	objlist->destructor = sched_free_job_list;
	objlist->cache = list_create( sched_obj_cache_entry_destructor );
	
	lock_slurmctld( job_read_lock );
	objlist->count = (int32_t) list_count( job_list );
	if ( ! objlist->count ) {
		unlock_slurmctld( job_read_lock );
		objlist->data = NULL;
		return objlist;
	}

	objlist->data = xmalloc( sizeof( struct job_record ) *
				 (objlist->count) );
	it = list_iterator_create( job_list );
	for ( i = 0, to = (struct job_record *) objlist->data;
	      i < objlist->count;
	      ++i, ++to ) {
		from = (struct job_record *) list_next( it );
		memcpy( to, from, sizeof( struct job_record ) );
		to->nodes		= NULL;
		to->details		= NULL;
		to->node_bitmap		= NULL;
		to->cpus_per_node	= NULL;
		to->cpu_count_reps	= NULL;
		to->alloc_node		= NULL;
		to->node_addr		= NULL;

		if ( from->details ) {
			to->details = copy_job_details( from->details );
		}
	}
	unlock_slurmctld( job_read_lock );
	list_iterator_destroy( it );
	
	return objlist;
}

/* ************************************************************** */
/*  TAG(                   copy_job_details                   )   */
/* ************************************************************** */
/* DESCRIPTION
 * Make a copy of a job_details structure.
 * 
 * ARGUMENTS
 * from (in) - the job_details structure to copy.
 *
 * RETURNS
 * A copy of the job_details structure.
 * 
 **************************************************************** */
static struct job_details *
copy_job_details( struct job_details *from )
{
	struct job_details *to;

	to = (struct job_details *) xmalloc( sizeof( struct job_details ) );
	memcpy( to, from, sizeof( struct job_details ) );

	/*
	 * The default is not to copy subordinate objects stored
	 * out a pointer.  This is to speed up creating the copy.
	 * If you write an accessor that needs data stored in
	 * these subordinate objects, write code below in this
	 * function -- not in your accessor -- to copy that data.
	 */
	to->req_nodes		= NULL;
	to->exc_nodes		= NULL;
	to->req_node_bitmap	= NULL;
	to->exc_node_bitmap	= NULL;
	to->features		= NULL;
	to->err			= NULL;
	to->in			= NULL;
	to->out			= NULL;
	to->work_dir		= NULL;

	/* Copy the subordinates we actually need. */
	if ( from->req_nodes ) to->req_nodes = xstrdup( from->req_nodes );

	return to;
}


/* ************************************************************** */
/*  TAG(                   free_job_details                   )   */
/* ************************************************************** */
/* DESCRIPTION
 * Free a job_details structure.
 * 
 * ARGUMENTS
 * doomed (in) - the job_details structure to destroy.
 *
 * RETURNS
 * None.
 * 
 **************************************************************** */
static void
free_job_details( struct job_details *doomed )
{
	if ( ! doomed ) return;

	/* Free subordinate objects here. */
	if ( doomed->req_nodes ) xfree( doomed->req_nodes );
	
	xfree( doomed );
}

/* ************************************************************************ */
/*  TAG(                       sched_get_job_id                          )  */
/* ************************************************************************ */
static void *
sched_get_job_id( sched_obj_list_t job_data,
		  int32_t idx,
		  char *type )
{
	void *cache;
	char str[ 16 ];
	
	/*
	 * This is the primary key for the job record which means that
	 * consolidated plugin code will want this as a string and not
	 * an integer.
	 */

	if ( type ) *type = 's';
	if ( ( cache = sched_obj_cache_entry_find( job_data,
						   idx,
						   "job_id" ) ) != NULL )
		return cache;
	snprintf( str, 16,
		 "%u",
		 ( (struct job_record *)job_data->data )[ idx ].job_id );
	cache = xstrdup( str );
	sched_obj_cache_entry_add( job_data, idx, "job_id", cache );
	return cache;
}

/* ************************************************************************ */
/*  TAG(                       sched_get_job_name                        )  */
/* ************************************************************************ */
static void *
sched_get_job_name( sched_obj_list_t job_data,
		    int32_t idx,
		    char *type )
{
	if ( type ) *type = 's';
	return (void *) ( (struct job_record *)job_data->data )[ idx ].name;
}

/* ************************************************************************ */
/*  TAG(                       sched_get_job_last_active                 )  */
/* ************************************************************************ */
static void *
sched_get_job_last_active( sched_obj_list_t job_data,
			   int32_t idx,
			   char *type )
{
	if ( type ) *type = 't';
	return (void *) &( (struct job_record *)job_data->data )[ idx ].time_last_active;
}

/* ************************************************************************ */
/*  TAG(                       sched_get_job_state                       )  */
/* ************************************************************************ */
static void *
sched_get_job_state( sched_obj_list_t job_data,
		     int32_t idx,
		     char *type )
{
	static struct job_state_rel {
		enum job_states id;
		char *label;
	} job_state_map[] = {
		{ JOB_PENDING,		JOB_STATE_LABEL_PENDING },
		{ JOB_RUNNING,		JOB_STATE_LABEL_RUNNING },
		{ JOB_SUSPENDED,	JOB_STATE_LABEL_SUSPENDED },
		{ JOB_COMPLETE,		JOB_STATE_LABEL_COMPLETE },
		{ JOB_FAILED,		JOB_STATE_LABEL_FAILED },
		{ JOB_TIMEOUT,		JOB_STATE_LABEL_TIMEOUT },
		{ JOB_NODE_FAIL,	JOB_STATE_LABEL_NODE_FAIL },
		{ JOB_END,		"" }
	}, *p;
	enum job_states cur_state = ( (struct job_record *)job_data->data )[ idx ].job_state;
	
	if ( type ) *type = 'e';

	for ( p = job_state_map; p->id != JOB_END; ++p ) {
		if ( p->id == cur_state ) {
			return (void *) p->label;
		}
	}
	error( "scheduler adapter: unmapped job state %d in job %u",
	       cur_state,
	       ( (struct job_record *)job_data->data )[ idx ].job_id );

	return (void *) "UNKNOWN";	       
}

/* ************************************************************************ */
/*  TAG(                       sched_get_job_time_limit                  )  */
/* ************************************************************************ */
static void *
sched_get_job_time_limit( sched_obj_list_t job_data,
			  int32_t idx,
			  char *type )
{
	time_t *cache;
	
	if ( type ) *type = 't';

	if ( ( cache = sched_obj_cache_entry_find( job_data, idx, "time_limit" ) ) != NULL )
		return cache;
	cache = xmalloc( sizeof( time_t ) );
	*cache = ( (struct job_record *)job_data->data )[ idx ].time_limit;
	
	switch ( *cache ) {
	case NO_VAL:
	case INFINITE:
		*cache = (time_t) 0;
		break;
	default:
		*cache *= 60;   // seconds, not mins.
		break;
	}
	sched_obj_cache_entry_add( job_data, idx, "time_limit", cache );
	return cache;
}

/* ************************************************************************ */
/*  TAG(                       sched_get_job_num_tasks                   )  */
/* ************************************************************************ */
static void *
sched_get_job_num_tasks( sched_obj_list_t job_data,
			 int32_t idx,
			 char *type )
{
	static uint16_t one = 1;
	struct job_details *det = ( (struct job_record *)job_data->data )[ idx ].details;
	if ( type ) *type = 'u';
	if ( det && det->req_tasks && 
	     ( det->req_tasks != (uint16_t) NO_VAL ) ) {
		return (void *) &det->req_tasks;
	} else {
		return (void *) &one;
	}
}

/* ************************************************************************ */
/*  TAG(                       sched_get_job_submit_time                 )  */
/* ************************************************************************ */
static void *
sched_get_job_submit_time( sched_obj_list_t job_data,
			   int32_t idx,
			   char *type )
{
	struct job_details *det = ( (struct job_record *)job_data->data )[ idx ].details;
	if ( type ) *type = 't';
	if ( det ) {
		return (void *) &det->submit_time;
	} else {
		return (void *) &zero_32;
	}
}

/* ************************************************************************ */
/*  TAG(                       sched_get_job_start_time                  )  */
/* ************************************************************************ */
static void *
sched_get_job_start_time( sched_obj_list_t job_data,
			  int32_t idx,
			  char *type )
{
	time_t start = ( (struct job_record *)job_data->data )[ idx ].start_time;
	if ( type ) *type = 't';

	if ( start != (time_t) 0 )
		return (void *) &( (struct job_record *)job_data->data )[ idx ].start_time;
	else
		return (void *) &unspecified_time;
}

/* ************************************************************************ */
/*  TAG(                       sched_get_job_end_time                    )  */
/* ************************************************************************ */
static void *
sched_get_job_end_time( sched_obj_list_t job_data,
			int32_t idx,
			char *type )
{
	if ( type ) *type = 't';
	return (void *) &( (struct job_record *)job_data->data )[ idx ].end_time;
}

/* ************************************************************************ */
/*  TAG(                       sched_get_job_user_id                     )  */
/* ************************************************************************ */
static void *
sched_get_job_user_id( sched_obj_list_t job_data,
		       int32_t idx,
		       char *type )
{
	// * This is probably not thread-safe.	
	if ( type ) *type = 's';

	return uid_to_string( (uid_t) ( (struct job_record *)job_data->data )[ idx ].user_id );
}

/* ************************************************************************ */
/*  TAG(                       sched_get_job_group_name                  )  */
/* ************************************************************************ */
static void *
sched_get_job_group_name( sched_obj_list_t job_data,
			  int32_t idx,
			  char *type )
{
	struct group *grp;	
	if ( type ) *type = 's';

	grp = getgrgid( (gid_t) ( (struct job_record *)job_data->data )[ idx ].group_id );
	return (void *) ( grp ? grp->gr_name : "nobody" );
}


/* ************************************************************************ */
/*  TAG(                          expand_hostlist                        )  */
/* ************************************************************************ */
static char *
expand_hostlist( char *ranged )
{
	size_t new_size = 64;
	char *str;
	hostlist_t h = hostlist_create( ranged );

	if ( ! h ) return NULL;
	str = (char *) xmalloc( new_size );
	while ( hostlist_deranged_string( h, new_size, str ) == -1 ) {
		new_size *= 2;
		xrealloc( str, new_size );
	}
	hostlist_destroy( h );
	return str;
}
       
/* ************************************************************************ */
/*  TAG(                       sched_get_job_req_nodes                   )  */
/* ************************************************************************ */
static void *
sched_get_job_req_nodes( sched_obj_list_t job_data,
			 int32_t idx,
			 char *type )
{
	struct job_details *details;
	void *cache;
	
	if ( type ) *type = 'S';
	details = ( (struct job_record *)job_data->data )[ idx ].details;
	if ( details && details->req_nodes ) {
		if ( ( cache = sched_obj_cache_entry_find( job_data,
							   idx,
							   "req_nodes" ) ) != NULL ) {
			return cache;
		}
		cache = expand_hostlist( details->req_nodes );
		if ( ! cache )
			return details->req_nodes;
		sched_obj_cache_entry_add( job_data, idx, "req_nodes", cache );
		return cache;
	}
	return "";
}


/* ************************************************************************ */
/*  TAG(                       sched_get_job_alloc_nodes                 )  */
/* ************************************************************************ */
static void *
sched_get_job_alloc_nodes( sched_obj_list_t job_data,
                         int32_t idx,
                         char *type )
{
        void *cache;
        char *nodes;

        if ( type ) *type = 'S';
        nodes = ( (struct job_record *)job_data->data )[ idx ].nodes;

        if ( nodes ) {
                if ( ( cache = sched_obj_cache_entry_find( job_data,
                                                           idx,
                                                           "alloc_nodes" ) ) != NULL ) {
                        return cache;
                }
                cache = expand_hostlist( nodes );
                if ( ! cache )
                        return nodes;
                sched_obj_cache_entry_add( job_data, idx, "alloc_nodes", cache );
                return cache;
        }
        return "";
}

/* ************************************************************************ */
/*  TAG(                       sched_get_job_min_nodes                   )  */
/* ************************************************************************ */
static void *
sched_get_job_min_nodes( sched_obj_list_t job_data,
			 int32_t idx,
			 char *type )
{
	struct job_details *details;
	
	if ( type ) *type = 'U';
	details = ( (struct job_record *)job_data->data )[ idx ].details;
	if ( details && details->min_nodes ) {
		return (void *) &details->min_nodes;
	} else {
		return (void *) &zero_32;
	}
}

/* ************************************************************************ */
/*  TAG(                       sched_get_job_partition                   )  */
/* ************************************************************************ */
static void *
sched_get_job_partition( sched_obj_list_t job_data,
			 int32_t idx,
			 char *type )
{
	if ( type ) *type = 's';
	return ( (struct job_record *)job_data->data )[ idx ].partition;
}

/* ************************************************************************ */
/*  TAG(                       sched_get_job_min_memory                  )  */
/* ************************************************************************ */
static void *
sched_get_job_min_memory( sched_obj_list_t job_data,
			  int32_t idx,
			  char *type )
{
	struct job_details *details;
	
	if ( type ) *type = 'U';
	details = ( (struct job_record *)job_data->data )[ idx ].details;
	if ( details ) {
		return (void *) &details->min_memory;
	} else {
		return (void *) &zero_32;
	}

}

/* ************************************************************************ */
/*  TAG(                       sched_get_job_min_disk                    )  */
/* ************************************************************************ */
static void *
sched_get_job_min_disk( sched_obj_list_t job_data,
			int32_t idx,
			char *type )
{
	struct job_details *details;
	
	if ( type ) *type = 'U';
	details = ( (struct job_record *)job_data->data )[ idx ].details;
	if ( details ) {
		return (void *) &details->min_tmp_disk;
	} else {
		return (void *) &zero_32;
	}

}




/* ************************************************************************ */
/*  TAG(                       sched_free_node_list                      )  */
/* ************************************************************************ */
int
sched_free_node_list( sched_obj_list_t objlist )
{
	xfree( objlist->data );
	return SLURM_SUCCESS;
}

/* ************************************************************************ */
/*  TAG(                       sched_get_node_list                       )  */
/* ************************************************************************ */
sched_obj_list_t 
sched_get_node_list( void )
{
	size_t node_list_size;
	slurmctld_lock_t node_read_lock = { 
                NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK };
	sched_obj_list_t objlist;

	objlist = (sched_obj_list_t )
		xmalloc( sizeof( struct sched_obj_list ) );
	objlist->destructor = sched_free_node_list;
	objlist->cache = list_create( sched_obj_cache_entry_destructor );

	lock_slurmctld( node_read_lock );
	objlist->count = (int32_t) node_record_count;
	if ( objlist->count == 0 ) {		
		objlist->data = NULL;
		unlock_slurmctld( node_read_lock );
		return objlist;
	}	
	node_list_size = node_record_count * sizeof( struct node_record );
	objlist->data = xmalloc( node_list_size );
	memcpy( objlist->data, node_record_table_ptr, node_list_size );
	unlock_slurmctld( node_read_lock );

	return objlist;
}



/* ************************************************************************ */
/*  TAG(                       sched_get_node_name                       )  */
/* ************************************************************************ */
static void *
sched_get_node_name( sched_obj_list_t node_data,
		     int32_t idx,
		     char *type )
{
	if ( type ) *type = 's';
	return ( (struct node_record *) node_data->data )[ idx ].name;
}

/* ************************************************************************ */
/*  TAG(                       sched_get_node_state                      )  */
/* ************************************************************************ */
static void *
sched_get_node_state( sched_obj_list_t node_data,
		      int32_t idx,
		      char *type )
{
	struct node_state_label_map_struct {
		enum node_states   	state;
		char *	       		label;
	};

	/*
	 * This is so we don't have to maintain a coordinated enumeration
	 * across the plugin interface.
	 *
	 * It would seem best to map UNKNOWN to UNKNOWN, but some
	 * schedulers don't accept UNKNOWN as an unschedulable state and
	 * so they wait and see if it comes back up.  UNKNOWN in our
	 * world typically means the slurmd has died, so no jobs can
	 * be scheduled there anyway.
	 */
	static struct node_state_label_map_struct
		node_state_label_map[] = {
		{ NODE_STATE_DOWN,     	NODE_STATE_LABEL_DOWN },
		{ NODE_STATE_UNKNOWN,	NODE_STATE_LABEL_DOWN },
		{ NODE_STATE_IDLE,     	NODE_STATE_LABEL_IDLE },
		{ NODE_STATE_ALLOCATED,	NODE_STATE_LABEL_ALLOCATED },
		{ NODE_STATE_END,      	NODE_STATE_LABEL_UNKNOWN }
	};

	struct node_state_label_map_struct *p;

	enum node_states state = ( (struct node_record *) node_data->data )[ idx ].node_state;

	if ( type ) *type = 'e';

	if ( state & NODE_STATE_NO_RESPOND ) {
		return NODE_STATE_LABEL_DOWN;
	}
	if ( state & NODE_STATE_COMPLETING ) {
		return NODE_STATE_LABEL_COMPLETING;
	}
	if ( state & NODE_STATE_DRAIN ) {
		if ((state & NODE_STATE_COMPLETING)
		||  ((state & NODE_STATE_BASE) == NODE_STATE_ALLOCATED))
			return NODE_STATE_LABEL_DRAINING;
		return NODE_STATE_LABEL_DRAINED;
	}
	for ( p = node_state_label_map;
		  p->state != NODE_STATE_END;
		  ++p ) {
		if ( state == p->state ) {
			return p->label;
		}
	}

	return NODE_STATE_LABEL_UNKNOWN;
}

/* ************************************************************************ */
/*  TAG(                       sched_get_node_num_cpus                   )  */
/* ************************************************************************ */
void *
sched_get_node_num_cpus( sched_obj_list_t node_data,
			 int32_t idx,
			 char *type )
{
	if ( type ) *type = 'U';
	return (void *) &( (struct node_record *) node_data->data )[ idx ].cpus;
}

/* ************************************************************************ */
/*  TAG(                       sched_get_node_real_mem                   )  */
/* ************************************************************************ */
void *
sched_get_node_real_mem( sched_obj_list_t node_data,
			 int32_t idx,
			 char *type )
{
	if ( type ) *type = 'U';
	return (void *) &( (struct node_record *) node_data->data )[ idx ].real_memory;
}

/* ************************************************************************ */
/*  TAG(                       sched_get_node_tmp_disk                   )  */
/* ************************************************************************ */
void *
sched_get_node_tmp_disk( sched_obj_list_t node_data,
			 int32_t idx,
			 char *type )
{
	if ( type ) *type = 'U';
	return (void *) &( (struct node_record *) node_data->data )[ idx ].tmp_disk;
}

/* ************************************************************************ */
/*  TAG(                       sched_get_node_partition                  )  */
/* NOTE: A SLURM node can be in multiple partitions/queues at the same time */
/* We return only the first of these partition names here or NULL if there  */
/* are no associated partitions. There are 'part_cnt' partitions associated */
/* with each node. There is an array of pointers to these partitions in the */
/* array 'part_pptr'. We probably want to change this function accordingly. */
/* ************************************************************************ */
void *
sched_get_node_partition( sched_obj_list_t node_data,
			  int32_t idx,
			  char *type )
{
	if ( type ) *type = 's';
	if ( ((struct node_record *) node_data->data )[ idx ].part_cnt == 0 )
		return NULL;

	return ( (struct node_record *) node_data->data )[ idx ].
			part_pptr[0]->name;
}

/* ************************************************************************ */
/*  TAG(                       sched_get_node_mod_time                   )  */
/* ************************************************************************ */
void *
sched_get_node_mod_time( sched_obj_list_t node_data,
			 int32_t idx,
			 char *type )
{
	if ( type ) *type = 't';
	return (void *) &( (struct node_record *) node_data->data )[ idx ].last_response;
}

static
char *_copy_nodelist_no_dup(char *node_list)
{
        int   new_size = 64;
        char *new_str;
        hostlist_t hl = hostlist_create( node_list );
	
        if ( hl == NULL )
                return NULL;
 
        hostlist_uniq( hl );
        new_str = xmalloc( new_size );
        while ( hostlist_ranged_string( hl, new_size, new_str ) == -1 ) {
                new_size *= 2;
                xrealloc( new_str, new_size );
        }
        hostlist_destroy( hl );
        return new_str;
}

/* ************************************************************************ */
/*  TAG(                       sched_set_nodelist                        )  */
/* ************************************************************************ */
int
sched_set_nodelist( const uint32_t job_id, char *nodes )
{
	ListIterator i;
	struct job_record *job;
	struct job_details *det;
	int rc = SLURM_ERROR;

	/* Write lock on job info, read lock on node info */
	slurmctld_lock_t job_write_lock = { 
                NO_LOCK,
		WRITE_LOCK,
		READ_LOCK,
		NO_LOCK
	};

	debug3( "Scheduler setting node list to %s for job %u", nodes, job_id );
	lock_slurmctld( job_write_lock );
	i = list_iterator_create( job_list );	
	while ( ( job = (struct job_record *) list_next( i ) ) != NULL ) {

		if ( job->job_id != job_id ) continue;
		
		/*
		 * Okay, the nice thing to do here would be to
		 * add a job details structure and put the node list
		 * in it.
		 */
		if ( ( det = job->details ) == NULL ) {
			debug2( "no job details for job %d", job_id );
			break;
		}

		/* Remove any old node list. */
		if ( det->req_nodes ) xfree( det->req_nodes );
		if ( det->req_node_bitmap ) bit_free( det->req_node_bitmap );

		/*
		 * Don't know what to do about the exclusion
		 * list.  Ergo, leave it alone.
		 */

		det->req_nodes = _copy_nodelist_no_dup( nodes );

		/* Now do a new bitmap. */
		node_name2bitmap( det->req_nodes, true,
				  &det->req_node_bitmap );

		rc = SLURM_SUCCESS;
		break;
	}

	unlock_slurmctld( job_write_lock );
	list_iterator_destroy( i );
	
	return rc;
}


/* ************************************************************************ */
/*  TAG(                       sched_start_job                           )  */
/* ************************************************************************ */
int
sched_start_job( const uint32_t job_id, uint32_t new_prio )
{
	ListIterator i;
	struct job_record *job;
	int rc;
	time_t now = time( NULL );
	
	/* write lock on job info, no other locks needed */
	slurmctld_lock_t job_write_lock = { 
                NO_LOCK,
		WRITE_LOCK,
		NO_LOCK,
		NO_LOCK
	};

	debug3( "Scheduler plugin requested launch of job %u", job_id );
	lock_slurmctld( job_write_lock );
	i = list_iterator_create( job_list );
	rc = SLURM_ERROR;
	while ( ( job = (struct job_record *) list_next( i ) ) != NULL ) {
		if ( job->job_id == job_id ) {
			job->priority = new_prio;
			job->time_last_active = now;
			last_job_update = now;
			rc = SLURM_SUCCESS;
			break;
		}
	}
	list_iterator_destroy( i );
	unlock_slurmctld( job_write_lock );
	/* Below functions provide their own locks */
	if (schedule()) {
		schedule_job_save();
		schedule_node_save();
	}
	return rc;
}


/* ************************************************************************ */
/*  TAG(                       sched_cancel_job                          )  */
/* ************************************************************************ */
int
sched_cancel_job( const uint32_t job_id )
{
	int rc = SLURM_SUCCESS;
	
	/* Locks: Read config, read nodes, write jobs */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, READ_LOCK, NO_LOCK };

	/*
	 * The nice way to do things would be to send SIGTERM, wait for
	 * about five seconds, and then send SIGKILL.  But rather than
	 * pre-empt the controller for five seconds, and rather than
	 * spawning a thread and then trying to rendezvous again with
	 * the plugin, we do the heavy-handed thing.
	 */
	debug3( "Scheduler plugin requested cancellation of job %u", job_id );
	lock_slurmctld( job_write_lock );
	rc = job_signal( job_id, SIGKILL, 0, getuid() );
	unlock_slurmctld( job_write_lock );

	return rc;
}
