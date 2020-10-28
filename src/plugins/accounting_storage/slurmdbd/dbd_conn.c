/****************************************************************************\
 *  dbd_conn.c - functions to manage the connection to the SlurmDBD
 *****************************************************************************
 *  Copyright (C) 2011-2020 SchedMD LLC.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@schedmd.com>
 *  Written by Morris Jette <jette1@llnl.gov>
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

#include "slurmdbd_agent.h"

/* partially based on _open_slurmdbd_conn() */
extern slurm_persist_conn_t *dbd_conn_open(uint16_t *persist_conn_flags,
					   char *cluster_name)
{
	int rc;
	char *backup_host = xstrdup(slurm_conf.accounting_storage_backup_host);
	slurm_persist_conn_t *pc = xmalloc(sizeof(*pc));

	if (persist_conn_flags)
		pc->flags = *persist_conn_flags;
	pc->flags |= (PERSIST_FLAG_DBD | PERSIST_FLAG_RECONNECT);
	pc->persist_type = PERSIST_TYPE_DBD;
	if (cluster_name)
		pc->cluster_name = xstrdup(cluster_name);
	else
		pc->cluster_name = xstrdup(slurm_conf.cluster_name);
	pc->timeout = (slurm_conf.msg_timeout + 35) * 1000;
	pc->rem_host = xstrdup(slurm_conf.accounting_storage_host);
	pc->rem_port = slurm_conf.accounting_storage_port;
	pc->version = SLURM_PROTOCOL_VERSION;

again:
	// A connection failure is only an error if backup dne or also fails
	if (backup_host)
		pc->flags |= PERSIST_FLAG_SUPPRESS_ERR;
	else
		pc->flags &= (~PERSIST_FLAG_SUPPRESS_ERR);

	if (((rc = slurm_persist_conn_open(pc))) && backup_host) {
		xfree(pc->rem_host);
		// Force the next error to display
		pc->comm_fail_time = 0;
		pc->rem_host = backup_host;
		backup_host = NULL;
		goto again;
	}

	xfree(backup_host);

	if (!rc) {
		debug("Sent PersistInit msg");
		return pc;
	} else {
		error("%s: unable to open slurmdb persistent connection: %s",
		      __func__, slurm_strerror(rc));
		/* fail gracefully */
		slurm_persist_conn_destroy(pc);
		return NULL;
	}
}

extern void dbd_conn_close(slurm_persist_conn_t **pc)
{
	if (!pc)
		return;
	slurm_persist_conn_destroy(*pc);
	*pc = NULL;
}
