
/*  Created: Dec.6, 1999, Last modified: Feb.17, 2000
 *  This test program was originally written to determine
 *  whether the hidden collective communicator inside
 *  the InterComm works as an IntraComm or not.
 * ----------------------------------------------
 *  Now, it IS the test program for inter_Bcast()
 * ----------------------------------------------
 */
 
# define DATA_INT			1001

# include "mpi.h"
# include <stdio.h>
# include <stdlib.h>
# include <string.h>

main(argc,argv)
int argc;
char *argv[];
{
	MPI_Group world_group, first_group, second_group;
	MPI_Comm first_comm, second_comm, inter_comm;

	int global_rank, intra_rank, inter_rank, root;	/* Ranks */
	int size;					/* Global Size */
	int i;						/* Counter */
	int namelen, data = 1;
	int *rank_array;
	int mpi_errno;
	double startwtime, endwtime;			/* Timing */
							/* measured only at "root" */
	
	FILE *fptr;					/* To check the output */
	char filename[MPI_MAX_PROCESSOR_NAME];          /* Each file writes to */
							/* a separate file */

/* -- You can use these variables later, if you want to --	
    int done = 0, n, myid, numprocs, i;
*/

	MPI_Init(&argc,&argv);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Get_processor_name(filename, &namelen);

	/* Open the output file for writing */
	strcat(filename, ".log");
        fptr = fopen(filename, "w");
        fprintf(fptr, "Log File   : %s\n\n",  filename);

	/* Create an IntraComm 
	 * First, get the group of MPI_COMM_WORLD
	 */
	MPI_Comm_group(MPI_COMM_WORLD, &world_group);
	
	/* We will create two new groups:
	 * The first with the first half of processes
	 * and the second with the second half
	 */
	if(size%2 != 0)
		rank_array = (int *) malloc(sizeof(int) * (size/2 + 1));
	else
		rank_array = (int *) malloc(sizeof(int) * size/2);

	for(i = 0; i < size/2; i++)
		rank_array[i] = i;
	MPI_Group_incl(world_group, size/2, rank_array, &first_group);
 
	for(i = size/2; i <= (size-1); i++)
		rank_array[i] = i;
	if(size%2 != 0)
		MPI_Group_incl(world_group, size/2 + 1, rank_array, &second_group);
	else
		MPI_Group_incl(world_group, size/2, rank_array, &second_group);

	/* Create two IntraComms */
	MPI_Comm_create(MPI_COMM_WORLD, first_group, &first_comm);
	MPI_Comm_create(MPI_COMM_WORLD, second_group, &second_comm);

	/* Remember that though all the above calls are made collective,
	 * I will belong to any one of the groups only
	 */

	/* Check my rank in both the newly created IntraComm and globally.
	 * I will obviously check only that IntraComm to which I belong.
	 * What if I query the wrong communicator? Check it out...
	 */
	MPI_Comm_rank(MPI_COMM_WORLD, &global_rank);
	if(global_rank < size/2)
		MPI_Comm_rank(first_comm, &intra_rank);
	else
		MPI_Comm_rank(second_comm, &intra_rank);

	/* Create an InterCom  with these two IntraComms 
	 * Note that we have a "default" of rank 0 in each IntraComm
	 * as the leader for that IntraComm
	 * Will the tag (999) have a problem?
	 */
	mpi_errno = MPI_Intercomm_create(first_comm, 0, second_comm, 0,
					999, &inter_comm);
	if(mpi_errno)
	{
		/* Exit from the program */
		fprintf(fptr, "\nERROR: MPI_Intercomm_create failed!\n\n");
		fclose(fptr);
		MPI_Finalize();
		return 1;
	}

 	/* Check my rank in the newly created InterComm */
	MPI_Comm_rank(inter_comm, &inter_rank);

	/* Get the pointer to the underlying Comm struct */
	/* ic_ptr = MPIR_GET_COMM_PTR(inter_comm); */

	/* First decide which process is going to act as the root
	 * for this Bcast. Let us use the middle process of the first group
	 * as the root. The rank in this case is decided by the total number
	 * of processes involved in the test. 
	 * Then, prepare the data (int for now) to be sent.
	 * Note that the value is DATA_INT before the Bcast only at the root
	 * Finally, decide the value of the "root" parameter
	 */
	root = size/4;			/* i.e. mid-process of first group */
	if(global_rank == root)
	{
		data = DATA_INT;	/* otherwise, it remains 1 */
		root = MPI_ROOT;	/* I AM the source */
	}
	else if(global_rank < size/2)
		root = MPI_PROC_NULL;	/* I am uninvolved in this Bcast */
	/* else...
	 * 	Otherwise, I am at the receiving group and so I will retain the
	 *      proper rank of the root at the other end.
	 *      HOWEVER, this is a special case. The rank of the root that we
	 *      have currently is the GLOBAL rank. What we actually need to pass
	 *      as parameter is the rank of the root in its LOCAL GROUP. In this
	 *      case, both the global and local rank are the same and = size/4)
	 *      You can verify the above assumption...
	 */

	/* At this point, check if everything is going fine so far */
        fprintf(fptr, "Global Rank          : %d\n", global_rank);
	fprintf(fptr, "Intra  Rank          : %d\n", intra_rank);
	fprintf(fptr, "Inter  Rank          : %d\n", inter_rank);
	fprintf(fptr, "Param (Root)         : %d\n", root);
	fprintf(fptr, "Data before Bcast    : %d\n", data);

	/* Now call the inter_Bcast */
	if(root == MPI_ROOT)
		startwtime = MPI_Wtime();	/* Start timer */
	mpi_errno = MPI_Bcast(&data, 1, MPI_INT, root, inter_comm);
	/* mpi_errno = inter_Bcast(&data, 1, MPI_INT, root, ic_ptr));  */

	if(root == MPI_ROOT)
	{
		endwtime = MPI_Wtime();		/* Stop timer */
		fprintf(fptr, "WC Time taken for Bcast = %f\n",
					 (endwtime - startwtime)); 
	}

	if(mpi_errno)
		fprintf(fptr, "\nERROR: during inter_Bcast!\n");
	else
		fprintf(fptr, "\ninter_Bcast executed successfully!\n");

	fprintf(fptr, "Data after Bcast     : %d\n", data);

	if(data == DATA_INT)
		fprintf(fptr, "Test Passed.\n");
	else
		fprintf(fptr, "Test Failed.\n");

	/* Free the newly created Comms / groups */
	MPI_Comm_free(&inter_comm);
	MPI_Comm_free(&second_comm);
	MPI_Comm_free(&first_comm);
	MPI_Group_free(&second_group);
	MPI_Group_free(&first_group);
	
	fclose(fptr);
	MPI_Finalize();
}

/* End of program: test_inter_bcast.c */

