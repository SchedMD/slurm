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
 * add clus_res's to accounting system
 * IN:  clus_res_list List of char *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_clus_res_add(void *db_conn, uint32_t uid,
				List clus_res_list)
{
	return acct_storage_g_add_clus_res(db_conn, getuid(), clus_res_list);
}

/*
 * get clus_res info from the storage
 * IN:  slurmdb_clus_res_cond_t *
 * RET: List of slurmdb_clus_res_rec_t *
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_clus_res_get(void *db_conn,
				slurmdb_clus_res_cond_t *clus_res_cond)
{
	return acct_storage_g_get_clus_res(db_conn, getuid(), clus_res_cond);
}

/*
 * modify existing clus_res in the accounting system
 * IN:  slurmdb_clus_res_cond_t *clus_res_cond
 * IN:  slurmdb_clus_res_rec_t *clus_res
 * RET: List containing (char *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_clus_res_modify(void *db_conn,
			       slurmdb_clus_res_cond_t *clus_res_cond,
			       slurmdb_clus_res_rec_t *clus_res)
{
	return acct_storage_g_modify_clus_res(db_conn, getuid(),
					clus_res_cond, clus_res);
}

/*
 * remove clus_res from accounting system
 * IN:  slurmdb_clus_res_cond_t *clus_res
 * RET: List containing (char *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_clus_res_remove(void *db_conn,
				slurmdb_clus_res_cond_t *clus_res_cond)
{
	return acct_storage_g_remove_clus_res(db_conn, getuid(), clus_res_cond);
}

/*
 * add ser_res's to accounting system
 * IN:  ser_res_list List of char *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_ser_res_add(void *db_conn, uint32_t uid, List ser_res_list)
{
	return acct_storage_g_add_ser_res(db_conn, getuid(), ser_res_list);
}

/*
 * get ser_res info from the storage
 * IN:  slurmdb_ser_res_cond_t *
 * RET: List of slurmdb_ser_res_rec_t *
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_ser_res_get(void *db_conn,
				slurmdb_ser_res_cond_t *ser_res_cond)
{
	return acct_storage_g_get_ser_res(db_conn, getuid(), ser_res_cond);
}

/*
 * modify existing ser_res in the accounting system
 * IN:  slurmdb_ser_res_cond_t *ser_res_cond
 * IN:  slurmdb_ser_res_rec_t *ser_res
 * RET: List containing (char *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_ser_res_modify(void *db_conn,
			       slurmdb_ser_res_cond_t *ser_res_cond,
			       slurmdb_ser_res_rec_t *ser_res)
{
	return acct_storage_g_modify_ser_res(db_conn, getuid(),
					ser_res_cond, ser_res);
}

/*
 * remove ser_res from accounting system
 * IN:  slurmdb_ser_res_cond_t *ser_res
 * RET: List containing (char *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_ser_res_remove(void *db_conn,
				slurmdb_ser_res_cond_t *ser_res_cond)
{
	return acct_storage_g_remove_ser_res(db_conn, getuid(), ser_res_cond);
}
