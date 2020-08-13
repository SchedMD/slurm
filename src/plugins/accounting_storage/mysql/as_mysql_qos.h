/*****************************************************************************\
 *  as_mysql_qos.h - functions dealing with qos.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#ifndef _HAVE_MYSQL_QOS_H
#define _HAVE_MYSQL_QOS_H

#include "accounting_storage_mysql.h"

extern int as_mysql_add_qos(mysql_conn_t *mysql_conn, uint32_t uid, List qos_list);

extern List as_mysql_modify_qos(mysql_conn_t *mysql_conn, uint32_t uid,
			     slurmdb_qos_cond_t *qos_cond,
			     slurmdb_qos_rec_t *qos);

extern List as_mysql_remove_qos(mysql_conn_t *mysql_conn, uint32_t uid,
			     slurmdb_qos_cond_t *qos_cond);

extern List as_mysql_get_qos(mysql_conn_t *mysql_conn, uid_t uid,
			  slurmdb_qos_cond_t *qos_cond);

#endif
