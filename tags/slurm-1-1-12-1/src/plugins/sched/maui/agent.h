/*****************************************************************************\
 *  agent.h - manages a single connection-oriented session.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
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

#ifndef __SLURM_PLUGIN_AGENT_H__
#define __SLURM_PLUGIN_AGENT_H__

#include <pthread.h>

#include "receptionist.h"
#include "courier.h"
#include "message.h"

// **************************************************************************
//  TAG(                             agent_t                             ) 
//
// Abstractly encapsulates the process of receiving over a network
// connection messages that indicate work should be done, then
// responding with a message to inform the sender how the work went.
// An agent_t is created by a receptionist_t that waits on incoming
// connection requests at a single address, one agent_t for each
// connection.  The agent_t spawns a thread to handle the transaction
// on this connection, leaving the receptionist to handle more
// incoming connections.
//
// A courier_t (see courier.h) unpacks the "payload" from its
// transport-specific packaging (e.g., HTTP) and produces a mailbag_t
// (see mailbag.h> that contains the logical contents of the payload
// divided into one or more message_t objects.  By iterating over the
// mailbag_t using its generalized iterator, the agent_t calls the
// action() method of each message in turn and arranges for any reply
// to be transmitted.
//
// The courier_t accumulates these replies in a return mailbag_t and
// transmits them as dictated by the mailbag_t's policy.
//
// **************************************************************************
class agent_t
{
protected:
	pthread_t		m_thread;
	const receptionist_t	*m_recep;
	courier_t		*m_courier;
	const mailbag_factory_t	* const m_mailbag_factory;
    
public:

	// **************************************************************
	//  TAG(                    agent_t                             ) 
	// **************************************************************
	// DESCRIPTION
	// Constructor.
	//  
	// ARGUMENTS
	// recep (in) - The parent receptionist.
	// courier (in, owns) - The specialized courier for this type
	//		of delivery.
	// num_types (in) - The number of message types (and therefore
	//		the number of message factories) this agent must
	//		deal with.
	//  
	// ASSUMPTIONS
	// None.
	//  
	// SIDE EFFECTS
	// Throws string literals as exceptions for improper arguments.
	//  
	// **************************************************************
	agent_t( const receptionist_t *recep,
		 courier_t *courier,
		 const mailbag_factory_t * const mailbag_factory );

	~agent_t();

	// **************************************************************
	//  TAG(                     start                              ) 
	// **************************************************************
	// DESCRIPTION
	// Instruct the agent to begin receiving and processing
	// communications.  This is explicitly a separate step so that
	// message factories can be set up prior to accepting intput.
	// This method returns immediately.
	//  
	// ARGUMENTS
	// None.
	//  
	// RETURNS
	// Zero if the processing thread was started successfully,
	// non-zero otherwise.
	//  
	// ASSUMPTIONS
	// None.
	//  
	// SIDE EFFECTS
	// A new thread is started.
	//  
	// **************************************************************
	int start( void );

protected:
	static void *thread_entry( void *me );
	void thread_main( void );

private:
	void spin( void );
};

#endif /*__SLURM_PLUGIN_AGENT_H__*/
