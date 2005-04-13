/*****************************************************************************\
 *  bgl_part_info.h - header for blue gene partition information.
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#ifndef _BGL_PART_INFO_H_
#define _BGL_PART_INFO_H_

#include "bluegene.h"

/*****************************************************/
extern int part_ready(struct job_record *job_ptr);
extern void pack_partition(bgl_record_t *bgl_record, Buf buffer);
extern int unpack_partition(bgl_info_record_t *bgl_info_record, Buf buffer);
extern int update_partition_list();
#endif /* _BGL_PART_INFO_H_ */
