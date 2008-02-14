/****************************************************************************\
 *  slurmdbd_defs.h - definitions used for Slurm DBD RPCs
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#ifndef _SLURMDBD_DEFS_H
#define _SLURMDBD_DEFS_H

#if HAVE_CONFIG_H 
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif			/* HAVE_INTTYPES_H */
#else				/* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif				/*  HAVE_CONFIG_H */

#include "src/common/pack.h"

/* Increment SLURM_DBD_VERSION if any of the RPCs change */
#define SLURM_DBD_VERSION 01

/* SLURM DBD message types */
typedef enum {
	DBD_INIT = 1400,
	DBD_GET_JOBS,
	DBD_JOB_COMPLETE,
	DBD_JOB_START,
	DBD_JOB_SUBMIT,
	DBD_JOB_SUSPEND,
	DBD_RC,
	DBD_STEP_COMPLETE,
	DBD_STEP_START
} slurmdbd_msg_type_t;

/*****************************************************************************\
 * Slurm DBD protocol data structures
 *
 * NOTE: The message sent over the wire has the format:
 *	uint32_t message size;
 *	uint16_t slurmdbd_msg_type_t;	See above
 *	dbd_*_msg;			One of the message formats below
\*****************************************************************************/

typedef struct dbd_get_jobs_msg {
	uint32_t job_id;	/* optional job ID filter or NO_VAL */
} dbd_get_jobs_msg_t;

typedef struct dbd_init_msg {
	uint16_t version;
	/* Add authentication information here */
} dbd_init_msg_t;

typedef struct dbd_job_comp_msg {
	uint32_t job_id;
} dbd_job_comp_msg_t;

typedef struct dbd_job_start_msg {
	uint32_t job_id;
} dbd_job_start_msg_t;

typedef struct dbd_job_submit_msg {
	uint32_t job_id;
} dbd_job_submit_msg_t;

typedef struct dbd_job_suspend_msg {
	uint32_t job_id;
} dbd_job_suspend_msg_t;

typedef struct dbd_rc_msg {
	uint32_t return_code;
} dbd_rc_msg_t;

typedef struct dbd_step_comp_msg {
	uint32_t job_id;
	uint32_t step_id;
} dbd_step_comp_msg_t;

typedef struct dbd_step_start_msg {
	uint32_t job_id;
	uint32_t step_id;
} dbd_step_start_msg_t;

/*****************************************************************************\
 * Slurm DBD message processing functions
\*****************************************************************************/

void inline slurm_dbd_free_get_jobs_msg(dbd_get_jobs_msg_t *msg);
void inline slurm_dbd_free_init_msg(dbd_init_msg_t *msg);
void inline slurm_dbd_free_job_complete_msg(dbd_job_comp_msg_t *msg);
void inline slurm_dbd_free_job_start_msg(dbd_job_start_msg_t *msg);
void inline slurm_dbd_free_job_submit_msg(dbd_job_submit_msg_t *msg);
void inline slurm_dbd_free_job_suspend_msg(dbd_job_suspend_msg_t *msg);
void inline slurm_dbd_free_rc_msg(dbd_rc_msg_t *msg);
void inline slurm_dbd_free_step_complete_msg(dbd_step_comp_msg_t *msg);
void inline slurm_dbd_free_step_start_msg(dbd_step_start_msg_t *msg);

void inline slurm_dbd_pack_get_jobs_msg(dbd_get_jobs_msg_t *msg,       Buf buffer);
void inline slurm_dbd_pack_init_msg(dbd_init_msg_t *msg,               Buf buffer);
void inline slurm_dbd_pack_job_complete_msg(dbd_job_comp_msg_t *msg,   Buf buffer);
void inline slurm_dbd_pack_job_start_msg(dbd_job_start_msg_t *msg,     Buf buffer);
void inline slurm_dbd_pack_job_submit_msg(dbd_job_submit_msg_t *msg,   Buf buffer);
void inline slurm_dbd_pack_job_suspend_msg(dbd_job_suspend_msg_t *msg, Buf buffer);
void inline slurm_dbd_pack_rc_msg(dbd_rc_msg_t *msg,                   Buf buffer);
void inline slurm_dbd_pack_step_complete_msg(dbd_step_comp_msg_t *msg, Buf buffer);
void inline slurm_dbd_pack_step_start_msg(dbd_step_start_msg_t *msg,   Buf buffer);

int inline slurm_dbd_unpack_get_jobs_msg(dbd_get_jobs_msg_t **msg,       Buf buffer);
int inline slurm_dbd_unpack_init_msg(dbd_init_msg_t **msg,               Buf buffer);
int inline slurm_dbd_unpack_job_complete_msg(dbd_job_comp_msg_t **msg,   Buf buffer);
int inline slurm_dbd_unpack_job_start_msg(dbd_job_start_msg_t **msg,     Buf buffer);
int inline slurm_dbd_unpack_job_submit_msg(dbd_job_submit_msg_t **msg,   Buf buffer);
int inline slurm_dbd_unpack_job_suspend_msg(dbd_job_suspend_msg_t **msg, Buf buffer);
int inline slurm_dbd_unpack_rc_msg(dbd_rc_msg_t **msg,                   Buf buffer);
int inline slurm_dbd_unpack_step_complete_msg(dbd_step_comp_msg_t **msg, Buf buffer);
int inline slurm_dbd_unpack_step_start_msg(dbd_step_start_msg_t **msg,   Buf buffer);

#endif	/* !_SLURMDBD_DEFS_H */
