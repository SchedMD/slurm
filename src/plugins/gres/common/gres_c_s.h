/*****************************************************************************\
 *  gres_c_s.h - common functions for shared gres plugins
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC
 *  Written by Danny Auble <da@schedmd.com>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef _HAVE_GRES_C_S_H
#define _HAVE_GRES_C_S_H

#include "gres_common.h"

typedef struct {
	uint64_t count;
	int id;
} shared_dev_info_t;

extern List shared_info; /* list of shared_dev_info_t */

extern void gres_c_s_fini(void);

/*
 * We could load gres state or validate it using various mechanisms here.
 * This only validates that the configuration was specified in gres.conf.
 * In the general case, no code would need to be changed.
 */
extern int gres_c_s_init_share_devices(List gres_conf_list,
				       List *share_devices,
				       node_config_load_t *config,
				       char *sharing_name,
				       char *shared_name);

/*
 * Send shared_info over to the stepd from the slurmd.
 */
extern void gres_c_s_send_stepd(buf_t *buffer);

/*
 * Fill in shared_info with GRES information from slurmd on the specified file
 * descriptor
 */
extern void gres_c_s_recv_stepd(buf_t *buffer);

#endif
