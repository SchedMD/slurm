/*****************************************************************************\
 *  common_as.h - header for common functions for accounting storage
 *
 *  $Id: common_as.c 13061 2008-01-22 21:23:56Z da $
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
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

#ifndef _HAVE_COMMON_AS_H
#define _HAVE_COMMON_AS_H

#include "src/common/assoc_mgr.h"

extern char *fix_double_quotes(char *str);

extern int send_accounting_update(List update_list, char *cluster, char *host,
				  uint16_t port, uint16_t rpc_version);

extern int update_assoc_mgr(List update_list);

extern int addto_update_list(List update_list, acct_update_type_t type,
			     void *object);

extern void dump_update_list(List update_list);

extern int cluster_first_reg(char *host, uint16_t port, uint16_t rpc_version);

extern int set_usage_information(char **usage_table, slurmdbd_msg_type_t type,
				 time_t *usage_start, time_t *usage_end);

extern void merge_delta_qos_list(List qos_list, List delta_qos_list);

extern bool is_user_min_admin_level(void *db_conn, uid_t uid,
				    acct_admin_level_t min_level);

/*
 * is_user_coord - whether user is coord of account
 *
 * IN user: user
 * IN account: account
 * RET: 1 if user is coord of account
 */
extern bool is_user_coord(acct_user_rec_t *user, char *account);

/*
 * is_user_any_coord - is the user coord of any account
 *
 * IN pg_conn: database connection
 * IN/OUT user: user record, which will be filled in
 * RET: 1 if the user is coord of some account, 0 else
 */
extern bool is_user_any_coord(void *db_conn, acct_user_rec_t *user);

#endif
