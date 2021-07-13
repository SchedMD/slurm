/*****************************************************************************\
 *  test12.11.prog.c - Simple test program for Slurm regression test12.11.
 *  Usage: test12.2.prog <megabytes> <sleep_secs>
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC
 *  Written by Alejandro Sanchez <alex@schedmd.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
	void *m = NULL;
	int megabytes = 0;
	unsigned int seconds = 0;
	size_t bytes = 0;

	if (argc != 3) {
		fprintf(stderr, "Usage: %s <megabytes> <sleep_secs>\n",
			argv[0]);
		exit(1);
	}

	megabytes = atoi(argv[1]);
	seconds = atoi(argv[2]);
	bytes = megabytes * 1024 * 1024;

	m = malloc(bytes);

	if (!m) {
		fprintf(stderr, "malloc: %m");
		exit(1);
	}

	memset(m, 0, bytes);
	sleep(seconds);
	free(m);

	return 0;
}
