/*****************************************************************************\
 *  test1.96.prog.upc - Basic SHMEM test via srun.
 *****************************************************************************
 *  Based upon "Code Example" in "SHMEM Tutorial"
 *  By Hung-Hsun Su, UPC Group, HSC Lab, Spring 2010
\*****************************************************************************/

#include <stdio.h>
#include <shmem.h>

int me, npes, i;
int src[8], dest[8];

int main(int argc, char * argv[])
{
	/* Get PE information */
	shmem_init();
	me = _my_pe();
	npes = _num_pes();

	/* Initialize and send on PE 0 */
	if (me == 0) {
		for (i = 0; i < 8; i++)
			src[i] = i + 1;
		/* Put source date at PE 0 to dest at PE 1+ */
		for (i = 1; i < npes; i++)
			shmem_put64(dest, src, 8 * sizeof(int) / 8, i);
	}

	/* Make sure the transfer is complete */
	shmem_barrier_all();

	/* Print from PE 1+ */
	if (me > 0) {
		printf("PE %d: %d", me, dest[0]);
		for (i = 1; i < 8; i++)
			printf(",%d", dest[i]);
		printf("\n");
	}
	shmem_finalize();

	return 0;
}
