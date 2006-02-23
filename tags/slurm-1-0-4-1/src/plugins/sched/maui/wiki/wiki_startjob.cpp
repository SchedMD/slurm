/*****************************************************************************\
 *  wiki_startjob.cpp - handle a Wiki STARTJOB command.
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

extern "C" {
# include "src/common/log.h"
# include "src/common/xassert.h"	
}

#include "wiki_message.h"
#include "wiki_parser.h"


// **************************************************************************
//  TAG(                        wiki_startjob.cpp                        ) 
// **************************************************************************
wiki_startjob_t::wiki_startjob_t( char *data, size_t len ) :
	wiki_command_t( data, len, wiki_message_t::STARTJOB )
{
	wiki_relation_t *relation;
	char *node;
	ListIterator j;
	
	// * Get the node list.
	relation = (wiki_relation_t *) list_next( m_arg_iterator );
	if ( relation == NULL )
		throw "malformed command (missing task list)";
	if ( strcmp( relation->name(), "TASKLIST" ) != 0 )
		throw "malformed command (expected TASKLIST=<nodes>)";

	m_nodelist = list_create( NULL );
	j = list_iterator_create( relation->values() );
	while ( ( node = (char *) list_next( j ) ) != NULL ) {
		list_append( m_nodelist, node );
	}
	list_iterator_destroy( j );
}


// **************************************************************************
//  TAG(                             action                             ) 
// **************************************************************************
message_t *
wiki_startjob_t::action( void )
{
	u_int32_t id = (u_int32_t) atol( m_jobid );
	int rc;
	char status_msg[128];

	// *
	// If Maui has specified a node list to run on, change the
	// controller's requested node list so that it matches.
	// *
	if ( list_count( m_nodelist ) > 0 ) {
		char *node;
		dstring_t node_list;
		ListIterator i = list_iterator_create( m_nodelist );

		// * There's at least one node.
		node = (char *) list_next( i );
		xassert( i );
		node_list += node;

		// * Add the rest of the nodes.
		while ( ( node = (char *) list_next( i ) ) != NULL ) {
			node_list += ",";
			node_list += node;
		}

		if ( sched_set_nodelist( id, node_list.s() ) == SLURM_ERROR ) {
			error( "Wiki cannot assign nodes to job %d", id );
		}
	}
	
	verbose( "Wiki starting job %s", m_jobid );	

	rc = sched_start_job( id, (u_int32_t) 1 );

	if (rc == SLURM_SUCCESS)
	{
		snprintf(status_msg, sizeof(status_msg), 
			"SUCCESS: job %s started successfully", m_jobid);
		return new wiki_status_t( 0, status_msg);
	}
	else
	{
		snprintf(status_msg, sizeof(status_msg),
			"ERROR: job %s failed to start", m_jobid);
		return new wiki_status_t( -1, status_msg);
	}
}
