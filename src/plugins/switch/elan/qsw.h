/*****************************************************************************\
 *  qsw.h - Library routines for initiating jobs on QsNet.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jim Garlick <garlick@llnl.gov>
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

#include <stdio.h>
#include <sys/types.h>

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include "src/common/slurm_xlator.h"

#ifndef _QSW_INCLUDED
#define _QSW_INCLUDED

#if HAVE_LIBELANCTRL
# include <elan/capability.h>
#elif HAVE_LIBELAN3
# include <elan3/elanvp.h>
#else
# error "Don't have either libelanctrl or libelan3!"
#endif

/* opaque data structures - no peeking! */
typedef struct qsw_libstate 	*qsw_libstate_t;
#ifndef __qsw_jobinfo_t_defined
#  define  __qsw_jobinfo_t_defined
   typedef struct qsw_jobinfo *qsw_jobinfo_t;	/* opaque data type */
#endif

#define QSW_LIBSTATE_PACK_MAX	12
#define QSW_JOBINFO_PACK_MAX	120
#define QSW_MAX_TASKS		ELAN_MAX_VPS
#define QSW_PACK_SIZE		(4 * (2+4+1+8+ELAN_BITMAPSIZE))

/* NOTE: error codes should be between ESLURM_SWITCH_MIN and
 * ESLURM_SWITCH MAX as defined in slurm/slurm_errno.h */
enum {
	/* Quadrics Elan specific error codes */
	ENOSLURM =					3000,
	EBADMAGIC_QSWLIBSTATE,
	EBADMAGIC_QSWJOBINFO,
	EINVAL_PRGCREATE,
	ECHILD_PRGDESTROY,
	EEXIST_PRGDESTROY,
	EELAN3INIT,
	EELAN3CONTROL,
	EELAN3CREATE,
	ESRCH_PRGADDCAP,
	EFAULT_PRGADDCAP,
	EINVAL_SETCAP,
	EFAULT_SETCAP,
	EGETNODEID,
	EGETNODEID_BYHOST,
	EGETHOST_BYNODEID,
	ESRCH_PRGSIGNAL,
	EINVAL_PRGSIGNAL
};

int		qsw_alloc_libstate(qsw_libstate_t *lsp);
void		qsw_free_libstate(qsw_libstate_t ls);

int		qsw_pack_libstate(qsw_libstate_t ls, Buf buffer);
int		qsw_unpack_libstate(qsw_libstate_t ls, Buf buffer);

int 		qsw_init(qsw_libstate_t restorestate);
void 		qsw_fini(qsw_libstate_t savestate);
int         qsw_clear(void);

int		qsw_alloc_jobinfo(qsw_jobinfo_t *jp);
qsw_jobinfo_t	qsw_copy_jobinfo(qsw_jobinfo_t j);
void		qsw_free_jobinfo(qsw_jobinfo_t j);
int         qsw_restore_jobinfo(struct qsw_jobinfo *jobinfo);

int		qsw_pack_jobinfo(qsw_jobinfo_t j, Buf buffer);
int		qsw_unpack_jobinfo(qsw_jobinfo_t j, Buf buffer);

int 		qsw_setup_jobinfo(qsw_jobinfo_t j, int ntasks,
			bitstr_t *nodeset, uint16_t *tasks_per_node,
			int cyclic_alloc);
void		qsw_teardown_jobinfo(qsw_jobinfo_t j);

int 		qsw_prog_init(qsw_jobinfo_t jobinfo, uid_t uid);
void 		qsw_prog_fini(qsw_jobinfo_t jobinfo);

int 		qsw_prgdestroy(qsw_jobinfo_t jobinfo); /* was qsw_prog_reap */

int 		qsw_setcap(qsw_jobinfo_t jobinfo, int procnum);
		/* was qsw_attach */

int		qsw_prgsignal(qsw_jobinfo_t jobinfo, int signum);
		/* was qsw_signal_job */

		/* return max ElanID in configuration */
int             qsw_maxnodeid(void);

int		qsw_getnodeid(void);
int		qsw_getnodeid_byhost(char *host);
int		qsw_gethost_bynodeid(char *host, int len, int elanid);

char *		qsw_capability_string(qsw_jobinfo_t j, char *buf, size_t len);
void		qsw_print_jobinfo(FILE *fp, struct qsw_jobinfo *jobinfo);

		/* Return  Elan shared memory state key */
int             qsw_statkey (qsw_jobinfo_t jobinfo, int *keyp);

#endif /* _QSW_INCLUDED */
