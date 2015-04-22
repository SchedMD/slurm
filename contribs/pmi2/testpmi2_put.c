#include <stdio.h>
#include <stdlib.h>
#include <slurm/pmi2.h>


int main(int argc, char **argv)
{
    int spawned, size, rank, appnum;
    int ret;
    char jobid[50];
    int msg = 0;
    char val[20] = "0\n";
    int len = 0;

    ret = PMI2_Init(&spawned, &size, &rank, &appnum);
    if (ret != PMI2_SUCCESS) {
        perror("PMI2_Init failed");
        return 1;
    }

    PMI2_Job_GetId(jobid, sizeof(jobid));
    printf("spawned=%d, size=%d, rank=%d, appnum=%d, jobid=%s\n",
           spawned, size, rank, appnum, jobid);
    fflush(stdout);

    PMI2_KVS_Fence();

    /* broadcast msg=42 from proc 0 */
    if (rank == 0) {
        msg = 42;
        snprintf(val, sizeof(val), "%d\n", msg);
        PMI2_KVS_Put("msg", val);
        printf("%d> send %d\n", rank, msg);
        fflush(stdout);
    }

    PMI2_KVS_Fence();

    PMI2_KVS_Get(jobid, PMI2_ID_NULL, "msg", val, sizeof(val), &len);
    msg = atoi(val);
    printf("%d> got %d\n", rank, msg);
    fflush(stdout);

    PMI2_Finalize();
    return 0;
}
