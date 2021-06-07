/*****************************************************************************\
 *  cgroup_common.c - Cgroup plugin common functions
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC
 *  Written by Felip Moll <felip.moll@schedmd.com>
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

#include "cgroup_common.h"

extern size_t cgroup_file_getsize(int fd)
{
	int rc;
	size_t fsize;
	off_t offset;
	char c;

	/* store current position and rewind */
	offset = lseek(fd, 0, SEEK_CUR);
	if (offset < 0)
		return -1;
	if (lseek(fd, 0, SEEK_SET) < 0)
		error("%s: lseek(0): %m", __func__);

	/* get file size */
	fsize = 0;
	do {
		rc = read(fd, (void*)&c, 1);
		if (rc > 0)
			fsize++;
	} while ((rc < 0 && errno == EINTR) || rc > 0);

	/* restore position */
	if (lseek(fd, offset, SEEK_SET) < 0)
		error("%s: lseek(): %m", __func__);

	if (rc < 0)
		return -1;
	else
		return fsize;
}

extern int cgroup_file_write_uint64s(char* file_path, uint64_t* values, int nb)
{
	int fstatus;
	int rc;
	int fd;
	char tstr[256];
	uint64_t value;
	int i;

	/* open file for writing */
	fd = open(file_path, O_WRONLY, 0700);
	if (fd < 0) {
		debug2("%s: unable to open '%s' for writing : %m",
			__func__, file_path);
		return SLURM_ERROR;
	}

	/* add one value per line */
	fstatus = SLURM_SUCCESS;
	for (i = 0; i < nb ; i++) {

		value = values[i];

		rc = snprintf(tstr, sizeof(tstr), "%"PRIu64"", value);
		if (rc < 0) {
			debug2("unable to build %"PRIu64" string value, "
			       "skipping", value);
			fstatus = SLURM_ERROR;
			continue;
		}

		do {
			rc = write(fd, tstr, strlen(tstr)+1);
		}
		while (rc < 0 && errno == EINTR);
		if (rc < 1) {
			debug2("%s: unable to add value '%s' to file '%s' : %m",
				__func__, tstr, file_path);
			if (errno != ESRCH)
				fstatus = SLURM_ERROR;
		}

	}

	/* close file */
	close(fd);

	return fstatus;
}

extern int cgroup_file_read_uint64s(char* file_path, uint64_t** pvalues,
				    int* pnb)
{
	int rc;
	int fd;

	size_t fsize;
	char* buf;
	char* p;

	uint64_t* pa=NULL;
	int i;

	/* check input pointers */
	if (pvalues == NULL || pnb == NULL)
		return SLURM_ERROR;

	/* open file for reading */
	fd = open(file_path, O_RDONLY, 0700);
	if (fd < 0) {
		debug2("%s: unable to open '%s' for reading : %m",
			__func__, file_path);
		return SLURM_ERROR;
	}

	/* get file size */
	fsize=cgroup_file_getsize(fd);
	if (fsize == -1) {
		close(fd);
		return SLURM_ERROR;
	}

	/* read file contents */
	buf = xmalloc(fsize + 1);
	do {
		rc = read(fd, buf, fsize);
	} while (rc < 0 && errno == EINTR);
	close(fd);
	buf[fsize]='\0';

	/* count values (splitted by \n) */
	i=0;
	if (rc > 0) {
		p = buf;
		while (xstrchr(p, '\n') != NULL) {
			i++;
			p = xstrchr(p, '\n') + 1;
		}
	}

	/* build uint64_t list */
	if (i > 0) {
		pa = (uint64_t*) xmalloc(sizeof(uint64_t) * i);
		p = buf;
		i = 0;
		while (xstrchr(p, '\n') != NULL) {
			long long unsigned int ll_tmp;
			sscanf(p, "%llu", &ll_tmp);
			pa[i++] = ll_tmp;
			p = xstrchr(p, '\n') + 1;
		}
	}

	/* free buffer */
	xfree(buf);

	/* set output values */
	*pvalues = pa;
	*pnb = i;

	return SLURM_SUCCESS;
}

extern int cgroup_file_write_uint32s(char* file_path, uint32_t* values, int nb)
{
	int rc;
	int fd;
	char tstr[256];

	/* open file for writing */
	if ((fd = open(file_path, O_WRONLY, 0700)) < 0) {
		error("%s: unable to open '%s' for writing: %m",
			__func__, file_path);
		return SLURM_ERROR;
	}

	/* add one value per line */
	for (int i = 0; i < nb; i++) {
		uint32_t value = values[i];

		if (snprintf(tstr, sizeof(tstr), "%u", value) < 0)
			fatal("%s: unable to build %u string value",
			      __func__, value);

		/* write terminating NUL byte */
		safe_write(fd, tstr, strlen(tstr) + 1);
	}

	/* close file */
	close(fd);
	return SLURM_SUCCESS;

rwfail:
	rc = errno;
	error("%s: write pid %s to %s failed: %m",
	      __func__, tstr, file_path);
	close(fd);
	return rc;;
}

extern int cgroup_file_read_uint32s(char* file_path, uint32_t** pvalues,
				    int* pnb)
{
	int rc;
	int fd;

	size_t fsize;
	char* buf;
	char* p;

	uint32_t* pa=NULL;
	int i;

	/* check input pointers */
	if (pvalues == NULL || pnb == NULL)
		return SLURM_ERROR;

	/* open file for reading */
	fd = open(file_path, O_RDONLY, 0700);
	if (fd < 0) {
		debug2("%s: unable to open '%s' for reading : %m",
			__func__, file_path);
		return SLURM_ERROR;
	}

	/* get file size */
	fsize =cgroup_file_getsize(fd);
	if (fsize == -1) {
		close(fd);
		return SLURM_ERROR;
	}

	/* read file contents */
	buf = xmalloc(fsize + 1);
	do {
		rc = read(fd, buf, fsize);
	} while (rc < 0 && errno == EINTR);
	close(fd);
	buf[fsize]='\0';

	/* count values (splitted by \n) */
	i=0;
	if (rc > 0) {
		p = buf;
		while (xstrchr(p, '\n') != NULL) {
			i++;
			p = xstrchr(p, '\n') + 1;
		}
	}

	/* build uint32_t list */
	if (i > 0) {
		pa = (uint32_t*) xmalloc(sizeof(uint32_t) * i);
		p = buf;
		i = 0;
		while (xstrchr(p, '\n') != NULL) {
			sscanf(p, "%u", pa+i);
			p = xstrchr(p, '\n') + 1;
			i++;
		}
	}

	/* free buffer */
	xfree(buf);

	/* set output values */
	*pvalues = pa;
	*pnb = i;

	return SLURM_SUCCESS;
}

extern int cgroup_file_write_content(char* file_path, char* content,
				     size_t csize)
{
	int fd;

	/* open file for writing */
	if ((fd = open(file_path, O_WRONLY, 0700)) < 0) {
		error("%s: unable to open '%s' for writing: %m",
			__func__, file_path);
		return SLURM_ERROR;
	}

	safe_write(fd, content, csize);

	/* close file */
	close(fd);
	return SLURM_SUCCESS;

rwfail:
	error("%s: unable to write %zu bytes to cgroup %s: %m",
	      __func__, csize, file_path);
	close(fd);
	return SLURM_ERROR;
}

extern int cgroup_file_read_content(char* file_path, char** content,
				    size_t *csize)
{
	int fstatus;
	int rc;
	int fd;
	size_t fsize;
	char* buf;

	fstatus = SLURM_ERROR;

	/* check input pointers */
	if (content == NULL || csize == NULL)
		return fstatus;

	/* open file for reading */
	fd = open(file_path, O_RDONLY, 0700);
	if (fd < 0) {
		debug2("%s: unable to open '%s' for reading : %m",
			__func__, file_path);
		return fstatus;
	}

	/* get file size */
	fsize=cgroup_file_getsize(fd);
	if (fsize == -1) {
		close(fd);
		return fstatus;
	}

	/* read file contents */
	buf = xmalloc(fsize + 1);
	buf[fsize]='\0';
	do {
		rc = read(fd, buf, fsize);
	} while (rc < 0 && errno == EINTR);

	/* set output values */
	if (rc >= 0) {
		*content = buf;
		*csize = rc;
		fstatus = SLURM_SUCCESS;
	} else {
		xfree(buf);
	}

	/* close file */
	close(fd);

	return fstatus;
}
