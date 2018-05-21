/*****************************************************************************\
 **  client.h - PMI client wire protocol message handling
 *****************************************************************************
 *  Copyright (C) 2011-2012 National University of Defense Technology.
 *  Written by Hongjia Cao <hjcao@nudt.edu.cn>.
 *  All rights reserved.
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

#ifndef _CLIENT_H
#define _CLIENT_H

#include <inttypes.h>

#include "spawn.h"

#define PMI11_VERSION 1
#define PMI11_SUBVERSION 1
#define PMI20_VERSION 2
#define PMI20_SUBVERSION 0

typedef struct client_request {
	int buf_len;
	char *buf;
	char sep;		/* cmd/value seperator */
	char term;		/* request terminator */
	int parse_idx;		/* ptr used in parsing */
	char *cmd;		/* cmd points to buf or other static memory*/
	char **pairs;		/* key-value in pairs always point to buf */
	uint32_t pairs_size;
	uint32_t pairs_cnt;
} client_req_t;

typedef struct client_response {
	char *buf;
} client_resp_t;


extern int get_pmi_version(int *version, int *subversion);
extern int set_pmi_version(int version, int subversion);
extern int is_pmi11(void);
extern int is_pmi20(void);

extern client_req_t *client_req_init(uint32_t len, char *buf);
extern void client_req_free(client_req_t *req);
extern int  client_req_parse_cmd(client_req_t *req);
extern int  client_req_parse_body(client_req_t *req);
extern bool  client_req_get_str(client_req_t *req, const char *key, char **val);
extern bool  client_req_get_int(client_req_t *req, const char *key, int *val);
extern bool  client_req_get_bool(client_req_t *req, const char *key, bool *val);

extern spawn_req_t *client_req_parse_spawn_req(client_req_t *req);
extern spawn_subcmd_t *client_req_parse_spawn_subcmd(client_req_t *req);

extern client_resp_t *client_resp_new(void);
extern int  client_resp_send(client_resp_t *req, int fd);
extern void client_resp_free(client_resp_t *resp);
/* XXX: this requires CPP */
#define client_resp_append(msg, fmt, ...) do { \
		xstrfmtcat(msg->buf, fmt, ## __VA_ARGS__);	\
	} while (0)


extern int send_kvs_fence_resp_to_clients(int rc, char *errmsg);

#endif	/* _CLIENT_H */
