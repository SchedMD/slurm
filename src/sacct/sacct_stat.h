/*****************************************************************************\
 *  sacct_stat.h - header file for sacct
 *
 *  $Id: sacct.h 7541 2006-03-18 01:44:58Z da $
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>.
 *  UCRL-CODE-217948.
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
#ifndef _SACCT_STAT_H
#define _SACCT_STAT_H

#include "src/common/slurm_protocol_api.h"

typedef struct sacct_struct {
       uint32_t max_vsize; 
       uint16_t max_vsize_task;
       float ave_vsize;
       uint32_t max_rss;
       uint16_t max_rss_task;
       float ave_rss;
       uint32_t max_pages;
       uint16_t max_pages_task;
       float ave_pages;
       float min_cpu;
       uint16_t min_cpu_task;
       float ave_cpu;	
} sacct_t;

extern int sacct_stat(uint32_t jobid, uint32_t stepid);

#endif
