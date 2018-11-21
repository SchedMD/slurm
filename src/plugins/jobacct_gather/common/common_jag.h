/*****************************************************************************\
 *  common_jag.h - slurm job accounting gather common plugin functions.
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Written by Danny Auble <da@schedmd.com>, who borrowed heavily
 *  from the original code in jobacct_gather/linux
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
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#ifndef __COMMON_JAG_H__
#define __COMMON_JAG_H__

#include "src/common/list.h"

typedef struct jag_prec {	/* process record */
	int	act_cpufreq;	/* actual average cpu frequency */
	int	last_cpu;	/* last cpu */
	pid_t	pid;
	pid_t	ppid;
	double  ssec;   /* system cpu time */
	/* Units of tres_[in|out] should be raw numbers (bytes/joules) */
	int     tres_count; /* count of tres in the tres_data */
	acct_gather_data_t *tres_data; /* array of tres data */
	double  usec;   /* user cpu time */
} jag_prec_t;

typedef struct jag_callbacks {
	void (*prec_extra) (jag_prec_t *prec, uint32_t taskid);
	List (*get_precs) (List task_list, bool pgid_plugin, uint64_t cont_id,
			   struct jag_callbacks *callbacks);
	void (*get_offspring_data) (List prec_list,
				    jag_prec_t *ancestor, pid_t pid);
} jag_callbacks_t;

extern void jag_common_init(long in_hertz);
extern void jag_common_fini(void);
extern void destroy_jag_prec(void *object);
extern void print_jag_prec(jag_prec_t *prec);

extern void jag_common_poll_data(
	List task_list, bool pgid_plugin, uint64_t cont_id,
	jag_callbacks_t *callbacks, bool profile);

#endif
