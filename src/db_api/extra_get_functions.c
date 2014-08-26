/*****************************************************************************\
 *  extra_get_functions.c - Interface to functions dealing with
 *                          getting info from the database, and this
 *                          these were unrelated to other functionality.
 ******************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble da@llnl.gov, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "slurm/slurmdb.h"

#include "src/common/slurm_accounting_storage.h"

/*
 * reconfigure the slurmdbd
 * RET: List of config_key_pairs_t *
 * note List needs to be freed when called
 */
extern int slurmdb_reconfig(void *db_conn)
{
	return acct_storage_g_reconfig(db_conn, 1);
}

/*
 * get info from the storage
 * RET: List of config_key_pairs_t *
 * note List needs to be freed when called
 */
extern List slurmdb_config_get(void *db_conn)
{
	return acct_storage_g_get_config(db_conn, "slurmdbd.conf");
}

/*
 * get info from the storage
 * IN:  slurmdb_event_cond_t *
 * RET: List of slurmdb_event_rec_t *
 * note List needs to be freed when called
 */
extern List slurmdb_events_get(void *db_conn,
			       slurmdb_event_cond_t *event_cond)
{
	return acct_storage_g_get_events(db_conn, getuid(), event_cond);
}

/*
 * get info from the storage
 * returns List of slurmdb_job_rec_t *
 * note List needs to be freed when called
 */
extern List slurmdb_jobs_get(void *db_conn, slurmdb_job_cond_t *job_cond)
{
	return jobacct_storage_g_get_jobs_cond(db_conn, getuid(), job_cond);
}

/*
 * get info from the storage
 * IN:  slurmdb_association_cond_t *
 * RET: List of slurmdb_association_rec_t *
 * note List needs to be freed when called
 */
extern List slurmdb_problems_get(void *db_conn,
				 slurmdb_association_cond_t *assoc_cond)
{
	return acct_storage_g_get_problems(db_conn, getuid(), assoc_cond);
}

/*
 * get info from the storage
 * IN:  slurmdb_reservation_cond_t *
 * RET: List of slurmdb_reservation_rec_t *
 * note List needs to be freed when called
 */
extern List slurmdb_reservations_get(void *db_conn,
				     slurmdb_reservation_cond_t *resv_cond)
{
	return acct_storage_g_get_reservations(db_conn, getuid(), resv_cond);
}

/*
 * get info from the storage
 * IN:  slurmdb_txn_cond_t *
 * RET: List of slurmdb_txn_rec_t *
 * note List needs to be freed when called
 */
extern List slurmdb_txn_get(void *db_conn, slurmdb_txn_cond_t *txn_cond)
{
	return acct_storage_g_get_txn(db_conn, getuid(), txn_cond);
}


