/*****************************************************************************\
 *  slurm_selecttype_info.c - Parse the SelectTypeParameters parameters
 *****************************************************************************
 *
 *  Copyright (C) 2006 Hewlett-Packard Development Company, L.P.
 *  Written by Susanne M. Balle, <susanne.balle@hp.com>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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
int parse_select_type_param(char *select_type_parameters, uint16_t *param)
{
	int rc = SLURM_SUCCESS;
	char *str_parameters, *st_str = NULL;
	int param_cnt = 0;

	*param = 0;
	st_str = xstrdup(select_type_parameters);
	str_parameters = strtok(st_str,",");
	while (str_parameters) {
		if (!strcasecmp(str_parameters, "CR_Socket")) {
			*param |= CR_SOCKET;
			param_cnt++;
		} else if (!strcasecmp(str_parameters, "CR_Socket_Memory")) {
			*param |= CR_SOCKET;
			*param |= CR_MEMORY;
			param_cnt++;
		} else if (!strcasecmp(str_parameters, "CR_Core")) {
			*param |= CR_CORE;
			param_cnt++;
		} else if (!strcasecmp(str_parameters, "CR_Core_Memory")) {
			*param |= CR_CORE;
			*param |= CR_MEMORY;
			param_cnt++;
		} else if (!strcasecmp(str_parameters, "CR_Memory")) {
			*param |= CR_MEMORY;
			param_cnt++;
		} else if (!strcasecmp(str_parameters, "CR_CPU")) {
			*param |= CR_CPU;
			param_cnt++;
		} else if (!strcasecmp(str_parameters, "CR_CPU_Memory")) {
			*param |= CR_CPU;
			*param |= CR_MEMORY;
			param_cnt++;
		} else if (!strcasecmp(str_parameters, "other_cons_res")) {
			*param |= CR_OTHER_CONS_RES;
		} else if (!strcasecmp(str_parameters,
				       "CR_ALLOCATE_FULL_SOCKET")) {
			*param |= CR_ALLOCATE_FULL_SOCKET;
		} else if (!strcasecmp(str_parameters,
				       "CR_ONE_TASK_PER_CORE")) {
			*param |= CR_ONE_TASK_PER_CORE;
		} else if (!strcasecmp(str_parameters,
				       "CR_CORE_DEFAULT_DIST_BLOCK")) {
			*param |= CR_CORE_DEFAULT_DIST_BLOCK;
		} else if (!strcasecmp(str_parameters, "CR_LLN")) {
			*param |= CR_LLN;
		} else if (!strcasecmp(str_parameters, "NHC_No")) {
			*param |= CR_NHC_STEP_NO;
			*param |= CR_NHC_NO;
		} else if (!strcasecmp(str_parameters, "NHC_No_Steps")) {
			*param |= CR_NHC_STEP_NO;
		} else if (!strcasecmp(str_parameters, "CR_PACK_NODES")) {
			*param |= CR_PACK_NODES;
		} else {
			error("Bad SelectTypeParameter: %s", str_parameters);
			rc = SLURM_ERROR;
			xfree(st_str);
			return rc;
		}
		str_parameters = strtok(NULL,",");
	}
	xfree(st_str);

	if (param_cnt > 1)
		rc = SLURM_ERROR;

	return rc;
}

/* Convert SelectTypeParameter to equivalent string
 * NOTE: Not reentrant */
extern char *select_type_param_string(uint16_t select_type_param)
{
	static char select_str[1024];

	select_str[0] = '\0';
	if ((select_type_param & CR_CPU) &&
	    (select_type_param & CR_MEMORY))
		strcat(select_str, "CR_CPU_MEMORY");
	else if ((select_type_param & CR_CORE) &&
		 (select_type_param & CR_MEMORY))
		strcat(select_str, "CR_CORE_MEMORY");
	else if ((select_type_param & CR_SOCKET) &&
		 (select_type_param & CR_MEMORY))
		strcat(select_str, "CR_SOCKET_MEMORY");
	else if (select_type_param & CR_CPU)
		strcat(select_str, "CR_CPU");
	else if (select_type_param & CR_CORE)
		strcat(select_str, "CR_CORE");
	else if (select_type_param & CR_SOCKET)
		strcat(select_str, "CR_SOCKET");
	else if (select_type_param & CR_MEMORY)
		strcat(select_str, "CR_MEMORY");

	if (select_type_param & CR_OTHER_CONS_RES) {
		if (select_str[0])
			strcat(select_str, ",");
		strcat(select_str, "OTHER_CONS_RES");
	}
	if (select_type_param & CR_NHC_NO) {
		if (select_str[0])
			strcat(select_str, ",");
		strcat(select_str, "NHC_NO");
	} else if (select_type_param & CR_NHC_STEP_NO) {
		if (select_str[0])
			strcat(select_str, ",");
		strcat(select_str, "NHC_STEP_NO");
	}
	if (select_type_param & CR_ONE_TASK_PER_CORE) {
		if (select_str[0])
			strcat(select_str, ",");
		strcat(select_str, "CR_ONE_TASK_PER_CORE");
	}
	if (select_type_param & CR_CORE_DEFAULT_DIST_BLOCK) {
		if (select_str[0])
			strcat(select_str, ",");
		strcat(select_str, "CR_CORE_DEFAULT_DIST_BLOCK");
	}
	if (select_type_param & CR_ALLOCATE_FULL_SOCKET) {
		if (select_str[0])
			strcat(select_str, ",");
		strcat(select_str, "CR_ALLOCATE_FULL_SOCKET");
	}
	if (select_type_param & CR_LLN) {
		if (select_str[0])
			strcat(select_str, ",");
		strcat(select_str, "CR_LLN");
	}
	if (select_type_param & CR_PACK_NODES) {
		if (select_str[0])
			strcat(select_str, ",");
		strcat(select_str, "CR_PACK_NODES");
	}
	if (select_str[0] == '\0')
		strcat(select_str, "NONE");

	return select_str;
}
