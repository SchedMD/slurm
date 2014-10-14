/*****************************************************************************\
 *  slurmdb_defs.h - definitions used by slurmdb api
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
#ifndef _SLURMDB_DEFS_H
#define _SLURMDB_DEFS_H

#include "slurm/slurmdb.h"

/* Defined purge macros */
#define SLURMDB_PURGE_GET_UNITS(_X) \
	(_X & SLURMDB_PURGE_BASE)
#define SLURMDB_PURGE_ARCHIVE_SET(_X) \
	(_X != NO_VAL && _X & SLURMDB_PURGE_ARCHIVE)
#define SLURMDB_PURGE_IN_HOURS(_X) \
	(_X != NO_VAL && _X & SLURMDB_PURGE_HOURS)
#define SLURMDB_PURGE_IN_DAYS(_X) \
	(_X != NO_VAL && _X & SLURMDB_PURGE_DAYS)
#define SLURMDB_PURGE_IN_MONTHS(_X) \
	(_X != NO_VAL && _X & SLURMDB_PURGE_MONTHS)

extern slurmdb_step_rec_t *slurmdb_create_step_rec();
extern slurmdb_job_rec_t *slurmdb_create_job_rec();

extern char *slurmdb_qos_str(List qos_list, uint32_t level);
extern uint32_t str_2_slurmdb_qos(List qos_list, char *level);
extern char *slurmdb_qos_flags_str(uint32_t flags);
extern uint32_t str_2_qos_flags(char *flags, int option);
extern char *slurmdb_res_flags_str(uint32_t flags);
extern uint32_t str_2_res_flags(char *flags, int option);
extern char *slurmdb_res_type_str(slurmdb_resource_type_t type);

extern char *slurmdb_admin_level_str(slurmdb_admin_level_t level);
extern slurmdb_admin_level_t str_2_slurmdb_admin_level(char *level);

/* The next two functions have pointers to assoc_list so do not
 * destroy assoc_list before using the list returned from this function.
 */
extern List slurmdb_get_hierarchical_sorted_assoc_list(List assoc_list);
extern List slurmdb_get_acct_hierarchical_rec_list(List assoc_list);

/* This reorders the list into a alphabetical hierarchy.
   IN/OUT: assoc_list
 */
extern void slurmdb_sort_hierarchical_assoc_list(List assoc_list);

/* IN/OUT: tree_list a list of slurmdb_print_tree_t's */
extern char *slurmdb_tree_name_get(char *name, char *parent, List tree_list);

extern int set_qos_bitstr_from_list(bitstr_t *valid_qos, List qos_list);
extern char *get_qos_complete_str_bitstr(List qos_list, bitstr_t *valid_qos);
extern char *get_qos_complete_str(List qos_list, List num_qos_list);

extern char *get_classification_str(uint16_t classification);
extern uint16_t str_2_classification(char *classification);

extern char *slurmdb_problem_str_get(uint16_t problem);
extern uint16_t str_2_slurmdb_problem(char *problem);

extern void log_assoc_rec(slurmdb_association_rec_t *assoc_ptr, List qos_list);

extern int slurmdb_report_set_start_end_time(time_t *start, time_t *end);

extern uint32_t slurmdb_parse_purge(char *string);
extern char *slurmdb_purge_string(uint32_t purge, char *string, int len,
				  bool with_archive);
extern int slurmdb_addto_qos_char_list(List char_list, List qos_list,
				       char *names, int option);
extern int slurmdb_send_accounting_update(List update_list, char *cluster,
					  char *host, uint16_t port,
					  uint16_t rpc_version);
extern slurmdb_report_cluster_rec_t *slurmdb_cluster_rec_2_report(
	slurmdb_cluster_rec_t *cluster);

/* OUT: job_id_str - filled in with the id of the job/array
 * RET: job_id_str */
extern char *slurmdb_get_selected_step_id(
	char *job_id_str, int len,
	slurmdb_selected_step_t *selected_step);

#endif
