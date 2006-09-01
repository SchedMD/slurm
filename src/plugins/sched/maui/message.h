/*****************************************************************************\
 *  message.h - statement in a scheduler conversation.
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
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifndef __SLURM_PLUGIN_MESSAGE_H__
#define __SLURM_PLUGIN_MESSAGE_H__

#include <stdio.h>

extern "C" {
#  include "src/common/log.h"
}

// **************************************************************************
//  TAG(                            message_t                            ) 
//  
// This is the atomic representation of a directive or missive to/from
// the scheduler.  You receive a sequence of these from the wire, and
// you place a sequence of these back on the wire in response.  The
// mapping of input data to any internal representation (i.e., textual
// message parsing) is done in the constructor.  The action() method
// carries out the operations suggested by the message contents and
// optionally produces a return message in response.  The action()
// "script" of such a reply should return NULL, as in the default
// below.  Message types are simple integers and are
// implementation-specific.
// **************************************************************************
class message_t
{
protected:
	int					m_type;
    
public:
	message_t( int type ) : m_type( type )
	{
	}

	virtual ~message_t()
	{
	}
	
	int type( void ) { return m_type; }
    

	// **************************************************************
	//  TAG(                     action                             ) 
	// **************************************************************
	// DESCRIPTION
	// Carry out the actions suggested by the contents of the message.
	//  
	// ARGUMENTS
	// None.
	//  
	// RETURNS
	// A message to send back to whoever sent the command, or NULL
	// if no response is warranted.  The caller owns the message
	// created by this method.
	//  
	// ASSUMPTIONS
	// None.
	//  
	// SIDE EFFECTS
	// Many and varied; that's the point.
	//  
	// **************************************************************
	virtual message_t *action( void )
	{
		debug3( "message::action: default message action attempted" );
		return NULL;
	}


	// **************************************************************
	//  TAG(                      text                              ) 
	// **************************************************************
	// DESCRIPTION
	// Produce a "textual" representation of this message.  "Text"
	// does not necessarily mean human-readable text, but rather a
	// representation of how this portion of the enclosing mailbag
	// might look when its text() method is called.
	//  
	// ARGUMENTS
	// None.
	//  
	// RETURNS
	// Pointer to the text representation of the message.
	//  
	// **************************************************************
	virtual char *text( void ) = 0;

    
	// **************************************************************
	//  TAG(                  text_length                           ) 
	// **************************************************************
	// DESCRIPTION
	// Give the size of the textual representation produced by text().
	//  
	// ARGUMENTS
	// None.
	//  
	// RETURNS
	// Number of bytes pointed to by text().
	//  
	// **************************************************************
	virtual size_t text_length( void ) = 0;
};


#endif /*__SLURM_PLUGIN_MESSAGE_H__*/
