/*****************************************************************************\
 *  resv_info.c - Functions related to advanced reservation display
 *  mode of sview.
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include "src/sview/sview.h"

extern int get_new_info_config(slurm_ctl_conf_info_msg_t **info_ptr)
{
	static slurm_ctl_conf_info_msg_t *ctl_info_ptr = NULL,
		*new_ctl_ptr = NULL;
	int error_code = SLURM_NO_CHANGE_IN_DATA;

	if (ctl_info_ptr) {
		error_code = slurm_load_ctl_conf(ctl_info_ptr->last_update,
						 &new_ctl_ptr);
		if (error_code == SLURM_SUCCESS)
			slurm_free_ctl_conf(ctl_info_ptr);
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_NO_CHANGE_IN_DATA;
			new_ctl_ptr = ctl_info_ptr;
		}
	} else
		error_code = slurm_load_ctl_conf((time_t) NULL, &new_ctl_ptr);

	ctl_info_ptr = new_ctl_ptr;

	if(*info_ptr != ctl_info_ptr)
		error_code = SLURM_SUCCESS;

	*info_ptr = new_ctl_ptr;
	return error_code;

}
