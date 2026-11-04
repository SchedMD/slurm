/*****************************************************************************\
 *  common_as.h - header for common functions for accounting storage
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
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

#ifndef _HAVE_AS_MYSQL_UTIL_H
#define _HAVE_AS_MYSQL_UTIL_H

#include "src/common/assoc_mgr.h"

extern int addto_update_list(list_t *update_list, slurmdb_update_type_t type,
			     void *object);

extern int cluster_first_reg(char *host, uint16_t port, uint16_t rpc_version);

extern int set_usage_information(char **usage_table, slurmdbd_msg_type_t type,
				 time_t *usage_start, time_t *usage_end);

extern bool is_user_min_admin_level(void *db_conn,
				    slurm_auth_level_t min_level);

extern bool is_user_any_coord_locked(void *db_conn, slurmdb_user_rec_t *user);

extern void user_rec_from_conn(void *db_conn, slurmdb_user_rec_t *user);

/*
 * Get what a column of user names stores for uid: the user's name, or the
 * uid as a string if no user has it. Quotes are escaped for SQL.
 * IN uid - uid to look up
 * RET user name or uid string, xfree() it
 */
extern char *uid_to_db_user(uint32_t uid);

/*
 * Get the SQL value to store for uid in a column of user names.
 * IN uid - uid to look up, NO_VAL or INFINITE for no user
 * RET quoted uid_to_db_user() of uid, or NULL if no user. xfree() it.
 */
extern char *uid_to_db_value(uint32_t uid);

/*
 * Get the SQL value to load from an archive for a column of user names.
 * Archives before 26.11 hold a uid there, like the database did.
 * IN user - user name, uid string, or NULL
 * RET quoted user name, or uid_to_db_value() of a uid. xfree() it.
 */
extern char *archive_user_value(char *user);

/*
 * is_user_any_coord - is the user coord of any account
 *
 * IN pg_conn: database connection
 * IN/OUT user: user record, which will be filled in
 * RET: 1 if the user is coord of some account, 0 else
 */
extern bool is_user_any_coord(void *db_conn, slurmdb_user_rec_t *user);

extern char *acct_get_db_name(void);

/*
 * Find or add the kv table row for a document hash, without its document.
 *
 * IN cluster - cluster whose kv table to use
 * IN hash - kv_hash() of the document, or NULL for no document
 * OUT hash_inx - row to reference, or 0 for no document
 * OUT need_doc - optional, set when the row has no document yet, so the
 *	sender should send it
 * RET SLURM_SUCCESS or SLURM_ERROR
 */
extern int kv_table_get_hash_inx(mysql_conn_t *mysql_conn, char *cluster,
				 char *hash, uint64_t *hash_inx,
				 bool *need_doc);

/*
 * Find or add the kv table row for a document, storing the document.
 *
 * IN cluster - cluster whose kv table to use
 * IN kv - document. NULL, null, and an empty object mean no document.
 * IN hash - hash to store kv under, as the sender computed it, or NULL to
 *	compute it with kv_hash()
 * OUT hash_inx - row to reference, or 0 for no document
 * RET SLURM_SUCCESS or SLURM_ERROR
 */
extern int kv_table_store(mysql_conn_t *mysql_conn, char *cluster,
			  const data_t *kv, char *hash, uint64_t *hash_inx);

extern time_t archive_setup_end_time(time_t last_submit, uint32_t purge);
extern int archive_run_script(slurmdb_archive_cond_t *arch_cond,
			      char *cluster_name, time_t last_submit);
extern int archive_write_file(buf_t *buffer, char *cluster_name,
			      time_t period_start, time_t period_end,
			      char *arch_dir, char *arch_type,
			      uint32_t archive_period);

#endif
