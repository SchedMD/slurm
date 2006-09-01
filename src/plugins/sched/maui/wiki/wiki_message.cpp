/*****************************************************************************\
 *  wiki_message.cpp - base class for all Wiki messages, see wiki_message.h
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

#include <ctype.h>
#include <string.h>

extern "C" {
# include "src/slurmctld/sched_plugin.h"
}

#include "wiki_message.h"


// ************************************************************************
//  TAG(                             atotime                             ) 
// ************************************************************************
// DESCRIPTION
// Converts a string of digits representing a time epoch into a time_t
// value.  We do it this way because the size of time_t is somewhat
// nondeterministic.  
//  
// ARGUMENTS
// str (in) - A string of (presumably) digits.  The prefix of the string
// 	consisting of base-10 digits is converted to its unsigned integer
//	value and placed in a data object of type <time_t>.
//  
// RETURNS
// The time epoch, or 0 if the string does not contain a prefix of base
// 10 digits.  Note that 0 will also be returned if the string validly
// contains "0" as the epoch.
//  
// ASSUMPTIONS
// No explicit test is made for the suitability of the input.  Zero may
// be a valid function value, or it may indicate an error.
//  
// SIDE EFFECTS
// None.
//  
// ************************************************************************
const time_t wiki_message_t::atotime( char * const str )
{
	char *p;
	time_t time = 0;

	for ( p = str; *p && isdigit( *p ); ++p ) {
		time = time * 10 + ( *p - '0' );
	}

	return time;
}
