/*****************************************************************************\
 *  wiki_message.h - base class for all Wiki messages, coming or going.
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

#ifndef __WIKI_MESSAGE_H__
#define __WIKI_MESSAGE_H__

#include <sys/types.h>
#include <string.h>

extern "C" {
#  include "list.h"
#  include "src/common/log.h"
#  include "src/common/xmalloc.h"	
#  include "src/common/xstring.h"
#  include "src/slurmctld/sched_plugin.h"	
}

#include "../message.h"
#include "wiki_parser.h"
#include "../dstring.h"
#include "../condition.h"

// *
//
// Inheritance map.  The significant similarity in the procedure for
// responding to GETJOBS and GETNODES commands ("requests") suggested
// centralizing their code into an intermediate abstract object.
//
// message_t (abstract)
//   +
//   |
//   +---- wiki_message_t (abstract)
//           |
//           +---- wiki_request_t (abstract)
//           |       |
//           |       +---- wiki_getnodes_t
//           |       |
//           |       +---- wiki_getjobs_t
//           |
//           +---- wiki_command_t
//           |       +
//           |       |
//           |       +---- wiki_startjob_t
//           |       |
//           |       +---- wiki_canceljob_t
//           |
//           +---- wiki_status_t
//                   |
//                   +---- wiki_response_t
//
// *



// **************************************************************************
//  TAG(                         wiki_message_t                         ) 
//  
// Base class for all Wiki messages, both incoming and outgoing.
// **************************************************************************
class wiki_message_t : public message_t
{
public:

	// *
	// Message types, to be used as indices for the message factory.
	// *
	static const int GETNODES		= 0;
	static const int GETJOBS		= 1;
	static const int STARTJOB		= 2;
	static const int CANCELJOB		= 3;
	static const int STATUS			= 4;

	static const int NUM_MESSAGE_TYPES	= 5;
    
    
protected:
	char *			m_text;
	size_t			m_text_len;
	wiki_expression_t	m_expr;
	time_t			m_epoch;
    
public:
	wiki_message_t( char *data, size_t len, int type ) :
		message_t( type ),
		m_text( data ),
		m_text_len( len ),
		m_expr( data, len ),
		m_epoch( 0 )
	{
	}
    
	virtual ~wiki_message_t()
	{
	}
    
	virtual message_t *action( void ) = 0;
    
	virtual char *text( void ) { return m_text; }
	virtual size_t text_length( void ) { return m_text_len; }

protected:
	const time_t atotime( char * const str );
};


// **************************************************************************
//  TAG(                         wiki_request_t                         ) 
//
// A general message to encapsulate requests made by the scheduler for
// information.  In order to satisfy that request, some list of data
// must be obtained from the SLURM controller, filtered according to
// criteria contained in the Wiki request, and then a response message
// is created with the resulting list of controller data.  Whether
// this uses node data or job data is determined by the specialized
// classes, wiki_getnodes_t wiki_getjobs_t.
//
// Wiki GETJOBS and GETNODES messages fall into this category.
// **************************************************************************
class wiki_request_t : public wiki_message_t
{
public:
	friend class wiki_response_t;
	
protected:
	// *
	// Convert between Wiki and SLURM field names.  This is filled
	// in by the specialized constructor.
	// *
	struct field_name_map {
		char 		*wiki_field;
		char		*slurm_field;
	};
	struct field_name_map	*m_field_map;

	// *
	// m_match - Expression for selecting nodes from the global
	//	list according to some criteria specified by the
	//	scheduler.  Wiki requires at least selection by
	//	modification time.
	// m_list_retriever - Function for retrieving the appropriate
	//	object list from the controller.
	// m_name_accessor - Accessor for retrieving the primary
	//	key for objects in the object list.
	// m_fields - Argv-like list of SLURM field names for the
	//	underlying object, that will be reported back to the
	//	scheduler.  I.e., not everything we know about is
	//	interesting to the scheduler.
	// *
	condition_t		*m_match;
	sched_objlist_fn_t	m_list_retriever;
	sched_accessor_fn_t	m_name_accessor;
	const char		**m_fields;
	
public:

	// **************************************************************
	//  TAG(                    wiki_request_t                    ) 
	// **************************************************************
	// DESCRIPTION
	// Constructor for request.
	//  
	// ARGUMENTS
	// data, len, type - arguments for wiki_message_t.
	// epoch_field (in) - field name for the epoch (modification
	//	time) in whatever underlying object list we will get
	//	from the controller in order to satisfy this request.
	// name_field (in) - field name for the primary ID for items
	//	in the underlying controller object list.  This should
	//	be the first entry in the "fields" to retrieve from
	//	the controller.
	// list_retriever (in) - function for retrieving the
	//	appropriate object list from the controller (either the
	//	node list or the job queue).
	//  
	// **************************************************************
	wiki_request_t( char			*data,
			size_t			len,
			int			type,
			char			*epoch_field,
			char			*name_field,
			sched_objlist_fn_t	list_retriever );

	virtual ~wiki_request_t()
	{
		delete m_match;
	}

	message_t *action( void );

	virtual char * const map_enum( char * const field,
				       char * const val ) const = 0;
	
private:
	
	void compose_response( wiki_request_t	*request,
			       dstring_t	&str,
			       int32_t		idx,
			       char * const * const fields,
			       sched_obj_list_t	obj_data );

	
	const char * const slurm2wiki( char * const field ) const;

	char * postproc_string( char * const field,
				const char * const val );

	static char *colonify_commas( const char * const val );
	
};


// **************************************************************************
//  TAG(                         wiki_getnodes_t                         ) 
//  
// The Wiki GETNODES message.
// **************************************************************************
class wiki_getnodes_t : public wiki_request_t
{
    
public:
	wiki_getnodes_t( char *data, size_t len );
	char * const map_enum( char * const field, char * const val ) const;
};



// **************************************************************************
//  TAG(                         wiki_getjobs_t                         ) 
//  
//  The Wiki GETJOBS message.
// **************************************************************************
class wiki_getjobs_t : public wiki_request_t
{
public:
	wiki_getjobs_t( char *data, size_t len );
	char * const map_enum( char * const field, char * const val ) const;
};



// **************************************************************************
//  TAG(                         wiki_command_t                         ) 
//  
// A Wiki command to alter the runnability of a job.  This includes
// STARTJOB and CANCELJOB.
// **************************************************************************
class wiki_command_t : public wiki_message_t
{
protected:
	char			*m_jobid;
	ListIterator		m_arg_iterator;
	
public:
	wiki_command_t( char *data, size_t len, int type );
	
	virtual ~wiki_command_t()
	{
		if ( m_arg_iterator )
			list_iterator_destroy( m_arg_iterator );
	}
};


// **************************************************************************
//  TAG(                         wiki_startjob_t                         ) 
//  
//  The Wiki STARTJOB message.
// **************************************************************************
class wiki_startjob_t : public wiki_command_t
{
protected:

	List			m_nodelist;

public:
	wiki_startjob_t( char *data, size_t len );
	~wiki_startjob_t()
	{
		list_destroy( m_nodelist );
	}

	message_t *action( void );
};




// **************************************************************************
//  TAG(                        wiki_canceljob_t                        ) 
//  
// The Wiki CANCELJOB message.
// **************************************************************************
class wiki_canceljob_t : public wiki_command_t
{
public:

	enum reason_t {
		ADMIN,
		WALLCLOCK
	};
    
protected:

	reason_t       		m_reason;

public:
	wiki_canceljob_t( char *data, size_t len );

	message_t *action( void );
};

// **************************************************************************
//  TAG(                          wiki_status_t                          ) 
//  
// The return message from a Wiki command, giving either the requested
// data (wiki_response_t) or the completion status of a command.
// **************************************************************************
class wiki_status_t : public wiki_message_t
{
protected:
	dstring_t		m_str;

public:
	wiki_status_t( int status, char * const msg = NULL );

	virtual ~wiki_status_t()
	{
	}

	virtual message_t *action( void ) { return NULL; }
	char *text( void ) { return m_str.s(); }
	size_t text_length( void ) { return m_str.length(); }

protected:
	void prefix_with_checksum( void );

private:
	void des( u_int32_t *lword, u_int32_t *irword ) const;
	const u_int16_t compute_crc( u_int16_t crc, u_int8_t onch ) const;
	void checksum( dstring_t &sum, const char * const key );
	const char * get_user_name( void ) const;	
};


// **************************************************************************
//  TAG(                       wiki_response_t                             ) 
//  
// A Wiki response to a wiki_request_t message.  A response contains the
// information that the request asked for.
// **************************************************************************
class wiki_response_t : public wiki_status_t
{
public:
	wiki_response_t( wiki_request_t		*request,
			 char * const * const	fields,
			 int32_t		obj_count,
			 int32_t		obj_hits,
			 sched_obj_list_t      	obj_data,
			 bool			*matches );

};



#endif /*__WIKI_MESSAGE_H__*/
