/*****************************************************************************\
 *  tls.h - TLS API definitions
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

#ifndef _INTERFACES_TLS_H
#define _INTERFACES_TLS_H

#include "src/common/slurm_time.h"

#include "src/interfaces/conn.h"

/*
 * WARNING: interfaces/tls is a simplified alias for interfaces/conn and must
 * be kept in sync with interfaces/tls as it reuses the same plugins.
 */

extern int tls_g_init(void);
extern int tls_g_fini(void);

/*
 * Get absolute time that next tls_g_*() should be delayed until after any
 * failure
 * NOTE: returned timespec may be {0,0} indicating no delay required
 */
extern timespec_t tls_g_get_delay(void *conn);

/*
 * Create new TLS connection
 * IN tls_conn_args - ptr to tls_conn_args_t
 * RET ptr to TLS state
 */
extern void *tls_g_create_conn(const conn_args_t *tls_conn_args);
extern void tls_g_destroy_conn(void *conn, bool close_fds);

/*
 * Attempt TLS connection negotiation
 * NOTE: Only to be called at start of connection and if defer_negotiation=true
 * RET SLURM_SUCCESS or EWOULDBLOCK or error
 */
extern int tls_g_negotiate_conn(void *conn);

extern ssize_t tls_g_recv(void *conn, void *buf, size_t n);

extern ssize_t tls_g_send(void *conn, const void *buf, size_t n);

/*
 * Set read/write fd's on TLS connection
 * NOTE: This resets send/recv callbacks/contexts in TLS connection
 * IN conn - TLS connection to reconfigure
 * IN input_fd - new read fd
 * IN output_fd - new write fd
 * RET SLURM_SUCCESS or error
 */
extern int tls_g_set_conn_fds(void *conn, int input_fd, int output_fd);

/*
 * Set read/write fd's on TLS connection
 * NOTE: This resets read/write fd's in TLS connection
 * IN conn - TLS connection to reconfigure
 * IN input_fd - new read fd
 * IN output_fd - new write fd
 * RET SLURM_SUCCESS or error
 */
extern int tls_g_set_conn_callbacks(void *conn, conn_callbacks_t *callbacks);

/*
 * Load own certificate into store
 *
 * This is useful when certificate is not known on startup, and must be loaded
 * later (e.g. slurmd getting a signed certificate from slurmctld)
 *
 * Set 'cert' to NULL to try to load certificate from file. This is only
 * relevant to Slurm daemons that have statically configured certificates.
 * If 'cert' is NULL, all other arguments will be ignored.
 *
 * IN cert - certificate PEM, or NULL if loading from file.
 * IN cert_len - length of cert
 * IN key - key PEM
 * IN key_len - length of key
 */
extern int tls_g_load_own_cert(char *cert, uint32_t cert_len, char *key,
			       uint32_t key_len);

/*
 * Return true if interface/tls has TLS plugin loaded
 * WARNING: tls_available() is different than tls_enabled()
 */
extern bool tls_available(void);

#endif
