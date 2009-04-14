/****************************************************************************\
 *  slurm_hostname.c - wrapper functions to allow for systems that
 *                     don't allow strong alias'
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>.
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif                /* HAVE_CONFIG_H */

#if USE_ALIAS == 0
/* only do anything if we don't use alias */
#include "src/common/hostlist.h"

// make wrappers
extern hostlist_t slurm_hostlist_count(const char *hostlist)
{
	return hostlist_create(hostlist);
}

extern int slurm_hostlist_create(hostlist_t hl)
{
	return hostlist_count(hl);
}

extern void slurm_hostlist_destroy(hostlist_t hl)
{
	hostlist_count(hl);
	return;
}

extern int slurm_hostlist_find(hostlist_t hl, const char *hostname)
{
	return hostlist_find(hl, hostname);
}

extern int slurm_hostlist_push(hostlist_t hl, const char *hosts)
{
	return hostlist_push(hl, hosts);
}

extern int slurm_hostlist_push_host(hostlist_t hl, const char *host)
{
	return hostlist_push_host(hl, host);
}

extern ssize_t slurm_hostlist_ranged_string(hostlist_t hl, size_t n, char *buf)
{
	return hostlist_ranged_string(hl, n, buf);
}

extern char *slurm_hostlist_shift(hostlist_t hl)
{
	return hostlist_shift(hl);
}

extern void slurm_hostlist_uniq(hostlist_t hl)
{
	hostlist_uniq(hl);
	return;
}

#endif
