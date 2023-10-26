/*****************************************************************************\
 *  collectives.c - Library for managing HPE Slingshot networks
 *****************************************************************************
 *  Copyright 2023 Hewlett Packard Enterprise Development LP
 *  Written by Jim Nordby <james.nordby@hpe.com>
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

#include "config.h"

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>

#include "switch_hpe_slingshot.h"
#include "rest.h"

static slingshot_rest_conn_t fm_conn;  /* Connection to fabric manager */

static bool collectives_enabled = false;

/*
 * Read any authentication files and connect to the fabric manager,
 * which implements a REST interface supporting Slingshot collectives
 */
extern bool slingshot_init_collectives(void)
{
	if (!slingshot_rest_connection(&fm_conn,
				       slingshot_config.fm_url,
				       slingshot_config.fm_auth,
				       slingshot_config.fm_authdir,
				       SLINGSHOT_FM_AUTH_BASIC_USER,
				       SLINGSHOT_FM_AUTH_BASIC_PWD_FILE,
				       SLINGSHOT_FM_TIMEOUT,
				       SLINGSHOT_FM_CONNECT_TIMEOUT,
				       "Slingshot Fabric Manager"))
		goto err;

	if (!slingshot_rest_connect(&fm_conn))
		goto err;

	collectives_enabled = true;
	return true;

err:
	info("Slingshot collectives support disabled due to errors");
	slingshot_rest_destroy_connection(&fm_conn);
	collectives_enabled = false;
	return false;
}

/*
 * Close connection to fabric manager REST interface, free memory
 */
extern void slingshot_fini_collectives(void)
{
	slingshot_rest_destroy_connection(&fm_conn);
}
