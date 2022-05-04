/*****************************************************************************\
 *  file_bcast.c - File transfer agent (handles message traffic)
 *****************************************************************************
 *  Copyright (C) 2015-2016 SchedMD LLC.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if HAVE_LZ4
# include <lz4.h>
#endif

#include "slurm/slurm_errno.h"
#include "src/common/forward.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/run_command.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/slurm_time.h"
#include "src/common/timers.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "file_bcast.h"

/*
 * This should likely be detected at build time, but I have not
 * seen any common systems where this is not the correct path.
 */
#define LDD_PATH "/usr/bin/ldd"
#define MAX_THREADS      8	/* These can be huge messages, so
				 * only run MAX_THREADS at one time */

int block_len;				/* block size */
int fd;					/* source file descriptor */
void *src;				/* source mmap'd address */
struct stat f_stat;			/* source file stats */
job_sbcast_cred_msg_t *sbcast_cred;	/* job alloc info and sbcast cred */

static int   _bcast_file(struct bcast_parameters *params);
static int   _file_bcast(struct bcast_parameters *params,
			 file_bcast_msg_t *bcast_msg,
			 job_sbcast_cred_msg_t *sbcast_cred);
static int   _file_state(struct bcast_parameters *params);
static List _fill_in_excluded_paths(struct bcast_parameters *params);
static int _find_subpath(void *x, void *key);
static int _foreach_shared_object(void *x, void *y);
static int   _get_job_info(struct bcast_parameters *params);
static int _get_lib_paths(char *filename, List lib_paths);

static int _file_state(struct bcast_parameters *params)
{
	/* validate the source file */
	if ((fd = open(params->src_fname, O_RDONLY)) < 0) {
		error("Can't open `%s`: %s", params->src_fname,
			strerror(errno));
		return SLURM_ERROR;
	}
	if (fstat(fd, &f_stat)) {
		error("Can't stat `%s`: %s", params->src_fname,
			strerror(errno));
		return SLURM_ERROR;
	}

	verbose("modes    = %o", (unsigned int) f_stat.st_mode);
	verbose("uid      = %d", (int) f_stat.st_uid);
	verbose("gid      = %d", (int) f_stat.st_gid);
	verbose("atime    = %s", slurm_ctime2(&f_stat.st_atime));
	verbose("mtime    = %s", slurm_ctime2(&f_stat.st_mtime));
	verbose("ctime    = %s", slurm_ctime2(&f_stat.st_ctime));
	verbose("size     = %ld", (long) f_stat.st_size);

	if (!f_stat.st_size) {
		error("Warning: file `%s` is empty.", params->src_fname);
		return SLURM_SUCCESS;
	}
	src = mmap(NULL, f_stat.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (src == MAP_FAILED) {
		error("Can't mmap file `%s`, %m.", params->src_fname);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

/* get details about this slurm job: jobid and allocated node */
static int _get_job_info(struct bcast_parameters *params)
{
	int rc;
	char job_id_str[64];

	xassert(params->selected_step);

	slurm_get_selected_step_id(job_id_str, sizeof(job_id_str),
				   params->selected_step);

	rc = slurm_sbcast_lookup(params->selected_step, &sbcast_cred);
	if (rc != SLURM_SUCCESS) {
		error("Slurm job %s lookup error: %s",
		      job_id_str, slurm_strerror(slurm_get_errno()));
		return rc;
	}
	verbose("jobid      = %s", job_id_str);
	verbose("node_list  = %s", sbcast_cred->node_list);

	if (params->verbose)
		print_sbcast_cred(sbcast_cred->sbcast_cred);

	/* do not bother to release the return message,
	 * we need to preserve and use most of the information later */

	return rc;
}

/* Issue the RPC to transfer the file's data */
static int _file_bcast(struct bcast_parameters *params,
		       file_bcast_msg_t *bcast_msg,
		       job_sbcast_cred_msg_t *sbcast_cred)
{
	List ret_list = NULL;
	ListIterator itr;
	ret_data_info_t *ret_data_info = NULL;
	int rc = SLURM_SUCCESS, msg_rc;
	slurm_msg_t msg;

	slurm_msg_t_init(&msg);
	slurm_msg_set_r_uid(&msg, SLURM_AUTH_UID_ANY);
	msg.data = bcast_msg;
	msg.flags = USE_BCAST_NETWORK;
	msg.forward.tree_width = params->fanout;
	msg.msg_type = REQUEST_FILE_BCAST;

	ret_list = slurm_send_recv_msgs(sbcast_cred->node_list, &msg,
					params->timeout);
	if (ret_list == NULL) {
		error("slurm_send_recv_msgs: %m");
		exit(1);
	}

	itr = list_iterator_create(ret_list);
	while ((ret_data_info = list_next(itr))) {
		msg_rc = slurm_get_return_code(ret_data_info->type,
					       ret_data_info->data);
		if (msg_rc == SLURM_SUCCESS)
			continue;

		error("REQUEST_FILE_BCAST(%s): %s",
		      ret_data_info->node_name,
		      slurm_strerror(msg_rc));
		rc = msg_rc;
	}
	list_iterator_destroy(itr);
	FREE_NULL_LIST(ret_list);

	return rc;
}

/* load a buffer with data from the file to broadcast,
 * return number of bytes read, zero on end of file */
static int _get_block_none(char **buffer, int *orig_len, bool *more,
			   bool file_start)
{
	static int64_t remaining = -1;
	static void *position;
	int size;

	if (file_start) {
		remaining = -1;
		position = NULL;
	}

	if (remaining < 0) {
		*buffer = xmalloc(block_len);
		remaining = f_stat.st_size;
		position = src;
	}

	size = MIN(block_len, remaining);
	memcpy(*buffer, position, size);
	remaining -= size;
	position += size;

	*orig_len = size;
	*more = (remaining) ? true : false;
	return size;
}

static int _get_block_lz4(struct bcast_parameters *params,
			  char **buffer,
			  int32_t *orig_len,
			  bool *more, bool file_start)
{
#if HAVE_LZ4
	int size_out;
	static int64_t remaining = -1;
	static void *position;
	int size;

	if (file_start) {
		remaining = -1;
		position = NULL;
	}

	if (!f_stat.st_size) {
		*more = false;
		return 0;
	}

	if (remaining < 0) {
		position = src;
		remaining = f_stat.st_size;
		*buffer = xmalloc(block_len);
	}

	/* intentionally limit decompressed size to 10x compressed
	 * to avoid problems on receive size when decompressed */
	size = MIN(block_len * 10, remaining);
	if (!(size_out = LZ4_compress_destSize(position, *buffer,
					       &size, block_len))) {
		/* compression failure */
		fatal("LZ4 compression error");
	}
	position += size;
	remaining -= size;

	*orig_len = size;
	*more = (remaining) ? true : false;
	return size_out;
#else
	info("lz4 compression not supported, sending uncompressed file.");
	params->compress = 0;
	return _get_block_none(buffer, orig_len, more, file_start);
#endif

}

static int _next_block(struct bcast_parameters *params,
		       char **buffer,
		       int32_t *orig_len,
		       bool *more, bool file_start)
{
	switch (params->compress) {
	case COMPRESS_OFF:
		return _get_block_none(buffer, orig_len, more, file_start);
	case COMPRESS_LZ4:
		return _get_block_lz4(params, buffer, orig_len, more,
				      file_start);
	}

	/* compression type not recognized */
	error("File compression type %u not supported,"
	      " sending uncompressed file.", params->compress);
	params->compress = 0;
	return _get_block_none(buffer, orig_len, more, file_start);
}

/* read and broadcast the file */
static int _bcast_file(struct bcast_parameters *params)
{
	int rc = SLURM_SUCCESS;
	file_bcast_msg_t bcast_msg;
	char *buffer = NULL;
	int32_t orig_len = 0;
	uint64_t size_uncompressed = 0, size_compressed = 0;
	uint32_t time_compression = 0;
	bool more = true, file_start = true;
	DEF_TIMERS;

	if (params->block_size)
		block_len = MIN(params->block_size, f_stat.st_size);
	else
		block_len = MIN((512 * 1024), f_stat.st_size);

	memset(&bcast_msg, 0, sizeof(file_bcast_msg_t));
	bcast_msg.fname		= params->dst_fname;
	bcast_msg.block_no	= 1;
	if (params->flags & BCAST_FLAG_FORCE)
		bcast_msg.flags |= FILE_BCAST_FORCE;
	if (params->flags & BCAST_FLAG_SHARED_OBJECT)
		bcast_msg.flags |= FILE_BCAST_SO;
	else if (params->flags & BCAST_FLAG_SEND_LIBS)
		bcast_msg.flags |= FILE_BCAST_EXE;
	bcast_msg.modes		= f_stat.st_mode;
	bcast_msg.uid		= f_stat.st_uid;
	bcast_msg.user_name	= uid_to_string(f_stat.st_uid);
	bcast_msg.gid		= f_stat.st_gid;
	bcast_msg.file_size	= f_stat.st_size;
	bcast_msg.cred          = sbcast_cred->sbcast_cred;

	if (params->flags & BCAST_FLAG_PRESERVE) {
		bcast_msg.atime     = f_stat.st_atime;
		bcast_msg.mtime     = f_stat.st_mtime;
	}

	if (!params->fanout)
		params->fanout = MAX_THREADS;
	else
		params->fanout = MIN(MAX_THREADS, params->fanout);

	while (more) {
		START_TIMER;
		bcast_msg.block_len = _next_block(params, &buffer, &orig_len,
						  &more, file_start);
		END_TIMER;
		file_start = false;
		time_compression += DELTA_TIMER;
		size_uncompressed += orig_len;
		size_compressed += bcast_msg.block_len;
		debug("block %u, size %u", bcast_msg.block_no,
		      bcast_msg.block_len);
		bcast_msg.compress = params->compress;
		bcast_msg.uncomp_len = orig_len;
		bcast_msg.block = buffer;
		if (!more)
			bcast_msg.flags |= FILE_BCAST_LAST_BLOCK;

		rc = _file_bcast(params, &bcast_msg, sbcast_cred);
		if (rc != SLURM_SUCCESS)
			break;
		if (bcast_msg.flags & FILE_BCAST_LAST_BLOCK)
			break;	/* end of file */
		bcast_msg.block_no++;
		bcast_msg.block_offset += orig_len;
	}
	xfree(bcast_msg.user_name);
	xfree(buffer);

	if (size_uncompressed && (params->compress != 0)) {
		int64_t pct = (int64_t) size_uncompressed - size_compressed;
		/* Dividing a negative by a positive in C99 results in
		 * "truncation towards zero" which gives unexpected values for
		 * pct. This construct avoids that problem.
		 */
		pct = (pct>=0) ? pct * 100 / size_uncompressed
			       : - (-pct * 100 / size_uncompressed);
		verbose("File compressed from %"PRIu64" to %"PRIu64" (%d percent) in %u usec",
			size_uncompressed, size_compressed, (int) pct,
			time_compression);
	}

	return rc;
}


static int _decompress_data_lz4(file_bcast_msg_t *req)
{
#if HAVE_LZ4
	char *out_buf;
	int out_len;

	if (!req->block_len)
		return 0;

	out_buf = xmalloc(req->uncomp_len);
	out_len = LZ4_decompress_safe(req->block, out_buf, req->block_len,
				      req->uncomp_len);
	xfree(req->block);
	req->block = out_buf;
	if (req->uncomp_len != out_len) {
		error("lz4 decompression error, original block length != decompressed length");
		return -1;
	}
	req->block_len = out_len;
	return 0;
#else
	return -1;
#endif
}

/*
 * IN: char pointer with the filename.
 * IN/OUT: List of shared object direct and indirect dependencies.
 *
 * RET:	SLURM_[SUCCESS|ERROR]
 */
static int _get_lib_paths(char *filename, List lib_paths)
{
	char **ldd_argv;
	char *result = NULL;
	char *lpath = NULL, *lpath_end = NULL;
	char *tok = NULL, *save_ptr = NULL;
	int status = SLURM_ERROR, rc = SLURM_SUCCESS;

	if (!filename || !lib_paths) {
		rc = SLURM_ERROR;
		goto fini;
	}

	ldd_argv = xcalloc(3, sizeof(char *));
	ldd_argv[0] = xstrdup("ldd");
	ldd_argv[1] = xstrdup(filename);
	/* Already zero'd out after xcalloc(), but make it NULL explicitly */
	ldd_argv[2] = NULL;

	/*
	 * NOTE: If using ldd ends up causing problems it is possible to
	 * leverage using other alternatives for ELF inspection like dlinfo(),
	 * libelf/gelf libraries or others. This would require recursing in
	 * search for non-direct dependencies and knowing where to find them by
	 * doing something similar to the search order of the dynamic linker.
	 */
	result = run_command("ldd", LDD_PATH, ldd_argv, NULL, 5000, 0, &status);
	free_command_argv(ldd_argv);

	if (status) {
		error("Cannot autodetect libraries for '%s' with ldd command",
		      filename);
		rc = SLURM_ERROR;
		goto fini;
	} else if (!result) {
		verbose("ldd exited normally but returned no libraries");
		rc = SLURM_SUCCESS;
		goto fini;
	}

	/*
	 * FIXME: does not handle spaces in library paths correctly.
	 * (Although libtool itself doesn't love that either.)
	 */
	tok = strtok_r(result, "\n", &save_ptr);
	while (tok) {
		if ((lpath = xstrstr(tok, "/"))) {
			if ((lpath_end = xstrstr(lpath, " "))) {
				*lpath_end = '\0';
				list_append(lib_paths, xstrdup(lpath));
			}
		}
		tok = strtok_r(NULL, "\n", &save_ptr);
	}

fini:
	xfree(result);
	return rc;
}

/*
 * ListFindF for excl_paths.
 *
 * IN:	x, list data with excluded path
 * IN:	key, shared object path to check
 *
 * RET: return of subpath()
 */
static int _find_subpath(void *x, void *key)
{
	char *exclude_path = x;
	char *so_path = key;

	return subpath(so_path, exclude_path);
}

static int _bcast_library(struct bcast_parameters *params)
{
	int rc;

	if ((rc = _file_state(params)) != SLURM_SUCCESS)
		return rc;
	if ((rc = _bcast_file(params)) != SLURM_SUCCESS)
		return rc;

	return rc;
}

/*
 * ListForF to attempt to bcast a shared object.
 *
 * IN:	x, list data
 * IN:	y, arguments
 * RET:	-1 on error, 0 on success
 */
static int _foreach_shared_object(void *x, void *y)
{
	foreach_shared_object_t *args = (foreach_shared_object_t *) y;
	char *library = (char *) x;

	if (list_find_first(args->excluded_paths, _find_subpath, library)) {
		verbose("Skipping broadcast of excluded '%s'", library);
		return 0;
	}

	args->params->src_fname = library;
	args->params->dst_fname = xbasename(library);

	args->return_code = _bcast_library(args->params);

	if (args->return_code != SLURM_SUCCESS) {
		error("Broadcast of '%s' failed", args->params->src_fname);
		return -1;
	}

	verbose("Broadcast of shared object '%s' to destination cache directory succeeded (%d/%d)",
		args->params->src_fname, ++args->bcast_sent_cnt,
		args->bcast_total_cnt);

	return 0;
}

/*
 * Validates params->exclude and fills in a List off it.
 *
 * IN: bcast_parameters
 *
 * RET: List of excluded paths.
 * NOTE: Caller should free the returned list.
 */
static List _fill_in_excluded_paths(struct bcast_parameters *params)
{
	char *tmp_str = NULL, *tok = NULL, *saveptr = NULL;
	List excl_paths = NULL;

	excl_paths = list_create(xfree_ptr);
	if (!xstrcasecmp(params->exclude, "none"))
		return excl_paths;

	tmp_str = xstrdup(params->exclude);
	tok = strtok_r(tmp_str, ",", &saveptr);
	while (tok) {
		if (tok[0] != '/')
			error("Ignoring non-absolute excluded path: '%s'",
			      tok);
		else
			list_append(excl_paths, xstrdup(tok));
		tok = strtok_r(NULL, ",", &saveptr);
	}

	xfree(tmp_str);
	return excl_paths;
}

/*
 * IN/OUT: bcast_parameters pointer
 *
 * RET: SLURM_[ERROR|SUCCESS]
 */
static int _bcast_shared_objects(struct bcast_parameters *params)
{
	foreach_shared_object_t args;
	int rc;
	List lib_paths = NULL, excl_paths = NULL;
	char *save_dst = params->dst_fname;
	char *save_src = params->src_fname;

	memset(&args, 0, sizeof(args));
	lib_paths = list_create(xfree_ptr);
	if ((rc = _get_lib_paths(params->src_fname, lib_paths)) !=
	    SLURM_SUCCESS)
		goto fini;

	if (!(args.bcast_total_cnt = list_count(lib_paths))) {
		verbose("No shared objects detected for '%s'",
			params->src_fname);
		goto fini;
	}

	params->flags |= BCAST_FLAG_SHARED_OBJECT;
	excl_paths = _fill_in_excluded_paths(params);
	args.params = params;
	args.excluded_paths = excl_paths;

	list_for_each(lib_paths, _foreach_shared_object, &args);
	rc = args.return_code;
	params->flags &= ~BCAST_FLAG_SHARED_OBJECT;
	params->dst_fname = save_dst;
	params->src_fname = save_src;

fini:
	FREE_NULL_LIST(excl_paths);
	FREE_NULL_LIST(lib_paths);
	return rc;
}

extern int bcast_file(struct bcast_parameters *params)
{
	int rc;

	if ((rc = _file_state(params)) != SLURM_SUCCESS)
		return rc;
	if ((rc = _get_job_info(params)) != SLURM_SUCCESS)
		return rc;
	if ((rc = _bcast_file(params)) != SLURM_SUCCESS)
		return rc;
	if ((params->flags & BCAST_FLAG_SEND_LIBS) &&
	    ((rc = _bcast_shared_objects(params)) != SLURM_SUCCESS))
		return rc;

/*	slurm_free_sbcast_cred_msg(sbcast_cred); */
	return rc;
}

extern int bcast_decompress_data(file_bcast_msg_t *req)
{
	switch (req->compress) {
	case COMPRESS_OFF:
		return 0;
	case COMPRESS_LZ4:
		return _decompress_data_lz4(req);
	}

	/* compression type not recognized */
	error("%s: compression type %u not supported.",
	      __func__, req->compress);
	return -1;
}
