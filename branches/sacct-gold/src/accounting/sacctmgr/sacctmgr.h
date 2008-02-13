/*****************************************************************************\
 *  sacctmgr.h - definitions for all sacctmgr modules.
 *****************************************************************************
 *  Copyright (C) 2002-2008 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  UCRL-CODE-226842.
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

#include "src/common/slurm_account_storage.h"
#include "src/common/slurm_clusteracct_storage.h"
#include "src/common/slurm_jobacct_storage.h"

#define CKPT_WAIT	10
#define	MAX_INPUT_FIELDS 128

extern char *command_name;
extern int exit_code;	/* sacctmgr's exit code, =1 on any error at any time */
extern int exit_flag;	/* program to terminate if =1 */
extern int input_words;	/* number of words of input permitted */
extern int one_liner;	/* one record per line if =1 */
extern int quiet_flag;	/* quiet=1, verbose=-1, normal=0 */

extern int sacctmgr_create_association(int argc, char *argv[]);
extern int sacctmgr_create_user(int argc, char *argv[]);
extern int sacctmgr_create_account(int argc, char *argv[]);
extern int sacctmgr_create_cluster(int argc, char *argv[]);

extern int sacctmgr_list_association(int argc, char *argv[]);
extern int sacctmgr_list_user(int argc, char *argv[]);
extern int sacctmgr_list_account(int argc, char *argv[]);
extern int sacctmgr_list_cluster(int argc, char *argv[]);

extern int sacctmgr_update_association(int argc, char *argv[]);
extern int sacctmgr_update_user(int argc, char *argv[]);
extern int sacctmgr_update_account(int argc, char *argv[]);
extern int sacctmgr_update_cluster(int argc, char *argv[]);

extern int sacctmgr_delete_association(int argc, char *argv[]);
extern int sacctmgr_delete_user(int argc, char *argv[]);
extern int sacctmgr_delete_account(int argc, char *argv[]);
extern int sacctmgr_delete_cluster(int argc, char *argv[]);

extern void print_header(void);
extern int  print_str(char *str, int width, bool right, bool cut_output);
extern void print_date(void);
extern int print_secs(long time, int width, bool right, bool cut_output);

#endif
