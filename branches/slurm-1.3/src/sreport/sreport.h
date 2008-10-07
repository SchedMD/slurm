/*****************************************************************************\
 *  sreport.h - report generating tool for slurm accounting header file.
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#ifndef __SREPORT_H__
#define __SREPORT_H__

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

typedef enum {
	SREPORT_TIME_SECS,
	SREPORT_TIME_MINS,
	SREPORT_TIME_HOURS,
	SREPORT_TIME_PERCENT,
	SREPORT_TIME_SECS_PER,
	SREPORT_TIME_MINS_PER,
	SREPORT_TIME_HOURS_PER,
} sreport_time_format_t;

typedef struct {
	char *acct;
	char *cluster;
	uint64_t cpu_secs;
	char *parent_acct;
	char *user;
} sreport_assoc_rec_t;

typedef struct {
	char *acct;
	List acct_list; /* list of char *'s */
	List assoc_list; /* list of acct_association_rec_t's */
	uint64_t cpu_secs;
	char *name;
	uid_t uid;
} sreport_user_rec_t;

typedef struct {
	List assoc_list; /* list of sreport_assoc_rec_t *'s */
	uint32_t cpu_count;
	uint64_t cpu_secs;
	char *name;
	List user_list; /* list of sreport_user_rec_t *'s */
} sreport_cluster_rec_t;

extern sreport_time_format_t time_format;
extern char *time_format_string;
extern char *command_name;
extern int exit_code;	/* sacctmgr's exit code, =1 on any error at any time */
extern int exit_flag;	/* program to terminate if =1 */
extern int input_words;	/* number of words of input permitted */
extern int quiet_flag;	/* quiet=1, verbose=-1, normal=0 */
extern void *db_conn;
extern uint32_t my_uid;
extern int all_clusters_flag;

extern void sreport_print_time(print_field_t *field,
			       uint64_t value, uint64_t total_time, int last);
extern int parse_option_end(char *option);
extern char *strip_quotes(char *option, int *increased);
extern int set_start_end_time(time_t *start, time_t *end);
extern void destroy_sreport_assoc_rec(void *object);
extern void destroy_sreport_user_rec(void *object);
extern void destroy_sreport_cluster_rec(void *object);
extern int sort_user_dec(sreport_user_rec_t *user_a,
			 sreport_user_rec_t *user_b);
extern int sort_cluster_dec(sreport_cluster_rec_t *cluster_a,
			    sreport_cluster_rec_t *cluster_b);
extern int sort_assoc_dec(sreport_assoc_rec_t *assoc_a,
			  sreport_assoc_rec_t *assoc_b);


#endif /* HAVE_SREPORT_H */
