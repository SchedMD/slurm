/*****************************************************************************\
 *  wiki_canceljob.cpp - Wiki command to stop a job.
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
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include <string.h>

extern "C" {
#  include "src/common/log.h"
#  include "src/slurmctld/sched_plugin.h" 	
}

#include "wiki_message.h"
#include "wiki_parser.h"

// **************************************************************************
//  TAG(                        wiki_canceljob_t                        ) 
// **************************************************************************
wiki_canceljob_t::wiki_canceljob_t( char *data, size_t len ) :
	wiki_command_t( data, len, wiki_message_t::CANCELJOB )
{
	ListIterator j;
	wiki_relation_t *relation;
	char *reason;
	
	// * Get the reason.
	relation = (wiki_relation_t *) list_next( m_arg_iterator );
	if ( relation == NULL )
		throw "malformed command (type missing)";
	if ( strcmp( relation->name(), "TYPE" ) != 0 )
		throw "malformed command (expected TYPE=<type>)";
	j = list_iterator_create( relation->values() );

	reason = (char *) list_next( j );
	list_iterator_destroy( j );
	if ( reason && strcmp( reason, "ADMIN" ) == 0 ) {
		m_reason = ADMIN;
	} else if ( reason && strcmp( reason, "WALLCLOCK" ) == 0 ) {
		m_reason = WALLCLOCK;
	} else {
		throw "unknown cancel mode";
	}
}


// **************************************************************************
//  TAG(                             action                             ) 
// **************************************************************************
message_t *
wiki_canceljob_t::action( void )
{
	u_int32_t id = (u_int32_t) atol( m_jobid );
	int rc;
	verbose( "Wiki canceling job %s", m_jobid );
	rc = sched_cancel_job( id );
	return new wiki_status_t( ( rc == SLURM_SUCCESS ) ? 0 : -rc );
}
