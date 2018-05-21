/*****************************************************************************\
 *  as_mysql_resource.h - functions dealing with resources.
 *****************************************************************************
 *
 *  Copyright (C) 2013 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *
 *  Written by Bill Brophy <bill.brophy@bull.com>
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
#ifndef _HAVE_MYSQL_RESOURCE_H
#define _HAVE_MYSQL_RESOURCE_H

#include "accounting_storage_mysql.h"

extern int as_mysql_add_res(mysql_conn_t *mysql_conn, uint32_t uid,
			    List res_list);

extern List as_mysql_modify_res(mysql_conn_t *mysql_conn, uint32_t uid,
				slurmdb_res_cond_t *res_cond,
				slurmdb_res_rec_t *res);

extern List as_mysql_remove_res(mysql_conn_t *mysql_conn, uint32_t uid,
				slurmdb_res_cond_t *res_cond);

extern List as_mysql_get_res(mysql_conn_t *mysql_conn, uid_t uid,
			     slurmdb_res_cond_t *res_cond);

#endif
