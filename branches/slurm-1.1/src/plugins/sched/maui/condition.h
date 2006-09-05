/*****************************************************************************\
 *  condition.h - a testable condition for arbitrary expressions.
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

#ifndef __SLURM_PLUGIN_CONDITION_H__
#define __SLURM_PLUGIN_CONDITION_H__


// **************************************************************************
//  TAG(                           condition_t                           ) 
//  
// A condition_t is a node in an expression tree.  Specializations are
// either comparators (leaves) or conjunctions (branches).  The represented
// boolean-valued expression establishes the criteria against which
// individual nodes can be matched.
//
// The default implementation is a tautology which can be useful to select
// all nodes with generic traversal code (albeit inefficiently).
// **************************************************************************
class condition_t
{
public:
    virtual const bool eval( void *obj_data, int32_t node_idx )
    {
	return true;
    }
};


// **************************************************************************
//  TAG(                           negation_t                           ) 
//  
// A condition which negates its subexpression:
//
//	( ! EXPR )
//
// **************************************************************************
class negation_t : public condition_t
{
protected:
    condition_t			*m_expr;

public:
    negation_t( condition_t *expr ) :
	m_expr( expr )
    {
    }

    ~negation_t()
    {
	delete m_expr;
    }
    
    const bool eval( void *obj_data, int32_t node_idx )
    {
	return ! m_expr->eval( obj_data, node_idx );
    }
};


// **************************************************************************
//  TAG(                          conjunction_t                          ) 
//  
// A condition which is evaluated according to its left and right operands
// conjoined or disjoined by a boolean operator.  This is a branch in the
// expression tree.  Short-circuiting should be performed when appropriate.
//
//	( EXPR1 && EXPR2 )
//
// or
//
//	( EXPR1 || EXPR2 )
//
// **************************************************************************
class conjunction_t : public condition_t
{
public:
    
    enum op_t {
	AND,
	OR
    };
    
protected:
    op_t			m_op;
    condition_t			*m_left;
    condition_t			*m_right;
    

public:

    conjunction_t( op_t op, condition_t *left, condition_t *right ) :
	m_op( op ),
	m_left( left ),
	m_right( right )
    {
    }
	
    ~conjunction_t()
    {
	delete m_left;
	delete m_right;
    }

    virtual const bool eval( void *obj_data, int32_t idx )
    {
	switch ( m_op ) {
	case AND:
	    return m_left->eval( obj_data, idx )
		    && m_right->eval( obj_data, idx );
	case OR:
	    return m_left->eval( obj_data, idx )
		    || m_right->eval( obj_data, idx );
	default:
	    return false;
	}
    }
};

#endif /*__SLURM_PLUGIN_CONDITION_H__*/
