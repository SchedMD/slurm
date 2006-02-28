/*****************************************************************************\
 *  dstring.h - yet another dynamic string class.
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

#ifndef __SLURM_DYNAMIC_STRING_H__
#define __SLURM_DYNAMIC_STRING_H__

#include <stdio.h>
#include <stdlib.h>

// **************************************************************************
//  TAG(                            dstring_t                            ) 
//  
// Yet Another Dynamic String.  Mostly an abstraction of reallocation to
// grow the string as needed.  This will probably go away in favor of
// growable C strings from the SLURM library.
// **************************************************************************
class dstring_t
{
protected:
	char				*m_buf;
	size_t				m_population;
	size_t				m_allocated;

	static const size_t	       	ALLOCATION_INCREMENT = 64;
    
public:

	dstring_t();
	dstring_t( const char *str );
	dstring_t( const char *str, const size_t len );
	dstring_t( const dstring_t &str );
	dstring_t( const size_t len );

	~dstring_t()
	{
		free( m_buf );
	}


	// **************************************************************
	//  TAG(                           s                           ) 
	// **************************************************************
	// DESCRIPTION
	// Produce a version of the string compatible with the C library.
	//  
	// ARGUMENTS
	// None.
	//  
	// RETURNS
	// Pointer to a non-modifiable version of the string.
	//  
	// ASSUMPTIONS
	// No embedded ASCII NUL characters.
	// **************************************************************
	char * const s( void )
	{
		m_buf[ m_population ] = 0;
		return m_buf;
	}

	
	// **************************************************************
	//  TAG(                           c                           ) 
	// **************************************************************
	// DESCRIPTION
	// Access a character in the string.
	//  
	// ARGUMENTS
	// idx (in) - Zero-based index of the character in the string.
	//  
	// RETURNS
	// The character at the given position in the string.
	//  
	// ASSUMPTIONS
	// idx is not tested for boundaries.
	//  
	// **************************************************************
	const char c( const u_int32_t idx ) const
	{
		return m_buf[ idx ];
	}	

	// **************************************************************
	//  TAG(                      operator =                      ) 
	// **************************************************************
	// DESCRIPTION
	// Assign the value of a string to the receiver.
	//  
	// ARGUMENTS
	// str (in) - the string from which the assignment is made.
	//  
	// RETURNS
	// The newly-assigned string, so as to be compatible with
	// assignments in C/C++ expressions.
	//
	// SIDE EFFECTS
	// The existing contents of the receiver, if any, are deallocated
	// and lost.
	// **************************************************************
	dstring_t & operator = ( const dstring_t &str );

	
	// **************************************************************
	//  TAG(                        append                        ) 
	// **************************************************************
	// DESCRIPTION
	// Append one string to the receiver.
	//  
	// ARGUMENTS
	// str (in) - the string to append.
	// len (in, optional) - the length of the string to append, if
	//		known.
	//  
	// RETURNS
	// None.
	//  
	// **************************************************************
	void append( const dstring_t &str );
	void operator += ( const dstring_t &str )
	{
		append( str );
	}
	
	void append( const char * const str );
	void append( const char * const str, const size_t len );
	void operator += ( const char * const str )
	{
		append( str );
	}

	
	// **************************************************************
	//  TAG(                        append                        ) 
	// **************************************************************
	// DESCRIPTION
	// Append a text version of a numerical argument to the receiver.
	//  
	// ARGUMENTS
	// val (in) - The value to convert to ASCII and append.  Options
	//		are provided for signed and unsigned values.
	// fmt (in, optional) - the printf() format to use for converting
	//		val to ASCII.  Avoid lengthy formats.
	//  
	// RETURNS
	// None.
	//  
	// **************************************************************
	void append( const u_int32_t val, const char *fmt = NULL )
	{
		char buf[ 32 ];
		sprintf( buf, fmt ? fmt : "%lu", val );
		append( buf );
	}

	void append( const int32_t val, const char *fmt = NULL )
	{
		char buf[ 32 ];
		sprintf( buf, fmt ? fmt : "%ld", val );
		append( buf );
	}

	void append( const long int val, const char *fmt = NULL )
	{
		char buf[ 64 ];
		sprintf( buf, fmt ? fmt : "%ld", val );
		append( buf );
	}

	void append( const unsigned long int val, const char *fmt = NULL )
	{
		char buf[ 64 ];
		sprintf( buf, fmt ? fmt : "%lu", val );
		append( buf );
	}
	
	void operator += ( const u_int32_t val )
	{
		append( val );
	}
	void operator += ( const int32_t val )
	{
		append( val );
	}
	void operator += ( const long int val )
	{
		append( val );
	}
	void operator += ( const unsigned long int val )
	{
		append( val );
	}
	const size_t length( void ) const
	{
		return m_population;
	}
};


#endif /*__SLURM_DYNAMIC_STRING_H__*/
