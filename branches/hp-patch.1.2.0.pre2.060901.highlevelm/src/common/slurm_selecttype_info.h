/*****************************************************************************\
 *  slurm_selecttype_info.h - Parse the SelectTypeParameters parameters
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

#ifndef __SLURM_SELECTTYPE_INFO_H__
#define __SLURM_SELECTTYPE_INFO_H__

#include <stdio.h>
#include <string.h>
#include <slurm/slurm.h>

extern int parse_select_type_param(char *select_type_parameters, 
				   select_type_plugin_info_t *param);

#endif /*__SLURM_SELECTTYPE_INFO_H__*/
