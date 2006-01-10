/*****************************************************************************\
 *  courier.h - general wire-protocol driver for messages.
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifndef __SLURM_PLUGIN_COURIER_H__
#define __SLURM_PLUGIN_COURIER_H__

#include <unistd.h>

extern "C" {
# include "src/common/log.h"
}

// * Forward declarations.
class mailbag_t;
class mailbag_factory_t;


// **************************************************************************
//  TAG(                            courier_t                            ) 
//  
// In the chain of data-handling objects, this one is closest to the actual
// wire, so it holds the file descriptor.  The courier delivers network
// data as a mailbag: the payload portion of each logical message with its
// packaging stripped.  It transmits mailbags after wrapping them in
// a transport-specific package.
//
// This class must be abstract because it is not possible to construct a
// generic mailbag in receive().
// **************************************************************************
class courier_t
{
protected:
	const int	       		m_fd;
	const mailbag_factory_t    	* const m_factory;

public:

	// **************************************************************
	//  TAG(                   courier_t                            ) 
	// **************************************************************
	// DESCRIPTION
	// Courier constructor.
	//  
	// ARGUMENTS
	// fd (in) - The file descriptor corresponding to the realized
	//		wire connection.
	// factory (in) - The mailbag factory which will create concrete
	//		mailbag objects given raw data from the packet.
	//		The caller retains ownership of the factory.
	//  
	// **************************************************************
	courier_t( const int fd, const mailbag_factory_t * const factory ) :
		m_fd( fd ),
		m_factory( factory )
	{
	}
    
	virtual ~courier_t()
	{
		(void) close( m_fd );
	}    

    
	// **************************************************************
	//  TAG(                   receive                             ) 
	// **************************************************************
	// DESCRIPTION
	// Reads data from the wire and produces a mailbag to
	// represent it.  This method *must* block if no data is
	// available, but will likely become available.	
	//  
	// ARGUMENTS
	// None.
	//  
	// RETURNS	
	// A mailbag corresponding to the payload portion of the
	// incoming data with the framing, if any, stripped away.  The
	// caller becomes the owner of the mailbag.  Returns NULL if
	// the state of the underlying connection is such that no
	// further mailbags will be delivered (e.g., the peer closed
	// the connection).
	//  
	// **************************************************************
	virtual mailbag_t *receive( void ) = 0;


    
	// **************************************************************
	//  TAG(                      send                              ) 
	// **************************************************************
	// DESCRIPTION
	// Send a mailbag to the remote peer after first wrapping it
	// in the appropriate framing or packaging.
	//  
	// ARGUMENTS
	// mailbag (in) - The mailbag to send.  The courier becomes the
	//		owner of the mailbag if the call succeeds,
	//		otherwise the caller retains ownership of the
	//		mailbag.
	//  
	// RETURNS
	// Zero if the mailbag was sent (or queued for sending, if
	// appropriate) successfully, and a negative number if it was
	// not.
	//  
	// **************************************************************
	virtual int send( mailbag_t *bag );

protected:
	size_t write_bytes( char * const buf, const size_t size );
	size_t read_bytes( char *buf, const size_t size );
};


// **************************************************************************
//  TAG(                        courier_factory_t                        ) 
//  
// Factory for couriers, chiefly used by the receptionist to configure
// the connection agent.
// **************************************************************************
class courier_factory_t
{
public:
	virtual courier_t *courier( int fd,
				    const mailbag_factory_t * const factory ) const = 0;
};

#endif /*__SLURM_PLUGIN_COURIER_H__*/
