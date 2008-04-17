/*****************************************************************************\
 *  src/salloc/msg.h - Message handler for salloc
 *
 *  $Id: salloc.c 8570 2006-07-13 21:12:58Z morrone $
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifndef _SALLOC_MSG_H
#define _SALLOC_MSG_H

#include <stdint.h>

typedef struct salloc_msg_thread salloc_msg_thread_t;

extern salloc_msg_thread_t *msg_thr_create(uint16_t *port);
extern void msg_thr_destroy(salloc_msg_thread_t *msg_thr);

#endif /* _SALLOC_MSG_H */
