/*****************************************************************************\
 *  prefix_courier.cpp - message packager for length-prefixed messages.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include <stdio.h>
#include <stdlib.h>

extern "C" {
# include "src/common/log.h"
# include "src/common/xassert.h"	
}

#include "prefix_courier.h"


// ************************************************************************
//  TAG(                             receive                             ) 
// ************************************************************************
mailbag_t *
prefix_courier_t::receive( void )
{
	char header[ 10 ];    
	uint32_t size;
	char *buf;
	mailbag_t *bag;

	// * Read the packet size.
	if ( courier_t::read_bytes( header, 9 ) != 9 ) {
		if ( errno != 0 )
			debug( "prefix_courier::receive: malformed header (wire)" );
		return NULL;
	}

	// * Convert to binary.
	if ( sscanf( header, "%ul", &size ) != 1 ) {
		if ( errno != 0 )
			debug( "prefix_courier::receive: malformed header (decode)" );
		return NULL;
	}

	// * Allocate a buffer
	buf = new char [ size ];
	xassert( buf );

	// * Read the actual packet data.
	if ( courier_t::read_bytes( buf, size ) != size ) {
		debug( "prefix_courier::receive: unable to receive fixed-length data" );
		delete [] buf;
		return NULL;
	}

	// * Invoke the factory to return the proper concrete object.
	bag = m_factory->mailbag( buf, size );
	delete [] buf;
	return bag;
}


// **************************************************************************
//  TAG(                              send                              ) 
// **************************************************************************
int
prefix_courier_t::send( mailbag_t *bag )
{
	char header[ 10 ];
	size_t size = bag->text_length();

	// * Write the packet size.
	(void) sprintf( header, "%08ul\n", size );
	if ( write_bytes( header, 9 ) != 9 ) {
		debug( "prefix_courier::send: unable to send fixed-length data" );
		return 0;
	}

	// *
	// Write the mailbag contents.  The mailbag is deleted in this
	// call if it succeeds.
	// *
	return courier_t::send( bag );
}
