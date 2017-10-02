/*****************************************************************************\
 **  shmem.h - Shared Memory manipulation functions
 *****************************************************************************
 *  Copyright (C) 2016 The Ohio State University
 *  This file was developed by the team members of The
 *  Ohio State University's Network-Based Computing Laboratory (NBCL),
 *  headed by Professor Dhabaleswar K. (DK) Panda.
 *  Contact:
 *  Prof. Dhabaleswar K. (DK) Panda
 *  Dept. of Computer Science and Engineering
 *  The Ohio State University
 *  2015 Neil Avenue
 *  Columbus, OH - 43210-1277
 *  Tel: (614)-292-5199; Fax: (614)-292-2911
 *  E-mail:panda@cse.ohio-state.edu
 *  All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#ifndef _SHMEM_H
#define _SHMEM_H

#if     HAVE_CONFIG_H
#  include "config.h"
#endif

#define PMI2_SHMEM_FILENAME_ALLGATHER  "/tmp/SLURM_PMI2_SHMEM_ALLG_%llu_%llu.tmp"

typedef struct {
    int fd;
    void *addr;
    char filename[256];
    int filesize;
} PMI2ShmemRegion;

extern int use_shmem_allgather;
extern PMI2ShmemRegion PMI2_Shmem_allgather;

extern int   kvs_create_shmem(PMI2ShmemRegion *shmem);
extern int   kvs_destroy_shmem(PMI2ShmemRegion *shmem);

#endif	/* _SHMEM_H */
