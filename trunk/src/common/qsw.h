/*****************************************************************************\
 *  qsw.h - Library routines for initiating jobs on QsNet. 
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jim Garlick <garlick@llnl.gov>
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include <sys/types.h>
#include <src/common/bitstring.h>

#ifndef _QSW_INCLUDED
#define _QSW_INCLUDED

/* opaque data structures - no peeking! */
typedef struct qsw_libstate 	*qsw_libstate_t;
typedef struct qsw_jobinfo 	*qsw_jobinfo_t;

#define QSW_LIBSTATE_PACK_MAX	12
#define QSW_JOBINFO_PACK_MAX	120
#define QSW_MAX_TASKS		1024
#define QSW_PACK_SIZE		(4 * (2+4+1+8+ELAN_BITMAPSIZE))

int		qsw_alloc_libstate(qsw_libstate_t *lsp);
void		qsw_free_libstate(qsw_libstate_t ls);

int		qsw_pack_libstate(qsw_libstate_t ls, void **data, int *len);
int		qsw_unpack_libstate(qsw_libstate_t ls, void **data, int *len);

int 		qsw_init(qsw_libstate_t restorestate);
void 		qsw_fini(qsw_libstate_t savestate);

int		qsw_alloc_jobinfo(qsw_jobinfo_t *jp);
void		qsw_free_jobinfo(qsw_jobinfo_t j);

int		qsw_pack_jobinfo(qsw_jobinfo_t j, void **data, int *len);
int		qsw_unpack_jobinfo(qsw_jobinfo_t j, void **data, int *len);

int 		qsw_setup_jobinfo(qsw_jobinfo_t j, int nprocs, 
			bitstr_t *nodeset, int cyclic_alloc);

int 		qsw_prog_init(qsw_jobinfo_t jobinfo, uid_t uid);
void 		qsw_prog_fini(qsw_jobinfo_t jobinfo);

int 		qsw_prgdestroy(qsw_jobinfo_t jobinfo); /* was qsw_prog_reap */

int 		qsw_setcap(qsw_jobinfo_t jobinfo, int procnum); /* was qsw_attach */

int		qsw_prgsignal(qsw_jobinfo_t jobinfo, int signum); /* was qsw_signal_job */

int		qsw_getnodeid(void);
int		qsw_getnodeid_byhost(char *host);
int		qsw_gethost_bynodeid(char *host, int len, int elanid);

void		qsw_print_jobinfo(FILE *fp, struct qsw_jobinfo *jobinfo);

#endif /* _QSW_INCLUDED */
