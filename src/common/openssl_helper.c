/*****************************************************************************\
 *  openssl_helper.c
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include <dlfcn.h>
#include <pthread.h>

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/openssl_helper.h"

strong_alias(openssl_helper_disable_atexit,
	     slurm_openssl_helper_disable_atexit);

/*
 * This helper disables OpenSSL's atexit() so that OPENSSL_cleanup() is not
 * called while another thread is using OpenSSL, potentially causing a segfault.
 *
 * A deliberate consequence is that libcrypto global state is never freed, and
 * may show up as leaked memory in valgrind.
 */

/*
 * Defined locally to eliminate build dependency on <openssl/crypto.h>. Whether
 * atexit cleanup needs disabling is a property of the runtime libcrypto, not
 * the build host. The ABI has been stable since OpenSSL 1.1.0.
 */
#define SLURM_OPENSSL_INIT_NO_ATEXIT 0x00080000UL

/*
 * Multiple plugins call this from their own init(), potentially concurrently,
 * and may each resolve to a different libcrypto. Skipping a repeat of the last
 * handled OPENSSL_init_crypto() avoids redundant work in the common case where
 * they all resolve to the same one. This is only an optimization since repeated
 * calls for the same libcrypto are harmless.
 */
static pthread_mutex_t disable_atexit_mutex = PTHREAD_MUTEX_INITIALIZER;
static int (*disabled_init_crypto)(uint64_t opts, const void *settings) = NULL;

extern void openssl_helper_disable_atexit(const void *plugin_addr)
{
	Dl_info info = { 0 };
	void *handle = NULL;
	unsigned long (*version_num)(void) = NULL;
	int (*init_crypto)(uint64_t opts, const void *settings) = NULL;

	/*
	 * Resolve the caller's shared object from an address it owns, then get a
	 * handle to it. Plugins are dlopen()ed with RTLD_LOCAL, so their
	 * libcrypto dependency is not in the global scope and RTLD_DEFAULT can't
	 * see it. A handle to the caller's own object searches its dependency
	 * tree, where libcrypto is resolvable.
	 */
	if (!dladdr(plugin_addr, &info) || !info.dli_fname) {
		debug2("%s: dladdr() could not resolve the caller's object, skipping disable of OpenSSL atexit cleanup.",
		       __func__);
		return;
	}

	/*
	 * RTLD_NOLOAD returns a handle to the already-loaded object without
	 * reloading it, and increments its reference count, so dlclose() it
	 * below.
	 */
	handle = dlopen(info.dli_fname, RTLD_NOLOAD | RTLD_LAZY);
	if (!handle) {
		debug2("%s: dlopen(%s, RTLD_NOLOAD) failed, skipping disable of OpenSSL atexit cleanup: %s",
		       __func__, info.dli_fname, dlerror());
		return;
	}

	/*
	 * Only OpenSSL 1.1.0 - 3.x install the handler by default, so skip 4.0+
	 * and any libcrypto whose version can't be confirmed.
	 *
	 * This may also fail if the object does not depend on OpenSSL's
	 * libcrypto and thus dlsym() cannot resolve the symbol address. In that
	 * case there is no atexit() handler to disable, and this is an
	 * intentional no-op.
	 */
	(void) dlerror();
	version_num = dlsym(handle, "OpenSSL_version_num");
	if (!version_num) {
		debug2("%s: OpenSSL_version_num unavailable, skipping disable of OpenSSL atexit cleanup: %s",
		      __func__, dlerror());
		goto done;
	}

	/* Major version is the top nibble of the packed version number. */
	if (((version_num() >> 28) & 0xf) >= 4) {
		debug2("%s: OpenSSL >= 4.0 does not install atexit cleanup, skipping disable of OpenSSL atexit cleanup.",
		       __func__);
		goto done;
	}

	(void) dlerror();
	init_crypto = dlsym(handle, "OPENSSL_init_crypto");
	if (!init_crypto) {
		debug2("%s: OPENSSL_init_crypto unavailable, unable to disable OpenSSL atexit cleanup: %s",
		      __func__, dlerror());
		goto done;
	}

	slurm_mutex_lock(&disable_atexit_mutex);
	if (init_crypto == disabled_init_crypto) {
		debug2("%s: OpenSSL atexit cleanup already disabled for this libcrypto",
		       __func__);
	} else if (init_crypto(SLURM_OPENSSL_INIT_NO_ATEXIT, NULL) != 1) {
		debug2("%s: OPENSSL_init_crypto() failed, unable to disable OpenSSL atexit cleanup.", __func__);
	} else {
		disabled_init_crypto = init_crypto;
		debug2("%s: disabled OpenSSL atexit cleanup", __func__);
	}
	slurm_mutex_unlock(&disable_atexit_mutex);
done:
	dlclose(handle);
}
