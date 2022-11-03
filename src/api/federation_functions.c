/*****************************************************************************\
 *  federation_functions.c - Interface to functions dealing with federations in
 *                           the database.
 *****************************************************************************
 *  Copyright (C) 2016 SchedMD LLC.
 *  Written by Brian Christiansen <brian@schedmd.com>
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
 * add federations to accounting system
 * IN:  list List of slurmdb_federation_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_federations_add(void *db_conn, List federation_list)
{
	if (db_api_uid == -1)
		db_api_uid = getuid();

	return acct_storage_g_add_federations(db_conn, db_api_uid,
					      federation_list);
}

/*
 * modify existing federations in the accounting system
 * IN:  slurmdb_federation_cond_t *fed_cond
 * IN:  slurmdb_federation_rec_t  *fed
 * RET: List containing (char *'s) else NULL on error
 */
extern List slurmdb_federations_modify(void *db_conn,
				       slurmdb_federation_cond_t *fed_cond,
				       slurmdb_federation_rec_t *fed)
{
	if (db_api_uid == -1)
		db_api_uid = getuid();

	return acct_storage_g_modify_federations(db_conn, db_api_uid, fed_cond,
						 fed);
}

/*
 * remove federations from accounting system
 * IN:  slurmdb_federation_cond_t *fed_cond
 * RET: List containing (char *'s) else NULL on error
 */
extern List slurmdb_federations_remove(void *db_conn,
				       slurmdb_federation_cond_t *fed_cond)
{
	if (db_api_uid == -1)
		db_api_uid = getuid();

	return acct_storage_g_remove_federations(db_conn, db_api_uid, fed_cond);
}

/*
 * get info from the storage
 * IN:  slurmdb_federation_cond_t *
 * RET: List of slurmdb_federation_rec_t *
 * note List needs to be freed when called
 */
extern List slurmdb_federations_get(void *db_conn,
				    slurmdb_federation_cond_t *fed_cond)
{
	if (db_api_uid == -1)
		db_api_uid = getuid();

	return acct_storage_g_get_federations(db_conn, db_api_uid, fed_cond);
}
