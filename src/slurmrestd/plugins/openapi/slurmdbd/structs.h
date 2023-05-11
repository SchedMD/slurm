/*****************************************************************************\
 *  structs.h - struct declarations
 *****************************************************************************
 *  Copyright (C) 2023 SchedMD LLC.
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

#ifndef SLURMRESTD_OPENAPI_SLURMDBD_STRUCTS
#define SLURMRESTD_OPENAPI_SLURMDBD_STRUCTS

#include "src/common/list.h"
#include "src/slurmrestd/openapi.h"

typedef struct {
	OPENAPI_RESP_STRUCT_META_FIELD;
	OPENAPI_RESP_STRUCT_ERRORS_FIELD;
	OPENAPI_RESP_STRUCT_WARNINGS_FIELD;
	list_t *clusters;
	list_t *tres;
	list_t *accounts;
	list_t *users;
	list_t *qos;
	list_t *wckeys;
	list_t *associations;
} openapi_resp_slurmdbd_config_t;

typedef struct {
	char *name;
} openapi_user_param_t;

typedef struct {
	bool with_deleted;
	bool without_assocs;
	bool without_coords;
	bool without_wckeys;
} openapi_user_query_t;

#endif
