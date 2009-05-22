/*****************************************************************************\
 *  jobinfo.c - functions used for the select_jobinfo_t structure
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov> et. al.
 *  CODE-OCEC-09-009. All rights reserved.
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

#include "jobinfo.h"

static char *_job_conn_type_string(uint16_t inx)
{
	switch(inx) {
	case SELECT_TORUS:
		return "torus";
		break;
	case SELECT_MESH:
		return "mesh";
		break;
	case SELECT_SMALL:
		return "small";
		break;
#ifndef HAVE_BGL
	case SELECT_HTC_S:
		return "htc_s";
		break;
	case SELECT_HTC_D:
		return "htc_d";
		break;
	case SELECT_HTC_V:
		return "htc_v";
		break;
	case SELECT_HTC_L:
		return "htc_l";
		break;
#endif
	default: 
		return "n/a";
	}
}

static char *_yes_no_string(uint16_t inx)
{
	if (inx == (uint16_t) NO_VAL)
		return "n/a";
	else if (inx)
		return "yes";
	else
		return "no";
}

/* allocate storage for a select job credential
 * OUT jobinfo - storage for a select job credential
 * RET         - jobinfo or NULL on error
 * NOTE: storage must be freed using select_g_free_jobinfo
 */
extern select_jobinfo_t *alloc_select_jobinfo(NULL)
{
	int i;
	select_jobinfo_t *jobinfo = xmalloc(sizeof(struct select_jobinfo));
	for (i=0; i<SYSTEM_DIMENSIONS; i++) {
		jobinfo->start[i]    = (uint16_t) NO_VAL;
		jobinfo->geometry[i] = (uint16_t) NO_VAL;
	}
	jobinfo->conn_type = SELECT_NAV;
	jobinfo->reboot = (uint16_t) NO_VAL;
	jobinfo->rotate = (uint16_t) NO_VAL;
	jobinfo->magic = JOBINFO_MAGIC;
	jobinfo->node_cnt = NO_VAL;
	jobinfo->max_procs =  NO_VAL;
	/* Remainder of structure is already NULL fulled */

	return jobinfo;
}

/* free storage previously allocated for a select job credential
 * IN jobinfo  - the select job credential to be freed
 */
extern int free_select_jobinfo(select_jobinfo_t *jobinfo)
{
	int rc = SLURM_SUCCESS;

	if (jobinfo) {	/* never set, treat as not an error */
		if (jobinfo->magic != JOBINFO_MAGIC) {
			error("free_jobinfo: jobinfo magic bad");
			return EINVAL;
		} 
		jobinfo->magic = 0;
		xfree(jobinfo->bg_block_id);
		xfree(jobinfo->nodes);
		xfree(jobinfo->ionodes);
		xfree(jobinfo->blrtsimage);
		xfree(jobinfo->linuximage);
		xfree(jobinfo->mloaderimage);
		xfree(jobinfo->ramdiskimage);
		xfree(jobinfo);
	}
	return rc;
}

/* fill in a previously allocated select job credential
 * IN/OUT jobinfo  - updated select job credential
 * IN data_type - type of data to enter into job credential
 * IN data - the data to enter into job credential
 */
extern int set_select_jobinfo(select_jobinfo_t *jobinfo,
			      enum select_jobdata_type data_type, void *data)
{
	int i, rc = SLURM_SUCCESS;
	uint16_t *uint16 = (uint16_t *) data;
	uint32_t *uint32 = (uint32_t *) data;
	char *tmp_char = (char *) data;

	if (jobinfo == NULL) {
		error("select_g_set_jobinfo: jobinfo not set");
		return SLURM_ERROR;
	}
	if (jobinfo->magic != JOBINFO_MAGIC) {
		error("set_jobinfo: jobinfo magic bad");
		return SLURM_ERROR;
	}

	switch (data_type) {
	case SELECT_JOBDATA_START:
		for (i=0; i<SYSTEM_DIMENSIONS; i++) 
			jobinfo->start[i] = uint16[i];
		break;
	case SELECT_JOBDATA_GEOMETRY:
		for (i=0; i<SYSTEM_DIMENSIONS; i++) 
			jobinfo->geometry[i] = uint16[i];
		break;
	case SELECT_JOBDATA_REBOOT:
		jobinfo->reboot = *uint16;
		break;
	case SELECT_JOBDATA_ROTATE:
		jobinfo->rotate = *uint16;
		break;
	case SELECT_JOBDATA_CONN_TYPE:
		jobinfo->conn_type = *uint16;
		break;
	case SELECT_JOBDATA_BLOCK_ID:
		/* we xfree() any preset value to avoid a memory leak */
		xfree(jobinfo->bg_block_id);
		jobinfo->bg_block_id = xstrdup(tmp_char);
		break;
	case SELECT_JOBDATA_NODES:
		xfree(jobinfo->nodes);
		jobinfo->nodes = xstrdup(tmp_char);
		break;
	case SELECT_JOBDATA_IONODES:
		xfree(jobinfo->ionodes);
		jobinfo->ionodes = xstrdup(tmp_char);
		break;
	case SELECT_JOBDATA_NODE_CNT:
		jobinfo->node_cnt = *uint32;
		break;
	case SELECT_JOBDATA_ALTERED:
		jobinfo->altered = *uint16;
		break;
	case SELECT_JOBDATA_MAX_PROCS:
		jobinfo->max_procs = *uint32;
		break;
	case SELECT_JOBDATA_BLRTS_IMAGE:
		/* we xfree() any preset value to avoid a memory leak */
		xfree(jobinfo->blrtsimage);
		jobinfo->blrtsimage = xstrdup(tmp_char);
		break;
	case SELECT_JOBDATA_LINUX_IMAGE:
		/* we xfree() any preset value to avoid a memory leak */
		xfree(jobinfo->linuximage);
		jobinfo->linuximage = xstrdup(tmp_char);
		break;
	case SELECT_JOBDATA_MLOADER_IMAGE:
		/* we xfree() any preset value to avoid a memory leak */
		xfree(jobinfo->mloaderimage);
		jobinfo->mloaderimage = xstrdup(tmp_char);
		break;
	case SELECT_JOBDATA_RAMDISK_IMAGE:
		/* we xfree() any preset value to avoid a memory leak */
		xfree(jobinfo->ramdiskimage);
		jobinfo->ramdiskimage = xstrdup(tmp_char);
		break;	
	default:
		debug("set_jobinfo data_type %d invalid", 
		      data_type);
	}

	return rc;
}

/* get data from a select job credential
 * IN jobinfo  - updated select job credential
 * IN data_type - type of data to enter into job credential
 * OUT data - the data to get from job credential, caller must xfree 
 *	data for data_tyep == SELECT_JOBDATA_BLOCK_ID 
 */
extern int get_select_jobinfo(select_jobinfo_t *jobinfo,
		enum select_jobdata_type data_type, void *data)
{
	int i, rc = SLURM_SUCCESS;
	uint16_t *uint16 = (uint16_t *) data;
	uint32_t *uint32 = (uint32_t *) data;
	char **tmp_char = (char **) data;

	if (jobinfo == NULL) {
		error("get_jobinfo: jobinfo not set");
		return SLURM_ERROR;
	}
	if (jobinfo->magic != JOBINFO_MAGIC) {
		error("get_jobinfo: jobinfo magic bad");
		return SLURM_ERROR;
	}

	switch (data_type) {
	case SELECT_JOBDATA_START:
		for (i=0; i<SYSTEM_DIMENSIONS; i++) {
			uint16[i] = jobinfo->start[i];
		}
		break;
	case SELECT_JOBDATA_GEOMETRY:
		for (i=0; i<SYSTEM_DIMENSIONS; i++) {
			uint16[i] = jobinfo->geometry[i];
		}
		break;
	case SELECT_JOBDATA_REBOOT:
		*uint16 = jobinfo->reboot;
		break;
	case SELECT_JOBDATA_ROTATE:
		*uint16 = jobinfo->rotate;
		break;
	case SELECT_JOBDATA_CONN_TYPE:
		*uint16 = jobinfo->conn_type;
		break;
	case SELECT_JOBDATA_BLOCK_ID:
		if ((jobinfo->bg_block_id == NULL)
		    ||  (jobinfo->bg_block_id[0] == '\0'))
			*tmp_char = NULL;
		else
			*tmp_char = xstrdup(jobinfo->bg_block_id);
		break;
	case SELECT_JOBDATA_NODES:
		if ((jobinfo->nodes == NULL)
		    ||  (jobinfo->nodes[0] == '\0'))
			*tmp_char = NULL;
		else
			*tmp_char = xstrdup(jobinfo->nodes);
		break;
	case SELECT_JOBDATA_IONODES:
		if ((jobinfo->ionodes == NULL)
		    ||  (jobinfo->ionodes[0] == '\0'))
			*tmp_char = NULL;
		else
			*tmp_char = xstrdup(jobinfo->ionodes);
		break;
	case SELECT_JOBDATA_NODE_CNT:
		*uint32 = jobinfo->node_cnt;
		break;
	case SELECT_JOBDATA_ALTERED:
		*uint16 = jobinfo->altered;
		break;
	case SELECT_JOBDATA_MAX_PROCS:
		*uint32 = jobinfo->max_procs;
		break;
	case SELECT_JOBDATA_BLRTS_IMAGE:
		if ((jobinfo->blrtsimage == NULL)
		    ||  (jobinfo->blrtsimage[0] == '\0'))
			*tmp_char = NULL;
		else
			*tmp_char = xstrdup(jobinfo->blrtsimage);
		break;
	case SELECT_JOBDATA_LINUX_IMAGE:
		if ((jobinfo->linuximage == NULL)
		    ||  (jobinfo->linuximage[0] == '\0'))
			*tmp_char = NULL;
		else
			*tmp_char = xstrdup(jobinfo->linuximage);
		break;
	case SELECT_JOBDATA_MLOADER_IMAGE:
		if ((jobinfo->mloaderimage == NULL)
		    ||  (jobinfo->mloaderimage[0] == '\0'))
			*tmp_char = NULL;
		else
			*tmp_char = xstrdup(jobinfo->mloaderimage);
		break;
	case SELECT_JOBDATA_RAMDISK_IMAGE:
		if ((jobinfo->ramdiskimage == NULL)
		    ||  (jobinfo->ramdiskimage[0] == '\0'))
			*tmp_char = NULL;
		else
			*tmp_char = xstrdup(jobinfo->ramdiskimage);
		break;
	default:
		debug2("get_jobinfo data_type %d invalid", 
		       data_type);
	}

	return rc;
}

/* copy a select job credential
 * IN jobinfo - the select job credential to be copied
 * RET        - the copy or NULL on failure
 * NOTE: returned value must be freed using free_jobinfo
 */
extern select_jobinfo_t *copy_select_jobinfo(select_jobinfo_t *jobinfo)
{
	struct select_jobinfo *rc = NULL;
	int i;
		
	if (jobinfo == NULL)
		;
	else if (jobinfo->magic != JOBINFO_MAGIC)
		error("copy_jobinfo: jobinfo magic bad");
	else {
		rc = xmalloc(sizeof(struct select_jobinfo));
		for (i=0; i<SYSTEM_DIMENSIONS; i++) {
			rc->start[i] = (uint16_t)jobinfo->start[i];
		}
		for (i=0; i<SYSTEM_DIMENSIONS; i++) {
			rc->geometry[i] = (uint16_t)jobinfo->geometry[i];
		}
		rc->conn_type = jobinfo->conn_type;
		rc->reboot = jobinfo->reboot;
		rc->rotate = jobinfo->rotate;
		rc->bg_block_id = xstrdup(jobinfo->bg_block_id);
		rc->magic = JOBINFO_MAGIC;
		rc->nodes = xstrdup(jobinfo->nodes);
		rc->ionodes = xstrdup(jobinfo->ionodes);
		rc->node_cnt = jobinfo->node_cnt;
		rc->altered = jobinfo->altered;
		rc->max_procs = jobinfo->max_procs;
		rc->blrtsimage = xstrdup(jobinfo->blrtsimage);
		rc->linuximage = xstrdup(jobinfo->linuximage);
		rc->mloaderimage = xstrdup(jobinfo->mloaderimage);
		rc->ramdiskimage = xstrdup(jobinfo->ramdiskimage);
	}

	return rc;
}

/* pack a select job credential into a buffer in machine independent form
 * IN jobinfo  - the select job credential to be saved
 * OUT buffer  - buffer with select credential appended
 * RET         - slurm error code
 */
extern int  pack_select_jobinfo(select_jobinfo_t *jobinfo, Buf buffer)
{
	int i;

	if (jobinfo) {
		/* NOTE: If new elements are added here, make sure to 
		 * add equivalant pack of zeros below for NULL pointer */
		for (i=0; i<SYSTEM_DIMENSIONS; i++) {
			pack16(jobinfo->start[i], buffer);
			pack16(jobinfo->geometry[i], buffer);
		}
		pack16(jobinfo->conn_type, buffer);
		pack16(jobinfo->reboot, buffer);
		pack16(jobinfo->rotate, buffer);

		pack32(jobinfo->node_cnt, buffer);
		pack32(jobinfo->max_procs, buffer);

		packstr(jobinfo->bg_block_id, buffer);
		packstr(jobinfo->nodes, buffer);
		packstr(jobinfo->ionodes, buffer);
		packstr(jobinfo->blrtsimage, buffer);
		packstr(jobinfo->linuximage, buffer);
		packstr(jobinfo->mloaderimage, buffer);
		packstr(jobinfo->ramdiskimage, buffer);
	} else {
		/* pack space for 3 positions for start and for geo
		 * then 1 for conn_type, reboot, and rotate
		 */
		for (i=0; i<((SYSTEM_DIMENSIONS*2)+3); i++)
			pack16((uint16_t) 0, buffer);

		pack32((uint32_t) 0, buffer); //node_cnt
		pack32((uint32_t) 0, buffer); //max_procs

		packnull(buffer); //bg_block_id
		packnull(buffer); //nodes
		packnull(buffer); //ionodes
		packnull(buffer); //blrts
		packnull(buffer); //linux
		packnull(buffer); //mloader
		packnull(buffer); //ramdisk
	}

	return SLURM_SUCCESS;
}

/* unpack a select job credential from a buffer
 * OUT jobinfo - the select job credential read
 * IN  buffer  - buffer with select credential read from current pointer loc
 * RET         - slurm error code
 * NOTE: returned value must be freed using free_jobinfo
 */
extern int unpack_select_jobinfo(select_jobinfo_t **jobinfo_pptr, Buf buffer)
{
	int i;
	uint32_t uint32_tmp;
	select_jobinfo_t *jobinfo = xmalloc(sizeof(struct select_jobinfo));
	*jobinfo_pptr = jobinfo;

	jobinfo->magic = JOBINFO_MAGIC
	for (i=0; i<SYSTEM_DIMENSIONS; i++) {
		safe_unpack16(&(jobinfo->start[i]), buffer);
		safe_unpack16(&(jobinfo->geometry[i]), buffer);
	}
	safe_unpack16(&(jobinfo->conn_type), buffer);
	safe_unpack16(&(jobinfo->reboot), buffer);
	safe_unpack16(&(jobinfo->rotate), buffer);

	safe_unpack32(&(jobinfo->node_cnt), buffer);
	safe_unpack32(&(jobinfo->max_procs), buffer);

	safe_unpackstr_xmalloc(&(jobinfo->bg_block_id),  &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&(jobinfo->nodes),        &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&(jobinfo->ionodes),      &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&(jobinfo->blrtsimage),   &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&(jobinfo->linuximage),   &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&(jobinfo->mloaderimage), &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&(jobinfo->ramdiskimage), &uint32_tmp, buffer);
	
	return SLURM_SUCCESS;

unpack_error:
	select_jobinfo_free(jobinfo);
	*jobinfo_pptr = NULL;
	return SLURM_ERROR;
}

/* write select job credential to a string
 * IN jobinfo - a select job credential
 * OUT buf    - location to write job credential contents
 * IN size    - byte size of buf
 * IN mode    - print mode, see enum select_print_mode
 * RET        - the string, same as buf
 */
extern char *sprint_select_jobinfo(select_jobinfo_t *jobinfo,
				     char *buf, size_t size, int mode)
{
	uint16_t geometry[SYSTEM_DIMENSIONS];
	int i;
	char max_procs_char[8], start_char[32];
	char *tmp_image = "default";
		
	if (buf == NULL) {
		error("sprint_jobinfo: buf is null");
		return NULL;
	}

	if ((mode != SELECT_PRINT_DATA)
	&& jobinfo && (jobinfo->magic != JOBINFO_MAGIC)) {
		error("sprint_jobinfo: jobinfo magic bad");
		return NULL;
	}

	if (jobinfo == NULL) {
		if (mode != SELECT_PRINT_HEAD) {
			error("sprint_jobinfo: jobinfo bad");
			return NULL;
		}
	} else if (jobinfo->geometry[0] == (uint16_t) NO_VAL) {
		for (i=0; i<SYSTEM_DIMENSIONS; i++)
			geometry[i] = 0;
	} else {
		for (i=0; i<SYSTEM_DIMENSIONS; i++)
			geometry[i] = jobinfo->geometry[i];
	}

	switch (mode) {
	case SELECT_PRINT_HEAD:
		snprintf(buf, size,
			 "CONNECT REBOOT ROTATE MAX_PROCS GEOMETRY START BLOCK_ID");
		break;
	case SELECT_PRINT_DATA:
		if (jobinfo->max_procs == NO_VAL)
			sprintf(max_procs_char, "None");
		else
			convert_num_unit((float)jobinfo->max_procs, 
					 max_procs_char, sizeof(max_procs_char),
					 UNIT_NONE);
		if (jobinfo->start[0] == (uint16_t) NO_VAL)
			sprintf(start_char, "None");
		else {
			snprintf(start_char, sizeof(start_char), 
				"%cx%cx%c",
				 alpha_num[jobinfo->start[0]],
				 alpha_num[jobinfo->start[1]],
				 alpha_num[jobinfo->start[2]]);
		} 
		snprintf(buf, size, 
			 "%7.7s %6.6s %6.6s %9s    %cx%cx%c %5s %-16s",
			 _job_conn_type_string(jobinfo->conn_type),
			 _yes_no_string(jobinfo->reboot),
			 _yes_no_string(jobinfo->rotate),
			 max_procs_char,
			 alpha_num[geometry[0]],
			 alpha_num[geometry[1]],
			 alpha_num[geometry[2]],
			 start_char, jobinfo->bg_block_id);
		break;
	case SELECT_PRINT_MIXED:
		if (jobinfo->max_procs == NO_VAL)
			sprintf(max_procs_char, "None");
		else
			convert_num_unit((float)jobinfo->max_procs,
					 max_procs_char, sizeof(max_procs_char),
					 UNIT_NONE);
		if (jobinfo->start[0] == (uint16_t) NO_VAL)
			sprintf(start_char, "None");
		else {
			snprintf(start_char, sizeof(start_char),
				"%cx%cx%c",
				 alpha_num[jobinfo->start[0]],
				 alpha_num[jobinfo->start[1]],
				 alpha_num[jobinfo->start[2]]);
		}
		
		snprintf(buf, size, 
			 "Connection=%s Reboot=%s Rotate=%s MaxProcs=%s "
			 "Geometry=%cx%cx%c Start=%s Block_ID=%s",
			 _job_conn_type_string(jobinfo->conn_type),
			 _yes_no_string(jobinfo->reboot),
			 _yes_no_string(jobinfo->rotate),
			 max_procs_char,
			 alpha_num[geometry[0]],
			 alpha_num[geometry[1]],
			 alpha_num[geometry[2]],
			 start_char, jobinfo->bg_block_id);
		break;
	case SELECT_PRINT_BG_ID:
		snprintf(buf, size, "%s", jobinfo->bg_block_id);
		break;
	case SELECT_PRINT_NODES:
		if(jobinfo->ionodes && jobinfo->ionodes[0]) 
			snprintf(buf, size, "%s[%s]",
				 jobinfo->nodes, jobinfo->ionodes);
		else
			snprintf(buf, size, "%s", jobinfo->nodes);
		break;
	case SELECT_PRINT_CONNECTION:
		snprintf(buf, size, "%s", 
			 _job_conn_type_string(jobinfo->conn_type));
		break;
	case SELECT_PRINT_REBOOT:
		snprintf(buf, size, "%s",
			 _yes_no_string(jobinfo->reboot));
		break;
	case SELECT_PRINT_ROTATE:
		snprintf(buf, size, "%s",
			 _yes_no_string(jobinfo->rotate));
		break;
	case SELECT_PRINT_GEOMETRY:
		snprintf(buf, size, "%cx%cx%c",
			 alpha_num[geometry[0]],
			 alpha_num[geometry[1]],
			 alpha_num[geometry[2]]);
		break;
	case SELECT_PRINT_START:
		if (jobinfo->start[0] == (uint16_t) NO_VAL)
			sprintf(buf, "None");
		else {
			snprintf(buf, size, 
				 "%cx%cx%c",
				 alpha_num[jobinfo->start[0]],
				 alpha_num[jobinfo->start[1]],
				 alpha_num[jobinfo->start[2]]);
		} 
	case SELECT_PRINT_MAX_PROCS:
		if (jobinfo->max_procs == NO_VAL)
			sprintf(max_procs_char, "None");
		else
			convert_num_unit((float)jobinfo->max_procs,
					 max_procs_char, sizeof(max_procs_char),
					 UNIT_NONE);
		
		snprintf(buf, size, "%s", max_procs_char);
		break;
	case SELECT_PRINT_BLRTS_IMAGE:
		if(jobinfo->blrtsimage)
			tmp_image = jobinfo->blrtsimage;
		snprintf(buf, size, "%s", tmp_image);		
		break;
	case SELECT_PRINT_LINUX_IMAGE:
		if(jobinfo->linuximage)
			tmp_image = jobinfo->linuximage;
		snprintf(buf, size, "%s", tmp_image);		
		break;
	case SELECT_PRINT_MLOADER_IMAGE:
		if(jobinfo->mloaderimage)
			tmp_image = jobinfo->mloaderimage;
		snprintf(buf, size, "%s", tmp_image);		
		break;
	case SELECT_PRINT_RAMDISK_IMAGE:
		if(jobinfo->ramdiskimage)
			tmp_image = jobinfo->ramdiskimage;
		snprintf(buf, size, "%s", tmp_image);		
		break;		
	default:
		error("sprint_jobinfo: bad mode %d", mode);
		if (size > 0)
			buf[0] = '\0';
	}
	
	return buf;
}

/* write select job info to a string
 * IN jobinfo - a select job credential
 * IN mode    - print mode, see enum select_print_mode
 * RET        - char * containing string of request
 */
extern char *xstrdup_select_jobinfo(select_jobinfo_t *jobinfo, int mode)
{
	uint16_t geometry[SYSTEM_DIMENSIONS];
	int i;
	char max_procs_char[8], start_char[32];
	char *tmp_image = "default";
	char *buf = NULL;
		
	if ((mode != SELECT_PRINT_DATA)
	    && jobinfo && (jobinfo->magic != JOBINFO_MAGIC)) {
		error("xstrdup_jobinfo: jobinfo magic bad");
		return NULL;
	}

	if (jobinfo == NULL) {
		if (mode != SELECT_PRINT_HEAD) {
			error("xstrdup_jobinfo: jobinfo bad");
			return NULL;
		}
	} else if (jobinfo->geometry[0] == (uint16_t) NO_VAL) {
		for (i=0; i<SYSTEM_DIMENSIONS; i++)
			geometry[i] = 0;
	} else {
		for (i=0; i<SYSTEM_DIMENSIONS; i++)
			geometry[i] = jobinfo->geometry[i];
	}

	switch (mode) {
	case SELECT_PRINT_HEAD:
		xstrcat(buf, 
			"CONNECT REBOOT ROTATE MAX_PROCS "
			"GEOMETRY START BLOCK_ID");
		break;
	case SELECT_PRINT_DATA:
		if (jobinfo->max_procs == NO_VAL)
			sprintf(max_procs_char, "None");
		else
			convert_num_unit((float)jobinfo->max_procs, 
					 max_procs_char, sizeof(max_procs_char),
					 UNIT_NONE);
		if (jobinfo->start[0] == (uint16_t) NO_VAL)
			sprintf(start_char, "None");
		else {
			snprintf(start_char, sizeof(start_char), 
				"%cx%cx%c",
				 alpha_num[jobinfo->start[0]],
				 alpha_num[jobinfo->start[1]],
				 alpha_num[jobinfo->start[2]]);
		} 
		xstrfmtcat(buf, 
			   "%7.7s %6.6s %6.6s %9s    %cx%cx%c %5s %-16s",
			   _job_conn_type_string(jobinfo->conn_type),
			   _yes_no_string(jobinfo->reboot),
			   _yes_no_string(jobinfo->rotate),
			   max_procs_char,
			   alpha_num[geometry[0]],
			   alpha_num[geometry[1]],
			   alpha_num[geometry[2]],
			   start_char, jobinfo->bg_block_id);
		break;
	case SELECT_PRINT_MIXED:
		if (jobinfo->max_procs == NO_VAL)
			sprintf(max_procs_char, "None");
		else
			convert_num_unit((float)jobinfo->max_procs,
					 max_procs_char, sizeof(max_procs_char),
					 UNIT_NONE);
		if (jobinfo->start[0] == (uint16_t) NO_VAL)
			sprintf(start_char, "None");
		else {
			snprintf(start_char, sizeof(start_char),
				"%cx%cx%c",
				 alpha_num[jobinfo->start[0]],
				 alpha_num[jobinfo->start[1]],
				 alpha_num[jobinfo->start[2]]);
		}
		
		xstrfmtcat(buf, 
			 "Connection=%s Reboot=%s Rotate=%s MaxProcs=%s "
			 "Geometry=%cx%cx%c Start=%s Block_ID=%s",
			 _job_conn_type_string(jobinfo->conn_type),
			 _yes_no_string(jobinfo->reboot),
			 _yes_no_string(jobinfo->rotate),
			 max_procs_char,
			 alpha_num[geometry[0]],
			 alpha_num[geometry[1]],
			 alpha_num[geometry[2]],
			 start_char, jobinfo->bg_block_id);
		break;
	case SELECT_PRINT_BG_ID:
		xstrfmtcat(buf, "%s", jobinfo->bg_block_id);
		break;
	case SELECT_PRINT_NODES:
		if(jobinfo->ionodes && jobinfo->ionodes[0]) 
			xstrfmtcat(buf, "%s[%s]",
				 jobinfo->nodes, jobinfo->ionodes);
		else
			xstrfmtcat(buf, "%s", jobinfo->nodes);
		break;
	case SELECT_PRINT_CONNECTION:
		xstrfmtcat(buf, "%s", 
			 _job_conn_type_string(jobinfo->conn_type));
		break;
	case SELECT_PRINT_REBOOT:
		xstrfmtcat(buf, "%s",
			 _yes_no_string(jobinfo->reboot));
		break;
	case SELECT_PRINT_ROTATE:
		xstrfmtcat(buf, "%s",
			 _yes_no_string(jobinfo->rotate));
		break;
	case SELECT_PRINT_GEOMETRY:
		xstrfmtcat(buf, "%cx%cx%c",
			 alpha_num[geometry[0]],
			 alpha_num[geometry[1]],
			 alpha_num[geometry[2]]);
		break;
	case SELECT_PRINT_START:
		if (jobinfo->start[0] == (uint16_t) NO_VAL)
			sprintf(buf, "None");
		else {
			xstrfmtcat(buf, 
				 "%cx%cx%c",
				 alpha_num[jobinfo->start[0]],
				 alpha_num[jobinfo->start[1]],
				 alpha_num[jobinfo->start[2]]);
		} 
	case SELECT_PRINT_MAX_PROCS:
		if (jobinfo->max_procs == NO_VAL)
			sprintf(max_procs_char, "None");
		else
			convert_num_unit((float)jobinfo->max_procs,
					 max_procs_char, sizeof(max_procs_char),
					 UNIT_NONE);
		
		xstrfmtcat(buf, "%s", max_procs_char);
		break;
	case SELECT_PRINT_BLRTS_IMAGE:
		if(jobinfo->blrtsimage)
			tmp_image = jobinfo->blrtsimage;
		xstrfmtcat(buf, "%s", tmp_image);		
		break;
	case SELECT_PRINT_LINUX_IMAGE:
		if(jobinfo->linuximage)
			tmp_image = jobinfo->linuximage;
		xstrfmtcat(buf, "%s", tmp_image);		
		break;
	case SELECT_PRINT_MLOADER_IMAGE:
		if(jobinfo->mloaderimage)
			tmp_image = jobinfo->mloaderimage;
		xstrfmtcat(buf, "%s", tmp_image);		
		break;
	case SELECT_PRINT_RAMDISK_IMAGE:
		if(jobinfo->ramdiskimage)
			tmp_image = jobinfo->ramdiskimage;
		xstrfmtcat(buf, "%s", tmp_image);		
		break;		
	default:
		error("xstrdup_jobinfo: bad mode %d", mode);
	}
	
	return buf;
}
