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

const int
arg_count( const arg_desc_t *desc )
{
	int i;

	if ( desc == NULL ) return 0;

	i = 0;
	while ( desc[ i ].name != NULL ) ++i;

	return i;
}


const int
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

