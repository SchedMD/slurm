/*****************************************************************************\
 *  slurmdb_defs.h - definitions used by slurmdb api
 ******************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble da@llnl.gov, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
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
#ifndef _SLURMDB_DEFS_H
#define _SLURMDB_DEFS_H

extern void destroy_acct_user_rec(void *object);
extern void destroy_acct_account_rec(void *object);
extern void destroy_acct_coord_rec(void *object);
extern void destroy_cluster_accounting_rec(void *object);
extern void destroy_acct_cluster_rec(void *object);
extern void destroy_acct_accounting_rec(void *object);
extern void destroy_acct_association_rec(void *object);
extern void destroy_acct_event_rec(void *object);
extern void destroy_acct_qos_rec(void *object);
extern void destroy_acct_reservation_rec(void *object);
extern void destroy_acct_txn_rec(void *object);
extern void destroy_acct_wckey_rec(void *object);
extern void destroy_acct_archive_rec(void *object);

extern void destroy_acct_user_cond(void *object);
extern void destroy_acct_account_cond(void *object);
extern void destroy_acct_cluster_cond(void *object);
extern void destroy_acct_association_cond(void *object);
extern void destroy_acct_event_cond(void *object);
extern void destroy_acct_job_cond(void *object);
extern void destroy_acct_qos_cond(void *object);
extern void destroy_acct_reservation_cond(void *object);
extern void destroy_acct_txn_cond(void *object);
extern void destroy_acct_wckey_cond(void *object);
extern void destroy_acct_archive_cond(void *object);

extern void destroy_acct_update_object(void *object);
extern void destroy_acct_used_limits(void *object);
extern void destroy_update_shares_rec(void *object);
extern void destroy_acct_print_tree(void *object);
extern void destroy_acct_hierarchical_rec(void *object);

extern void init_acct_association_rec(acct_association_rec_t *assoc);
extern void init_acct_qos_rec(acct_qos_rec_t *qos);

#endif
