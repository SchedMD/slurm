/****************************************************************************\
 *  arg_desc.c
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  LLNL-CODE-402394.
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
\****************************************************************************/
#include <string.h>
#include "src/common/arg_desc.h"
#include "src/common/macros.h"
#include "src/common/xassert.h"

/*
 * Define slurm-specific aliases for use by plugins, see slurm_xlator.h 
 * for details. 
 */
strong_alias(arg_count,		slurm_arg_count);
strong_alias(arg_idx_by_name,	slurm_arg_idx_by_name);
strong_alias(arg_name_by_idx,	slurm_arg_name_by_idx);

int
arg_count( const arg_desc_t *desc )
{
	int i;

	if ( desc == NULL ) return 0;

	i = 0;
	while ( desc[ i ].name != NULL ) ++i;

	return i;
}


int
arg_idx_by_name( const arg_desc_t *desc, const char *name )
{
	int i;

	if ( desc == NULL ) return -1;
	if ( name == NULL ) return -1;
	
	for ( i = 0; desc[ i ].name != NULL; ++i ) {
		if ( strcmp( desc[ i ].name, name ) == 0 ) {
			return i;
		}
	}

	return -1;
}


const char *
arg_name_by_idx( const arg_desc_t *desc, const int idx )
{
	int i = idx;

	if ( desc == NULL ) return NULL;

	while ( i > 0 ) {
		if ( desc[ i ].name != NULL ) --i;
	}

	return desc[ i ].name;
}

