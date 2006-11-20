/*****************************************************************************\
 *  dstring.cpp - dynamically growable string.
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

#include <string.h>
#include <stdlib.h>

#include "dstring.h"

dstring_t::dstring_t()
{
	m_allocated = ALLOCATION_INCREMENT;
	m_population = 0;
	m_buf = (char *) malloc( m_allocated );
	if ( ! m_buf ) throw "dstring_t: unable to allocate";
}

dstring_t::dstring_t( const char *str )
{
	m_population = strlen( str );
	m_allocated = m_population + 1;
	m_buf = (char *) malloc( m_allocated );
	if ( ! m_buf ) throw "dstring_t: unable to allocate";
	strncpy( m_buf, str, m_population );
}


dstring_t::dstring_t( const char *str, const size_t len )
{
	m_population = len;
	m_allocated = len + ALLOCATION_INCREMENT;
	m_buf = (char *) malloc( m_allocated );
	if ( ! m_buf ) throw "dstring_t: cannot allocate";
	memcpy( m_buf, str, len );
}


dstring_t::dstring_t( const dstring_t &str )
{
	m_allocated = str.m_allocated;
	m_buf = (char *) malloc( m_allocated );
	m_population = str.m_population;
	strncpy( m_buf, str.m_buf, m_population );
}


dstring_t::dstring_t( const size_t len )
{
	m_allocated = len;
	m_population = 0;
	m_buf = (char *) malloc( m_allocated );
	if ( ! m_buf ) throw "dstring_t: cannot allocate";
}


void
dstring_t::append( const dstring_t &str )
{
	size_t bigger_size = m_population + str.m_population + 1;

	if ( bigger_size > m_allocated ) {
		m_buf = (char *)
			realloc( m_buf, bigger_size + ALLOCATION_INCREMENT );
		m_allocated = bigger_size;
	}
	memcpy( &m_buf[ m_population ], str.m_buf, str.m_population );
	m_population += str.m_population;
}

dstring_t &
dstring_t::operator = ( const dstring_t &str )
{
	free( m_buf );
	m_buf = (char *) malloc( str.m_allocated );
	memcpy( m_buf, str.m_buf, str.m_population );
	m_allocated = str.m_allocated;
	m_population = str.m_population;
	return *this;
}

void
dstring_t::append( const char * const str, size_t len )
{
	if ( ( m_population + len ) >= m_allocated ) {
		m_allocated += (len + ALLOCATION_INCREMENT);
		m_buf = (char *) realloc( m_buf, m_allocated );
	}
	strncpy( &m_buf[ m_population ], str, len );
	m_population += len;
}


void
dstring_t::append( const char *const str )
{
	append( str, strlen( str ) );
}
