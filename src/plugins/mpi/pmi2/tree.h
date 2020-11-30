/*****************************************************************************\
 **  tree.h - PMI tree communication handling code
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

#ifndef _TREE_H
#define _TREE_H

enum {
	TREE_CMD_KVS_FENCE,
	TREE_CMD_KVS_FENCE_RESP,
	TREE_CMD_SPAWN,
	TREE_CMD_SPAWN_RESP,
	TREE_CMD_NAME_PUBLISH,
	TREE_CMD_NAME_UNPUBLISH,
	TREE_CMD_NAME_LOOKUP,
	TREE_CMD_RING,
	TREE_CMD_RING_RESP,
	TREE_CMD_COUNT
};


extern int handle_tree_cmd(int fd);
extern int tree_msg_to_srun(uint32_t len, char *msg);
extern int tree_msg_to_srun_with_resp(uint32_t len, char *msg,
				      buf_t **resp_ptr);
extern int tree_msg_to_spawned_sruns(uint32_t len, char *msg);



#endif	/* _TREE_H */
