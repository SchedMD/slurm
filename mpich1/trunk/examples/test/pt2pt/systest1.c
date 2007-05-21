#include "mpi.h"
#include <stdio.h>

#define MAX2(a,b) (((a)>(b)) ? (a) : (b))

int  GlobalReadInteger();
void Hello();
/*
void Ring();
void Stress();
void Globals();
*/

int main(argc,argv)
int argc;
char **argv;
{

    int me, option;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD,&me);

    fprintf(stderr,"Process %d is alive\n",me);

    while (1) {

	MPI_Barrier(MPI_COMM_WORLD);

      again:
	if (me == 0) {
	    /* Read user input for action */
	    (void) printf("\nOptions: 0=quit, 1=Hello, 2=Ring, 3=Stress, ");
	    (void) printf("4=Globals : ");
	    (void) fflush(stdout);
	}
	option = GlobalReadInteger();
	if ( (option < 0) || (option > 4) )
	    goto again;

	switch (option) {
	  case 0:
	    MPI_Finalize();
	    return;
	  case 1:
	    Hello();     break;
/*
	  case 2:
	    Ring();      break;
	  case 3:
	    Stress();    break;
	  case 4:
	    Globals();   break;
*/
	  default:
	    fprintf(stderr,"systest: invalid option %d\n", option);   break;
	}
    }
}

int GlobalReadInteger()
/*
  Process zero reads an integer from stdin and broadcasts
  to everyone else
*/
{
    int me, value, *msg, msg_len, type=999 ,zero=0;

    MPI_Comm_rank(MPI_COMM_WORLD, &me);
    if (me == 0) {
	if (scanf("%d", &value) != 1)
	    fprintf(stderr,"failed reading integer value from stdin\n");
    }
    MPI_Bcast(&value, 1, MPI_INT, 0, MPI_COMM_WORLD);
    return value;
}

static void Hello()
/*
  Everyone exchanges a hello message with everyone else.
  The hello message just comprises the sending and target nodes.
*/
{
    int nproc, me;
    int type = 1;
    int buffer[2], node, length;
    MPI_Status status;

    MPI_Comm_rank(MPI_COMM_WORLD, &me);
    MPI_Comm_size(MPI_COMM_WORLD, &nproc);

    if (me == 0) {
	printf("\nHello test ... show network integrity\n----------\n\n");
	fflush(stdout);
    }

    for (node = 0; node<nproc; node++) {
	if (node != me) {
	    buffer[0] = me;
	    buffer[1] = node;
	    MPI_Send(buffer, 2, MPI_INT, node, type, MPI_COMM_WORLD);
	    buffer[0] = buffer[1] = 7777;
	    MPI_Recv(buffer, 2, MPI_INT, node, type, MPI_COMM_WORLD, &status);

	    if ( (buffer[0] != node) || (buffer[1] != me) ) {
		(void) fprintf(stderr, "Hello: %d!=%d or %d!=%d\n",
			       buffer[0], node, buffer[1], me);
		printf("Mismatch on hello process ids; node = %d\n", node);
	    }

	    printf("Hello from %d to %d\n", me, node);
	    fflush(stdout);
	}
    }
}
