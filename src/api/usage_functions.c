/*****************************************************************************\
 *  usage_functions.c - Interface to functions dealing with usage
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

#include "src/common/slurm_accounting_storage.h"

/*
 * get info from the storage
 * IN/OUT:  in void * (slurmdb_assoc_rec_t *) or
 *          (slurmdb_wckey_rec_t *) of (slurmdb_cluster_rec_t *) with
 *          the id, and cluster set.
 * IN:  type what type is 'in'
 * IN:  start time stamp for records >=
 * IN:  end time stamp for records <=
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_usage_get(void *db_conn, void *in, int type,
			     time_t start, time_t end)
{
	if (db_api_uid == -1)
		db_api_uid = getuid();

	return acct_storage_g_get_usage(db_conn, db_api_uid,
					in, type, start, end);
}

/*
 * roll up data in the storage
 * IN: sent_start (option time to do a re-roll or start from this point)
 * IN: sent_end (option time to do a re-roll or end at this point)
 * IN: archive_data (if 0 old data is not archived in a monthly rollup)
 * IN/OUT: rollup_stats data structure in which to save rollup statistics
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_usage_roll(void *db_conn, time_t sent_start,
			      time_t sent_end, uint16_t archive_data,
			      rollup_stats_t *rollup_stats)
{
	return acct_storage_g_roll_usage(db_conn, sent_start,
					 sent_end, archive_data, rollup_stats);
}
