/*****************************************************************************\
 *  bg_enums.h - hearder file containing enums for the Blue Gene/Q plugin.
 *****************************************************************************
 *  Copyright (C) 2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#ifndef _BG_ENUMS_H_
#define _BG_ENUMS_H_

typedef enum bg_layout_type {
	LAYOUT_STATIC,  /* no overlaps, except for full system block
			   blocks never change */
	LAYOUT_OVERLAP, /* overlaps permitted, must be defined in
			   bluegene.conf file */
	LAYOUT_DYNAMIC	/* slurm will make all blocks */
} bg_layout_t;

typedef enum {
	BG_BLOCK_NAV = 0,   // Block state is undefined
	BG_BLOCK_FREE,      // Block is free
	BG_BLOCK_BOOTING,   // Block is booting
	BG_BLOCK_INITED,    // Block is initialized
	BG_BLOCK_ALLOCATED, // Block is allocated
	BG_BLOCK_TERM,      // Block is terminating
	BG_BLOCK_ERROR,     // Block is in error
} bgq_block_status_t;

typedef enum {
        BG_JOB_SETUP = 0,   //!< Job is setting up.
        BG_JOB_LOADING,     //!< Job is loading.
        BG_JOB_STARTING,    //!< Job is starting.
        BG_JOB_RUNNING,     //!< Job is running.
        BG_JOB_CLEANUP,     //!< Job is ending.
        BG_JOB_TERMINATED,  //!< Job is terminated.
        BG_JOB_ERROR        //!< Job is in error status.
} bgq_job_status_t;

#define BG_SWITCH_NONE         0x0000
#define BG_SWITCH_OUT          0x0001
#define BG_SWITCH_IN           0x0002
#define BG_SWITCH_OUT_PASS     0x0004
#define BG_SWITCH_IN_PASS      0x0008
#define BG_SWITCH_WRAPPED      0x0003 /* just wrap used */
#define BG_SWITCH_PASS_FLAG    0x0010 /* flag for marking a midplane
				       * with a passthough used */
#define BG_SWITCH_PASS_USED    0x000C /* passthough ports used */
#define BG_SWITCH_PASS         0x001C /* just passthough used */
#define BG_SWITCH_WRAPPED_PASS 0x001F /* all ports are in use, but no torus */
#define BG_SWITCH_TORUS        0x000F /* all ports are in use in a torus */
#define BG_SWITCH_START        0x0200 /* modified from the start list */

#define switch_overlap(__switch_a, __switch_b) \
	((__switch_a != BG_SWITCH_NONE) && (__switch_b != BG_SWITCH_NONE)) \
	&& !(__switch_a & __switch_b)

/* typedef enum { */
/* 	BG_SWITCH_NONE = 0, // Switch is not in use */
/* 	BG_SWITCH_TORUS,    // Switch is included, (Torus config) */
/* 	BG_SWITCH_OUT,      // Switch is included, only output port used */
/* 	BG_SWITCH_IN,       // Switch is included, only input port used */
/* 	BG_SWITCH_WRAPPED,  // Switch is not included and ports are wrapped */
/* 	BG_SWITCH_PASS,     // Switch is not included and ports are */
/* 			    // used for passthrough */
/* 	BG_SWITCH_WRAPPED_PASS // Switch is not included and ports are */
/* 			       // wrapped and used for passthrough */
/* } bg_switch_usage_t; */

#endif /* _BG_ENUMS_H_ */
