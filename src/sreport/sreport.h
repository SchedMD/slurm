/*****************************************************************************\
 *  sreport.h - report generating tool for slurm accounting header file.
 *****************************************************************************
 *  Copyright (C) 2010-2015 SchedMD LLC.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#ifndef __SREPORT_H__
#define __SREPORT_H__

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if HAVE_READLINE
#  include <readline/readline.h>
#  include <readline/history.h>
#endif

#include "slurm/slurm.h"
#include "slurm/slurmdb.h"

#include "src/common/slurm_jobacct_gather.h"
#include "src/common/parse_time.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xstring.h"
#include "src/common/print_fields.h"

#define	MAX_INPUT_FIELDS 128

typedef enum {
	GROUP_BY_ACCOUNT,
	GROUP_BY_ACCOUNT_JOB_SIZE,
	GROUP_BY_ACCOUNT_JOB_SIZE_DURATION,
	GROUP_BY_USER,
	GROUP_BY_USER_JOB_SIZE,
	GROUP_BY_USER_JOB_SIZE_DURATION,
	GROUP_BY_NONE
} report_grouping_t;

extern slurmdb_report_time_format_t time_format;
extern char *time_format_string;
extern char *command_name;
extern int exit_code;	/* sacctmgr's exit code, =1 on any error at any time */
extern int exit_flag;	/* program to terminate if =1 */
extern char *fed_name;	/* Set if operating in federation mode */
extern int input_words;	/* number of words of input permitted */
extern int quiet_flag;	/* quiet=1, verbose=-1, normal=0 */
extern int sort_user_tres_id; /* controls sorting users (eg sort_user_dec) */
extern char *tres_str;	/* --tres= value */
extern List g_tres_list;/* tres list from database - unaltered */
extern List tres_list;	/* TRES list based of tres_str (--tres=str) */
extern void *db_conn;
extern int all_clusters_flag;
extern slurmdb_report_sort_t sort_flag;
extern char *cluster_flag;
extern char *tres_usage_str;
extern bool user_case_norm;

extern void slurmdb_report_print_time(print_field_t *field,
			       uint64_t value, uint64_t total_time, int last);
extern int parse_option_end(char *option);
extern time_t sanity_check_endtime(time_t endtime);
extern char *strip_quotes(char *option, int *increased);
extern int sort_user_dec(void *, void *);
extern int sort_cluster_dec(void *, void *);
extern int sort_assoc_dec(void *, void *);

extern int get_uint(char *in_value, uint32_t *out_value, char *type);

/* Fills in cluster_tres_rec and tres_rec and validates tres_rec has
 * allocated seconds.  As we still want to print a line if the usage
 * is zero NULLs must be handled after the function is called.
 */
extern void sreport_set_tres_recs(slurmdb_tres_rec_t **cluster_tres_rec,
				  slurmdb_tres_rec_t **tres_rec,
				  List cluster_tres_list, List tres_list,
				  slurmdb_tres_rec_t *tres_rec_in);

/* Since usage columns can get big, instead of always giving a 20
 * column spacing, figure it out here.
 */
extern void sreport_set_usage_col_width(print_field_t *field, uint64_t number);

extern void sreport_set_usage_column_width(print_field_t *usage_field,
					   print_field_t *energy_field,
					   List slurmdb_report_cluster_list);

/* For duplicate user/account records, combine TRES records into the original
 * list and purge the duplicate records */
extern void combine_assoc_tres(List first_assoc_list, List new_assoc_list);

/* Given two TRES lists, combine the content of the second with the first,
 * adding the counts for duplicate TRES IDs */
extern void combine_tres_list(List orig_tres_list, List dup_tres_list);

/* For duplicate user/account records, combine TRES records into the original
 * list and purge the duplicate records */
extern void combine_user_tres(List first_user_list, List new_user_list);

#endif /* HAVE_SREPORT_H */
