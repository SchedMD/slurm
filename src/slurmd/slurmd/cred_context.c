/*****************************************************************************\
 *  cred_context.c
 *****************************************************************************
 *  Copyright (C) 2023 SchedMD LLC.
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

#include "src/common/pack.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/cred.h"

#include "src/slurmd/slurmd/slurmd.h"

extern void restore_cred_state(void)
{
	char *file_name = NULL;
	buf_t *buffer = NULL;

	file_name = xstrdup(conf->spooldir);
	xstrcat(file_name, "/cred_state");

	if (!(buffer = create_mmap_buf(file_name)))
		goto cleanup;

	slurm_cred_ctx_unpack(buffer);

cleanup:
	xfree(file_name);
	FREE_NULL_BUFFER(buffer);
}
