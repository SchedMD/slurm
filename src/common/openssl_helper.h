/*****************************************************************************\
 *  openssl_helper.h
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

#ifndef _OPENSSL_HELPER_H
#define _OPENSSL_HELPER_H

/*
 * Disable OpenSSL atexit() cleanup, which tears down libcrypto global state.
 *
 * This is used to avoid running libcrypto's atexit() handler while another
 * thread is still using libcrypto, since Slurm often calls exit() without
 * joining all threads.
 *
 * A deliberate consequence is that libcrypto global state is never freed, and
 * may show up as leaked memory in valgrind.
 *
 * Call this from a plugin's init() before any libcrypto use (e.g. before
 * s2n_init()), so the atexit() handler is disabled before OpenSSL registers
 * it. It resolves the caller's shared object from plugin_addr and searches its
 * dependency tree for libcrypto.
 *
 * This is a no-op if the caller's object has no libcrypto dependency, or if
 * libcrypto was already initialized before this call.
 *
 * IN plugin_addr - address of any symbol in the calling plugin's shared object
 */
extern void openssl_helper_disable_atexit(const void *plugin_addr);

#endif
