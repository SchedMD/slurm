/*****************************************************************************\
 *  wiki_parser.h - parse Wiki selection expressions.
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

#ifndef __WIKI_PARSER_H__
#define __WIKI_PARSER_H__

extern "C" {
#   include "src/common/list.h"
}

// **************************************************************************
//  TAG(                        wiki_expression_t                        ) 
//  
// A Wiki command line, of the form
//
//	NAME1=VAL[:VAL]... [NAME2=VAL[:VAL]...] ...
//
// Each space-delimited element of this input line is "relation" represented
// by the wiki_relation_t object below.
// **************************************************************************
class wiki_expression_t
{
protected:
	List			m_relations;
	char			*m_raw;

public:

	// **************************************************************
	//  TAG(               wiki_expression_t                        ) 
	// **************************************************************
	// DESCRIPTION
	// Parse the input line.
	//  
	// ARGUMENTS
	// data (in) - the input data as it came off the wire.  It is
	//		copied here so that it can be modified in place,
	//		if necessary, by the parser.
	// len (in) - length of input data.
	//  
	// **************************************************************
	wiki_expression_t( char *data, size_t len );
    
	~wiki_expression_t()
	{
		list_destroy( m_relations );
		if ( m_raw ) delete [] m_raw;
	}

	// **************************************************************
	//  TAG(                       relations                       ) 
	// **************************************************************
	// DESCRIPTION
	// Produce a list of wiki_relation_t objects that pertain to
	// this Wiki expression.
	//  
	// ARGUMENTS
	// None.
	//  
	// RETURNS
	// <List> of wiki_relation_t objects.  The list is owned by
	// this object and should not be modified by the caller.
	//  
	// **************************************************************
	List relations( void ) { return m_relations; }
};


    
// **************************************************************************
//  TAG(                         wiki_relation_t                         ) 
//  
// The portion of the Wiki command line that looks like
//
//	NAME=VAL[:VAL]...
//
// **************************************************************************
class wiki_relation_t
{
public:
	friend class wiki_expression_t;
    
protected:
	List			m_values;
	char			*m_raw;
	char			*m_name;

public:
    
	// **************************************************************
	//  TAG(                wiki_relation_t                         ) 
	// **************************************************************
	// DESCRIPTION
	// Parse a Wiki relation.
	//  
	// ARGUMENTS
	// relstr (in) - The pointer to the Wiki relation.  This is a
	//		pointer to a substring in a copy of the original
	//		data; the relation object may modify it, but users
	//		of the object shouldn't.
	//  
	// **************************************************************
	wiki_relation_t( char *relstr );
    
	~wiki_relation_t()
	{
		list_destroy( m_values );
	}

	// *
	// Accessors.  name() returns the name in the name-value(s)
	// pair expressed in this relation.  values() returns a <List>
	// of the values.
	// *
	const char * const name( void ) { return m_name; }
	List values( void ) { return m_values; }

private:

	// *
	// A static destructor.  Passed to the list_create() function
	// as a non-C++ way of getting rid of relations when the
	// expression to which they belong is destroyed.
	// *
	static void dtor( void *doomed );
};

#endif /*__WIKI_PARSER_H__*/
