/*****************************************************************************\
 *  fetch_config.h - functions for "configless" slurm operation
 *****************************************************************************
 *  Copyright (C) 2020 SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef _FETCH_CONFIG_H_
#define _FETCH_CONFIG_H_

#include "src/common/slurm_protocol_defs.h"

typedef struct {
	char *conf_file;
	List include_list;
} conf_includes_map_t;

extern List conf_includes_list;

extern config_response_msg_t *fetch_config(char *conf_server, uint32_t flags);

extern config_response_msg_t *fetch_config_from_controller(uint32_t flags);

extern int dump_to_memfd(char *type, char *config, char **filename);

extern int find_conf_by_name(void *x, void *key);

extern int find_map_conf_file(void *x, void *key);

extern int write_configs_to_conf_cache(config_response_msg_t *msg,
				       char *dir);

extern void load_config_response_msg(config_response_msg_t *msg, int flags);

extern void load_config_response_list(config_response_msg_t *msg,
				      char *files[]);

extern void destroy_config_file(void *object);

#endif
