/*****************************************************************************\
 *  courier.cpp - generalized message packager for wire protocols.
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

#include <stdlib.h>

#include "courier.h"
#include "mailbag.h"


// **************************************************************************
//  TAG(                              send                              ) 
//  
// The default implementation assumes no framing is needed and the text
// representation of the mailbag contents is suitable for the wire.
// **************************************************************************
int
courier_t::send( mailbag_t *bag )
{
	if ( write_bytes( bag->text(), bag->text_length() ) == 0 ) {
		debug( "courier_t::send: cannot write to output socket" );
		return -1;
	} else {
		delete bag;
		return 0;
	}
}

// *
// These following two functions really need to migrate themselves into
// the SLURM library, or equivalents for them already existing need to
// be used.
// *

// **************************************************************************
//  TAG(                           write_bytes                           ) 
// **************************************************************************
size_t
courier_t::write_bytes( char * const buf, const size_t size )
{
	ssize_t bytes_remaining, bytes_written;
	char *ptr;

	bytes_remaining = size;
	ptr = buf;
	while ( bytes_remaining > 0 ) {
		bytes_written = write( m_fd, ptr, size );
		if ( bytes_written < 0 ) return 0;
		bytes_remaining -= bytes_written;
		ptr += bytes_written;
	}
	return size;
}


// **************************************************************************
//  TAG(                           read_bytes                           ) 
// **************************************************************************
size_t
courier_t::read_bytes( char *buf, const size_t size )
{
	ssize_t bytes_remaining, bytes_read;
	char *ptr;

	bytes_remaining = size;
	ptr = buf;
	while ( bytes_remaining > 0 ) {
		bytes_read = read( m_fd, ptr, bytes_remaining );
		if ( bytes_read == 0 ) return 0;
		if ( bytes_read < 0 ) return 0;
		bytes_remaining -= bytes_read;
		ptr += bytes_read;
	}
	
	return size;
}
