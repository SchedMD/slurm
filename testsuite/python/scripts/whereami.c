/*****************************************************************************\
 *  test1.91.prog.c - Simple test program for SLURM regression test1.91.
 *  Reports SLURM task ID and the CPU mask,
 *  similar functionality to "taskset" command
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/
#define _GNU_SOURCE
#define __USE_GNU
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *_get_cpu_bindings()
{
    FILE *cpuinfo = fopen("/proc/self/status", "rb");
    char *line = 0;
    size_t size = 0;
    char *cpus = calloc(1024, sizeof(char));
    while(getdelim(&line, &size, '\n', cpuinfo) != -1) {
        if (strstr(line, "Cpus_")) {
            char *end = strstr(line, "\n");
            if (end)
                *end = '\0';
            sprintf(cpus + strlen(cpus), "%s%s", line, (cpus[0]) ? "" : "\t");
        }
    }
    free(line);
    fclose(cpuinfo);
    return cpus;
}


int main (int argc, char **argv)
{
    char *task_str;
    char *node_name;
    int task_id;

    /* On POE systems, MP_CHILD is equivalent to SLURM_PROCID */
    if (((task_str = getenv("SLURM_PROCID")) == NULL) &&
        ((task_str = getenv("MP_CHILD")) == NULL)) {
        fprintf(stderr, "ERROR: getenv(SLURM_PROCID) failed\n");
        exit(1);
    }

    node_name = getenv("SLURMD_NODENAME");
    task_id = atoi(task_str);
    printf("%4d %s - %s\n", task_id, node_name, _get_cpu_bindings());


    if (argc > 1) {
        int sleep_time = strtol(argv[1] ,0, 10);
        //printf("sleeping %d seconds\n", sleep_time);
        fflush(stdout);
        sleep(sleep_time);
    }
    exit(0);
}
