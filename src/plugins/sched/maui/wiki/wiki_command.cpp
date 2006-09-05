/*****************************************************************************\
 *  wiki_command.cpp - generic Wiki command (as opposed to request for info).
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

#include "wiki_message.h"

wiki_command_t::wiki_command_t( char *data, size_t len, int type ) :
	wiki_message_t( data, len, type )
{
	ListIterator j;
	wiki_relation_t *relation;
	char *node;
	
	// * Skip over "CMD=STARTJOB".
	m_arg_iterator = list_iterator_create( m_expr.relations() );
	(void) list_next( m_arg_iterator );

	// * Get the job ID.
	relation = (wiki_relation_t *) list_next( m_arg_iterator );
	if ( relation == NULL )
		throw "malformed command (missing argument)";
	if ( strcmp( relation->name(), "ARG" ) != 0 )
		throw "malformed command (expected ARG=<job>)";
	j = list_iterator_create( relation->values() );
	m_jobid = (char *) list_next( j );
	if ( m_jobid == NULL )
		throw "missing job ID";
	list_iterator_destroy( j );
}


