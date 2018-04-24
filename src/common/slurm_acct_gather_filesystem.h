/*****************************************************************************\
 *  slurm_acct_gather_filesystem.h - implementation-independent job filesystem
 *  accounting plugin definitions
 *****************************************************************************
 *  Copyright (C) 2013 Bull
 *  Written by Yiannis Georgiou <yiannis.georgiou@bull.net>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com>.
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

#ifndef __SLURM_ACCT_GATHER_FILESYSTEM_H__
#define __SLURM_ACCT_GATHER_FILESYSTEM_H__

#include <inttypes.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurmdb.h"

#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/list.h"
#include "src/common/xmalloc.h"
#include "src/common/slurm_acct_gather.h"
#include "src/common/slurm_jobacct_gather.h"

extern int acct_gather_filesystem_init(void); /* load the plugin */
extern int acct_gather_filesystem_fini(void); /* unload the plugin */
extern int acct_gather_filesystem_startpoll(uint32_t);
extern int acct_gather_filesystem_g_node_update(void);
extern int acct_gather_filesystem_g_get_data(acct_gather_data_t *data);
/*
 * Define plugin local conf for acct_gather.conf
 *
 * Parameters
 *      full_options -- pointer that will receive list of plugin local
 *                      definitions
 *      full_options_cnt -- count of plugin local definitions
 */
extern int acct_gather_filesystem_g_conf_options(s_p_options_t **full_options,
						  int *full_options_cnt);
/*
 * set plugin local conf from acct_gather.conf into its structure
 *
 * Parameters
 *      tbl - hash table of acct_gather.conf key-values.
 */
extern int acct_gather_filesystem_g_conf_set(s_p_hashtbl_t *tbl);

/* Get the values from the plugin that are setup in the .conf
 * file. This function should most likely only be called from
 * src/common/slurm_acct_gather.c (acct_gather_get_values())
 */
extern int acct_gather_filesystem_g_conf_values(void *data);

#endif /*__SLURM_ACCT_GATHER_FILESYSTEM_H__*/
