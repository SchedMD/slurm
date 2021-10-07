/*****************************************************************************\
 *  as_mysql_convert.h - functions dealing with converting from tables in
 *                    slurm <= 17.02.
 *****************************************************************************
 *  Copyright (C) 2015-2017 SchedMD LLC.
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

#ifndef _HAVE_AS_MYSQL_CONVERT_H
#define _HAVE_AS_MYSQL_CONVERT_H

#include "accounting_storage_mysql.h"

/* Functions for converting tables before they are created in new schema */
extern int as_mysql_convert_tables_pre_create(mysql_conn_t *mysql_conn);

/* Functions for converting tables after they are created */
extern int as_mysql_convert_tables_post_create(mysql_conn_t *mysql_conn);

/*
 * Functions for converting tables that aren't cluster centric as the other
 * functions in this deal with.
 */
extern int as_mysql_convert_non_cluster_tables_post_create(
	mysql_conn_t *mysql_conn);

/*
 * Only use this when running "ALTER TABLE" during an upgrade.  This is to get
 * around that mysql cannot rollback an "ALTER TABLE", but its possible that the
 * rest of the upgrade transaction was aborted.
 *
 * We may not always use this function, but don't delete it just in case we
 * need to alter tables in the future.
 */
extern int as_mysql_convert_alter_query(mysql_conn_t *mysql_conn, char *query);

#endif
