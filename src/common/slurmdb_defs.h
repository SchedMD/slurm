/*****************************************************************************\
 *  slurmdb_defs.h - definitions used by slurmdb api
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

/* This is used to point out constants that exist in the
 * TRES records.  This should be the same order as
 * the enum pointing out the order in the array that is defined in
 * src/slurmctld/slurmctld.h.  If this changes please also update
 * src/plugins/accounting_storage/filetxt/accounting_storage_filetxt.c
 * acct_storage_p_get_tres() to reflect things as it is static.
 */
typedef enum {
	TRES_CPU = 1,
	TRES_MEM,
	TRES_ENERGY,
	TRES_NODE,
	TRES_BILLING,
	TRES_FS_DISK,
	TRES_VMEM,
	TRES_PAGES,
	TRES_STATIC_CNT
} tres_types_t;

/* These #defines are for the tres_str functions below and should be
 * sent when flags are allowed in the functions.
 */
#define TRES_STR_FLAG_NONE        0x00000000 /* No flags, meaning by
					      * default the string
					      * will contain -1 and
					      * be unique honoring
					      * the first value found
					      * in an incoming string */
#define TRES_STR_FLAG_ONLY_CONCAT 0x00000001 /* Only concat the
					      * string, this will
					      * most likely trump the
					      * other flags below. */
#define TRES_STR_FLAG_REPLACE     0x00000002 /* Replace previous count
					      * values found, if this
					      * is not set duplicate
					      * entries will be skipped. */
#define TRES_STR_FLAG_REMOVE      0x00000004 /* If -1 entries are
					      * found remove them, by
					      * default they will be
					      * added to the string
					      */
#define TRES_STR_FLAG_SORT_ID     0x00000008 /* sort string by ID */
#define TRES_STR_FLAG_SIMPLE      0x00000010 /* make a simple string */
#define TRES_STR_FLAG_COMMA1      0x00000020 /* make a first char a comma */
#define TRES_STR_FLAG_NO_NULL     0x00000040 /* return blank string
					      * instead of NULL */
#define TRES_STR_CONVERT_UNITS    0x00000080 /* Convert number units */
#define TRES_STR_FLAG_SUM         0x00000100 /* Sum entries of the same type
					      * ignoring -1 */
#define TRES_STR_FLAG_MAX         0x00000200 /* Set Max value from entries of
					      * the same type ignoring -1 */
#define TRES_STR_FLAG_MIN         0x00000400 /* Set Min value from entries of
					      * the same type ignoring -1 */
#define TRES_STR_FLAG_ALLOW_REAL  0x00000800 /* Allow all counts (even zero)
					      * unless INFINITE64 or NO_VAL64 */
#define TRES_STR_FLAG_BYTES       0x00000800 /* Convertable Usage in Bytes */

typedef struct {
	slurmdb_cluster_rec_t *cluster_rec;
	int preempt_cnt;
	time_t start_time;
} local_cluster_rec_t;

extern slurmdb_job_rec_t *slurmdb_create_job_rec();
extern slurmdb_step_rec_t *slurmdb_create_step_rec();
extern slurmdb_assoc_usage_t *slurmdb_create_assoc_usage(int tres_cnt);
extern slurmdb_qos_usage_t *slurmdb_create_qos_usage(int tres_cnt);

extern char *slurmdb_cluster_fed_states_str(uint32_t states);
extern uint32_t str_2_cluster_fed_states(char *states);
extern char *slurmdb_federation_flags_str(uint32_t flags);
extern uint32_t str_2_federation_flags(char *flags, int option);
extern char *slurmdb_job_flags_str(uint32_t flags);
extern uint32_t str_2_job_flags(char *flags);
extern char *slurmdb_qos_str(List qos_list, uint32_t level);
extern uint32_t str_2_slurmdb_qos(List qos_list, char *level);
extern char *slurmdb_qos_flags_str(uint32_t flags);
extern uint32_t str_2_qos_flags(char *flags, int option);
extern char *slurmdb_res_flags_str(uint32_t flags);
extern uint32_t str_2_res_flags(char *flags, int option);
extern char *slurmdb_res_type_str(slurmdb_resource_type_t type);

extern char *slurmdb_admin_level_str(slurmdb_admin_level_t level);
extern slurmdb_admin_level_t str_2_slurmdb_admin_level(char *level);

/* The next three functions have pointers to assoc_list so do not
 * destroy assoc_list before using the list returned from this function.
 */
extern List slurmdb_get_hierarchical_sorted_assoc_list(List assoc_list,
						       bool use_lft);
extern List slurmdb_get_acct_hierarchical_rec_list(List assoc_list);
extern List slurmdb_get_acct_hierarchical_rec_list_no_lft(List assoc_list);

/* This reorders the list into a alphabetical hierarchy.
   IN/OUT: assoc_list
 */
extern void slurmdb_sort_hierarchical_assoc_list(List assoc_list, bool use_lft);

/* IN/OUT: tree_list a list of slurmdb_print_tree_t's */
extern char *slurmdb_tree_name_get(char *name, char *parent, List tree_list);

extern int set_qos_bitstr_from_string(bitstr_t *valid_qos, char *names);
extern int set_qos_bitstr_from_list(bitstr_t *valid_qos, List qos_list);
extern char *get_qos_complete_str_bitstr(List qos_list, bitstr_t *valid_qos);
extern List get_qos_name_list(List qos_list, List num_qos_list);
extern char *get_qos_complete_str(List qos_list, List num_qos_list);

extern char *get_classification_str(uint16_t classification);
extern uint16_t str_2_classification(char *classification);

extern const char *rollup_interval_to_string(int interval);

extern char *slurmdb_problem_str_get(uint16_t problem);
extern uint16_t str_2_slurmdb_problem(char *problem);

extern void log_assoc_rec(slurmdb_assoc_rec_t *assoc_ptr, List qos_list);

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

/* OUT: out - copy grp/max/qos limits from in
 * IN:  in  - what to copy from
 */
extern void slurmdb_copy_assoc_rec_limits(slurmdb_assoc_rec_t *out,
					  slurmdb_assoc_rec_t *in);
extern void slurmdb_copy_cluster_rec(slurmdb_cluster_rec_t *out,
				     slurmdb_cluster_rec_t *in);
extern void slurmdb_copy_federation_rec(slurmdb_federation_rec_t *out,
					slurmdb_federation_rec_t *in);
extern void slurmdb_copy_qos_rec_limits(slurmdb_qos_rec_t *out,
					slurmdb_qos_rec_t *in);
extern slurmdb_tres_rec_t *slurmdb_copy_tres_rec(slurmdb_tres_rec_t *tres);
extern List slurmdb_copy_tres_list(List tres);
extern List slurmdb_diff_tres_list(List tres_list_old, List tres_list_new);
extern char *slurmdb_tres_string_combine_lists(
	List tres_list_old, List tres_list_new);
/* make a tres_string from a given list
 * IN tres - list of slurmdb_tres_rec_t's
 * IN flags - see the TRES_STR_FLAGS above
 *                 Meaningful flags are TRES_STR_FLAG_SIMPLE
 *                                      TRES_STR_FLAG_COMMA1
 * RET char * of tres_str
 */
extern char *slurmdb_make_tres_string(List tres, uint32_t flags);
extern char *slurmdb_format_tres_str(
	char *tres_in, List full_tres_list, bool simple);
/*
 * Comparator used for sorting tres by id
 *
 * returns: -1 tres_a < tres_b   0: tres_a == tres_b   1: tres_a > tres_b
 *
 */
extern int slurmdb_sort_tres_by_id_asc(void *v1, void *v2);

/* Used to turn a tres string into a list containing
 * slurmdb_tres_rec_t's with only id's and counts filled in, no
 * formatted types or names.
 *
 * IN/OUT: tres_list - list created from the simple tres string
 * IN    : tres - simple string you want convert
 * IN    : flags - see the TRES_STR_FLAGS above
 *                 Meaningful flags are TRES_STR_FLAG_REPLACE
 *                                      TRES_STR_FLAG_REMOVE
 *                                      TRES_STR_FLAG_SORT_ID
 */
extern void slurmdb_tres_list_from_string(
	List *tres_list, const char *tres, uint32_t flags);

/* combine a name array and count array into a string */
extern char *slurmdb_make_tres_string_from_arrays(char **tres_names,
						  uint64_t *tres_cnts,
						  uint32_t tres_cnt,
						  uint32_t flags);

extern char *slurmdb_make_tres_string_from_simple(
	char *tres_in, List full_tres_list, int spec_unit,
	uint32_t convert_flags, uint32_t tres_str_flags, char *nodes);

/* Used to combine 2 different TRES strings together
 *
 * IN/OUT: tres_str_old - original simple tres string
 * IN    : tres_str_new - string you want added
 * IN    : flags - see the TRES_STR_FLAGS above
 *                 Meaningful flags are TRES_STR_FLAG_ONLY_CONCAT
 *                                      TRES_STR_FLAG_REPLACE
 *                                      TRES_STR_FLAG_REMOVE
 *                                      TRES_STR_FLAG_SORT_ID
 *                                      TRES_STR_FLAG_SIMPLE
 *                                      TRES_STR_FLAG_COMMA1
 *                                      TRES_STR_FLAG_NO_NULL
 * RET   : new tres_str_old - the new string (also sent out)
 */
extern char *slurmdb_combine_tres_strings(
	char **tres_str_old, char *tres_str_new, uint32_t flags);
extern slurmdb_tres_rec_t *slurmdb_find_tres_in_string(
	char *tres_str_in, int id);
extern uint64_t slurmdb_find_tres_count_in_string(char *tres_str_in, int id);
extern int slurmdb_find_qos_in_list_by_name(void *x, void *key);
extern int slurmdb_find_qos_in_list(void *x, void *key);
extern int slurmdb_find_selected_step_in_list(void *x, void *key);
extern int slurmdb_find_assoc_in_list(void *x, void *key);
extern int slurmdb_find_update_object_in_list(void *x, void *key);
extern int slurmdb_find_tres_in_list(void *x, void *key);
extern int slurmdb_find_tres_in_list_by_count(void *x, void *key);
extern int slurmdb_find_tres_in_list_by_type(void *x, void *key);
extern int slurmdb_find_cluster_in_list(void *x, void *key);
extern int slurmdb_find_cluster_accting_tres_in_list(void *x, void *key);
extern int slurmdb_add_cluster_accounting_to_tres_list(
	slurmdb_cluster_accounting_rec_t *accting,
	List *tres);
extern int slurmdb_add_accounting_to_tres_list(
	slurmdb_accounting_rec_t *accting,
	List *tres);
extern int slurmdb_add_time_from_count_to_tres_list(
	slurmdb_tres_rec_t *tres_in, List *tres, time_t elapsed);
extern int slurmdb_sum_accounting_list(
	slurmdb_cluster_accounting_rec_t *accting,
	List *total_tres_acct);
extern void slurmdb_transfer_acct_list_2_tres(
	List accounting_list, List *tres);
extern void slurmdb_transfer_tres_time(
	List *tres_list_out, char *tres_str, int elapsed);

extern int slurmdb_get_tres_base_unit(char *tres_type);
extern char *slurmdb_ave_tres_usage(char *tres_string, int tasks);

/* Setup cluster rec with plugin_id that indexes into select list */
extern int slurmdb_setup_cluster_rec(slurmdb_cluster_rec_t *cluster_rec);

extern void slurmdb_job_cond_def_start_end(slurmdb_job_cond_t *job_cond);
extern int slurmdb_job_sort_by_submit_time(void *v1, void *v2);

/*
 * Merge bitmap2 into bitmap1 creating it if it doesn't exist.
 * Also add counts from job_cnt2 (if NULL then just add 1) to job_cnt1.
 */
extern void slurmdb_merge_grp_node_usage(bitstr_t **grp_node_bitmap1,
					 uint16_t **grp_node_job_cnt1,
					 bitstr_t *grp_node_bitmap2,
					 uint16_t *grp_node_job_cnt2);

extern char *slurmdb_get_job_id_str(slurmdb_job_rec_t *job);

#endif
