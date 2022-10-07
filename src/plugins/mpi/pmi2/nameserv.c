/*****************************************************************************\
 **  nameserv.c - name publish/unpublish/lookup functions
 *****************************************************************************
 *  Copyright (C) 2013 National University of Defense Technology.
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


#include "pmi.h"
#include "setup.h"
#include "client.h"

typedef struct name_port {
	char *name;
	char *port;
	struct name_port *next;
} name_port_t;

/*
 * just a list for now.
 * a db or directory is more useful.
 * or execute a script to pub/unpub/lookup.
 */
static name_port_t *local_name_list = NULL;

extern char *
name_lookup_local (char *name)
{
	name_port_t *np;

	np = local_name_list;
	while (np && xstrcmp(np->name, name))
		np = np->next;

	return np ? xstrdup(np->port) : NULL;
}

extern int
name_publish_local (char *name, char *port)
{
	name_port_t *np;

	np = local_name_list;
	while (np && xstrcmp(np->name, name))
		np = np->next;
	if (np) {
		xfree(np->port);
		np->port = xstrdup(port);
	} else {
		np = xmalloc(sizeof(name_port_t));
		np->name = xstrdup(name);
		np->port = xstrdup(port);
		np->next = local_name_list;
		local_name_list = np;
	}
	return SLURM_SUCCESS;
}

extern int
name_unpublish_local (char *name)
{
	name_port_t *np, **pprev;

	pprev = &local_name_list;
	np = *pprev;
	while (np) {
		if (xstrcmp(np->name, name)) {
			pprev = &np->next;
			np = np->next;
		} else {
			*pprev = np->next;
			xfree(np->name);
			xfree(np->port);
			xfree(np);
			np = *pprev;
			break;
		}
	}
	return SLURM_SUCCESS;
}

extern int
name_publish_up(char *name, char *port)
{
	buf_t *buf = NULL, *resp_buf = NULL;
	uint32_t size, tmp_32;
	int rc;

	buf = init_buf(1024);
	pack16((uint16_t)TREE_CMD_NAME_PUBLISH, buf);
	packstr(name, buf);
	packstr(port, buf);
	size = get_buf_offset(buf);

	rc = tree_msg_to_srun_with_resp(size, get_buf_data(buf), &resp_buf);
	FREE_NULL_BUFFER(buf);

	if (rc == SLURM_SUCCESS) {
		safe_unpack32(&tmp_32, resp_buf);
		rc = (int) tmp_32;
	}

unpack_error:
	FREE_NULL_BUFFER(resp_buf);

	return rc;
}

extern int
name_unpublish_up(char *name)
{
	buf_t *buf = NULL, *resp_buf = NULL;
	uint32_t size, tmp_32;
	int rc;

	buf = init_buf(1024);
	pack16((uint16_t)TREE_CMD_NAME_UNPUBLISH, buf);
	packstr(name, buf);
	size = get_buf_offset(buf);

	rc = tree_msg_to_srun_with_resp(size, get_buf_data(buf), &resp_buf);
	FREE_NULL_BUFFER(buf);

	if (rc == SLURM_SUCCESS) {
		safe_unpack32(&tmp_32, resp_buf);
		rc = (int) tmp_32;
	}

unpack_error:
	FREE_NULL_BUFFER(resp_buf);

	return rc;
}


extern char *
name_lookup_up(char *name)
{
	buf_t *buf = NULL, *resp_buf = NULL;
	uint32_t size;
	char * port = NULL;
	int rc;

	buf = init_buf(1024);
	pack16((uint16_t)TREE_CMD_NAME_LOOKUP, buf);
	packstr(name, buf);
	size = get_buf_offset(buf);

	rc = tree_msg_to_srun_with_resp(size, get_buf_data(buf), &resp_buf);
	FREE_NULL_BUFFER(buf);

	if (rc == SLURM_SUCCESS)
		safe_unpackstr_xmalloc(&port, (uint32_t *)&size, resp_buf);
unpack_error:
	FREE_NULL_BUFFER(resp_buf);

	return port;
}
