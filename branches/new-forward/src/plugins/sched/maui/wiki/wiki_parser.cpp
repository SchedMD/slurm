/*****************************************************************************\
 *  wiki_parser.cpp - parse resource-matching expressions from Wiki msg.
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

#include <stdio.h>
#include <string.h>
#include "wiki_parser.h"

// **************************************************************************
//  TAG(                        wiki_expression_t                        ) 
// **************************************************************************
wiki_expression_t::wiki_expression_t( char *data, size_t len )
{
	char *tail;
	char *p;

	m_relations = list_create( wiki_relation_t::dtor );

	// * Make a copy of the string so we can modify it.
	if ( ( data != NULL ) && ( len > 0 ) ) {
		m_raw = new char[ len + 1 ];
		strncpy( m_raw, data, len );
		m_raw[ len ] = 0;
	} else {
		m_raw = NULL;
		return;
	}


	// * Find the space-delimited relations.
	p = tail = m_raw;
	while ( 1 ) {
		if ( *p == 0 ) {
			(void) list_append( m_relations,
					    new wiki_relation_t( tail ) );
			break;
		}
		if ( *p == ' ' ) {
			*p = 0;
			(void) list_append( m_relations,
					    new wiki_relation_t( tail ) );
			tail = p + 1;
		}
		++p;
	}
}

// **************************************************************************
//  TAG(                         wiki_relation_t                         ) 
// **************************************************************************
wiki_relation_t::wiki_relation_t( char *relstr )
{
	char *tail;
	char *p;

	// *
	// The input string is (or should be) a copy of the original string
	// obtained off the wire.  We presume we can modify it.
	// *
	m_raw = relstr;
    
	m_values = list_create( NULL );

	// *
	// Find the "=".  If found, remember where it was and then
	// null-terminate that prefix so we can use it as the <name>
	// in a relation.
	// *
	m_name = NULL;
	for ( tail = m_raw; *tail; ++tail ) {
		if ( *tail == '=' ) {
			*tail = 0;
			++tail;
			m_name = m_raw;
			break;
		}
	}
	if ( m_name == NULL ) throw "malformed Wiki expression";

	// *
	// Now iteratively look for the elements in the tail.
	// *
	p = tail;
	while ( 1 ) {
		if ( *p == 0 ) {
			(void) list_append( m_values, tail );
			break;
		}
		if ( *p == ':' ) {
			*p = 0;
			(void) list_append( m_values, tail );
			tail = p + 1;
		}
		++p;
	}
}


// **************************************************************************
//  TAG(                      wiki_relation_t::dtor                      ) 
// **************************************************************************
void
wiki_relation_t::dtor( void *doomed )
{
	delete (wiki_relation_t *) doomed;
}
