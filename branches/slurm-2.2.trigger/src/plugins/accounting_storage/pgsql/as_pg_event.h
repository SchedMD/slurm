/*****************************************************************************\
 *  as_pg_event.h - accounting interface to pgsql - cluster/node event related
 *  functions.
 *
 *  $Id: as_pg_event.h 13061 2008-01-22 21:23:56Z da $
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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
\*****************************************************************************/
#ifndef _HAVE_AS_PGSQL_EVENT_H
#define _HAVE_AS_PGSQL_EVENT_H

#include "as_pg_common.h"

extern char *event_table;

extern int check_clusteracct_tables(PGconn *db_conn, char *user);

extern int get_cluster_cpu_nodes(pgsql_conn_t *pg_conn,
				 slurmdb_cluster_rec_t *cluster);
extern int cs_pg_node_down(pgsql_conn_t *pg_conn,
			   struct node_record *node_ptr,
			   time_t event_time, char *reason,
			   uint32_t reason_uid);
extern int cs_pg_node_up(pgsql_conn_t *pg_conn,
			 struct node_record *node_ptr,
			 time_t event_time);
extern int cs_pg_register_ctld(pgsql_conn_t *pg_conn,
			       char *cluster,
			       uint16_t port);
extern int cs_pg_cluster_cpus(pgsql_conn_t *pg_conn,
			      char *cluster_nodes,
			      uint32_t cpus,
			      time_t event_time);
extern int cs_pg_get_usage(
	pgsql_conn_t *pg_conn, uid_t uid,
	slurmdb_cluster_rec_t *cluster_rec, int type, time_t start, time_t end);

extern List as_pg_get_events(pgsql_conn_t *pg_conn, uid_t uid, 
			     slurmdb_event_cond_t *event_cond);


#endif /* _HAVE_AS_PGSQL_EVENT_H */
