/*****************************************************************************\
 *  certmgr.h - certmgr API definitions
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

#ifndef _INTERFACES_CERTMGR_H
#define _INTERFACES_CERTMGR_H

#include <inttypes.h>
#include <stdbool.h>

#include "src/common/read_config.h"

extern int certmgr_g_init(void);
extern int certmgr_g_fini(void);

/*
 * Check if the certmgr plugin is initialized (and not no-op)
 */
extern bool certmgr_enabled(void);

/*
 * Get period in minutes for which a new certificate will be requested to
 * replace an old certificate.
 *
 * RET SLURM_SUCCESS or error
 */
extern int certmgr_get_renewal_period_mins(void);

/*
 * Get node private key
 *
 * IN node_name - get private key associated with this node name
 *
 * RET SLURM_SUCCESS or error
 */
extern char *certmgr_g_get_node_cert_key(char *node_name);

/*
 * Get unique node token to validate an accompanying CSR
 *
 * IN node_name - get the token associated with this node name
 *
 * RET SLURM_SUCCESS or error
 */
extern char *certmgr_g_get_node_token(char *node_name);

/*
 * Generate certificate signing request to send to slurmctld
 *
 * IN node_name - generate CSR for node with this node name
 *
 * RET SLURM_SUCCESS or error
 */
extern char *certmgr_g_generate_csr(char *node_name);

/*
 * Validate incoming certificate signing request on slurmctld
 *
 * IN csr  - CSR PEM character string.
 * IN token - unique token associated with CSR to check validity
 * IN name - hostname or node name of client that generated CSR
 *
 * RET CSR PEM character string or NULL on error.
 */
extern char *certmgr_g_sign_csr(char *csr, char *token, char *name);

#endif
