/*****************************************************************************\
 *  bg_job_info.h - definitions of functions used for the select_jobinfo_t
 *              structure
 *****************************************************************************
 *  Copyright (C) 2009-2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov> et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#ifndef _HAVE_SELECT_JOBINFO_H
#define _HAVE_SELECT_JOBINFO_H

#include "src/common/node_select.h"

#define JOBINFO_MAGIC 0x83ac

struct select_jobinfo {
	uint16_t altered;       /* see if we have altered this job
				 * or not yet */
	bg_record_t *bg_record; /* For internal use only DO NOT PACK */
	char *bg_block_id;	/* Blue Gene block ID */
	char *blrtsimage;       /* BlrtsImage for this block */
	uint32_t block_cnode_cnt; /* how many cnodes in the block
				   * This is used to say we are
				   * running a sub-block job. */
	uint16_t cleaning;      /* if the job is cleaning or not */
	uint32_t cnode_cnt;     /* how many cnodes in job running on block */
	uint16_t conn_type[HIGHEST_DIMENSIONS];	/* see enum connection_type */
	uint16_t dim_cnt;       /* how many dimensions this
				 * represents in most cases this will
				 * be SYSTEM_DIMENSIONS, but in the
				 * case of a sub-block allocation
				 * this will be the number of
				 * dimensions a cnode represent.  In
				 * Q that is 5 while a midplane is
				 * only representing 4. */
	uint16_t geometry[HIGHEST_DIMENSIONS];	/* node count in various
						 * dimensions, e.g. AXYZ */
	char *ionode_str;       /* for bg to tell which ionodes of a small
				 * block the job is running */
	char *linuximage;       /* LinuxImage for this block */
	uint16_t magic;		/* magic number */
	char *mp_str;           /* midplane list given for estimated start */
	char *mloaderimage;     /* mloaderImage for this block */
	char *ramdiskimage;     /* RamDiskImage for this block */
	uint16_t reboot;	/* reboot block before starting job */
	uint16_t rotate;	/* permit geometry rotation if set */
	uint16_t start_loc[HIGHEST_DIMENSIONS];	/* where in block we
						 * are starting from */
	bitstr_t *units_avail;  /* Used for a job allocation in a sub block,
				   the cnodes available to be used are
				   set. */
	bitstr_t *units_used;   /* bit not set are available for running. */
	char *user_name;        /* user name of the user_id only used
				 * when running multiple jobs per block */
};

/* allocate storage for a select job credential
 * OUT jobinfo - storage for a select job credential
 * RET         - jobinfo or NULL on error
 * NOTE: storage must be freed using select_g_free_jobinfo
 */
extern select_jobinfo_t *alloc_select_jobinfo();

/* free storage previously allocated for a select job credential
 * IN jobinfo  - the select job credential to be freed
 */
extern int free_select_jobinfo  (select_jobinfo_t *jobinfo);

/* fill in a previously allocated select job credential
 * IN/OUT jobinfo  - updated select job credential
 * IN data_type - type of data to enter into job credential
 * IN data - the data to enter into job credential
 */
extern int set_select_jobinfo(select_jobinfo_t *jobinfo,
			      enum select_jobdata_type data_type, void *data);

/* get data from a select job credential
 * IN jobinfo  - updated select job credential
 * IN data_type - type of data to enter into job credential
 * OUT data - the data to get from job credential, caller must xfree
 *	data for data_type == SELECT_JOBDATA_BLOCK_ID
 */
extern int get_select_jobinfo(select_jobinfo_t *jobinfo,
			      enum select_jobdata_type data_type, void *data);

/* copy a select job credential
 * IN jobinfo - the select job credential to be copied
 * RET        - the copy or NULL on failure
 * NOTE: returned value must be freed using free_jobinfo
 */
extern select_jobinfo_t *copy_select_jobinfo(select_jobinfo_t *jobinfo);

/* pack a select job credential into a buffer in machine independent form
 * IN jobinfo  - the select job credential to be saved
 * OUT buffer  - buffer with select credential appended
 * IN protocol_version - slurm protocol version of client
 * RET         - slurm error code
 */
extern int  pack_select_jobinfo(select_jobinfo_t *jobinfo, Buf buffer,
				uint16_t protocol_version);

/* unpack a select job credential from a buffer
 * OUT jobinfo - the select job credential read
 * IN  buffer  - buffer with select credential read from current pointer loc
 * IN protocol_version - slurm protocol version of client
 * RET         - slurm error code
 * NOTE: returned value must be freed using free_jobinfo
 */
extern int unpack_select_jobinfo(select_jobinfo_t **jobinfo_pptr, Buf buffer,
				 uint16_t protocol_version);

/* write select job credential to a string
 * IN jobinfo - a select job credential
 * OUT buf    - location to write job credential contents
 * IN size    - byte size of buf
 * IN mode    - print mode, see enum select_print_mode
 * RET        - the string, same as buf
 */
extern char *sprint_select_jobinfo(select_jobinfo_t *jobinfo,
				     char *buf, size_t size, int mode);

/* write select job info to a string
 * IN jobinfo - a select job credential
 * IN mode    - print mode, see enum select_print_mode
 * RET        - char * containing string of request
 */
extern char *xstrdup_select_jobinfo(select_jobinfo_t *jobinfo, int mode);

#endif
