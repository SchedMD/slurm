/*****************************************************************************\
 *  slurm_selecttype_info.c - Parse the SelectTypeParameters parameters
 *****************************************************************************
 *
 *  Copyright (C) 2006 Hewlett-Packard Development Company, L.P.
 *  Written by Susanne M. Balle, <susanne.balle@hp.com>
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
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
int parse_select_type_param(char *select_type_parameters, 
				   select_type_plugin_info_t *param)
{
	int rc = SLURM_SUCCESS;	
	char *str_parameters, *st_str = NULL;


	st_str = xstrdup(select_type_parameters);
	if ((str_parameters = strtok(st_str,",")) != NULL) {
		do {
			if (strcasecmp(str_parameters, "CR_Socket") == 0) {
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
				error("Bad SelectTypeParameter: %s\n", 
				      str_parameters );
				rc = SLURM_ERROR;
				xfree(st_str);
				return rc;
			}
		} while ((str_parameters = strtok(NULL,",")));
	}
	xfree(st_str);
	
	return rc;
}
