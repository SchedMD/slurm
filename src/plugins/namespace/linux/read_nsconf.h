/*****************************************************************************\
 *  read_nsconf.h - parse namespace.conf configuration file.
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#define SLURM_NEWNS_DEF_DIRS "/tmp,/dev/shm"

/*
 * Slurm linux namespace plugin initialization parameters
 */
typedef struct ns_conf {
	bool auto_basepath;
	char *basepath;
	char *clonensscript;
	char *clonensflags_str;
	char *clonensepilog;
	uint32_t clonensscript_wait;
	uint32_t clonensflags;
	uint32_t clonensepilog_wait;
	char *dirs;
	char *initscript;
	bool shared;
	char *usernsscript;
} ns_conf_t;

extern char *ns_conf_file;

/*
 * Init the namespace config if required.
 *
 * Return a pointer to the config structure if successful or NULL on error.
 */
extern ns_conf_t *init_slurm_ns_conf(void);

/* Set slurm_ns_conf based on the provided buffer. */
extern ns_conf_t *set_slurm_ns_conf(buf_t *buf);

/* Return pointer to the slurm_ns_conf */
extern ns_conf_t *get_slurm_ns_conf(void);

/* Return pointer to the the slurm_ns_conf_buf */
extern buf_t *get_slurm_ns_conf_buf(void);

/* Free the job_container config structures */
extern void free_ns_conf(void);

#endif
