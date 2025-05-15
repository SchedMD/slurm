/*****************************************************************************\
 *  tls_fingerprint.c - definitions for TLS fingerprinting
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

#include <stddef.h>
#include <stdint.h>

#include "slurm/slurm_errno.h"

#include "src/common/log.h"
#include "src/common/read_config.h"

#include "src/conmgr/tls_fingerprint.h"

#define HEADER_MSG_TYPE_HANDSHAKE 0x16 /* SSLv3: handshake(22) */
#define HEADER_MSG_TYPE_CLIENT_HELLO 0x01 /* TLSv1.X: client_hello(1) */

#define HEADER_LENGTH_MIN (sizeof(uint16_t))
#define HEADER_LENGTH_MAX 0x0FFF

#define PROTOCOL_VERSION_MIN 0x0300
#define PROTOCOL_VERSION_MAX 0x03ff

static int _is_sslv3_handshake(const void *buf, const size_t n)
{
	const uint8_t *p = buf;
	uint8_t msg_type = 0;
	uint16_t protocol_version = 0;
	uint16_t length = 0;

	/* Extract header if possible */
	if (n < 5)
		return EWOULDBLOCK;

	/*
	 * Match per SSLv3 RFC#6101:
	 *
	 * Record Handshake Header:
	 * |------------------------------------------------------|
	 * | 8 - msg_type | 16 - SSL version | 16 - packet length |
	 * |------------------------------------------------------|
	 *
	 * Example Record Headers:
	 *	0x16 03 01 02 00
	 *	0x16 03 01 00 f4
	 */

	if ((msg_type = p[0]) != HEADER_MSG_TYPE_HANDSHAKE)
		return ENOENT;

	protocol_version |= p[1] << 8;
	protocol_version |= p[2];

	if ((protocol_version < PROTOCOL_VERSION_MIN) ||
	    (protocol_version > PROTOCOL_VERSION_MAX))
		return ENOENT;

	length |= p[3] << 8;
	length |= p[4];

	if ((length < HEADER_LENGTH_MIN) || (length > HEADER_LENGTH_MAX))
		return ENOENT;

	return SLURM_SUCCESS;
}

static int _is_tls_handshake(const void *buf, const size_t n)
{
	const uint8_t *p = buf;
	uint8_t msg_type = 0;
	uint32_t length = 0;
	uint16_t protocol_version = 0;

	/* Extract header if possible */
	if (n < 6)
		return EWOULDBLOCK;

	/*
	 * Match per TLSv1.x RFC#8446:
	 *
	 * Client Hello Header:
	 * |----------------------------------------------------|
	 * | 8 - msg_type | 24 - length | 16 - protocol version |
	 * |----------------------------------------------------|
	 *
	 * Example Hello: 0x01 00 01 fc 03 03
	 */

	if ((msg_type = p[0]) != HEADER_MSG_TYPE_CLIENT_HELLO)
		return ENOENT;

	length |= p[1] << 16;
	length |= p[2] << 8;
	length |= p[3];

	if ((length < HEADER_LENGTH_MIN) || (length > HEADER_LENGTH_MAX))
		return ENOENT;

	protocol_version |= p[4] << 8;
	protocol_version |= p[5];

	if ((protocol_version < PROTOCOL_VERSION_MIN) ||
	    (protocol_version > PROTOCOL_VERSION_MAX))
		return ENOENT;

	return SLURM_SUCCESS;
}

extern int tls_is_handshake(const void *buf, const size_t n, const char *name)
{
	int match_tls = EINVAL, match_ssl = EINVAL;

	if (!(match_ssl = _is_sslv3_handshake(buf, n))) {
		log_flag(NET, "%s: [%s] SSLv3 handshake fingerprint matched",
			 __func__, name);
		log_flag_hex(NET_RAW, buf, n, "[%s] matched SSLv3 handshake",
			     name);
		return SLURM_SUCCESS;
	}

	if (!(match_tls = _is_tls_handshake(buf, n))) {
		log_flag(NET, "%s: [%s] TLS handshake fingerprint matched",
			 __func__, name);
		log_flag_hex(NET_RAW, buf, n, "[%s] matched TLS handshake",
			     name);
		return SLURM_SUCCESS;
	}

	if ((match_tls == EWOULDBLOCK) || (match_ssl == EWOULDBLOCK)) {
		log_flag(NET, "%s: [%s] waiting for more bytes to fingerprint match TLS handshake",
				 __func__, name);
		return EWOULDBLOCK;
	}

	if ((match_tls == ENOENT) && (match_ssl == ENOENT)) {
		log_flag(NET, "%s: [%s] TLS not detected",
			 __func__, name);
		log_flag_hex(NET_RAW, buf, n,
			     "[%s] unable to match TLS handshake", name);
		return ENOENT;
	}

	return MAX(match_tls, match_ssl);
}
