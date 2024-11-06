/*****************************************************************************\
 *  tres_functions.c - Interface to functions dealing with tres
 *                        in the database.
 ******************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble da@llnl.gov, et. al.
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

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "slurm/slurmdb.h"

#include "src/interfaces/accounting_storage.h"

/*
 * add tres's to accounting system
 * IN:  tres_list List of char *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_tres_add(void *db_conn, List tres_list)
{
	if (db_api_uid == -1)
		db_api_uid = getuid();

	return acct_storage_g_add_tres(db_conn, db_api_uid, tres_list);
}

/*
 * get info from the storage
 * IN:  slurmdb_tres_cond_t *
 * RET: List of slurmdb_tres_rec_t *
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_tres_get(void *db_conn, slurmdb_tres_cond_t *tres_cond)
{
	if (db_api_uid == -1)
		db_api_uid = getuid();

	return acct_storage_g_get_tres(db_conn, db_api_uid, tres_cond);
}
