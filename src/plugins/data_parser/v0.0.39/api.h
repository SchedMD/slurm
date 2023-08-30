/*****************************************************************************\
 *  api.h - Slurm data parsing handlers
 *****************************************************************************
 *  Copyright (C) 2022 SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
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

#ifndef DATA_PARSER_API
#define DATA_PARSER_API

#include "src/interfaces/data_parser.h"

/*
 * These macros are defined by the Makefile.am:
 * DATA_VERSION
 * PLUGIN_ID
 */

#define MAGIC_ARGS 0x2ea1bebb

typedef struct {
	int magic; /* MAGIC_ARGS */
	data_parser_on_error_t on_parse_error;
	data_parser_on_error_t on_dump_error;
	data_parser_on_error_t on_query_error;
	void *error_arg;
	data_parser_on_warn_t on_parse_warn;
	data_parser_on_warn_t on_dump_warn;
	data_parser_on_warn_t on_query_warn;
	void *warn_arg;
	void *db_conn;
	bool close_db_conn;
	List tres_list;
	List qos_list;
	List assoc_list;
} args_t;

extern args_t *data_parser_p_new(data_parser_on_error_t on_parse_error,
				 data_parser_on_error_t on_dump_error,
				 data_parser_on_error_t on_query_error,
				 void *error_arg,
				 data_parser_on_warn_t on_parse_warn,
				 data_parser_on_warn_t on_dump_warn,
				 data_parser_on_warn_t on_query_warn,
				 void *warn_arg);
extern void data_parser_p_free(args_t *args);

extern int data_parser_p_assign(args_t *args, data_parser_attr_type_t type,
				void *obj);

extern int data_parser_p_dump(args_t *args, data_parser_type_t type, void *src,
			      ssize_t src_bytes, data_t *dst);
extern int data_parser_p_parse(args_t *args, data_parser_type_t type, void *dst,
			       ssize_t dst_bytes, data_t *src,
			       data_t *parent_path);

#endif
