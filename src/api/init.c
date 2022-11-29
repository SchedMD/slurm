/*****************************************************************************\
 *  init.c - libslurm library initialization
 *****************************************************************************
 *  Copyright (C) 2020 SchedMD LLC
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#include "src/common/read_config.h"

#include "src/interfaces/accounting_storage.h"
#include "src/interfaces/cli_filter.h"
#include "src/interfaces/gres.h"
#include "src/interfaces/select.h"

extern void slurm_init(const char *conf)
{
	slurm_conf_init(conf);
	slurm_client_init_plugins();
}

extern void slurm_fini(void)
{
	slurm_client_fini_plugins();
	slurm_conf_destroy();
}

extern void slurm_client_init_plugins(void)
{
	if (slurm_acct_storage_init() != SLURM_SUCCESS)
		fatal("failed to initialize the accounting storage plugin");

	if (select_g_init(0) != SLURM_SUCCESS)
		fatal("failed to initialize node selection plugin");

	if (cli_filter_init() != SLURM_SUCCESS)
		fatal("failed to initialize cli_filter plugin");

	if (gres_init() != SLURM_SUCCESS)
		fatal("failed to initialize gres plugin");
}

extern void slurm_client_fini_plugins(void)
{
	gres_fini();
	cli_filter_fini();
	select_g_fini();
	slurm_acct_storage_fini();
}
