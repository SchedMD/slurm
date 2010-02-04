/*****************************************************************************\
 *  slurm_rlimits_info.h - resource limits that are used by srun and the slurmd
 * $Id: slurm.hp.rlimits.patch,v 1.5 2005/07/18 18:39:11 danielc Exp $
 *****************************************************************************
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
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


#ifndef __SLURM_RLIMITS_INFO_H__
#define __SLURM_RLIMITS_INFO_H__


/*
 * Values for the propagate rlimits flag.
 */
#define PROPAGATE_RLIMITS    1  /* The default is to propagate rlimits */
#define NO_PROPAGATE_RLIMITS 0

struct slurm_rlimits_info {
        int  resource;          /* Values:  RLIMIT_NPROC, RLIMIT_MEMLOCK, ... */
        char *name;             /* String: "NPROC",      "MEMLOCK", ...       */
	int  propagate_flag;    /* PROPAGATE_RLIMITS or NO_PROPAGATE_RLIMITS  */
};

typedef struct slurm_rlimits_info slurm_rlimits_info_t;


extern slurm_rlimits_info_t *get_slurm_rlimits_info( void );

extern int parse_rlimits( char *rlimits_str, int propagate_flag );

extern void print_rlimits( void );

#endif /*__SLURM_RLIMITS_INFO_H__*/
