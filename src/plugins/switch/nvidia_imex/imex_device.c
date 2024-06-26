/*****************************************************************************\
 *  imex_device.c
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

#define _GNU_SOURCE

#include <sched.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#include "src/common/slurm_xlator.h"

#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define IMEX_DEV_DIR "/dev/nvidia-caps-imex-channels"
#define TARGET_DEV_LINE "nvidia-caps-imex-channels"
#define IMEX_CHANNEL_PATTERN IMEX_DEV_DIR "/channel%u"

static int device_major = -1;

static int _find_major(void)
{
	char *line = NULL;
	FILE *fp = NULL;
	size_t len = 0;

	if ((line = conf_get_opt_str(slurm_conf.switch_param,
				     "imex_dev_major="))) {
		device_major = atoi(line);
		info("using configured imex_dev_major: %d", device_major);
		return SLURM_SUCCESS;
	}

	if (!(fp = fopen("/proc/devices", "r"))) {
		error("Could not open /proc/devices: %m");
		return SLURM_ERROR;
	}

	while (getline(&line, &len, fp) != -1) {
		int tmp = 0;
		char tmp_char[41] = {0};

		if ((sscanf(line, "%d %40s", &tmp, tmp_char) == 2) &&
		    !xstrcmp(tmp_char, TARGET_DEV_LINE)) {
			device_major = tmp;
			break;
		}
	}

	free(line);
	fclose(fp);

	if (device_major == -1)
		warning("%s: nvidia-caps-imex-channels major device not found, plugin disabled",
			plugin_type);
	else
		info("nvidia-caps-imex-channels major: %d", device_major);

	return SLURM_SUCCESS;
}

static int _make_devdir(void)
{
	mode_t mask;

	mask = umask(0);
	if ((mkdir(IMEX_DEV_DIR, 0755) < 0) && (errno != EEXIST)) {
		error("could not create %s: %m", IMEX_DEV_DIR);
		return SLURM_ERROR;
	}
	umask(mask);

	(void) rmdir_recursive(IMEX_DEV_DIR, false);

	return SLURM_SUCCESS;
}

extern int slurmd_init(void)
{
	if (_find_major() != SLURM_SUCCESS)
		return SLURM_ERROR;

	if (device_major == -1)
		return SLURM_SUCCESS;

	if (_make_devdir() != SLURM_SUCCESS)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

extern int stepd_init(void)
{
	if (_find_major() != SLURM_SUCCESS)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

extern int setup_imex_channel(uint32_t channel, bool create_ns)
{
	int rc = SLURM_SUCCESS;
	mode_t mask;
	dev_t dev = makedev(device_major, channel);
	char *path = NULL;

	if (device_major == -1) {
		debug("skipping setup for channel %u", channel);
		return SLURM_SUCCESS;
	}

	if (create_ns && unshare(CLONE_NEWNS) < 0) {
		error("%s: unshare() failed: %m", __func__);
		return SLURM_ERROR;
	}

	if (mount(NULL, "/", NULL, MS_SLAVE | MS_REC, NULL) < 0) {
		error("%s: mount() for / failed: %m", __func__);
		return SLURM_ERROR;
	}

	if (mount("tmpfs", IMEX_DEV_DIR, "tmpfs", MS_NOSUID | MS_NOEXEC,
		  "size=0,mode=0755") < 0) {
		error("%s: mount() for tmpfs failed: %m", __func__);
		return SLURM_ERROR;
	}

	xstrfmtcat(path, IMEX_CHANNEL_PATTERN, channel);
	mask = umask(0);
	if (mknod(path, S_IFCHR | 0666, dev) < 0) {
		error("%s: failed to create %s: %m", __func__, path);
		rc = SLURM_ERROR;
	}
	umask(mask);
	xfree(path);

	return rc;
}
