/*****************************************************************************\
 *  comparator.h - a condition evaluated by comparison to a constant.
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
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef __SLURM_PLUGIN_COMPARATOR_H__
#define __SLURM_PLUGIN_COMPARATOR_H__

#include "condition.h"

extern "C" {
#  include "src/slurmctld/sched_plugin.h"
}

// **************************************************************************
//  TAG(                          comparator_t                          ) 
//  
// A condition which is evaluated according to a comparison between
// requested attributes and actual attributes.  This is a leaf in the
// expression tree.
//
// The comparator is specialized into the cross-product of comparison
// operators and data types.  These can be determined when the
// expression specification is parsed, saving them from being
// evaluated from the data on each comparison.  The specialized
// comparators are instantiated with the table of values for one
// attribute of a node or a job (i.e., all the memory sizes in an
// ordered array).  The eval() method is called with the node or job
// index.
//
// **************************************************************************
class comparator_t : public condition_t
{
protected:
	sched_accessor_fn_t		m_func;
public:

	comparator_t( sched_accessor_fn_t fn ) :
		m_func( fn )
	{
		if ( m_func == NULL ) throw "comparator: no such field";
	}
    
	virtual const bool eval( sched_obj_list_t obj_data, int32_t idx ) = 0;
};


// **************************************************************************
//  TAG(                       int_eq_comparator_t                       ) 
//  
// Compares numerical equality.
// **************************************************************************
class int_eq_comparator_t : public comparator_t
{
protected:
	time_t			m_val;
public:
	int_eq_comparator_t( sched_accessor_fn_t fn,
			     time_t expected ) :
		comparator_t( fn ),
		m_val( expected )
	{
	}

	const bool eval( sched_obj_list_t obj_data, int32_t idx )
	{
		return (time_t) m_func( obj_data,
					 idx,
					 NULL ) == m_val;
	}
};


// **************************************************************************
//  TAG(                       int_lt_comparator_t                       ) 
//  
// Compares for values less than the expected value.
// **************************************************************************
class int_lt_comparator_t : public comparator_t
{
protected:
	time_t			m_val;
public:
	int_lt_comparator_t( sched_accessor_fn_t fn,
			     time_t expected ) :
		comparator_t( fn ),
		m_val( expected )
	{
	}

	const bool eval( sched_obj_list_t obj_data, int32_t idx )
	{
		return (time_t) m_func( obj_data, idx, NULL ) < m_val;
	}
};


// **************************************************************************
//  TAG(                       int_gt_comparator_t                       ) 
//  
// Compares for values greater than the expected value.
// **************************************************************************
class int_gt_comparator_t : public comparator_t
{
protected:
	time_t			m_val;
public:
	int_gt_comparator_t( sched_accessor_fn_t fn,
			     time_t expected ) :
		comparator_t( fn ),
		m_val( expected )
	{
	}

	const bool eval( sched_obj_list_t obj_data, int32_t idx )
	{
		return (time_t) m_func( obj_data, idx, NULL ) > m_val;
	}
};


// **************************************************************************
//  TAG(                       string_comparator_t                       ) 
//  
// Base class for string comparators, to embody strcmp().
// **************************************************************************
class string_comparator_t : public comparator_t
{
protected:
	char			*m_string;

public:
	string_comparator_t( sched_accessor_fn_t fn,
			     char *expected_value ) :
		comparator_t( fn ),
		m_string( expected_value )
	{
	}

protected:
	int cmp( sched_obj_list_t obj_data, int32_t idx )
	{
		return strcmp( (char *) m_func( obj_data,
						idx,
						NULL ),
			       m_string );
	}
    
};


// **************************************************************************
//  TAG(                     string_eq_comparator_t                     ) 
// **************************************************************************
class string_eq_comparator_t : public string_comparator_t
{
public:
	string_eq_comparator_t( sched_accessor_fn_t fn,
				char *expected ) :
		string_comparator_t( fn, expected )
	{
	}

	const bool eval( sched_obj_list_t obj_data, int32_t idx )
	{
		return cmp( obj_data, idx ) == 0;
	}
};


// **************************************************************************
//  TAG(                     string_lt_comparator_t                     ) 
// **************************************************************************
class string_lt_comparator_t : public string_comparator_t
{
public:
	string_lt_comparator_t( sched_accessor_fn_t fn,
				char *expected ) :
		string_comparator_t( fn, expected )
	{
	}

	const bool eval( sched_obj_list_t obj_data, int32_t idx )
	{
		return cmp( obj_data, idx ) < 0;
	}
};


// **************************************************************************
//  TAG(                     string_gt_comparator_t                     ) 
// **************************************************************************
class string_gt_comparator_t : public string_comparator_t
{
public:
	string_gt_comparator_t( sched_accessor_fn_t fn,
				char *expected ) :
		string_comparator_t( fn, expected )
	{
	}

	const bool eval( sched_obj_list_t obj_data, int32_t idx )
	{
		return cmp( obj_data, idx ) > 0;
	}
};

#endif /*__SLURM_PLUGIN_COMPARATOR_H__*/
