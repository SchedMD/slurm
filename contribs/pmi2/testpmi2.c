
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include <slurm/pmi2.h>
#include <sys/time.h>

static char *mrand(int, int);

int
main(int argc, char **argv)
{
	int rank;
	int size;
	int appnum;
	int spawned;
	int flag;
	int len;
	int i;
	struct timeval tv;
	struct timeval tv2;
	char jobid[128];
	char key[128];
	char val[128];
	char buf[128];

	{
		int x = 1;
		while (x == 0) {
			sleep(2);
		}
	}

	gettimeofday(&tv, NULL);
	srand(tv.tv_sec);

	PMI2_Init(&spawned, &size, &rank, &appnum);

	PMI2_Job_GetId(jobid, sizeof(buf));

	memset(val, 0, sizeof(val));
	PMI2_Info_GetJobAttr("mpi_reserved_ports",
						 val,
						 PMI2_MAX_ATTRVALUE,
						 &flag);

	sprintf(key, "mpi_reserved_ports");
	PMI2_KVS_Put(key, val);

	memset(val, 0, sizeof(val));
	sprintf(buf, "PMI_netinfo_of_task");
	PMI2_Info_GetJobAttr(buf,
						 val,
						 PMI2_MAX_ATTRVALUE,
						 &flag);
	sprintf(key, buf);
	PMI2_KVS_Put(key, val);

	memset(val, 0, sizeof(val));
	sprintf(key, "david@%d", rank);
	sprintf(val, "%s", mrand(97, 122));
	PMI2_KVS_Put(key, val);

	PMI2_KVS_Fence();

	for (i = 0; i < size; i++) {

		memset(val, 0, sizeof(val));
		sprintf(key, "PMI_netinfo_of_task");
		PMI2_KVS_Get(jobid,
					 PMI2_ID_NULL,
					 key,
					 val,
					 sizeof(val),
					 &len);
		printf("rank: %d key:%s val:%s\n", rank, key, val);

		memset(val, 0, sizeof(val));
		sprintf(key, "david@%d", rank);
		PMI2_KVS_Get(jobid,
					 PMI2_ID_NULL,
					 key,
					 val,
					 sizeof(val),
					 &len);
		printf("rank: %d key:%s val:%s\n", rank, key, val);

		memset(val, 0, sizeof(val));
		sprintf(key, "mpi_reserved_ports");
		PMI2_KVS_Get(jobid,
					 PMI2_ID_NULL,
					 key,
					 val,
					 sizeof(val),
					 &len);
		printf("rank: %d key:%s val:%s\n", rank, key, val);
	}

	PMI2_Finalize();

	gettimeofday(&tv2, NULL);
	printf("%f\n",
		   ((tv2.tv_sec - tv.tv_sec) * 1000.0
			+ (tv2.tv_usec - tv.tv_usec) / 1000.0));

	return 0;
}

/* Generate a random number between
 * min and Max and convert it to
 * a string.
 */
static char *
mrand(int m, int M)
{
	int i;
	time_t t;
	static char buf[64];

	memset(buf, 0, sizeof(buf));
	for (i = 0; i  < 16; i++)
		buf[i] = rand() % (M - m + 1) + m;

	return buf;
}
