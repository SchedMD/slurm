
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
//#include <mpi.h>
#include <slurm/pmi2.h>
#include <sys/time.h>

/*
 * To build:
 *
 * gcc -g -O0 -o testpmixring testpmixring.c -I<slurm_install>/include -Wl,-rpath,<slurm_install>/lib -L<slurm_install>/lib -lpmi2
 *
 * To run:
 *
 * srun -n8 -m block ./testpmixring
 * srun -n8 -m cyclic ./testpmixring
 */

int
main(int argc, char **argv)
{
    int spawned, size, rank, appnum;
    struct timeval tv, tv2;
    int ring_rank, ring_size;
    char val[128];
    char buf[128];
    char left[128];
    char right[128];

    {
        int x = 0;

        while (x) {
            fprintf(stderr, "attachme %d\n", getpid());
            sleep(2);
        }
    }

    gettimeofday(&tv, NULL);

    PMI2_Init(&spawned, &size, &rank, &appnum);

    /* test PMIX_Ring */
    snprintf(val, sizeof(val), "pmi_rank=%d", rank);
    PMIX_Ring(val, &ring_rank, &ring_size, left, right, 128);

    printf("pmi_rank:%d ring_rank:%d ring_size:%d left:%s mine:%s right:%s\n",
           rank, ring_rank, ring_size, left, val, right);

    PMI2_Finalize();

    gettimeofday(&tv2, NULL);
    printf("%f\n",
           ((tv2.tv_sec - tv.tv_sec) * 1000.0
            + (tv2.tv_usec - tv.tv_usec) / 1000.0));

    return 0;
}
