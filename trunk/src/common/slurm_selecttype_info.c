/*****************************************************************************\
 *  slurm_selecttype_info.c - Parse the SelectTypeParameters parameters
 *****************************************************************************
 *
 *  Copyright (C) 2006 Hewlett-Packard Development Company, L.P.
 *  Written by Susanne M. Balle, <susanne.balle@hp.com>
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
 *
\*****************************************************************************/

#include "src/common/slurm_selecttype_info.h"
#include "src/common/macros.h"
#include "src/common/xstring.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
/*
 * Parse a comma separated list of SelectType Parameters
 *
 * Return SLURM_SUCCESS on success, or SLURM_ERROR otherwise
 */
extern int parse_select_type_param(char *select_type_parameters, 
				   select_type_plugin_info_t *param)
{
	int rc = SLURM_SUCCESS;	
	char *str_parameters;


	char *st_str = xstrdup(select_type_parameters);
	if ((str_parameters = strtok(st_str,",")) != NULL) {
	  do {
	    if (strcasecmp(str_parameters, "CR_SOCKET") == 0) {
	      *param = CR_SOCKET;
	    } else if (strcasecmp(str_parameters, "CR_Socket_Memory") == 0) {
	      *param = CR_SOCKET_MEMORY;
	    } else if (strcasecmp(str_parameters, "CR_Core") == 0) {
	      *param = CR_CORE;
	    } else if (strcasecmp(str_parameters, "CR_Core_Memory") == 0) {
	      *param = CR_CORE_MEMORY;
	    } else if (strcasecmp(str_parameters, "CR_Memory") == 0) {
	      *param = CR_MEMORY;
	    } else if (strcasecmp(str_parameters, "CR_CPU") == 0) {
	      *param = CR_CPU;
	    } else if (strcasecmp(str_parameters, "CR_CPU_Memory") == 0) {
	      *param = CR_CPU_MEMORY;
	    } else {
	      error( "Bad SelectType Parameter: %s\n", str_parameters );
	      rc = SLURM_ERROR;
	      xfree(str_parameters);
	      return rc;
	    }
	  } while ((str_parameters = strtok(NULL,",")));
	}
	xfree(str_parameters);
	
	return rc;
}
