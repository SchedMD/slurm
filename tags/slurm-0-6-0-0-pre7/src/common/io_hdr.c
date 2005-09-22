/*****************************************************************************\
 * src/common/io_hdr.c - IO connection header functions
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark A. Grondona <mgrondona@llnl.gov>.
 *  UCRL-CODE-2002-040.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "src/common/io_hdr.h"
#include "src/common/slurm_protocol_defs.h"

#define IO_HDR_VERSION 0xa001

/*
static void
_print_data(char *data, int datalen)
{
	char buf[1024];
	size_t len = 0;
	int i;

	for (i = 0; i < datalen; i += sizeof(char))
		len += sprintf(buf+len, "%02x", data[i]);

	info("data: %s", buf);
}
*/


static void
io_hdr_pack(io_hdr_t *hdr, Buf buffer)
{
	pack16(hdr->version, buffer);
	packmem((char *) hdr->key, (uint16_t) SLURM_IO_KEY_SIZE, buffer);
	pack32(hdr->taskid,  buffer);
	pack16(hdr->type,    buffer);
}

static int
io_hdr_unpack(io_hdr_t *hdr, Buf buffer)
{
	uint16_t val;

	safe_unpack16(&hdr->version, buffer);

	safe_unpackmem((char *) hdr->key, &val, buffer);

	if (val != SLURM_IO_KEY_SIZE)
		goto unpack_error;

	safe_unpack32(&hdr->taskid, buffer);
	safe_unpack16(&hdr->type, buffer);

	return SLURM_SUCCESS;

    unpack_error:
	return SLURM_ERROR;
}

int 
io_hdr_packed_size()
{
	return sizeof(uint32_t) + 3*sizeof(uint16_t) + SLURM_IO_KEY_SIZE;
}

int 
io_hdr_write_cb(cbuf_t cb, io_hdr_t *hdr)
{
	int retval = SLURM_SUCCESS;

	Buf buffer = init_buf(1024);
	hdr->version = IO_HDR_VERSION;

	io_hdr_pack(hdr, buffer);

	xassert(buffer->processed == io_hdr_packed_size());

	retval = cbuf_write(cb, buffer->head, buffer->processed, NULL);
	free_buf(buffer);

	return retval;
} 

int 
io_hdr_read_cb(cbuf_t cb, io_hdr_t *hdr)
{
	Buf buffer = init_buf(4096);
	int rc     = SLURM_SUCCESS;

	cbuf_read(cb, buffer->head, io_hdr_packed_size());
	
	rc = io_hdr_unpack(hdr, buffer);

	free_buf(buffer);
	return rc; 
}

int 
io_hdr_validate(io_hdr_t *hdr, const char *key, int len)
{
	
	if (hdr->version != IO_HDR_VERSION) {
		error("Invalid IO header version");
		return SLURM_ERROR;
	}

	if ((hdr->type != SLURM_IO_STDOUT) && (hdr->type != SLURM_IO_STDERR)) {
		error("Invalid IO header type: %d", hdr->type);
		return SLURM_ERROR;
	}

	len = len < SLURM_IO_KEY_SIZE ? len : SLURM_IO_KEY_SIZE;

	if (memcmp((void *) key, (void *) hdr->key, len)) {
		error("Invalid IO header signature");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

