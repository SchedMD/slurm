/*****************************************************************************\
 *  accounting_storage_mysql.h - accounting interface to as_mysql header file.
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

#ifndef _HAVE_ACCOUNTING_STORAGE_MYSQL_H
#define _HAVE_ACCOUNTING_STORAGE_MYSQL_H

#include <string.h>
#include <stdlib.h>

/* we can't include common/slurm_xlator.h here since it contains
 * list_push and list_pop which are mysql macros what for some reason
 * are given to us.  So if you need something from the header just
 * copy it here.
 */
#define	fatal			slurm_fatal
#define	error			slurm_error
#define	info			slurm_info
#define	verbose			slurm_verbose
#define	debug			slurm_debug
#define	debug2			slurm_debug2
#define	debug3			slurm_debug3
#define	debug4			slurm_debug4
#define	debug5			slurm_debug5

/*
 * Allow up to 999 static TRES
 * NOTE: If this changes for some reason you will also need to update the 1001
 * in accounting_storage_mysql.c...
 *
 * 	if (mysql_db_create_table(mysql_conn, tres_table,
 *				  tres_table_fields,
 *				  ", primary key (id), "
 *				  "unique index (type(20), name(20))) "
 *				  "auto_increment=1001")
 *
 */
#define TRES_OFFSET 1000

#include "src/common/assoc_mgr.h"
#include "src/common/macros.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/slurm_auth.h"
#include "src/common/uid.h"

#include "src/database/mysql_common.h"

#include "src/slurmdbd/read_config.h"

#include "../common/common_as.h"

extern char *acct_coord_table;
extern char *acct_table;
extern char *tres_table;
extern char *assoc_day_table;
extern char *assoc_hour_table;
extern char *assoc_month_table;
extern char *assoc_table;
extern char *clus_res_table;
extern char *cluster_day_table;
extern char *cluster_hour_table;
extern char *cluster_month_table;
extern char *cluster_table;
extern char *convert_version_table;
extern char *federation_table;
extern char *event_table;
extern char *job_table;
extern char *last_ran_table;
extern char *qos_table;
extern char *resv_table;
extern char *res_table;
extern char *step_table;
extern char *txn_table;
extern char *user_table;
extern char *suspend_table;
extern char *wckey_day_table;
extern char *wckey_hour_table;
extern char *wckey_month_table;
extern char *wckey_table;

/* Since tables are cluster centric we have a global cluster list to
 * go off of.
 */
extern List as_mysql_cluster_list;
extern List as_mysql_total_cluster_list;
extern pthread_mutex_t as_mysql_cluster_list_lock;

extern uint64_t debug_flags;
extern bool backup_dbd;

typedef enum {
	QOS_LEVEL_NONE,
	QOS_LEVEL_SET,
	QOS_LEVEL_MODIFY
} qos_level_t;

#define DB_DEBUG(conn, fmt, ...) \
	info("%d(%s:%d) "fmt, conn, THIS_FILE, __LINE__, ##__VA_ARGS__);

/*global functions */
extern int check_connection(mysql_conn_t *mysql_conn);
extern char *fix_double_quotes(char *str);
extern int last_affected_rows(mysql_conn_t *mysql_conn);
extern void reset_mysql_conn(mysql_conn_t *mysql_conn);
extern int create_cluster_assoc_table(
	mysql_conn_t *mysql_conn, char *cluster_name);
extern int create_cluster_tables(mysql_conn_t *mysql_conn, char *cluster_name);
extern int remove_cluster_tables(mysql_conn_t *mysql_conn, char *cluster_name);
extern int setup_assoc_limits(slurmdb_assoc_rec_t *assoc,
				    char **cols, char **vals,
				    char **extra, qos_level_t qos_level,
				    bool for_add);
extern int modify_common(mysql_conn_t *mysql_conn,
			 uint16_t type,
			 time_t now,
			 char *user_name,
			 char *table,
			 char *cond_char,
			 char *vals,
			 char *cluster_name);
extern int remove_common(mysql_conn_t *mysql_conn,
			 uint16_t type,
			 time_t now,
			 char *user_name,
			 char *table,
			 char *name_char,
			 char *assoc_char,
			 char *cluster_name,
			 List ret_list,
			 bool *jobs_running);

extern void mod_tres_str(char **out, char *mod, char *cur,
			 char *cur_par, char *name, char **vals,
			 uint32_t id, bool assoc);


/*local api functions */
extern int acct_storage_p_commit(mysql_conn_t *mysql_conn, bool commit);

extern int acct_storage_p_add_assocs(mysql_conn_t *mysql_conn,
					   uint32_t uid,
					   List assoc_list);

extern int acct_storage_p_add_wckeys(mysql_conn_t *mysql_conn, uint32_t uid,
				     List wckey_list);

extern List acct_storage_p_get_assocs(
	mysql_conn_t *mysql_conn, uid_t uid,
	slurmdb_assoc_cond_t *assoc_cond);

extern List acct_storage_p_get_wckeys(mysql_conn_t *mysql_conn, uid_t uid,
				      slurmdb_wckey_cond_t *wckey_cond);

extern int acct_storage_p_get_usage(mysql_conn_t *mysql_conn, uid_t uid,
				    void *in, slurmdbd_msg_type_t type,
				    time_t start, time_t end);

extern int clusteracct_storage_p_get_usage(
	mysql_conn_t *mysql_conn, uid_t uid,
	slurmdb_cluster_rec_t *cluster_rec,  slurmdbd_msg_type_t type,
	time_t start, time_t end);

extern List acct_storage_p_remove_coord(mysql_conn_t *mysql_conn, uint32_t uid,
					List acct_list,
					slurmdb_user_cond_t *user_cond);

extern List acct_storage_p_remove_wckeys(mysql_conn_t *mysql_conn,
					 uint32_t uid,
					 slurmdb_wckey_cond_t *wckey_cond);

#endif
