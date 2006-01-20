/*****************************************************************************\
 **  federation_keys.h - Key definitions used by the get_jobinfo functions
 **  $Id$
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jason King <jking@llnl.gov>
 *  UCRL-CODE-217948.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifndef _FEDERATION_KEYS_INCLUDED
#define _FEDERATION_KEYS_INCLUDED

#define FED_ADAPTERNAME_LEN 5

enum {
	/* Federation specific get_jobinfo keys */
	FED_JOBINFO_TABLEINFO,
	FED_JOBINFO_TABLESPERTASK,
	FED_JOBINFO_KEY,
	FED_JOBINFO_PROTOCOL,
	FED_JOBINFO_MODE
};

/* Information shared between slurm_ll_api and the slurm federation driver */
typedef struct fed_tableinfo {
	uint32_t table_length;
	NTBL **table;
	char adapter_name[FED_ADAPTERNAME_LEN];
} fed_tableinfo_t;

#endif /* _FEDERATION_KEYS_INCLUDED */
