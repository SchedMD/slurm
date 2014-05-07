/*****************************************************************************\
 *  bg_job_info.c - functions used for the select_jobinfo_t structure
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

#include "src/common/slurm_xlator.h"
#include "bg_core.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/uid.h"

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
extern select_jobinfo_t *alloc_select_jobinfo()
{
	int i;
	select_jobinfo_t *jobinfo = xmalloc(sizeof(struct select_jobinfo));
	jobinfo->dim_cnt = 0; /* This will be setup later */
	for (i=0; i<HIGHEST_DIMENSIONS; i++) {
		jobinfo->geometry[i] = (uint16_t) NO_VAL;
		jobinfo->conn_type[i] = (uint16_t) NO_VAL;
	}
	jobinfo->reboot = (uint16_t) NO_VAL;
	jobinfo->rotate = (uint16_t) NO_VAL;
	jobinfo->magic = JOBINFO_MAGIC;
	jobinfo->block_cnode_cnt = 0;
	jobinfo->cnode_cnt = 0;

	/* This bitstr is created when used. */
	//jobinfo->units_avail = bit_alloc(bg_conf->mp_cnode_cnt);
	//jobinfo->units_used = bit_alloc(bg_conf->mp_cnode_cnt);

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
		jobinfo->bg_record = NULL;
		xfree(jobinfo->bg_block_id);
		xfree(jobinfo->mp_str);
		xfree(jobinfo->ionode_str);
		xfree(jobinfo->blrtsimage);
		xfree(jobinfo->linuximage);
		xfree(jobinfo->mloaderimage);
		xfree(jobinfo->ramdiskimage);
		FREE_NULL_BITMAP(jobinfo->units_avail);
		FREE_NULL_BITMAP(jobinfo->units_used);
		xfree(jobinfo->user_name);
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
	uid_t *uid = (uid_t *)data;
	char *tmp_char = (char *) data;
	bg_record_t *bg_record = (bg_record_t *) data;
	uint32_t new_size;

	if (!jobinfo) {
		error("set_select_jobinfo: jobinfo is NULL");
		return SLURM_ERROR;
	} else if (jobinfo->magic != JOBINFO_MAGIC) {
		error("set_select_jobinfo: jobinfo magic bad");
		return SLURM_ERROR;
	}

	if (!jobinfo->dim_cnt)
		jobinfo->dim_cnt = SYSTEM_DIMENSIONS;

	switch (data_type) {
	case SELECT_JOBDATA_DIM_CNT:
		jobinfo->dim_cnt = *uint16;
		break;
	case SELECT_JOBDATA_GEOMETRY:
		new_size = 1;
		for (i=0; i<jobinfo->dim_cnt; i++) {
			/* If geo[i] is NO_VAL then we know this
			   doesn't need to be reset.
			*/
			if (jobinfo->geometry[i] != (uint16_t) NO_VAL) {
				/* Make sure the conn type is correct with the
				 * new count (if Geometry is requested it
				 * can't be small) */
				jobinfo->conn_type[i] =	SELECT_NAV;
			}
			jobinfo->geometry[i] = uint16[i];
			new_size *= uint16[i];
		}

		break;
	case SELECT_JOBDATA_REBOOT:
		jobinfo->reboot = *uint16;
		break;
	case SELECT_JOBDATA_ROTATE:
		jobinfo->rotate = *uint16;
		break;
	case SELECT_JOBDATA_CONN_TYPE:
		for (i=0; i<jobinfo->dim_cnt; i++)
			jobinfo->conn_type[i] = uint16[i];
		break;
	case SELECT_JOBDATA_BLOCK_ID:
		/* we xfree() any preset value to avoid a memory leak */
		xfree(jobinfo->bg_block_id);
		jobinfo->bg_block_id = xstrdup(tmp_char);
		break;
	case SELECT_JOBDATA_BLOCK_NODE_CNT:
		jobinfo->block_cnode_cnt = *uint32;
		break;
	case SELECT_JOBDATA_BLOCK_PTR:
		jobinfo->bg_record = bg_record;
		xfree(jobinfo->bg_block_id);
		if (bg_record) {
			jobinfo->bg_block_id = xstrdup(bg_record->bg_block_id);
			jobinfo->block_cnode_cnt = bg_record->cnode_cnt;
		} else {
			jobinfo->bg_block_id = xstrdup("unassigned");
			jobinfo->block_cnode_cnt = 0;
		}
		break;
	case SELECT_JOBDATA_NODES:
		xfree(jobinfo->mp_str);
		jobinfo->mp_str = xstrdup(tmp_char);
		break;
	case SELECT_JOBDATA_IONODES:
		xfree(jobinfo->ionode_str);
		if (tmp_char) {
#ifdef HAVE_BGQ
			jobinfo->dim_cnt = 5;
			for (i=0; i<jobinfo->dim_cnt; i++) {
				jobinfo->start_loc[i] =
					select_char2coord(tmp_char[i]);
			}
#else
			jobinfo->dim_cnt = SYSTEM_DIMENSIONS;
#endif
			jobinfo->ionode_str = xstrdup(tmp_char);
		} else
			jobinfo->dim_cnt = SYSTEM_DIMENSIONS;

		break;
	case SELECT_JOBDATA_NODE_CNT:
		jobinfo->cnode_cnt = *uint32;
		/* Make sure the conn type is correct with the new count */
		if (bg_conf->sub_mp_sys
		    || (jobinfo->cnode_cnt < bg_conf->mp_cnode_cnt)) {
			if (jobinfo->conn_type[0] < SELECT_SMALL)
				jobinfo->conn_type[0] = SELECT_SMALL;
		} else if (jobinfo->conn_type[0] >= SELECT_SMALL) {
			for (i=0; i<SYSTEM_DIMENSIONS; i++)
				jobinfo->conn_type[i] = SELECT_NAV;
		}
		break;
	case SELECT_JOBDATA_ALTERED:
		jobinfo->altered = *uint16;
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
	case SELECT_JOBDATA_START_LOC:
		for (i=0; i<jobinfo->dim_cnt; i++)
			jobinfo->start_loc[i] = uint16[i];
		break;
	case SELECT_JOBDATA_USER_NAME:
		xfree(jobinfo->user_name);
		jobinfo->user_name = uid_to_string(*uid);
		break;
	default:
		debug3("set_select_jobinfo: data_type %d invalid",
		       data_type);
	}

	return rc;
}

/* get data from a select job credential
 * IN jobinfo  - updated select job credential
 * IN data_type - type of data to enter into job credential
 * OUT data - the data to get from job credential, caller must xfree
 *	data for data_type == SELECT_JOBDATA_BLOCK_ID
 */
extern int get_select_jobinfo(select_jobinfo_t *jobinfo,
			      enum select_jobdata_type data_type, void *data)
{
	int i, rc = SLURM_SUCCESS;
	uint16_t *uint16 = (uint16_t *) data;
	uint32_t *uint32 = (uint32_t *) data;
	bg_record_t **bg_record = (bg_record_t **) data;
	char **tmp_char = (char **) data;

	if (!jobinfo) {
		if (data_type == SELECT_JOBDATA_CLEANING) {
			debug2("get_select_jobinfo: jobinfo is NULL");
			*uint16 = 0;
		} else {
			error("get_select_jobinfo: jobinfo is NULL");
			rc = SLURM_ERROR;
		}
		return rc;
	} else if (jobinfo->magic != JOBINFO_MAGIC) {
		error("get_jobinfo: jobinfo magic bad");
		return SLURM_ERROR;
	}

	if (!jobinfo->dim_cnt)
		jobinfo->dim_cnt = SYSTEM_DIMENSIONS;

	switch (data_type) {
	case SELECT_JOBDATA_DIM_CNT:
		*uint16 = jobinfo->dim_cnt;
		break;
	case SELECT_JOBDATA_GEOMETRY:
		for (i=0; i<jobinfo->dim_cnt; i++) {
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
		for (i=0; i<jobinfo->dim_cnt; i++)
			uint16[i] = jobinfo->conn_type[i];
		break;
	case SELECT_JOBDATA_BLOCK_ID:
		if ((jobinfo->bg_block_id == NULL)
		    ||  (jobinfo->bg_block_id[0] == '\0'))
			*tmp_char = NULL;
		else
			*tmp_char = xstrdup(jobinfo->bg_block_id);
		break;
	case SELECT_JOBDATA_BLOCK_NODE_CNT:
		*uint32 = jobinfo->block_cnode_cnt;
		break;
	case SELECT_JOBDATA_BLOCK_PTR:
		*bg_record = jobinfo->bg_record;
		break;
	case SELECT_JOBDATA_NODES:
		if ((jobinfo->mp_str == NULL)
		    ||  (jobinfo->mp_str[0] == '\0'))
			*tmp_char = NULL;
		else
			*tmp_char = xstrdup(jobinfo->mp_str);
		break;
	case SELECT_JOBDATA_IONODES:
		if ((jobinfo->ionode_str == NULL)
		    ||  (jobinfo->ionode_str[0] == '\0'))
			*tmp_char = NULL;
		else
			*tmp_char = xstrdup(jobinfo->ionode_str);
		break;
	case SELECT_JOBDATA_NODE_CNT:
		*uint32 = jobinfo->cnode_cnt;
		break;
	case SELECT_JOBDATA_ALTERED:
		*uint16 = jobinfo->altered;
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
	case SELECT_JOBDATA_START_LOC:
		for (i=0; i<jobinfo->dim_cnt; i++) {
			uint16[i] = jobinfo->start_loc[i];
		}
		break;
	case SELECT_JOBDATA_USER_NAME:
		if ((jobinfo->user_name == NULL)
		    ||  (jobinfo->user_name[0] == '\0'))
			*tmp_char = NULL;
		else
			*tmp_char = xstrdup(jobinfo->user_name);
		break;
	case SELECT_JOBDATA_CLEANING:
		/* In the case of Cleaning it means we have a block_ptr. */
		/* if (jobinfo->bg_record) */
		/* 	*uint16 = 1; */
		/* else */
		/* 	*uint16 = 0; */
		*uint16 = jobinfo->cleaning;
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

	if (jobinfo == NULL)
		;
	else if (jobinfo->magic != JOBINFO_MAGIC)
		error("copy_jobinfo: jobinfo magic bad");
	else {
		rc = xmalloc(sizeof(struct select_jobinfo));
		rc->dim_cnt = jobinfo->dim_cnt;
		memcpy(rc->geometry, jobinfo->geometry, sizeof(rc->geometry));
		memcpy(rc->conn_type, jobinfo->conn_type,
		       sizeof(rc->conn_type));
		memcpy(rc->start_loc, jobinfo->start_loc,
		       sizeof(rc->start_loc));
		rc->reboot = jobinfo->reboot;
		rc->rotate = jobinfo->rotate;
		rc->bg_record = jobinfo->bg_record;
		rc->bg_block_id = xstrdup(jobinfo->bg_block_id);
		rc->magic = JOBINFO_MAGIC;
		rc->mp_str = xstrdup(jobinfo->mp_str);
		rc->ionode_str = xstrdup(jobinfo->ionode_str);
		rc->block_cnode_cnt = jobinfo->block_cnode_cnt;
		rc->cleaning = jobinfo->cleaning;
		rc->cnode_cnt = jobinfo->cnode_cnt;
		rc->altered = jobinfo->altered;
		rc->blrtsimage = xstrdup(jobinfo->blrtsimage);
		rc->linuximage = xstrdup(jobinfo->linuximage);
		rc->mloaderimage = xstrdup(jobinfo->mloaderimage);
		rc->ramdiskimage = xstrdup(jobinfo->ramdiskimage);
		if (jobinfo->units_avail)
			rc->units_avail = bit_copy(jobinfo->units_avail);
		if (jobinfo->units_used)
			rc->units_used = bit_copy(jobinfo->units_used);
		rc->user_name = xstrdup(jobinfo->user_name);
	}

	return rc;
}

/* pack a select job credential into a buffer in machine independent form
 * IN jobinfo  - the select job credential to be saved
 * OUT buffer  - buffer with select credential appended
 * IN protocol_version - slurm protocol version of client
 * RET         - slurm error code
 */
extern int  pack_select_jobinfo(select_jobinfo_t *jobinfo, Buf buffer,
				uint16_t protocol_version)
{
	int i;
	int dims = slurmdb_setup_cluster_dims();

	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		if (jobinfo) {
			if (jobinfo->dim_cnt)
				dims = jobinfo->dim_cnt;
			else if (bg_recover != NOT_FROM_CONTROLLER)
				xassert(0);

			pack16(dims, buffer);
			/* NOTE: If new elements are added here, make sure to
			 * add equivalant pack of zeros below for NULL
			 * pointer */
			for (i=0; i<dims; i++) {
				pack16(jobinfo->geometry[i], buffer);
				pack16(jobinfo->conn_type[i], buffer);
				pack16(jobinfo->start_loc[i], buffer);
			}
			pack16(jobinfo->reboot, buffer);
			pack16(jobinfo->rotate, buffer);

			pack32(jobinfo->block_cnode_cnt, buffer);
			pack16(jobinfo->cleaning, buffer);
			pack32(jobinfo->cnode_cnt, buffer);

			packstr(jobinfo->bg_block_id, buffer);
			packstr(jobinfo->mp_str, buffer);
			packstr(jobinfo->ionode_str, buffer);

			packstr(jobinfo->blrtsimage, buffer);
			packstr(jobinfo->linuximage, buffer);
			packstr(jobinfo->mloaderimage, buffer);
			packstr(jobinfo->ramdiskimage, buffer);
			if (bg_conf) {
				pack16(bg_conf->mp_cnode_cnt, buffer);
				pack_bit_fmt(jobinfo->units_avail, buffer);
				pack_bit_fmt(jobinfo->units_used, buffer);
			} else {
				pack16(0, buffer);
				packnull(buffer);
				packnull(buffer);
			}
		} else {
			pack16(dims, buffer);
			/* pack space for 3 positions for geo
			 * conn_type and start_loc and then, reboot, and rotate
			 */
			for (i=0; i<((dims*3)+2); i++) {
				pack16((uint16_t) 0, buffer);
			}
			pack32((uint32_t) 0, buffer); //block_cnode_cnt
			pack16((uint16_t) 0, buffer); //cleaning
			pack32((uint32_t) 0, buffer); //cnode_cnt
			packnull(buffer); //bg_block_id
			packnull(buffer); //nodes
			packnull(buffer); //ionodes

			packnull(buffer); //blrts
			packnull(buffer); //linux
			packnull(buffer); //mloader
			packnull(buffer); //ramdisk
			pack16((uint16_t) 0, buffer); //mp_cnode_cnt
			packnull(buffer); //units_avail
			packnull(buffer); //units_used
		}
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		if (jobinfo) {
			if (jobinfo->dim_cnt)
				dims = jobinfo->dim_cnt;
			else if (bg_recover != NOT_FROM_CONTROLLER)
				xassert(0);

			pack16(dims, buffer);
			/* NOTE: If new elements are added here, make sure to
			 * add equivalant pack of zeros below for NULL
			 * pointer */
			for (i=0; i<dims; i++) {
				pack16(jobinfo->geometry[i], buffer);
				pack16(jobinfo->conn_type[i], buffer);
				pack16(jobinfo->start_loc[i], buffer);
			}
			pack16(jobinfo->reboot, buffer);
			pack16(jobinfo->rotate, buffer);

			pack32(jobinfo->block_cnode_cnt, buffer);
			pack32(jobinfo->cnode_cnt, buffer);

			packstr(jobinfo->bg_block_id, buffer);
			packstr(jobinfo->mp_str, buffer);
			packstr(jobinfo->ionode_str, buffer);

			packstr(jobinfo->blrtsimage, buffer);
			packstr(jobinfo->linuximage, buffer);
			packstr(jobinfo->mloaderimage, buffer);
			packstr(jobinfo->ramdiskimage, buffer);
			if (bg_conf) {
				pack16(bg_conf->mp_cnode_cnt, buffer);
				pack_bit_fmt(jobinfo->units_avail, buffer);
				pack_bit_fmt(jobinfo->units_used, buffer);
			} else {
				pack16(0, buffer);
				packnull(buffer);
				packnull(buffer);
			}
		} else {
			pack16(dims, buffer);
			/* pack space for 3 positions for geo
			 * conn_type and start_loc and then, reboot, and rotate
			 */
			for (i=0; i<((dims*3)+2); i++) {
				pack16((uint16_t) 0, buffer);
			}
			pack32((uint32_t) 0, buffer); //block_cnode_cnt
			pack32((uint32_t) 0, buffer); //cnode_cnt
			packnull(buffer); //bg_block_id
			packnull(buffer); //nodes
			packnull(buffer); //ionodes

			packnull(buffer); //blrts
			packnull(buffer); //linux
			packnull(buffer); //mloader
			packnull(buffer); //ramdisk
			pack16((uint16_t) 0, buffer); //mp_cnode_cnt
			packnull(buffer); //units_avail
			packnull(buffer); //units_used
		}
	} else {
 		error("pack_select_jobinfo: protocol_version "
 		      "%hu not supported", protocol_version);
	}
	return SLURM_SUCCESS;
}

/* unpack a select job credential from a buffer
 * OUT jobinfo - the select job credential read
 * IN  buffer  - buffer with select credential read from current pointer loc
 * IN protocol_version - slurm protocol version of client
 * RET         - slurm error code
 * NOTE: returned value must be freed using free_jobinfo
 */
extern int unpack_select_jobinfo(select_jobinfo_t **jobinfo_pptr, Buf buffer,
				 uint16_t protocol_version)
{
	int i;
	uint32_t uint32_tmp;
	uint16_t mp_cnode_cnt;
	int dims;
	select_jobinfo_t *jobinfo = xmalloc(sizeof(struct select_jobinfo));
	char *bit_char = NULL;
	*jobinfo_pptr = jobinfo;

	jobinfo->magic = JOBINFO_MAGIC;

	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		safe_unpack16(&jobinfo->dim_cnt, buffer);

		xassert(jobinfo->dim_cnt);
		dims = jobinfo->dim_cnt;

		for (i=0; i<dims; i++) {
			safe_unpack16(&(jobinfo->geometry[i]), buffer);
			safe_unpack16(&(jobinfo->conn_type[i]), buffer);
			safe_unpack16(&(jobinfo->start_loc[i]), buffer);
		}

		safe_unpack16(&(jobinfo->reboot), buffer);
		safe_unpack16(&(jobinfo->rotate), buffer);

		safe_unpack32(&(jobinfo->block_cnode_cnt), buffer);
		safe_unpack16(&(jobinfo->cleaning), buffer);
		safe_unpack32(&(jobinfo->cnode_cnt), buffer);

		safe_unpackstr_xmalloc(&(jobinfo->bg_block_id), &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&(jobinfo->mp_str), &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&(jobinfo->ionode_str), &uint32_tmp,
				       buffer);

		safe_unpackstr_xmalloc(&(jobinfo->blrtsimage),
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&(jobinfo->linuximage), &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&(jobinfo->mloaderimage), &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&(jobinfo->ramdiskimage), &uint32_tmp,
				       buffer);
		safe_unpack16(&mp_cnode_cnt, buffer);
		safe_unpackstr_xmalloc(&bit_char, &uint32_tmp, buffer);
		if (bit_char) {
			jobinfo->units_avail = bit_alloc(mp_cnode_cnt);
			bit_unfmt(jobinfo->units_avail, bit_char);
			xfree(bit_char);
		}
		safe_unpackstr_xmalloc(&bit_char, &uint32_tmp, buffer);
		if (bit_char) {
			jobinfo->units_used = bit_alloc(mp_cnode_cnt);
			bit_unfmt(jobinfo->units_used, bit_char);
			xfree(bit_char);
		}
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		safe_unpack16(&jobinfo->dim_cnt, buffer);

		xassert(jobinfo->dim_cnt);
		dims = jobinfo->dim_cnt;

		for (i=0; i<dims; i++) {
			safe_unpack16(&(jobinfo->geometry[i]), buffer);
			safe_unpack16(&(jobinfo->conn_type[i]), buffer);
			safe_unpack16(&(jobinfo->start_loc[i]), buffer);
		}

		safe_unpack16(&(jobinfo->reboot), buffer);
		safe_unpack16(&(jobinfo->rotate), buffer);

		safe_unpack32(&(jobinfo->block_cnode_cnt), buffer);
		safe_unpack32(&(jobinfo->cnode_cnt), buffer);

		safe_unpackstr_xmalloc(&(jobinfo->bg_block_id), &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&(jobinfo->mp_str), &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&(jobinfo->ionode_str), &uint32_tmp,
				       buffer);

		safe_unpackstr_xmalloc(&(jobinfo->blrtsimage),
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&(jobinfo->linuximage), &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&(jobinfo->mloaderimage), &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&(jobinfo->ramdiskimage), &uint32_tmp,
				       buffer);
		safe_unpack16(&mp_cnode_cnt, buffer);
		safe_unpackstr_xmalloc(&bit_char, &uint32_tmp, buffer);
		if (bit_char) {
			jobinfo->units_avail = bit_alloc(mp_cnode_cnt);
			bit_unfmt(jobinfo->units_avail, bit_char);
			xfree(bit_char);
		}
		safe_unpackstr_xmalloc(&bit_char, &uint32_tmp, buffer);
		if (bit_char) {
			jobinfo->units_used = bit_alloc(mp_cnode_cnt);
			bit_unfmt(jobinfo->units_used, bit_char);
			xfree(bit_char);
		}
	} else {
 		error("unpack_select_jobinfo: protocol_version "
 		      "%hu not supported", protocol_version);
	}
	return SLURM_SUCCESS;

unpack_error:
	free_select_jobinfo(jobinfo);
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
	char *geo = NULL;
	int i;
	char *tmp_image = "default";
	char *header = "CONNECT REBOOT ROTATE GEOMETRY BLOCK_ID";
	bool print_x = 1;
	char *conn_type = NULL;

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

		snprintf(buf, size, "%s", header);
		return buf;
	}

	if (mode == SELECT_PRINT_GEOMETRY)
		print_x = 0;

	if (jobinfo->geometry[0] == (uint16_t) NO_VAL) {
		for (i=0; i<jobinfo->dim_cnt; i++) {
			if (geo && print_x)
				xstrcat(geo, "x0");
			else
				xstrcat(geo, "0");
		}
	} else if (mode != SELECT_PRINT_START_LOC) {
		geo = give_geo(jobinfo->geometry, jobinfo->dim_cnt, print_x);
		conn_type = conn_type_string_full(jobinfo->conn_type);
	}

	switch (mode) {
	case SELECT_PRINT_HEAD:
		snprintf(buf, size, "%s", header);
		break;
	case SELECT_PRINT_DATA:
		snprintf(buf, size,
			 "%7.7s %6.6s %6.6s    %s %-16s",
			 conn_type,
			 _yes_no_string(jobinfo->reboot),
			 _yes_no_string(jobinfo->rotate),
			 geo,
			 jobinfo->bg_block_id);
		break;
	case SELECT_PRINT_MIXED_SHORT:
		snprintf(buf, size,
			 "Connection=%s Reboot=%s Rotate=%s "
			 "Geometry=%s",
			 conn_type,
			 _yes_no_string(jobinfo->reboot),
			 _yes_no_string(jobinfo->rotate),
			 geo);
		break;
	case SELECT_PRINT_MIXED:
		snprintf(buf, size,
			 "Connection=%s Reboot=%s Rotate=%s "
			 "Geometry=%s Block_ID=%s",
			 conn_type,
			 _yes_no_string(jobinfo->reboot),
			 _yes_no_string(jobinfo->rotate),
			 geo,
			 jobinfo->bg_block_id);
		break;
	case SELECT_PRINT_BG_ID:
		snprintf(buf, size, "%s", jobinfo->bg_block_id);
		break;
	case SELECT_PRINT_NODES:
		if (jobinfo->ionode_str && jobinfo->ionode_str[0])
			snprintf(buf, size, "%s[%s]",
				 jobinfo->mp_str, jobinfo->ionode_str);
		else
			snprintf(buf, size, "%s", jobinfo->mp_str);
		break;
	case SELECT_PRINT_CONNECTION:
		snprintf(buf, size, "%s", conn_type);
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
		snprintf(buf, size, "%s", geo);
		break;
	case SELECT_PRINT_BLRTS_IMAGE:
		if (jobinfo->blrtsimage)
			tmp_image = jobinfo->blrtsimage;
		snprintf(buf, size, "%s", tmp_image);
		break;
	case SELECT_PRINT_LINUX_IMAGE:
		if (jobinfo->linuximage)
			tmp_image = jobinfo->linuximage;
		snprintf(buf, size, "%s", tmp_image);
		break;
	case SELECT_PRINT_MLOADER_IMAGE:
		if (jobinfo->mloaderimage)
			tmp_image = jobinfo->mloaderimage;
		snprintf(buf, size, "%s", tmp_image);
		break;
	case SELECT_PRINT_RAMDISK_IMAGE:
		if (jobinfo->ramdiskimage)
			tmp_image = jobinfo->ramdiskimage;
		snprintf(buf, size, "%s", tmp_image);
		break;
	case SELECT_PRINT_START_LOC:
		xfree(geo);
		geo = give_geo(jobinfo->start_loc, jobinfo->dim_cnt, 0);
		snprintf(buf, size, "%s", geo);
		break;
	default:
		error("sprint_jobinfo: bad mode %d", mode);
		if (size > 0)
			buf[0] = '\0';
	}
	xfree(geo);
	xfree(conn_type);
	return buf;
}

/* write select job info to a string
 * IN jobinfo - a select job credential
 * IN mode    - print mode, see enum select_print_mode
 * RET        - char * containing string of request
 */
extern char *xstrdup_select_jobinfo(select_jobinfo_t *jobinfo, int mode)
{
	char *geo = NULL;
	int i;
	char *tmp_image = "default";
	char *buf = NULL;
	char *header = "CONNECT REBOOT ROTATE GEOMETRY BLOCK_ID";
	bool print_x = 1;
	char *conn_type = NULL;

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
		xstrcat(buf, header);
		return buf;
	}

	if (mode == SELECT_PRINT_GEOMETRY)
		print_x = 0;

	if (jobinfo->geometry[0] == (uint16_t) NO_VAL) {
		for (i=0; i<SYSTEM_DIMENSIONS; i++) {
			if (geo && print_x)
				xstrcat(geo, "x0");
			else
				xstrcat(geo, "0");
		}
	} else if (mode != SELECT_PRINT_START_LOC) {
		geo = give_geo(jobinfo->geometry, jobinfo->dim_cnt, print_x);
		conn_type = conn_type_string_full(jobinfo->conn_type);
	}
	switch (mode) {
	case SELECT_PRINT_HEAD:
		xstrcat(buf, header);
		break;
	case SELECT_PRINT_DATA:
		xstrfmtcat(buf,
			   "%7.7s %6.6s %6.6s    %s %-16s",
			   conn_type,
			   _yes_no_string(jobinfo->reboot),
			   _yes_no_string(jobinfo->rotate),
			   geo,
			   jobinfo->bg_block_id);
		break;
	case SELECT_PRINT_MIXED:
		xstrfmtcat(buf,
			   "Connection=%s Reboot=%s Rotate=%s "
			   "Geometry=%s Block_ID=%s",
			   conn_type,
			   _yes_no_string(jobinfo->reboot),
			   _yes_no_string(jobinfo->rotate),
			   geo,
			   jobinfo->bg_block_id);
		break;
	case SELECT_PRINT_BG_ID:
		xstrfmtcat(buf, "%s", jobinfo->bg_block_id);
		break;
	case SELECT_PRINT_NODES:
		if (jobinfo->ionode_str && jobinfo->ionode_str[0])
			xstrfmtcat(buf, "%s[%s]",
				   jobinfo->mp_str, jobinfo->ionode_str);
		else
			xstrfmtcat(buf, "%s", jobinfo->mp_str);
		break;
	case SELECT_PRINT_CONNECTION:
		xstrfmtcat(buf, "%s", conn_type);
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
		xstrfmtcat(buf, "%s", geo);
		break;
	case SELECT_PRINT_BLRTS_IMAGE:
		if (jobinfo->blrtsimage)
			tmp_image = jobinfo->blrtsimage;
		xstrfmtcat(buf, "%s", tmp_image);
		break;
	case SELECT_PRINT_LINUX_IMAGE:
		if (jobinfo->linuximage)
			tmp_image = jobinfo->linuximage;
		xstrfmtcat(buf, "%s", tmp_image);
		break;
	case SELECT_PRINT_MLOADER_IMAGE:
		if (jobinfo->mloaderimage)
			tmp_image = jobinfo->mloaderimage;
		xstrfmtcat(buf, "%s", tmp_image);
		break;
	case SELECT_PRINT_RAMDISK_IMAGE:
		if (jobinfo->ramdiskimage)
			tmp_image = jobinfo->ramdiskimage;
		xstrfmtcat(buf, "%s", tmp_image);
		break;
	case SELECT_PRINT_START_LOC:
		xfree(geo);
		geo = give_geo(jobinfo->start_loc, jobinfo->dim_cnt, 0);
		xstrfmtcat(buf, "%s", geo);
		break;
	default:
		error("xstrdup_jobinfo: bad mode %d", mode);
	}
	xfree(geo);
	xfree(conn_type);
	return buf;
}
