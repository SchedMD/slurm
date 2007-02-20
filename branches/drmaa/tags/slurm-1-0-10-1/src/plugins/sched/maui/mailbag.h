/*****************************************************************************\
 *  mailbag.h - one or more messages as concatenated on the wire.
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

#ifndef __SLURM_PLUGIN_MAILBAG_H__
#define __SLURM_PLUGIN_MAILBAG_H__

#include <sys/types.h>

#include "message.h"

class mailbag_iterator_t;

// **************************************************************************
//  TAG(                            mailbag_t                            ) 
//  
// A mailbag is the representation of an incoming message after it arrives
// on the wire and after it is stripped of any framing apparatus.  It
// represents the format of an outgoing message before the framing apparatus
// is added.  Whether the mailbag contains more than one message is
// implementation-dependent.
// **************************************************************************
class mailbag_t
{
public:
	friend class mailbag_iterator_t;

protected:
	int			m_num_items;
    
public:

	mailbag_t()
	{
		m_num_items = 0;
	}

	// **************************************************************
	//  TAG(                   mailbag_t                            ) 
	// **************************************************************
	// DESCRIPTION
	// Construct a mailbag given a pointer to data obtained from the
	// wire.
	//  
	// ARGUMENTS
	// buf (in) - The data obtained from the wire.
	// len (in) - The number of bytes pointed to by buf.
	//  
	// **************************************************************
	mailbag_t( char *buf, size_t len )
	{
	}
    
	const int num_items( void ) const { return m_num_items; }
    
	// **************************************************************
	//  TAG(                    iterator                            ) 
	// **************************************************************
	// DESCRIPTION
	// Produce an interator suitable for iterating over this object.
	//  
	// ARGUMENTS
	// None.
	//  
	// RETURNS
	// An uninitialized iterator, or NULL if an error occurs.
	//  
	// *************************************************************
	virtual mailbag_iterator_t *iterator( void ) = 0;

	// **************************************************************
	//  TAG(                        message                        ) 
	// **************************************************************
	// DESCRIPTION
	// Message factory method.  Given an iterator over the mailbag,
	// deference the iterator in terms of a message
	//  
	// ARGUMENTS
	// it (in) - The iterator to the mailbag.  It is presumed that
	//		the iterator is defined over the receiver, but
	//		this does not necesarily have to be tested.
	//  
	// RETURNS
	// A message corresponding to the data at the current position
	// of the iterator, or NULL if the data is garbled, the iterator
	// is invalid, etc.
	//  
	// **************************************************************
	virtual message_t *message( mailbag_iterator_t *it ) = 0;
	
	// *************************************************************
	//  TAG(                   is_full                             ) 
	// *************************************************************
	// DESCRIPTION
	// Tells whether a call to add() would fail because there is
	// no more room left in the mailbag.
	//  
	// ARGUMENTS
	// None.
	//  
	// RETURNS
	// <bool> true if the mailbag is currently full.
	//  
	// *************************************************************
	virtual bool is_full( void ) const = 0;


	// *************************************************************
	//  TAG(                     add                               ) 
	// *************************************************************
	// DESCRIPTION
	// Add a message to the mailbag.
	//  
	// ARGUMENTS
	// msg (in/owns) - The message to add.  If the message is
	//		successfully added then the mailbag becomes
	//		the owner of the message.
	//  
	// RETURNS
	// Zero if the message was added successfully, and a negative number
	// otherwise.
	//  
	// *************************************************************
	virtual int add( message_t *msg ) = 0;


	// *************************************************************
	//  TAG(                     text                              ) 
	// *************************************************************
	// DESCRIPTION
	// Export the contents of the mailbag as text suitable for
	// transmission on the wire.  "Text" does not necessarily assume
	// human-readable text.
	//  
	// ARGUMENTS
	// None.
	//  
	// RETURNS
	// Const pointer to the wire representation of the contents of the
	// mailbag.
	//  
	// SIDE EFFECTS
	// The text() method may have the side effect of translating the
	// internal representation of the mailbag to a textual format and
	// this may be  computationally expensive.  The caller may cache
	// the results of this method, but the data accessed by this method
	// is not guaranteed to be consistent if messages are added to the
	// mailbag after the caching has occurred.
	//  
	// **************************************************************
	virtual char *text( void ) = 0;


	// **************************************************************
	//  TAG(                  text_length                           ) 
	// **************************************************************
	// DESCRIPTION
	// Give the length of the data pointed to by the text() method.
	//  
	// ARGUMENTS
	// None.
	//  
	// RETURNS
	// The length of the text representation exported by text().
	//  
	// ASSUMPTIONS	
	// The text() method may have the side effect of translating
	// the internal representation into textual form.  The value
	// returned by this method is expected to be consistent with
	// prior or subsequent calls to text(), provided that no
	// messages are added between the calls.  Thus this method and
	// text() may be called in either order, but any intervening
	// changes to the mailbag are allowed to render the results
	// inconsistent.
	//  
	// SIDE EFFECTS
	// None.
	//  
	// **************************************************************
	virtual size_t text_length( void ) = 0;
};




// **************************************************************************
//  TAG(                       mailbag_iterator_t                       ) 
//  
// An iterator over a mailbag.  For mailbags that contain, or can contain,
// multiple elements, this object successively accesses subsets of the
// mailbag raw data that can be used to construct individual messages.
// **************************************************************************
class mailbag_iterator_t
{
public:

	// *
	// The constructor is written here to take an argument because we
	// don't want to create orphaned iterators.  There is no meaningful
	// default constructor.
	// *
	mailbag_iterator_t( const mailbag_t *bag )
	{
	}
    

	// **************************************************************
	//  TAG(                     first                              ) 
	// **************************************************************
	// DESCRIPTION
	// Set the iterator to point at the first message in the mailbag.
	// This must be done explicitly before the iterator can be used.
	//  
	// ARGUMENTS
	// None.
	//  
	// RETURNS
	// None.
	//  
	// **************************************************************
	virtual void first( void ) = 0;

	// **************************************************************
	//  TAG(                      next                              ) 
	// **************************************************************
	// DESCRIPTION
	// Advance the iterator to the next message.
	//  
	// ARGUMENTS
	// None.
	//  
	// RETURNS
	// None.
	//  
	// **************************************************************
	virtual void next( void ) = 0;

	// **************************************************************
	//  TAG(                     at_end                             ) 
	// **************************************************************
	// DESCRIPTION
	// Determines whether the iterator has reached the end of the
	// mailbag.
	//  
	// ARGUMENTS
	// None.
	//  
	// RETURNS
	// <bool> true if a call to current() will return NULL because the
	// iterator has reached the end of the mailbag.
	//  
	// **************************************************************
	virtual bool at_end( void ) = 0;
	void operator ++( void ) { next(); }

	// **************************************************************
	//  TAG(                     current                            ) 
	// **************************************************************
	// DESCRIPTION
	// Dereferences the iterator to arrive at the subset of the data
	// that corresponds to the current message.
	//
	// ARGUMENTS
	// None.
	//  
	// RETURNS
	// An opaque pointer, expected to be meaningful to the caller.
	// The caller does not own the memory at the end of the
	// pointer -- it is managed by the iterator in cooperation
	// with the mailbag.  This pointer is acceptable as input to
	// the message factory functions.
	//
	// **************************************************************
	virtual void *current( void ) = 0;
};


// **************************************************************************
//  TAG(                        mailbag_factory_t                        ) 
//  
// Used by the concrete courier classes to create mailbags from packets which
// the courier has just unframed.  Mailbag implementations must provide
// one of these so that it can be installed in the courier.  This is so that
// the full cross-section of couriers and mailbags need not be supported
// statically as multiply-inherited classes.
// **************************************************************************
class mailbag_factory_t
{
public:

	// **************************************************************
	//  TAG(                    mailbag                             ) 
	// **************************************************************
	// DESCRIPTION
	// Create a new mailbag in the free store.
	//  
	// ARGUMENTS
	// data (in, owns) - The unframed data from the wire, retrieved
	//		and unpacked by the courier.
	// len (in) - The length of the data portion of the packet.
	//  
	// RETURNS
	// A concrete mailbag class, or NULL if an error occurs.  The
	// caller owns the produced mailbag.
	//  
	// **************************************************************
	virtual mailbag_t *mailbag( void *data, size_t len ) const = 0;

	virtual mailbag_t *mailbag( void ) const = 0;    
};

#endif /*__SLURM_PLUGIN_MAILBAG_H__*/
