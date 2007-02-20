/*****************************************************************************\
 *  wiki_request - message from scheduler to ask for resource status.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
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

#include <string.h>
#include <slurm/slurm_errno.h>

extern "C" {
#	include "src/common/log.h"
#	include "src/common/xassert.h"	
#	include "src/slurmctld/sched_plugin.h"
}

#include "wiki_message.h"
#include "../comparator.h"


// **************************************************************************
//  TAG(                         wiki_request_t                         ) 
// **************************************************************************
wiki_request_t::wiki_request_t( char			*data,
				size_t			len,
				int			type,
				char			*epoch_field,
				char			*name_field,
				sched_objlist_fn_t	list_retriever ) :
	wiki_message_t( data, len, type ),
	m_list_retriever ( list_retriever ),
	m_name_accessor( sched_get_accessor( name_field ) )
{
	wiki_status_t *response		= NULL;
	wiki_relation_t *relation	= NULL;
	sched_accessor_fn_t epoch_accessor = NULL;
	char *arg			= NULL;
	int j;
	ListIterator i;

	// * First relation is always "CMD=GET<whatever>".
	i = list_iterator_create( m_expr.relations() );
	(void) list_next( i );

	// * Get ARG= term.
	relation = (wiki_relation_t *) list_next( i );
	list_iterator_destroy( i );
	if ( relation == NULL )
		throw "malformed command (missing argument)";
	if ( strcmp( relation->name(), "ARG" ) != 0 )
		throw "malformed command (expected ARG=)";

	// * First argument is always the epoch.
	i = list_iterator_create( relation->values() );
	arg = (char *) list_next( i );
	if ( arg == NULL )
		throw "malformed command (missing epoch)";
	m_epoch = atotime( arg );

	// * Get "ALL" or object list.
	arg = (char *) list_next( i );
	if ( arg == NULL )
		throw "malformed command (expected object list or ALL)";

	// * Create expression to match the epoch.
	epoch_accessor = sched_get_accessor( epoch_field );
	m_match = new int_eq_comparator_t( epoch_accessor, m_epoch );
	m_match = new conjunction_t( conjunction_t::OR,
				     m_match,
				     new int_gt_comparator_t( epoch_accessor,
							      m_epoch ) );

	// * If there are no specific object names given, we're done.
	if ( strcmp( arg, "ALL" ) == 0 ) return;

	// * Include any names in match expression.
	while ( arg ) {
		m_match = new conjunction_t( conjunction_t::OR,
					     m_match,
					     new string_eq_comparator_t( m_name_accessor,
									 arg ) );
		arg = (char *) list_next( i );
	}
}


// **************************************************************************
//  TAG(                             action                             ) 
// **************************************************************************
message_t *
wiki_request_t::action( void )
{
	sched_obj_list_t obj_data	= NULL;
	int32_t obj_count		= 0;
	bool *obj_matches      		= NULL;
	int32_t obj_hits		= 0;
	int32_t i;

	// *
	// Get object list from controller.  This will be either
	// the node list or the job queue.
	// *
	if ( ( obj_data = (*m_list_retriever)() ) == NULL ) {
		error( "Wiki scheduler: can't get job queue from controller" );
		return new wiki_status_t( -1 );
	}
	// * See if there is actually any data.
	obj_count = sched_get_obj_count( obj_data );
	if ( obj_count == 0 ) {
		debug3( "Wiki scheduler: empty resource list" );
		sched_free_obj_list( obj_data );
		return new wiki_response_t( NULL, NULL, 0, 0, NULL, NULL );
	}

	// * Allocate array of flags for matches.
	obj_matches = new bool[ obj_count ];
	xassert( obj_matches );

	// * Run the matching expression against the object list.
	for ( i = 0; i < obj_count; ++i ) {
		obj_matches[ i ] = m_match->eval( obj_data, i );
		if ( obj_matches[ i ] ) ++obj_hits;
	}

	// *
	// Create a list of matching objects to return to the scheduler.
	// This must be done even if the list is empty.
	// *
	return new wiki_response_t( this,
				    (char * const *) m_fields,
				    obj_count,
				    obj_hits,
				    obj_data,
				    obj_matches );
}



// **************************************************************************
//  TAG(                        compose_response                        ) 
// **************************************************************************
void
wiki_request_t::compose_response( wiki_request_t	*request,
				  dstring_t		&str,
				  int32_t		idx,
				  char * const * const	fields,
				  sched_obj_list_t	obj_data )
{
	int i;
	sched_accessor_fn_t field_accessor;
	void *val;
	const char *field_str;
	char *val_str;
	char type;

	// *
	// Give primary key: name for node, job ID for job.  The data
	// member m_name_accessor is a pointer to a function in the
	// plugin API that will do the right thing for this contoller
	// object list.
	// *
	str += "#";
	str += (char *) (*m_name_accessor)( obj_data, idx, &type );
	str += ":";
	
	for ( i = 1; fields[ i ]; ++i ) {

		// * Get the accessor for this field and call it.
		field_accessor = sched_get_accessor( fields[ i ] );
		if ( ! field_accessor ) {
			debug3( "Wiki request: no field accessor for %s",
				fields[ i ] );
			continue;
		}
		val = (*field_accessor)( obj_data, idx, &type );

		field_str = request->slurm2wiki( fields[ i ] );
		
		// *
		// Based on the return type of the accessor, add the
		// value to the entry.  Why not factor out the code
		// that writes the field name?  Because after getting
		// the value back from the accessor and cooking it if
		// necessary we might discover that we don't want to
		// say anything at all about that particular field.
		// If we've already written the field name then we
		// have to back that out again.
		// *
		switch ( type ) {

			// *
			// Enumerations are passed back as
			// self-describing strings.  This is so we
			// don't have to try to keep integers
			// synchronized across the plugin interface.
			// There are qualitative differences in the
			// meaning of enumerated values between SLURM
			// and the scheduler, so we have to
			// programmatically convert the meaning of an
			// enumerated value into its Wiki equivalent.
			// This has to happen in the plugin, not in
			// the accessor, because different schedulers
			// will have different ideas of, for example,
			// a "job state".
			// *
		case 'e':
			val_str = request->map_enum( fields[ i ],
						     (char *) val );
			if ( val_str && *val_str ) {
				str += field_str;
				str += "=";
				str += val_str;
				str += ";";
			}
			break;
		case 's':
			if ( val && *( (char *)val ) ) {
				str += field_str;
				str += "=";
				str += (char *) val;
				str += ";";
			}
			break;
		case 'S':
			if ( val && *( (char *)val ) )  {
				char *cooked = request->postproc_string( fields[ i ],
									 (char *) val );
				str += field_str;
				str += "=";
				if ( cooked ) {
					str += cooked;
					xfree( cooked );
				} else {
					str += (char *) val;
				}
				str += ";";
			}
			break;
		case 't':
			if ( (*(time_t *) val) != (time_t) NO_VAL ) {
				str += field_str;
				str += "=";
				str += (*(time_t *) val);
				str += ";";
			}
			break;
		case 'i':
			if ( (uint16_t) (*(int16_t *) val) != 
			     (uint16_t) NO_VAL ) {
				str += field_str;
				str += "=";
				str += (*(int16_t *) val);
				str += ";";
			}
			break;
		case 'I':
			if ( (*(int32_t *) val) != NO_VAL ) {
				str += field_str;
				str += "=";
				str += (*(int32_t *) val);
				str += ";";
			}
			break;
			
		case 'u':
			if ( (uint16_t) (*(uint16_t *) val) != 
			     (uint16_t) NO_VAL ) {
				str += field_str;
				str += "=";
				str += (*(uint16_t *) val);
				str += ";";
			}
			break;
		case 'U':
			if ( (*(uint32_t *) val) != NO_VAL ) {
				str += field_str;
				str += "=";
				str += (*(uint32_t *) val);
				str += ";";
			}
			break;
		default:
			str += field_str;
			str += "UNKNOWN";
			str += ";";
		}
	}
}



// **************************************************************
//  TAG(                      slurm2wiki                      ) 
// **************************************************************
// DESCRIPTION
// Map a SLURM field name into a Wiki field name.
//  
// ARGUMENTS
// field (in) - The SLURM field name.
//  
// RETURNS
// Pointer to the equivalent Wiki field name, or NULL if no
// equivalent exists.
//  
// **************************************************************
const char * const
wiki_request_t::slurm2wiki( char * const field ) const
{
	struct field_name_map *p;

	for ( p = m_field_map; p->slurm_field != NULL; ++p ) {
		if ( strcmp( field, p->slurm_field ) == 0 ) {
			return p->wiki_field;
		}
	}
	error( "No Wiki-equivalent name for field %s", field );
	return NULL;
}


// **************************************************************
//  TAG(                    postproc_string                    ) 
// **************************************************************
// DESCRIPTION
// Post-process a string returned by the plugin upcall.  The
// scheduling plugin returns string-valued attributes in SLURM's
// internal format.  Very often this is not suitable for the
// external scheduler, in this case Wiki.  This is somewhat
// different than post-processing a string-valued enumeration,
// although it may be advantageous in the future to consolidate
// those functions.
//  
// ARGUMENTS
// field (in) - the SLURM object field whose string value is to
// 		be post-processed.
// val (in) - the value returned by the SLURM scheduler plugin
//		interface.
//  
// RETURNS
// An xmalloc-allocated string contained the "corrected" version
// of the input string.  The caller owns this string and should
// free it with xfree when finished.
//  
// **************************************************************
char *
wiki_request_t::postproc_string( char * const field,
				 const char * const val )
{
	static struct post_proc_map_struct {
		char *field_name;
		char * (*post_processor)( const char * const val );
	} post_proc_map [] = {
		{ JOB_FIELD_REQ_NODES,	wiki_request_t::colonify_commas },
		{ JOB_FIELD_ALLOC_NODES,wiki_request_t::colonify_commas },
		{ NULL,			NULL }
	};

	struct post_proc_map_struct *p;

	for ( p = post_proc_map; p->field_name != NULL; ++p ) {
		if ( strcmp( p->field_name, field ) == 0 ) {
			return (*(p->post_processor))( val );
		}
	}
	return NULL;
}

// **************************************************************
//  TAG(                    colonify_commas                    ) 
// **************************************************************
// DESCRIPTION
// A string post-processor which replaces commas with colons.
// Most SLURM lists are comma-separated whereas most Wiki lists
// want to be colon-delimited.
//  
// ARGUMENTS
// val (in) - The original string.
//  
// RETURNS
// A heap-allocated string that is a copy of <val> with the
// commas translated to strings.  The caller owns the returned
// string.
//  
// **************************************************************
char *
wiki_request_t::colonify_commas( const char * const val )
{
	char *str = xstrdup( val );

	for ( char *p = str; *p; ++p )
		if ( *p == ',' ) *p = ':';

	return str;
}

// **************************************************************************
//  TAG(                         wiki_getnodes_t                         ) 
// **************************************************************************
wiki_getnodes_t::wiki_getnodes_t( char *data, size_t len ) :
	wiki_request_t( data,
			len,
			wiki_message_t::GETNODES,
			NODE_FIELD_MOD_TIME,
			NODE_FIELD_NAME,
			sched_get_node_list )
{
	// *
	// Fields from the SLURM node structure that we will supply via Wiki.
	// *
	static const char *node_fields[] = {
		NODE_FIELD_NAME,
		NODE_FIELD_STATE,
		NODE_FIELD_REAL_MEM,
		NODE_FIELD_TMP_DISK,
		NODE_FIELD_NUM_CPUS,
		
		NULL
	};

	// *
	// Mapping between SLURM node field names and Wiki node field names.
	// *
	static struct wiki_request_t::field_name_map node_field_map[] = {
		//   Wiki name             SLURM name
		{ "UPDATETIME",		NODE_FIELD_MOD_TIME },
		{ "STATE",		NODE_FIELD_STATE },
		{ "CMEMORY",		NODE_FIELD_REAL_MEM },
		{ "CDISK",		NODE_FIELD_TMP_DISK },
		{ "CPROC",		NODE_FIELD_NUM_CPUS },

		{ NULL,			NULL }
	};

	m_fields = node_fields;
	m_field_map = node_field_map;
}


// **************************************************************************
//  TAG(                    wiki_getnodes_t::map_enum                    ) 
// **************************************************************************
char * const
wiki_getnodes_t::map_enum( char * const field,
			   char * const val ) const
{
	struct string_map {
		char *slurm_label;
		char *wiki_label;
	};

	static struct string_map node_state_map[] = {
		{ NODE_STATE_LABEL_DOWN,       	"Down" },
		{ NODE_STATE_LABEL_UNKNOWN,    	"Unknown" },
		{ NODE_STATE_LABEL_IDLE,       	"Idle" },
		{ NODE_STATE_LABEL_DRAINED,    	"Draining" },
		{ NODE_STATE_LABEL_DRAINING,	"Draining" },
		{ NODE_STATE_LABEL_ALLOCATED,	"Running" },
		{ NODE_STATE_LABEL_COMPLETING,	"Busy" },
		
		{ NULL,				NULL }
	};

	if ( strcmp( field, NODE_FIELD_STATE ) == 0 ) {
		for ( struct string_map *p = node_state_map;
		      p->slurm_label != NULL;
		      ++p ) {
			if ( strcmp( p->slurm_label, val ) == 0 ) {
				return p->wiki_label;
			}
		}
		return "Unknown";
	} else {
		return "Unknown";
	}
}



// **************************************************************************
//  TAG(                         wiki_getjobs_t                         ) 
// **************************************************************************
wiki_getjobs_t::wiki_getjobs_t( char *data, size_t len ) :
	wiki_request_t( data,
			len,
			wiki_message_t::GETJOBS,
			JOB_FIELD_LAST_ACTIVE,
			JOB_FIELD_ID,
			sched_get_job_list )
{
	// *
	// Fields from the SLURM job data structures that we will
	// supply via Wiki.
	//
	// XXX - consolidate this list with the following one.
	// *
	static const char *job_fields[] = {
		JOB_FIELD_ID,
		JOB_FIELD_LAST_ACTIVE,
		JOB_FIELD_STATE,
		JOB_FIELD_TIME_LIMIT,
		JOB_FIELD_NUM_TASKS,
		JOB_FIELD_SUBMIT_TIME,
		JOB_FIELD_START_TIME,
		// JOB_FIELD_END_TIME,  -- this confuses Maui
		JOB_FIELD_USER_ID,
		JOB_FIELD_GROUP_ID,
		JOB_FIELD_ALLOC_NODES,
		JOB_FIELD_REQ_NODES,
		JOB_FIELD_PARTITION,
		JOB_FIELD_MIN_NODES,
		JOB_FIELD_MIN_MEMORY,
		JOB_FIELD_MIN_DISK,
		
		NULL
	};

	// *
	// Mapping between SLURM job field names and Wiki job
	// field names.
	// *
	static struct wiki_request_t::field_name_map job_field_map[] = {
		// Wiki name			SLURM name
		{ "UPDATETIME",		JOB_FIELD_LAST_ACTIVE },
		{ "STATE",		JOB_FIELD_STATE },
		{ "WCLIMIT",		JOB_FIELD_TIME_LIMIT },
		{ "TASKS",		JOB_FIELD_NUM_TASKS },
		{ "QUEUETIME",		JOB_FIELD_SUBMIT_TIME },
		{ "STARTTIME",		JOB_FIELD_START_TIME },
		{ "COMPLETIONTIME",    	JOB_FIELD_END_TIME },
		{ "UNAME",		JOB_FIELD_USER_ID },
		{ "GNAME",		JOB_FIELD_GROUP_ID },
		{ "HOSTLIST",		JOB_FIELD_REQ_NODES },
		{ "TASKLIST",           JOB_FIELD_ALLOC_NODES },
		{ "PARTITIONMASK",     	JOB_FIELD_PARTITION },
		{ "NODES",		JOB_FIELD_MIN_NODES },
		{ "RMEM",		JOB_FIELD_MIN_MEMORY },
		{ "RDISK",		JOB_FIELD_MIN_DISK },
		
		{ NULL,			NULL }
	};

	m_fields = job_fields;
	m_field_map = job_field_map;
}


// **************************************************************************
//  TAG(                    wiki_getjobs_t::map_enum                    ) 
// **************************************************************************
char * const
wiki_getjobs_t::map_enum( char * const field,
			  char * const val ) const
{
	struct string_map {
		char *slurm_label;
		char *wiki_label;
	};

	static struct string_map job_state_map[] = {
		{ JOB_STATE_LABEL_PENDING,	"Idle" },
		{ JOB_STATE_LABEL_RUNNING,    	"Running" },
		{ JOB_STATE_LABEL_COMPLETE,	"Completed" },
		{ JOB_STATE_LABEL_FAILED,    	"Removed" },
		{ JOB_STATE_LABEL_TIMEOUT,	"Removed" },
		{ JOB_STATE_LABEL_NODE_FAIL,	"Removed" },
		{ "UNKNOWN",			"Removed" },
		
		{ NULL,				NULL }
	};

	if ( strcmp( field, JOB_FIELD_STATE ) == 0 ) {
		for ( struct string_map *p = job_state_map;
		      p->slurm_label != NULL;
		      ++p ) {
			if ( strcmp( p->slurm_label, val ) == 0 ) {
				return p->wiki_label;
			}
		}
		error( "Wiki scheduler: no mapping for job state '%s'", val );
		return "Unknown";
	} else {
		return "Unknown";
	}

}
