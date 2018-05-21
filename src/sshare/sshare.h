/*****************************************************************************\
 *  sshare.h - definitions for all sshare modules.
 *****************************************************************************
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

#ifndef __SSHARE_H__
#define __SSHARE_H__

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

#include "src/common/parse_time.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/xstring.h"
#include "src/common/print_fields.h"
#include "src/common/slurmdb_defs.h"

#define CKPT_WAIT	10
#define	MAX_INPUT_FIELDS 128

/* Print only the users and not the hierarchy.
 */
#define PRINT_USERS_ONLY 0x01
/* If you have partition base associations
 * print them
 */
#define PRINT_PARTITIONS 0x02

typedef enum {
	SSHARE_TIME_SECS,
	SSHARE_TIME_MINS,
	SSHARE_TIME_HOURS,
} sshare_time_format_t;

enum {
	PRINT_ACCOUNT,
	PRINT_CLUSTER,
	PRINT_TRESMINS,
	PRINT_EUSED,
	PRINT_FSFACTOR,
	PRINT_ID,
	PRINT_NORMS,
	PRINT_NORMU,
	PRINT_PART,
	PRINT_RAWS,
	PRINT_RAWU,
	PRINT_RUNMINS,
	PRINT_USER,
	PRINT_LEVELFS,
	PRINT_GRPTRESRAW
};

extern int exit_code;	/* sshare's exit code, =1 on any error at any time */
extern int quiet_flag;	/* quiet=1, verbose=-1, normal=0 */
extern uint32_t my_uid;
extern sshare_time_format_t time_format;
extern char *time_format_string;
extern List clusters;
extern print_field_t fields[];
extern char **tres_names;
extern uint32_t tres_cnt;
extern int long_flag;
extern char *opt_field_list;

extern int process(shares_response_msg_t *msg, uint16_t options);

#endif
