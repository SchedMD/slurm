/****************************************************************************\
 *  arg_desc.c
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
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

