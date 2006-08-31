/*****************************************************************************\
 *  wiki_mailbag.cpp - logical message holder for Wiki messages.
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
#include "wiki_mailbag.h"
#include "wiki_message.h"


// **************************************************************************
//  TAG(                             message                             ) 
// **************************************************************************
message_t *
wiki_mailbag_t::message( mailbag_iterator_t *base_it )
{
	char *cmd, *text;
	size_t text_len;
	wiki_mailbag_iterator_t *it = (wiki_mailbag_iterator_t *) base_it;
	static const char marker[] = "CMD=";
    
	// * Verify that the iterator refers to this instance.
	if ( it->m_bag != this ) {
		debug3( "Wiki mailbag: misdirected iterator" );
		return NULL;
	}

	// *
	// Now we can peek into the iterator to find out what we
	// need to know.
	// *
	if ( it->m_pos == NULL ) {
		debug3( "Wiki mailbag: malformed iterator" );		
		return NULL;
	}
	text_len = m_str.length();

	// * Find the start of the command signalled by "CMD=".
	if ( ( text = strstr( it->m_pos, marker ) ) == NULL ) {
		debug( "Wiki mailbag: can't find start of payload" );
		return NULL;
	}
	cmd = text + strlen( marker );
	text_len -= (text - it->m_pos);

	// *
	// Can't do STATUS because it doesn't have a CMD= prefix.
	// Even though we won't receive any, there's nothing in the
	// message API that says you can't call this on an outgoing
	// mailbox.
	// *
	if ( strncmp( cmd, "GETNODES", 8 ) == 0 ) {
		return new wiki_getnodes_t( text, text_len );
	} else if ( strncmp( cmd, "GETJOBS", 7 ) == 0 ) {
		return new wiki_getjobs_t( text, text_len );
	} else if ( strncmp( cmd, "STARTJOB", 8 ) == 0 ) {
		return new wiki_startjob_t( text, text_len );
	} else if ( strncmp( cmd, "CANCELJOB", 9 ) == 0 ) {
		return new wiki_canceljob_t( text, text_len );
	} else {
		return NULL;
	}
}
	

// **************************************************************************
//  TAG(                               add                               ) 
// **************************************************************************
int wiki_mailbag_t::add( message_t *msg )
{
	// * Wiki mailbags can only hold one message.
	if ( m_num_items >= 1 ) {
		return -1;
	}

	// * Add the message text and then get rid of the message.
	m_str.append( msg->text(), msg->text_length() );
	delete msg;
	m_num_items++;
	return 0;
}

