/****************************************************************************\
 *  resource_functions.c
 *****************************************************************************
 *  Copyright (C) 2013 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *
 *  Written by Bill Brophy <bill.brophy@bull.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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
\****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "slurm/slurmdb.h"

#include "src/common/slurm_accounting_storage.h"

/*
 * add res's to accounting system
 * IN:  res_list List of char *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_res_add(void *db_conn, uint32_t uid, List res_list)
{
	return acct_storage_g_add_res(db_conn, getuid(), res_list);
}

/*
 * get res info from the storage
 * IN:  slurmdb_res_cond_t *
 * RET: List of slurmdb_res_rec_t *
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_res_get(void *db_conn,
			    slurmdb_res_cond_t *res_cond)
{
	return acct_storage_g_get_res(db_conn, getuid(), res_cond);
}

/*
 * modify existing res in the accounting system
 * IN:  slurmdb_res_cond_t *res_cond
 * IN:  slurmdb_res_rec_t *res
 * RET: List containing (char *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_res_modify(void *db_conn,
			       slurmdb_res_cond_t *res_cond,
			       slurmdb_res_rec_t *res)
{
	return acct_storage_g_modify_res(db_conn, getuid(), res_cond, res);
}

/*
 * remove res from accounting system
 * IN:  slurmdb_res_cond_t *res
 * RET: List containing (char *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_res_remove(void *db_conn,
			       slurmdb_res_cond_t *res_cond)
{
	return acct_storage_g_remove_res(db_conn, getuid(), res_cond);
}
