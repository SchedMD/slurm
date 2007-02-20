/*****************************************************************************\
 *  wiki_response.cpp - respond to a Wiki request for resource status.
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
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "wiki_message.h"

// **************************************************************************
//  TAG(                         wiki_response_t                         ) 
// **************************************************************************
wiki_response_t::wiki_response_t( wiki_request_t	*request,
				  char * const * const	fields,
				  int32_t		obj_count,
				  int32_t		obj_hits,
				  sched_obj_list_t     	obj_data,
				  bool			*matches ) :
	wiki_status_t( 0 )
{
	int32_t i;

	// * Encode argument count.
        m_str += " ARG=";
        m_str += obj_hits;

	// * If there were no matches found, we're done.
        if ( obj_hits == 0 ) {
                prefix_with_checksum();
                debug3( "Wiki plugin response = \"%s\"", m_str.s() );
                return;
        }

	// * List all the matches.
        for ( i = 0; i < obj_count; ++i ) {
                if ( matches[ i ] ) {
                        request->compose_response( request,
						   m_str,
						   i,
						   fields,
						   obj_data );
                }
        }

	sched_free_obj_list( obj_data );
	delete [] matches;
	
	// * Do the Wiki checksumming.
        prefix_with_checksum();
	debug3( "Wiki plugin response = \"%s\"", m_str.s() );
}
