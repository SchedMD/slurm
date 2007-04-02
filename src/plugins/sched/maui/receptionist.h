/*****************************************************************************\
 *  receptionist.h - connection manager for passive scheduler plugins.
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

#ifndef __SLURM_PLUGIN_RECEPTIONIST_H__
#define __SLURM_PLUGIN_RECEPTIONIST_H__

#include <netinet/in.h>

#include "courier.h"

// **************************************************************************
//  TAG(                         receptionist_t                         ) 
//  
// Objects of this class listen on the given socket and spawn agent_t objects
// to deal with the actual connections.  You need only one of these objects
// per incoming socket.
//
// The courier_factor_t component produces an appropriate courier which the
// agent uses to speak the appropriate packaging protocol on the wire.
//
// The mailbag_factory_t component produces mailbags.  The receptionist does
// not use this directly, but a mailbag_factory must be supplied to the
// courier, and hence to the courier's factory.  The courier
// knows how to frame a logical message for transport over the wire.
// **************************************************************************
class receptionist_t
{
protected:
    const courier_factory_t    		* const m_courier_factory;
    const mailbag_factory_t		* const m_mailbag_factory;
    const struct sockaddr_in		*m_addr;
    int					m_sock;
    
public:

    // ************************************************************************
    //  TAG(                         receptionist_t                         ) 
    // ************************************************************************
    // DESCRIPTION
    // Create a receptionist to listen on the socket.
    //  
    // ARGUMENTS
    // courier_factory (in, owns) - An appropriate factory for the type of
    //		courier needed to frame/unframe a set of messages according
    //		to the protocol spoken on this wire.
    // mailbag_factory_t (in, owns) - An appropriate factory for the type of
    //		content encodings arriving on this wire.
    //  
    // SIDE EFFECTS
    // None.
    //  
    // ************************************************************************
    receptionist_t( const courier_factory_t * const courier_factory,
		    const mailbag_factory_t * const mailbag_factory,
		    const struct sockaddr_in *addr );

    ~receptionist_t()
    {
	(void) close( m_sock );
	delete m_courier_factory;
    }


    // ************************************************************************
    //  TAG(                             listen                             ) 
    // ************************************************************************
    // DESCRIPTION
    // Begin listening on the connection.
    //  
    // ARGUMENTS
    // None.
    //  
    // RETURNS
    // SLURM return code.
    //  
    // ************************************************************************
    virtual int listen( void );
};

#endif /*__SLURM_PLUGIN_RECEPTIONIST_H__*/
