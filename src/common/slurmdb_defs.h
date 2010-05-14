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

#include <slurm/slurmdb.h>

/* Defined purge macros */
#define SLURMDB_PURGE_GET_UNITS(_X) \
	(_X & SLURMDB_PURGE_BASE)
#define SLURMDB_PURGE_ARCHIVE_SET(_X) \
	(_X & SLURMDB_PURGE_ARCHIVE)
#define SLURMDB_PURGE_IN_HOURS(_X) \
	(_X & SLURMDB_PURGE_HOURS)
#define SLURMDB_PURGE_IN_DAYS(_X) \
	(_X & SLURMDB_PURGE_DAYS)
#define SLURMDB_PURGE_IN_MONTHS(_X) \
	(_X & SLURMDB_PURGE_MONTHS)

extern slurmdb_step_rec_t *slurmdb_create_step_rec();
extern slurmdb_job_rec_t *slurmdb_create_job_rec();

extern void slurmdb_destroy_user_rec(void *object);
extern void slurmdb_destroy_account_rec(void *object);
extern void slurmdb_destroy_coord_rec(void *object);
extern void slurmdb_destroy_cluster_accounting_rec(void *object);
extern void slurmdb_destroy_cluster_rec(void *object);
extern void slurmdb_destroy_accounting_rec(void *object);
extern void slurmdb_destroy_association_rec(void *object);
extern void slurmdb_destroy_event_rec(void *object);
extern void slurmdb_destroy_job_rec(void *object);
extern void slurmdb_destroy_qos_rec(void *object);
extern void slurmdb_destroy_reservation_rec(void *object);
extern void slurmdb_destroy_step_rec(void *object);
extern void slurmdb_destroy_txn_rec(void *object);
extern void slurmdb_destroy_wckey_rec(void *object);
extern void slurmdb_destroy_archive_rec(void *object);
extern void slurmdb_destroy_report_assoc_rec(void *object);
extern void slurmdb_destroy_report_user_rec(void *object);
extern void slurmdb_destroy_report_cluster_rec(void *object);

extern void slurmdb_destroy_user_cond(void *object);
extern void slurmdb_destroy_account_cond(void *object);
extern void slurmdb_destroy_cluster_cond(void *object);
extern void slurmdb_destroy_association_cond(void *object);
extern void slurmdb_destroy_event_cond(void *object);
extern void slurmdb_destroy_job_cond(void *object);
extern void slurmdb_destroy_qos_cond(void *object);
extern void slurmdb_destroy_reservation_cond(void *object);
extern void slurmdb_destroy_txn_cond(void *object);
extern void slurmdb_destroy_wckey_cond(void *object);
extern void slurmdb_destroy_archive_cond(void *object);

extern void slurmdb_destroy_update_object(void *object);
extern void slurmdb_destroy_used_limits(void *object);
extern void slurmdb_destroy_update_shares_rec(void *object);
extern void slurmdb_destroy_print_tree(void *object);
extern void slurmdb_destroy_hierarchical_rec(void *object);
extern void slurmdb_destroy_selected_step(void *object);

extern void slurmdb_destroy_report_job_grouping(void *object);
extern void slurmdb_destroy_report_acct_grouping(void *object);
extern void slurmdb_destroy_report_cluster_grouping(void *object);

extern void slurmdb_init_association_rec(slurmdb_association_rec_t *assoc);
extern void slurmdb_init_qos_rec(slurmdb_qos_rec_t *qos);

extern uint32_t slurmdb_setup_cluster_flags();
extern List slurmdb_get_info_cluster(char *cluster_name);
extern char *slurmdb_qos_str(List qos_list, uint32_t level);
extern uint32_t str_2_slurmdb_qos(List qos_list, char *level);
extern char *slurmdb_admin_level_str(slurmdb_admin_level_t level);
extern slurmdb_admin_level_t str_2_slurmdb_admin_level(char *level);

/* The next two functions have pointers to assoc_list so do not
 * destroy assoc_list before using the list returned from this function.
 */
extern List slurmdb_get_hierarchical_sorted_assoc_list(List assoc_list);
extern List slurmdb_get_acct_hierarchical_rec_list(List assoc_list);

/* IN/OUT: tree_list a list of slurmdb_print_tree_t's */
extern char *slurmdb_tree_name_get(char *name, char *parent, List tree_list);

extern int set_qos_bitstr_from_list(bitstr_t *valid_qos, List qos_list);
extern char *get_qos_complete_str_bitstr(List qos_list, bitstr_t *valid_qos);
extern char *get_qos_complete_str(List qos_list, List num_qos_list);

extern char *get_classification_str(uint16_t class);
extern uint16_t str_2_classification(char *class);

extern char *slurmdb_problem_str_get(uint16_t problem);
extern uint16_t str_2_slurmdb_problem(char *problem);

extern void log_assoc_rec(slurmdb_association_rec_t *assoc_ptr, List qos_list);

extern int slurmdb_report_set_start_end_time(time_t *start, time_t *end);

extern uint32_t slurmdb_parse_purge(char *string);
extern char *slurmdb_purge_string(uint32_t purge, char *string, int len,
				  bool with_archive);


#endif
