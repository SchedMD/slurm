/*****************************************************************************\
 *  receptionist.cpp - connection manager for scheduling plugins.
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

#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>

extern "C" {
# include "src/common/log.h"
}

#include "receptionist.h"
#include "agent.h"

// **************************************************************************
//  TAG(                         receptionist_t                         ) 
// **************************************************************************
receptionist_t::receptionist_t( const courier_factory_t * const courier_factory,
				const mailbag_factory_t * const mailbag_factory,
				const struct sockaddr_in *addr ) :
	m_courier_factory( courier_factory ),
	m_mailbag_factory( mailbag_factory ),
	m_addr( addr )
{
	int dummy = 1;
	int rc;

	// * Open a socket.
	if ( ( m_sock = socket( PF_INET, SOCK_STREAM, 0 ) ) < 0 ) {
		throw strerror( errno );
	}

	// *
	// Bind the socket to the given address and set it to close immediately
	// on error or when released.  Either of these operations should close
	// the socket if it fails.
	// *
	try {
		if ( bind( m_sock,
			   (const sockaddr *) m_addr,
			   sizeof( struct sockaddr_in ) ) < 0 ) {
			throw strerror( errno );
		}

		if ( setsockopt( m_sock,
				 SOL_SOCKET,
				 SO_REUSEADDR,
				 &dummy, sizeof( dummy ) ) < 0 ) {
			throw strerror( errno );
		}

	
	} catch ( const char *msg ) {
		// * Close the socket and then propagate the exception upward.
		(void) close( m_sock );
		throw msg;
	}
}


// **************************************************************************
//  TAG(                             listen                             ) 
// **************************************************************************
int
receptionist_t::listen( void )
{
	struct sockaddr_in caller;
	socklen_t caller_len;
	int accepted;
	agent_t *agent;

	if ( ::listen( m_sock, 5 ) == -1 ) return -1;

	while ( 1 ) {

		// * Wait for a connection.
		if ( ( accepted = accept( m_sock,
					  (sockaddr *) &caller,
					  &caller_len ) ) < 0 ) {
			if ( errno != EINTR ) {
				error( "Wiki: accept() failed" );
				return -1;
			}
			break;
		}

		// * Build an agent to handle this connection.
		agent = new agent_t( this,
				     m_courier_factory->courier( accepted,
								 m_mailbag_factory ),
				     m_mailbag_factory );

		// * Start the agent.
		if ( agent->start() < 0 ) {
			// * Panic, or something.
			error( "Wiki: starting the agent failed" );
		}

		// *
		// N.B. The agent deletes itself when its thread exits;
		// it is not leaked from here, as casual inspection suggests.
		// *
	
	}

	return 0;
}
