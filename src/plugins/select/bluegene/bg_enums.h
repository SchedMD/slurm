/*****************************************************************************\
 *  bg_enums.h - hearder file containing enums for the Blue Gene/Q plugin.
 *****************************************************************************
 *  Copyright (C) 2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#ifndef ATTACH_BGL_H	/* Test for attach_bgl.h on BGL */
#ifndef ATTACH_BG_H	/* Test for attach_bg.h on BGP */
#define ATTACH_BGL_H	/* Replacement for attach_bgl.h on BGL */
#define ATTACH_BG_H	/* Replacement for attach_bg.h on BGP */

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef HAVE_BG_FILES

/* MPI Debug support */
typedef struct {
	const char * host_name;        /* Something we can pass to inet_addr */
	const char * executable_name;  /* The name of the image */
	int    pid;                    /* The pid of the process */
} MPIR_PROCDESC;

# ifdef HAVE_BG_L_P
# include "rm_api.h"
# endif

#elif defined HAVE_BG_L_P
typedef char *   pm_partition_id_t;
typedef int      rm_connection_type_t;
typedef int      rm_partition_mode_t;
typedef int      rm_partition_state_t;
typedef void *   rm_partition_t;
typedef char *   rm_BGL_t;
typedef char *   rm_BG_t;
typedef char *   rm_component_id_t;
typedef rm_component_id_t rm_bp_id_t;
typedef int      rm_BP_state_t;
typedef char *   rm_job_list_t;
#endif

#ifdef HAVE_BGL
typedef rm_BGL_t my_bluegene_t;
#define PARTITION_ALREADY_DEFINED -6
#elif defined HAVE_BGP
typedef rm_BG_t my_bluegene_t;
#else
typedef void * my_bluegene_t;
#endif

typedef enum bg_layout_type {
	LAYOUT_STATIC,  /* no overlaps, except for full system block
			   blocks never change */
	LAYOUT_OVERLAP, /* overlaps permitted, must be defined in
			   bluegene.conf file */
	LAYOUT_DYNAMIC	/* slurm will make all blocks */
} bg_layout_t;

typedef enum {
	BG_BLOCK_FREE = 0,  // Block is free
	BG_BLOCK_ALLOCATED, // Block is allocated (reserved either
			    // right before booting or right before free
	BG_BLOCK_BUSY,      // Block is Busy
	BG_BLOCK_BOOTING,   // Block is booting
	BG_BLOCK_INITED,    // Block is initialized
	BG_BLOCK_REBOOTING, // Block is rebooting
	BG_BLOCK_TERM,      // Block is terminating
	BG_BLOCK_NAV,       // Block state is undefined
} bg_block_status_t;

typedef enum {
        BG_JOB_SETUP = 0,   //!< Job is setting up.
        BG_JOB_LOADING,     //!< Job is loading.
        BG_JOB_STARTING,    //!< Job is starting.
        BG_JOB_RUNNING,     //!< Job is running.
        BG_JOB_CLEANUP,     //!< Job is ending.
        BG_JOB_TERMINATED,  //!< Job is terminated.
        BG_JOB_ERROR        //!< Job is in error status.
} bg_job_status_t;

typedef enum {
	BG_BLOCK_ACTION_NAV = 0,
	BG_BLOCK_ACTION_NONE,
	BG_BLOCK_ACTION_BOOT,
	BG_BLOCK_ACTION_FREE
} bg_block_action_t;

#define BG_BLOCK_ERROR_FLAG    0x1000  // Block is in error


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

#define BG_SWITCH_CABLE_ERROR      0x0100 /* Flag to notify cable is in a
					   * error state.
					   */
#define BG_SWITCH_CABLE_ERROR_SET  0x0200 /* If a cable goes into an error
					   * state we set the cable in
					   * an error and the OUT_PASS
					   * as well.
					   * Currently SLURM only really
					   * cares about the out port of a
					   * switch.
					   */
#define BG_SWITCH_CABLE_ERROR_FULL 0x0300 /* Used to clear both
					   * BG_SWITCH_CABLE_ERROR
					   * && BG_SWITCH_CABLE_ERROR_SET
					   */

/*
 * Total time to boot a bglblock should not exceed
 * BG_FREE_PREVIOUS_BLOCK + BG_MIN_BLOCK_BOOT +
 * (BG_INCR_BLOCK_BOOT * base partition count).
 * For example, if BG_MIN_BLOCK_BOOT=300, BG_MIN_BLOCK_BOOT=200,
 * BG_INCR_BLOCK_BOOT=20 and there are 4 blocks being booted,
 * wait up to 580 seconds (300 + 200 (20 * 4)).
 */

#define BG_FREE_PREVIOUS_BLOCK 300 	/* time in seconds */
#define BG_MIN_BLOCK_BOOT  300		/* time in seconds */
#define BG_INCR_BLOCK_BOOT 20		/* time in seconds per BP */

#define MAX_PTHREAD_RETRIES  1
#define BLOCK_ERROR_STATE    -3
#define ADMIN_ERROR_STATE    -4
#define BUFSIZE 4096
#define BITSIZE 128

#define BLOCK_MAGIC 0x3afd

#define REMOVE_USER_ERR  -1
#define REMOVE_USER_NONE  0
#define REMOVE_USER_FOUND 2

typedef enum {
	BG_ERROR_INVALID_STATE = 100,
	BG_ERROR_BLOCK_NOT_FOUND,
	BG_ERROR_BOOT_ERROR,
	BG_ERROR_JOB_NOT_FOUND,
	BG_ERROR_MP_NOT_FOUND,
	BG_ERROR_SWITCH_NOT_FOUND,
	BG_ERROR_BLOCK_ALREADY_DEFINED,
	BG_ERROR_JOB_ALREADY_DEFINED,
	BG_ERROR_CONNECTION_ERROR,
	BG_ERROR_INTERNAL_ERROR,
	BG_ERROR_INVALID_INPUT,
	BG_ERROR_INCONSISTENT_DATA,
	BG_ERROR_NO_IOBLOCK_CONNECTED,
	BG_ERROR_FREE,
} bg_errno_t;

#endif	/* #ifndef ATTACH_BG_H */
#endif	/* #ifndef ATTACH_BGL_H */
