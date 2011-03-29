/*****************************************************************************\
 *  cray_config.h
 *
 *****************************************************************************
 *  Copyright (C) 2011 SchedMD LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@schedmd.com>
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

#ifndef _CRAY_CONFIG_H_
#define _CRAY_CONFIG_H_

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include "slurm/slurm.h"

typedef struct {
	char *alps_dir;
	char *apbasil;
	char *apkill;
	char *sdb_db;
	char *sdb_host;
	char *sdb_pass;
	uint32_t sdb_port;
	char *sdb_user;
	uint32_t slurm_debug_flags;
} cray_config_t;

/* Location of Alps install dir */
#define DEFAULT_ALPS_DIR          "/usr"
/* Location of Alps apbasil executable (supported on XT/XE CNL) */
#define DEFAULT_APBASIL           DEFAULT_ALPS_DIR "/bin/apbasil"
/* Location of Alps apkill executable (supported on XT/XE CNL) */
#define DEFAULT_APKILL            DEFAULT_ALPS_DIR "/bin/apkill"
/* database name to use  */
#define DEFAULT_CRAY_SDB_DB       "XTAdmin"
/* DNS name of SDB host */
#define DEFAULT_CRAY_SDB_HOST "sdb"
/* If NULL, use value from my.cnf */
#define DEFAULT_CRAY_SDB_PASS     NULL
/* If NULL, use value from my.cnf */
#define DEFAULT_CRAY_SDB_PORT     0
/* If NULL, use value from my.cnf */
#define DEFAULT_CRAY_SDB_USER     NULL

extern cray_config_t *cray_conf;

extern int create_config(void);
extern int destroy_config(void);

#endif
