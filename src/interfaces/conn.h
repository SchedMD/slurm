/*****************************************************************************\
 *  conn.h - connection API definitions
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

#ifndef _INTERFACES_CONN_H
#define _INTERFACES_CONN_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/uio.h>

#include "src/common/slurm_time.h"

typedef enum {
	TLS_CONN_NULL = 0,
	TLS_CONN_SERVER,
	TLS_CONN_CLIENT,
} conn_mode_t;

typedef struct {
	/* Function pointer type is the same as s2n_recv_fn */
	int (*recv)(void *io_context, uint8_t *buf, uint32_t len);
	/* Function pointer type is the same as s2n_send_fn */
	int (*send)(void *io_context, const uint8_t *buf, uint32_t len);

	/* Pointer to hand to recv() and send() callbacks */
	void *io_context;
} conn_callbacks_t;

typedef struct {
	/* file descriptor for incoming data */
	int input_fd;
	/* file descriptor for outgoing data */
	int output_fd;
	/* Ignore any errors for this connection */
	bool maybe;
	/* TLS connection mode (@see conn_mode_t) */
	conn_mode_t mode;
	/*
	 * False: Enable any library based blinding delays
	 * True: Disable any library based blinding delays which caller will
	 *	need to be honored via call to conn_g_get_delay() after any
	 *	conn_g_*() failure
	 */
	bool defer_blinding;
	conn_callbacks_t callbacks;
	/*
         * False: Attempt TLS negotiation in conn_g_create()
         * True: Defer TLS negotiation in conn_g_create() to explicit call
         *      to conn_g_negotiate_tls()
         */
	bool defer_negotiation;
	/*
	 * server certificate used by TLS_CONN_CLIENT connections when server
	 * certificate is not signed by a CA in our trust store
	 */
	char *cert;
} conn_args_t;

extern char *conn_mode_to_str(conn_mode_t mode);

/*
 * Return true if TLS is enabled for Slurm communications
 * WARNING: tls_enabled() is different than tls_available()
 */
extern bool tls_enabled(void);

extern int conn_g_init(void);
extern int conn_g_fini(void);

/*
 * Get self signed public certificate pem.
 */
extern char *conn_g_get_own_public_cert(void);

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
 * Note that this certificate must be trusted by the configured CA trust store.
 *
 * IN cert - certificate PEM, or NULL if loading from file.
 * IN cert_len - length of cert
 * IN key - key PEM
 * IN key_len - length of key
 */
extern int conn_g_load_own_cert(char *cert, uint32_t cert_len, char *key,
				uint32_t key_len);

/*
 * Load self-signed certificate into store
 *
 * This is needed for client commands that open listening sockets.
 * RET SLURM_SUCCESS or error
 */
extern int conn_g_load_self_signed_cert(void);

/*
 * Returns true if own certificate has ever been loaded
 */
extern bool conn_g_own_cert_loaded(void);

/*
 * Load CA cert into trust store
 * IN cert_file - path to CA certificate pem. Set to NULL to load CA certificate
 *	pem file from the configuration in slurm.conf or in the default path
 * RET SLURM_SUCCESS or error
 */
extern int conn_g_load_ca_cert(char *cert_file);

/*
 * Create new TLS connection
 * IN conn_args - ptr to conn_args_t
 * RET ptr to TLS state
 */
extern void *conn_g_create(const conn_args_t *conn_args);
extern void conn_g_destroy(void *conn, bool close_fds);

/*
 * Attempt TLS connection negotiation
 * NOTE: Only to be called at start of connection and if defer_negotiation=true
 * RET SLURM_SUCCESS or EWOULDBLOCK or error
 */
extern int conn_g_negotiate_tls(void *conn);

/*
 * Return true if client is authenticated (mTLS)
 * NOTE: Only to be called by server connections
 */
extern bool conn_g_is_client_authenticated(void *conn);

/*
 * Retrieve connection read file descriptor.
 * Needed for poll() and similar status monitoring.
 * Assumes both read and write file descriptor are the same.
 */
extern int conn_g_get_fd(void *conn);

/*
 * Set read/write fd's on TLS connection
 * NOTE: This resets send/recv callbacks/contexts in TLS connection
 * IN conn - TLS connection to reconfigure
 * IN input_fd - new read fd
 * IN output_fd - new write fd
 * RET SLURM_SUCCESS or error
 */
extern int conn_g_set_fds(void *conn, int input_fd, int output_fd);

/*
 * Set read/write fd's on TLS connection
 * NOTE: This resets read/write fd's in TLS connection
 * IN conn - TLS connection to reconfigure
 * IN input_fd - new read fd
 * IN output_fd - new write fd
 * RET SLURM_SUCCESS or error
 */
extern int conn_g_set_callbacks(void *conn, conn_callbacks_t *callbacks);

/*
 * Enable graceful TLS shutdown on connection
 *
 * Places that talk to a peer that blocks until a connection is closed (i.e.
 * peer waits until conn_g_recv() returns 0) need to do a graceful shutdown.
 * Otherwise, the peer's conn_g_recv will return an error, and the peer will not
 * know if the connection was intentionally closed.
 *
 * NOTE: Most Slurm connections do not need to do this as RPC conversations have
 * a clear end.
 *
 * IN conn - TLS connection enable graceful shutdown
 */
extern void conn_g_set_graceful_shutdown(void *conn, bool do_graceful_shutdown);

/*
 * Get absolute time that next conn_g_*() should be delayed until after any
 * failure
 * NOTE: returned timespec may be {0,0} indicating no delay required
 */
extern timespec_t conn_g_get_delay(void *conn);

extern ssize_t conn_g_send(void *conn, const void *buf, size_t n);
extern ssize_t conn_g_sendv(void *conn, const struct iovec *bufs, int count);
extern uint32_t conn_g_peek(void *conn);
extern ssize_t conn_g_recv(void *conn, void *buf, size_t n);

#endif
