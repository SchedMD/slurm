/*****************************************************************************\
 * src/common/io_hdr.h - IO connection header functions
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark A. Grondona <mgrondona@llnl.gov>.
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

#ifndef _HAVE_IO_HDR_H
#define _HAVE_IO_HDR_H


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include "src/common/pack.h"
#include "src/common/cbuf.h"

#define SLURM_IO_KEY_SIZE 8	/* IO key is 64 bits */

/* 
 * Slurm IO stream types:
 *
 * STDOUT = stdout/stdin
 * STDERR = stderr/signals
 */
#define SLURM_IO_STDOUT   0x00
#define SLURM_IO_STDERR   0x01

typedef struct slurm_io_header {
	unsigned char key[SLURM_IO_KEY_SIZE]; 
	uint32_t      taskid;
	uint16_t      version;
	uint16_t      type;
} io_hdr_t;


/*
 * Return the packed size of an IO header in bytes;
 */
int io_hdr_packed_size();


/* 
 * Write an io header into the cbuf in packed form
 */
int io_hdr_write_cb(cbuf_t cb, io_hdr_t *hdr);

/*
 * Read an io header from the cbuf into hdr
 */
int io_hdr_read_cb(cbuf_t cb, io_hdr_t *hdr);

/*
 * Validate io header hdr against len bytes of the data in key
 *
 * Returns 0 on success, -1 if any of the following is not true
 *
 *  version          != internal version
 *  type             != (SLURM_IO_STDOUT or SLURM_IO_STDERR)
 *  len bytes of key != hdr->key
 *
 */
int io_hdr_validate(io_hdr_t *hdr, const char *key, int len);

#endif /* !_HAVE_IO_HDR_H */
