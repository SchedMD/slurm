/*****************************************************************************\
 **  kvs.h - KVS manipulation functions
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

#ifndef _KVS_H
#define _KVS_H

#include <slurm/slurm_errno.h>
#include "src/common/slurm_xlator.h"
#include "src/common/xstring.h"
#include "src/common/pack.h"


extern int tasks_to_wait;
extern int children_to_wait;
extern int kvs_seq;
extern int waiting_kvs_resp;

extern int   temp_kvs_init(void);
extern int   temp_kvs_add(char *key, char *val);
extern int   temp_kvs_merge(buf_t *buf);
extern int   temp_kvs_send(void);

extern int   kvs_init(void);
extern char *kvs_get(char *key);
extern int   kvs_put(char *key, char *val);
extern int   kvs_clear(void);


#endif	/* _KVS_H */
