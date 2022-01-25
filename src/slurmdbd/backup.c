/*****************************************************************************\
 *  backup.c - backup slurm dbd
 *****************************************************************************
 *  Copyright (C) 2009  Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#include <poll.h>

#include "src/common/xmalloc.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/net.h"
#include "src/common/slurmdbd_defs.h"

#include "src/slurmdbd/backup.h"

bool primary_resumed = false;
bool backup = false;
bool have_control = false;

/* run_dbd_backup - this is the backup controller, it should run in standby
 *	mode, assuming control when the primary controller stops responding */
extern void run_dbd_backup(void)
{
	slurm_persist_conn_t slurmdbd_conn;

	primary_resumed = false;

	memset(&slurmdbd_conn, 0, sizeof(slurm_persist_conn_t));
	slurmdbd_conn.rem_host = slurmdbd_conf->dbd_addr;
	slurmdbd_conn.rem_port = slurmdbd_conf->dbd_port;
	slurmdbd_conn.cluster_name = "backup_slurmdbd";
	slurmdbd_conn.fd = -1;
	slurmdbd_conn.shutdown = &shutdown_time;
	// Prevent constant reconnection tries from filling up the error logs
	slurmdbd_conn.flags |= PERSIST_FLAG_SUPPRESS_ERR;

	slurm_persist_conn_open_without_init(&slurmdbd_conn);
	if (slurmdbd_conn.fd > 0)
		net_set_keep_alive(slurmdbd_conn.fd);

	/* repeatedly ping Primary */
	while (!shutdown_time) {
		int writeable = slurm_persist_conn_writeable(&slurmdbd_conn);
		//info("%d %d", have_control, writeable);

		if (have_control && writeable == 1) {
			info("Primary has come back");
			primary_resumed = true;
			shutdown_threads();
			have_control = false;
			break;
		} else if (!have_control && writeable <= 0) {
			have_control = true;
			info("Taking Control");
			break;
		}

		sleep(1);
		if (writeable <= 0)
			slurm_persist_conn_reopen(&slurmdbd_conn, false);
	}

	slurm_persist_conn_close(&slurmdbd_conn);

	return;
}
