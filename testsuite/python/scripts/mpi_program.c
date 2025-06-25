#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#define BUF_SIZE 64
#define COMM_TAG 1000
#define ITERATIONS 1

typedef struct rank_info {
	char host[BUF_SIZE];
	int rank;
} rank_info_t;

static void pass_its_neighbor(const int rank, const int size)
{
	struct utsname uts;
	MPI_Request request[2];
	rank_info_t out_buf, in_buf;
	char *host_env = getenv("SLURMD_NODENAME");

	out_buf.rank = rank;
	if (host_env) {
		strncpy(out_buf.host, host_env, BUF_SIZE);
	} else {
		uname(&uts);
		strncpy(out_buf.host, uts.nodename, BUF_SIZE);
	}
	out_buf.host[BUF_SIZE - 1] = '\0';

	MPI_Irecv((void *) &in_buf, sizeof(rank_info_t), MPI_CHAR,
		  ((rank + size - 1) % size), COMM_TAG, MPI_COMM_WORLD,
		  &request[0]);
	MPI_Isend((void *) &out_buf, sizeof(rank_info_t), MPI_CHAR,
		  ((rank + 1) % size), COMM_TAG, MPI_COMM_WORLD, &request[1]);
	MPI_Waitall(2, request, MPI_STATUS_IGNORE);

	printf("Rank[%d] on %s just received msg from Rank %d on %s\n", rank,
	       out_buf.host, in_buf.rank, in_buf.host);
}

int main(int argc, char *argv[])
{
	int i;
	int size, rank;
	time_t now;

	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	for (i = 0; i < ITERATIONS; i++) {
		if (i)
			sleep(1);
		pass_its_neighbor(rank, size);
		if ((ITERATIONS > 1) && (rank == 0)) {
			static time_t last_time = 0;
			now = time(NULL);
			printf("Iteration:%d Time:%s", i, ctime(&now));
			if (last_time && (last_time < (now - 2)))
				printf("Woke from suspend\n");
			last_time = now;
		}
	}

	MPI_Finalize();
	return 0;
}
