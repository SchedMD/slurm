/*****************************************************************************\
 *  accounting_storage_mysql.h - accounting interface to mysql header file.
 *
 *  $Id: accounting_storage_mysql.h 13061 2008-01-22 21:23:56Z da $
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#ifndef _HAVE_ACCOUNTING_STORAGE_MYSQL_H
#define _HAVE_ACCOUNTING_STORAGE_MYSQL_H

#include <strings.h>
#include <stdlib.h>

#include "src/common/assoc_mgr.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/slurm_auth.h"
#include "src/common/uid.h"

#include "src/database/mysql_common.h"

#include "src/slurmdbd/read_config.h"

#include "../common/common_as.h"

extern char *acct_coord_table;
extern char *acct_table;
extern char *assoc_day_table;
extern char *assoc_hour_table;
extern char *assoc_month_table;
extern char *assoc_table;
extern char *cluster_day_table;
extern char *cluster_hour_table;
extern char *cluster_month_table;
extern char *cluster_table;
extern char *event_table;
extern char *job_table;
extern char *last_ran_table;
extern char *qos_table;
extern char *resv_table;
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
extern List mysql_cluster_list;
extern pthread_mutex_t mysql_cluster_list_lock;


typedef enum {
	QOS_LEVEL_NONE,
	QOS_LEVEL_SET,
	QOS_LEVEL_MODIFY
} qos_level_t;

/*global functions */
extern int check_connection(mysql_conn_t *mysql_conn);
extern char *fix_double_quotes(char *str);
extern int last_affected_rows(MYSQL *mysql_db);
extern int setup_association_limits(acct_association_rec_t *assoc,
				    char **cols, char **vals,
				    char **extra, qos_level_t qos_level,
				    bool get_fs);
extern int modify_common(mysql_conn_t *mysql_conn,
			 uint16_t type,
			 time_t now,
			 char *user_name,
			 char *table,
			 char *cond_char,
			 char *vals);
extern int remove_common(mysql_conn_t *mysql_conn,
			 uint16_t type,
			 time_t now,
			 char *user_name,
			 char *table,
			 char *name_char,
			 char *assoc_char);

/*local api functions */
extern int acct_storage_p_commit(mysql_conn_t *mysql_conn, bool commit);

extern int acct_storage_p_add_associations(mysql_conn_t *mysql_conn,
					   uint32_t uid,
					   List association_list);

extern int acct_storage_p_add_wckeys(mysql_conn_t *mysql_conn, uint32_t uid,
				     List wckey_list);

extern List acct_storage_p_get_associations(
	mysql_conn_t *mysql_conn, uid_t uid,
	acct_association_cond_t *assoc_cond);

extern List acct_storage_p_get_wckeys(mysql_conn_t *mysql_conn, uid_t uid,
				      acct_wckey_cond_t *wckey_cond);

extern int acct_storage_p_get_usage(mysql_conn_t *mysql_conn, uid_t uid,
				    void *in, slurmdbd_msg_type_t type,
				    time_t start, time_t end);

extern int clusteracct_storage_p_get_usage(
	mysql_conn_t *mysql_conn, uid_t uid,
	acct_cluster_rec_t *cluster_rec,  slurmdbd_msg_type_t type,
	time_t start, time_t end);

extern List acct_storage_p_remove_coord(mysql_conn_t *mysql_conn, uint32_t uid,
					List acct_list,
					acct_user_cond_t *user_cond);

extern List acct_storage_p_remove_wckeys(mysql_conn_t *mysql_conn,
					 uint32_t uid,
					 acct_wckey_cond_t *wckey_cond);

#endif
