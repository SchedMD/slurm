/*****************************************************************************\
 *  sacctmgr.h - definitions for all sacctmgr modules.
 *****************************************************************************
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#ifndef __SACCTMGR_H__
#define __SACCTMGR_H__

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_GETOPT_H
#  include <getopt.h>
#else
#  include "src/common/getopt.h"
#endif

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_STRING_H
#  include <string.h>
#endif
#ifdef HAVE_STRINGS_H
#  include <strings.h>
#endif
#include <time.h>
#include <unistd.h>

#if HAVE_READLINE
#  include <readline/readline.h>
#  include <readline/history.h>
#endif

#if HAVE_INTTYPES_H
#  include <inttypes.h>
#else  /* !HAVE_INTTYPES_H */
#  if HAVE_STDINT_H
#    include <stdint.h>
#  endif
#endif  /* HAVE_INTTYPES_H */

#include <slurm/slurm.h>

#include "src/common/jobacct_common.h"
#include "src/common/parse_time.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/xstring.h"
#include "src/common/print_fields.h"

#define CKPT_WAIT	10
#define	MAX_INPUT_FIELDS 128

extern char *command_name;
extern int exit_code;	/* sacctmgr's exit code, =1 on any error at any time */
extern int exit_flag;	/* program to terminate if =1 */
extern int input_words;	/* number of words of input permitted */
extern int one_liner;	/* one record per line if =1 */
extern int quiet_flag;	/* quiet=1, verbose=-1, normal=0 */
extern int rollback_flag;/* immediate execute=0, else = 1 */
extern int with_assoc_flag;/* show acct/user associations flag */
extern int readonly_flag; /* make it so you can only run list commands */
extern void *db_conn;
extern uint32_t my_uid;
extern List g_qos_list;

extern int sacctmgr_add_association(int argc, char *argv[]);
extern int sacctmgr_add_user(int argc, char *argv[]);
extern int sacctmgr_add_account(int argc, char *argv[]);
extern int sacctmgr_add_cluster(int argc, char *argv[]);
extern int sacctmgr_add_coord(int argc, char *argv[]);
extern int sacctmgr_add_qos(int argc, char *argv[]);

extern int sacctmgr_list_association(int argc, char *argv[]);
extern int sacctmgr_list_user(int argc, char *argv[]);
extern int sacctmgr_list_account(int argc, char *argv[]);
extern int sacctmgr_list_cluster(int argc, char *argv[]);
extern int sacctmgr_list_config(bool have_db_conn);
extern int sacctmgr_list_problem(int argc, char *argv[]);
extern int sacctmgr_list_qos(int argc, char *argv[]);
extern int sacctmgr_list_wckey(int argc, char *argv[]);

extern int sacctmgr_modify_association(int argc, char *argv[]);
extern int sacctmgr_modify_user(int argc, char *argv[]);
extern int sacctmgr_modify_account(int argc, char *argv[]);
extern int sacctmgr_modify_cluster(int argc, char *argv[]);

extern int sacctmgr_delete_association(int argc, char *argv[]);
extern int sacctmgr_delete_user(int argc, char *argv[]);
extern int sacctmgr_delete_account(int argc, char *argv[]);
extern int sacctmgr_delete_cluster(int argc, char *argv[]);
extern int sacctmgr_delete_coord(int argc, char *argv[]);
extern int sacctmgr_delete_qos(int argc, char *argv[]);

extern int sacctmgr_dump_cluster(int argc, char *argv[]);

extern int sacctmgr_archive_dump(int argc, char *argv[]);
extern int sacctmgr_archive_load(int argc, char *argv[]);

/* common.c */
extern int parse_option_end(char *option);
extern char *strip_quotes(char *option, int *increased, bool make_lower);
extern int notice_thread_init();
extern int notice_thread_fini();
extern int commit_check(char *warning);
extern int get_uint(char *in_value, uint32_t *out_value, char *type);
extern int get_uint16(char *in_value, uint16_t *out_value, char *type);
extern int get_uint64(char *in_value, uint64_t *out_value, char *type);
extern int get_double(char *in_value, double *out_value, char *type);
extern int addto_qos_char_list(List char_list, List qos_list, char *names, 
			       int option);
extern int addto_action_char_list(List char_list, char *names);
extern List copy_char_list(List qos_list);
extern void sacctmgr_print_coord_list(
	print_field_t *field, List value, int last);
extern void sacctmgr_print_qos_list(print_field_t *field, List qos_list,
				    List value, int last);
extern void sacctmgr_print_qos_bitstr(print_field_t *field, List qos_list,
				      bitstr_t *value, int last);

extern void sacctmgr_print_assoc_limits(acct_association_rec_t *assoc);
extern void sacctmgr_print_qos_limits(acct_qos_rec_t *qos);
extern int sort_coord_list(acct_coord_rec_t *coord_a,
			   acct_coord_rec_t *coord_b);

/* you need to free the objects returned from these functions */
extern acct_association_rec_t *sacctmgr_find_association(char *user,
							 char *account,
							 char *cluster,
							 char *partition);
extern acct_association_rec_t *sacctmgr_find_account_base_assoc(
	char *account, char *cluster);
extern acct_association_rec_t *sacctmgr_find_root_assoc(char *cluster);
extern acct_user_rec_t *sacctmgr_find_user(char *name);
extern acct_account_rec_t *sacctmgr_find_account(char *name);
extern acct_cluster_rec_t *sacctmgr_find_cluster(char *name);

/* do not free any of the object returned from these functions since
 * they are pointing to an object in the list given 
 */

extern acct_association_rec_t *sacctmgr_find_association_from_list(
	List assoc_list, char *user, char *account, 
	char *cluster, char *partition);
extern acct_association_rec_t *sacctmgr_find_account_base_assoc_from_list(
	List assoc_list, char *account, char *cluster);
extern acct_qos_rec_t *sacctmgr_find_qos_from_list(
	List qos_list, char *name);
extern acct_user_rec_t *sacctmgr_find_user_from_list(
	List user_list, char *name);
extern acct_account_rec_t *sacctmgr_find_account_from_list(
	List acct_list, char *name);
extern acct_cluster_rec_t *sacctmgr_find_cluster_from_list(
	List cluster_list, char *name);
extern acct_wckey_rec_t *sacctmgr_find_wckey_from_list(
	List wckey_list, char *user, char *name, char *cluster);


/* file_functions.c */
extern int print_file_add_limits_to_line(char **line,
					 acct_association_rec_t *assoc);

extern int print_file_acct_hierarchical_rec_list(FILE *fd, 
					  List acct_hierarchical_rec_list,
					  List user_list,
					  List acct_list);

extern void load_sacctmgr_cfg_file (int argc, char *argv[]);

/* txn_functions.c */
extern int sacctmgr_list_txn(int argc, char *argv[]);

#endif
