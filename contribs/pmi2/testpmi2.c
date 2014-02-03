
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include <pmi2.h>
#include <sys/time.h>

static char *mrand(void);
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

	gettimeofday(&tv, NULL);

	PMI2_Init(&spawned, &size, &rank, &appnum);

	PMI2_Job_GetId(jobid, sizeof(buf));

	PMI2_Info_GetJobAttr("mpi_reserved_ports",
						 val,
						 PMI2_MAX_ATTRVALUE,
						 &flag);

	sprintf(key, "mpi_reserved_ports");
	PMI2_KVS_Put(key, val);

	val[0] = 0;
	sprintf(buf, "PMI_netinfo_of_task_%d", rank);
	PMI2_Info_GetJobAttr(buf,
						 val,
						 PMI2_MAX_ATTRVALUE,
						 &flag);
	sprintf(key, buf);
	PMI2_KVS_Put(key, val);

	sprintf(key, "david@%d", rank);
	sprintf(val, "%s", mrand());
	PMI2_KVS_Put(key, val);

	PMI2_KVS_Fence();

	for (i = 0; i < size; i++) {

		sprintf(key, "PMI_netinfo_of_task_%d", i);
		PMI2_KVS_Get(jobid,
					 PMI2_ID_NULL,
					 key,
					 val,
					 sizeof(val),
					 &len);
		printf("rank: %d key:%s val:%s\n", rank, key, val);

		sprintf(key, "david@%d", rank);
		PMI2_KVS_Get(jobid,
					 PMI2_ID_NULL,
					 key,
					 val,
					 sizeof(val),
					 &len);
		printf("rank: %d key:%s val:%s\n", rank, key, val);

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

static char *
mrand(void)
{
	int i;
	int len;
	static char buf[128];

	memset(buf, 0, sizeof(buf));
	srand(time(NULL));

	len = 64;
	for (i = 0; i < len; i++)
		buf[i] = '0' + rand()%72;

	return buf;
}
