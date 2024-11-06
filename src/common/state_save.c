/*****************************************************************************\
 *  state_save.c - common state save and load handling
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#include <pthread.h>

#include "src/common/fd.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/state_save.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

strong_alias(lock_state_files, slurm_lock_state_files);
strong_alias(unlock_state_files, slurm_unlock_state_files);
strong_alias(save_buf_to_state, slurm_save_buf_to_state);

static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;

extern void lock_state_files(void)
{
	slurm_mutex_lock(&state_mutex);
}

extern void unlock_state_files(void)
{
	slurm_mutex_unlock(&state_mutex);
}

static int _write_file(int fd, buf_t *buf)
{
	safe_write(fd, get_buf_data(buf), get_buf_offset(buf));
	return SLURM_SUCCESS;

rwfail:
	return errno ? errno : SLURM_ERROR;
}

extern int save_buf_to_state(const char *target_file, buf_t *buf,
			     uint32_t *high_buffer_size)
{
	int rc = 0, fd = -1;
	char *new_file, *old_file, *reg_file;
	char *state_location = slurm_conf.state_save_location;

	new_file = xstrdup_printf("%s/%s.new", state_location, target_file);
	old_file = xstrdup_printf("%s/%s.old", state_location, target_file);
	reg_file = xstrdup_printf("%s/%s", state_location, target_file);

	lock_state_files();
	fd = open(new_file, O_CREAT|O_WRONLY|O_TRUNC|O_CLOEXEC, 0600);
	if (fd < 0) {
		rc = errno ? errno : SLURM_ERROR;
		error("Can't save state, error creating file %s: %m",
		      new_file);
		goto fail;
	}

	if ((rc = _write_file(fd, buf)) != SLURM_SUCCESS) {
		error("Can't save state, error writing file %s: %m",
		      new_file);
		(void) close(fd);
		goto fail;
	}

	/* provides own logging on error */
	if ((rc = fsync_and_close(fd, new_file)) < 0)
		goto fail;

	(void) unlink(old_file);
	if (link(reg_file, old_file))
		debug2("unable to create link for %s -> %s: %m",
		       reg_file, old_file);

	(void) unlink(reg_file);
	if (link(new_file, reg_file))
		debug2("unable to create link for %s -> %s: %m",
		       new_file, reg_file);

	if (high_buffer_size)
		*high_buffer_size = MAX(get_buf_offset(buf), *high_buffer_size);

fail:
	(void) unlink(new_file);

	unlock_state_files();

	xfree(old_file);
	xfree(reg_file);
	xfree(new_file);

	return rc;
}
