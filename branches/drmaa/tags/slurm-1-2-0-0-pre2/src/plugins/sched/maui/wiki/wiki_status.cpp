/*****************************************************************************\
 *  wiki_status.cpp - return a status message to the Wiki scheduler.
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

#include <string.h>
#include <unistd.h>
#include <pwd.h>

extern "C" {
# include "src/common/log.h"
# include "src/slurmctld/sched_plugin.h"	
}

#include "wiki_message.h"


// **************************************************************************
//  TAG(                          wiki_status_t                          ) 
// **************************************************************************
wiki_status_t::wiki_status_t( int status, char * const msg ) :
	wiki_message_t( NULL, 0, wiki_message_t::STATUS )
{
	m_str += "TS=";
	m_str += (u_int32_t) time( NULL );
	m_str += " AUTH=";
	m_str += get_user_name();
	m_str += " DT=SC=";
	m_str += status;
	if ( msg ) {
		m_str += " RESPONSE=";
		m_str += msg;
	}

	debug3( "Wiki plugin status = \"%s\"", m_str.s() );
}

// **************************************************************
//  TAG(                 prefix_with_checksum                 ) 
// **************************************************************
// DESCRIPTION
// Prefixes the current string representation of the response
// with the Wiki checksum.
//  
// ARGUMENTS
// key (in) - the DES key used to create the checksum.
//  
// RETURNS
// None.
//  
// ASSUMPTIONS
// The current response string starts with "TS=" and is the
// otherwise fully-formatted reply.
//  
// SIDE EFFECTS
// Replaces the receiver's string; any cached C versions will
// be dangling pointers.
//  
// **************************************************************
void
wiki_status_t::prefix_with_checksum( void )
{
	dstring_t sum;
	const char * const key = sched_get_auth();

	checksum( sum, key );
	sum += " ";
	sum += m_str;
	m_str = sum;
}


// **************************************************************
//  TAG(                          des                          ) 
// **************************************************************
// DESCRIPTION
// Compute a DES digest for a CRC according to a particular
// key.
//  
// ARGUMENTS
// lword (in/out) - The CRC to encode, which becomes the first
//		lexical segment of the checksum.
// irword (in/out ) - The key with which to encode the CRC,
//		which becomes the second lexical segment of
//		the checksum.
//  
// RETURNS
// None.
//
// SOURCE
// Cluster Resources, Inc., no rights reserved.
//
// **************************************************************
void
wiki_status_t::des( u_int32_t *lword, u_int32_t *irword ) const
{
	int idx;
	u_int32_t ia, ib, iswap, itmph, itmpl;

	static const int MAX_ITERATION = 4;
	static uint32_t c1[ MAX_ITERATION ] = {
		0xcba4e531,
		0x537158eb,
		0x145cdc3c,
		0x0d3fdeb2
	};
	static uint32_t c2[ MAX_ITERATION ] = {
		0x12be4590,
		0xab54ce58,
		0x6954c7a6,
		0x15a2ca46
	};

	itmph = 0;
	itmpl = 0;

	for ( idx = 0; idx < MAX_ITERATION; ++idx ) {
		iswap = *irword;
		ia = iswap ^ c1[ idx ];
		itmpl = ia & 0xffff;
		itmph = ia >> 16;
		ib = itmpl * itmpl + ~( itmph * itmph );
		ia = (ib >> 16) | ( (ib & 0xffff) << 16 );
		*irword = (*lword) ^ ( (ia ^c2[ idx ]) + (itmpl * itmph) );
		*lword = iswap;
	}
}


// **************************************************************
//  TAG(                      compute_crc                      ) 
// **************************************************************
// DESCRIPTION
// Compute a cyclic redundancy check (CRC) character-wise.
//  
// ARGUMENTS
// crc (in) - The CRC computed thus far.
// onech (in) - The character to be added to the CRC.
//  
// RETURNS
// The new CRC value.
//
// SOURCE
// Cluster Resources, Inc., no rights reserved.
//
// **************************************************************
const u_int16_t
wiki_status_t::compute_crc( u_int16_t crc, u_int8_t onech ) const
{
	int idx;
	u_int32_t ans  = ( crc ^ onech << 8 );

	for ( idx = 0; idx < 8; ++idx ) {
		if ( ans & 0x8000 ) {
			ans = (ans <<= 1) ^ 4129;
		} else {
			ans <<= 1;
		}
	}

	return ans;
}


// **************************************************************
//  TAG(                       checksum                       ) 
// **************************************************************
// DESCRIPTION
// Compute a Wiki checksum for the current message contents
// and return the result as a Wiki name-value pair.
//  
// ARGUMENTS
// sum (out) - The dynamic string in which to store the
// 		resulting checksum.
// key(in) - The seed value for the checksum.  This must be
//		coordinated with the scheduler so that they
//		both use the same value.  It is a string of
//		ASCII decimal digits.
//  
// RETURNS
// None.
//  
// **************************************************************
void
wiki_status_t::checksum( dstring_t &sum, const char * const key )
{
	u_int32_t crc = 0;
	u_int32_t lword, irword;
	int idx;
	u_int32_t seed = (u_int32_t) strtol( key, NULL, 0 );

	for ( idx = 0; idx < m_str.length(); ++idx ) {
		crc = (u_int32_t) compute_crc( crc, m_str.c( idx ) );
	}

	lword = crc;
	irword = seed;

	des( &lword, &irword );

	sum = "CK=";
	sum.append( lword, "%08x" );
	sum.append( irword, "%08x" );
}


// **************************************************************
//  TAG(                     get_user_name                     ) 
// **************************************************************
// DESCRIPTION
// Retrieves the system's idea of the user name under which
// the controller is running.
//  
// ARGUMENTS
// None.
//  
// RETURNS
// A pointer to a possibly static string containing the user
// name.  It is not owned by the caller.
//  
// ASSUMPTIONS
// Not know to be thread-safe.
//  
// **************************************************************
const char *
wiki_status_t::get_user_name( void ) const
{
	struct passwd *pw;

	pw = getpwuid( getuid() );
	if ( ! pw ) return NULL;
	return pw->pw_name;
}
