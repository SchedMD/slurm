/*****************************************************************************\
 *  read_jcconf.h - parse job_container.conf configuration file.
 *****************************************************************************
 *  Copyright (C) 2019-2021 Regents of the University of California
 *  Produced at Lawrence Berkeley National Laboratory
 *  Written by Aditi Gaur <agaur@lbl.gov>
 *  All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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
#ifndef _READ_JCCONF_H
#define _READ_JCCONF_H

#include "src/slurmd/slurmd/slurmd.h"

#define SLURM_TMPFS_DEF_DIRS "/tmp,/dev/shm"

/*
 * Slurm namespace job container plugin initialization
 * parameters
 */
typedef struct slurm_jc_conf {
	bool auto_basepath;
	char *basepath;
	char *dirs;
	char *initscript;
	bool shared;
} slurm_jc_conf_t;

extern char *tmpfs_conf_file;

/*
 * Init the job_container/tmpfs config if required.
 *
 * Return a pointer to the config structure if succesful or NULL on error.
 */
extern slurm_jc_conf_t *init_slurm_jc_conf(void);

/* Set slurm_jc_conf based on the provided buffer. */
extern slurm_jc_conf_t *set_slurm_jc_conf(buf_t *buf);

/* Return pointer to the slurm_jc_conf */
extern slurm_jc_conf_t *get_slurm_jc_conf(void);

/* Return pointer to the the slurm_jc_conf_buf */
extern buf_t *get_slurm_jc_conf_buf(void);

/* Free the job_container config structures */
extern void free_jc_conf(void);

#endif
