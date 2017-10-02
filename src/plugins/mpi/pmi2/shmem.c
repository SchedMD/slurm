/*****************************************************************************\
 **  shmem.c - Shared Memory manipulation functions
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

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "shmem.h"

#include "src/common/slurm_xlator.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"

int use_shmem_allgather = 0;
PMI2ShmemRegion PMI2_Shmem_allgather = { .fd=-1, .addr=NULL, .filesize=0 };

extern int
kvs_create_shmem(PMI2ShmemRegion *shmem)
{
    struct stat file_status;
    debug("in kvs_create_shmem: filename: %s, filesize: %d", shmem->filename, shmem->filesize);

    if ((shmem->fd = open(shmem->filename, O_RDWR | O_CREAT, S_IRWXU | S_IRGRP | S_IROTH)) < 0) {
        error("Can not open shmem file: %s, error: %m", shmem->filename);
        return -1;
    }

    if(ftruncate(shmem->fd, 0)
            || ftruncate(shmem->fd, shmem->filesize)
            || lseek(shmem->fd, 0, SEEK_SET)
      ) {
        error("Can not ftruncate shmem file: %s, error: %m", shmem->filename);
        return -1;
    }

    do {
        if (fstat(shmem->fd, &file_status) != 0) {
            error("Can not fstat shmem file: %s, error: %m", shmem->filename);
            return -1;
        }
        usleep(1);
    } while (file_status.st_size != shmem->filesize);

    shmem->addr = mmap(0, shmem->filesize, (PROT_READ | PROT_WRITE), (MAP_SHARED), shmem->fd, 0);
    if (shmem->addr == MAP_FAILED) {
        error("Can not mmap shmem file: %s, size: %d, error: %m", shmem->filename, shmem->filesize);
    }
    memset(shmem->addr, 0, shmem->filesize);

    debug("out kvs_create_shmem: filename: %s, filesize: %d, fd: %d", shmem->filename, shmem->filesize, shmem->fd);
    return 0;
}

extern int
kvs_destroy_shmem(PMI2ShmemRegion *shmem)
{
    if (shmem->fd != -1) {
        munmap(shmem->addr, shmem->filesize);
        close(shmem->fd);
        unlink(shmem->filename);
        shmem->addr = NULL;
        shmem->filesize = 0;
        shmem->fd = -1;
    }
    return 0;
}

