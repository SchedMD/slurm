/*****************************************************************************\
 *  tls.h - Internal declarations for TLS handlers
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#ifndef _CONMGR_TLS_H
#define _CONMGR_TLS_H

#include "src/conmgr/conmgr.h"
#include "src/conmgr/mgr.h"

/* Perform TLS creation */
extern void tls_create(conmgr_callback_args_t conmgr_args, void *arg);
/* Extract TLS components from connection */
extern int tls_extract(conmgr_fd_t *con, extract_fd_t *extract);
/* Adopt existing TLS state into connection */
extern void tls_adopt(conmgr_fd_t *con, void *tls_conn);

/*
 * Perform TLS shutdown and cleanup
 * WARNING: will defer cleanup if file descriptors are still open
 */
extern void tls_close(conmgr_callback_args_t conmgr_args, void *arg);

/* Read from con->input_fd */
extern void tls_handle_read(conmgr_callback_args_t conmgr_args, void *arg);

/* Handle data in con->tls_in buffer */
extern void tls_handle_decrypt(conmgr_callback_args_t conmgr_args, void *arg);

/* Handle data in con->out */
extern void tls_handle_encrypt(conmgr_callback_args_t conmgr_args, void *arg);

/* Handle data in con->tls_out buffer */
extern void tls_handle_write(conmgr_callback_args_t conmgr_args, void *arg);

#endif /* _CONMGR_TLS_H */
