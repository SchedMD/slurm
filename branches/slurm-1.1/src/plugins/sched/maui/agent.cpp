/*****************************************************************************\
 *  agent.cpp - manages a single connection-defined transaction.
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

extern "C" {
# include "src/common/log.h"
# include "src/common/macros.h"
}

#include "agent.h"
#include "mailbag.h"

// **************************************************************************
//  TAG(                             agent_t                             ) 
// **************************************************************************
agent_t::agent_t( const receptionist_t *recep,
		  courier_t *courier,
		  const mailbag_factory_t * const mailbag_factory ) :
	m_recep( recep ),
	m_courier( courier ),
	m_mailbag_factory( mailbag_factory )
{
	// *
	// Check that all required specialization objects are provided.
	// *
	if ( ! m_recep ) throw "agent_t: no receptionist specified";
	if ( ! m_courier ) throw "agent_t: no courier specified";
	if ( ! m_mailbag_factory ) throw "agent_t: no mailbag factory specified";
}


// **************************************************************************
//  TAG(                            ~agent_t                            ) 
// **************************************************************************
agent_t::~agent_t()
{
	delete m_courier;
	debug3( "agent_t thread exiting" );
	pthread_exit( 0 );
}


// **************************************************************************
//  TAG(                              start                              ) 
// **************************************************************************
int
agent_t::start( void )
{
	pthread_attr_t attr;
	int rc;

	slurm_attr_init( &attr );
	pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
	rc = pthread_create( &m_thread,
			     &attr,
			     agent_t::thread_entry,
			     this );
	slurm_attr_destroy( &attr );
	return rc;
}


// **************************************************************************
//  TAG(                          thread_entry                          ) 
//  
// The required static entry point for the thread.  The agent is passed as
// the anonymous pointer which is downcast.
// **************************************************************************
void *
agent_t::thread_entry( void *me )
{
	agent_t *agent = (agent_t *) me;
	debug3( "agent_t thread created" );
	agent->thread_main();
	return NULL;
}


// **************************************************************************
//  TAG(                          thread_main                          ) 
//
// The thread main() function.
// **************************************************************************
void
agent_t::thread_main( void )
{
	// *
	// FIXME: Here is where we would flush to the courier
	// any messages pending from previous sessions.
	// *

	// * Do the main loop.
	spin();


	// *
	// A bit dangerous, but it's the only way to assure we won't
	// leak agents.
	// *

	delete this;       
}



// **************************************************************************
//  TAG(                              spin                              ) 
//  
// The main loop of the thread.  Obtains mailbags from the courier and acts
// on them.  If the connection goes down the courier stops delivering mail-
// bags and we exit.
// **************************************************************************
void
agent_t::spin( void )
{
	mailbag_t *in_bag, *out_bag;
	mailbag_iterator_t *it;
	message_t *msg, *response;

	// * Allocate an initial mailbag.
	out_bag = m_mailbag_factory->mailbag();
    
	// *
	// Wait for the courier to deliver mailbags until it decides not
	// to anymore.  Pull messages out of the mailbag and then call
	// each one's action method to get a reply.
	// *
	for ( in_bag = m_courier->receive();
	      in_bag;
	      in_bag = m_courier->receive() ) {

		it = in_bag->iterator();
		if ( it == NULL ) {
			debug2( "agent_t::spin: warning - empty packet" );
			delete in_bag;
			continue;
		}

		for ( it->first(); ! it->at_end(); it->next() ) {

			msg = in_bag->message( it );
			if ( msg == NULL ) {
				debug2( "agent_t::spin: warning - empty message" );
				continue;
			}

			// *
			// Call the message's action method.  That
			// should do the work suggested by the message
			// and return a response message containing
			// the answer.  It is perfectly acceptable for
			// action methods not to return a response,
			// such as when the message itself is a
			// response and we only need to acknowledge
			// having received it.
			// *
			response = msg->action();
			if ( response ) {
				if ( out_bag->is_full() ) {
					if ( m_courier->send( out_bag ) < 0 ) {
						delete out_bag;
						throw "agent: can't send mailbag";
					}
					out_bag = m_mailbag_factory->mailbag();
				}
		
				if ( out_bag->add( response ) < 0 ) {
					throw "agent: can't add last response to mailbag";
				}

				if ( out_bag->is_full() ) {
					if ( m_courier->send( out_bag ) < 0 ) {
						delete out_bag;
						throw "agent: can't send mailbag";
					}
					out_bag = m_mailbag_factory->mailbag();
				}
			}

			delete msg;
		}

		delete it;
		delete in_bag;
	}

	delete out_bag;
	
#if 0	
	// * Attempt to send any pending messages.	
	if ( out_bag->num_items() > 0 ) {
		if ( m_courier->send( out_bag ) < 0 ) {
			delete out_bag;
		}
	}
#endif	
}
