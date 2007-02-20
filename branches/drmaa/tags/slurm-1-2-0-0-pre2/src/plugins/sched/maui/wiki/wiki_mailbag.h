/*****************************************************************************\
 *  wiki_mailbag.h - Wiki implementation of mailbag_t.
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

#ifndef __WIKI_MAILBAG_H__
#define __WIKI_MAILBAG_H__

extern "C" {
# include "src/common/log.h"
}

#include "../mailbag.h"
#include "wiki_message.h"
#include "../dstring.h"

// **************************************************************************
//  TAG(                         wiki_mailbag_t                         ) 
//  
// The Wiki implementation of the mailbag.  There's only one message per
// bag in the Wiki world, so a lot of this is overkill.
// **************************************************************************
class wiki_mailbag_t : public mailbag_t
{
protected:
	dstring_t			m_str;
    
public:

	wiki_mailbag_t() :
		mailbag_t(),
		m_str()
	{
	}
    
	wiki_mailbag_t( char *buf, size_t len ) :
		m_str( buf, len )
	{
		debug3( "wiki_mailbag: created with contents (%s)", m_str.s() );
	}

	// **************************************************************
	//  TAG(            wiki_mailbag_iterator_t                     ) 
	//
	// The (largely useless) iterator for the Wiki mailbag.
	// There's only one message in a Wiki mailbag.
	// *************************************************************
	class wiki_mailbag_iterator_t : public mailbag_iterator_t
	{
	public:
		friend class wiki_mailbag_t;
		
	protected:
		wiki_mailbag_t   	*m_bag;	 // The mailbag.
		char 			*m_pos;	 // Position in mailbag buffer.

	public:
		wiki_mailbag_iterator_t( mailbag_t *bag ) :
			mailbag_iterator_t( bag ),
			m_pos( NULL )
		{
			m_bag = (wiki_mailbag_t *) bag;
		}

		void first( void )
		{
			// *
			// m_pos has only three normal values.  It's
			// either NULL, meaning it's uninitialized.
			// Or it's a pointer to the text of the
			// mailbag.  Or it's (char *) -1, meaning it's
			// gone off the end.  This last value is so we
			// can distinguish between the uninitialized
			// and exhausted states of the iterator, if
			// that's important.
			// *
			m_pos = (char *) m_bag->text();
		}

		void next( void )
		{
			// *
			// If it's NULL, keep it NULL because it's not
			// initialized.
			// *
			
			if ( m_pos == m_bag->text() )
				m_pos = (char *) -1;
		}

		bool at_end( void )
		{
			// *
			// This interprets at_end() strictly.  It
			// returns true only if the iterator has truly
			// gone off the end, not merely if the
			// iterator is not currently dereferenceable.
			// This may pose problems.
			// *
			return ( m_pos == (char *) -1 );
		}

		void *current( void )
		{
			// *
			// It will either be a valid pointer, or NULL,
			// or the sentinel.  Anything but the sentinel
			// is a valid return value.
			// *
			if ( m_pos != (char *) -1 ) return m_pos;
		}

		int type( void );
	};

	// * wiki_mailbag_t resumes ...
	
	mailbag_iterator_t *iterator( void )
	{
		return new wiki_mailbag_iterator_t( this );
	}

	message_t *message( mailbag_iterator_t *it );
	
	bool is_full( void ) const
	{
		return m_num_items >= 1;
	}
    
	int add( message_t *msg );
	char *text( void ) { return m_str.s(); }
	size_t text_length( void ) { return m_str.length(); }
};



// ************************************************************************
//  TAG(                     wiki_mailbag_factory_t                     ) 
// ************************************************************************
class wiki_mailbag_factory_t : public mailbag_factory_t
{
public:

	mailbag_t *mailbag( void *data, size_t len ) const
	{
		return new wiki_mailbag_t( (char *) data, len );
	}

	mailbag_t *mailbag( void ) const
	{
		return new wiki_mailbag_t;
	}

	const int num_message_types( void ) const
	{
		return wiki_message_t::NUM_MESSAGE_TYPES;
	}

};


#endif /*__WIKI_MAILBAG_H__*/
