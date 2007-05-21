#include "mpeconf.h"
#include "clog.h"
#include "mpi.h"
#include "clog_merge.h"

int send1, send2, sendstate;
int recv1, recv2, recvstate;
int comp1, comp2, compstate;
int redu1, redu2, redustate;
int barr1, barr2, barrstate;

void clog_setup();

void main(argc, argv)
int argc;
char *argv[];
{

#   define DSIZE 5
    int i, j=3;
    int size, rank;
    int data[DSIZE];
    MPI_Status status;
    int tag= 200;
    int newevent;

    MPI_Init(&argc, &argv);

    CLOG_Init();
    CLOG_LOGCOMM(INIT, -1, (int) MPI_COMM_WORLD);
    
    clog_setup();

    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    while (j--) {
	CLOG_LOGCOLL(barr1, 0, 0, (int) MPI_COMM_WORLD);
	MPI_Barrier(MPI_COMM_WORLD);
	CLOG_LOGCOLL(barr2, 0, 0, (int) MPI_COMM_WORLD);

	newevent = CLOG_get_new_event();
	CLOG_LOGEVENT(newevent, "raw desc");
	CLOG_LOGRAW(newevent, 42, "raw event"); 

	if (rank == 0) {
	    for (i = 0; i < DSIZE; i++)
		data[i] = i;
	    CLOG_LOGMSG(send1, tag, (rank+1) % size, (int) MPI_COMM_WORLD, DSIZE);
	    MPI_Send(data, DSIZE, MPI_INT, (rank+1) % size, tag, MPI_COMM_WORLD);
	    CLOG_LOGMSG(send2, tag, (rank+1) % size, (int) MPI_COMM_WORLD, DSIZE);

	    CLOG_LOGMSG(recv1, tag, size-1, (int) MPI_COMM_WORLD, DSIZE);
	    MPI_Recv(data, DSIZE, MPI_INT, size-1, tag, MPI_COMM_WORLD, &status);
	    CLOG_LOGMSG(recv2, tag, size-1, (int) MPI_COMM_WORLD, DSIZE);
	    for (i = 0; i < DSIZE; i++)
		if (data[i] != i)
		    printf("mismatch: %d %d\n", data[1], i);
	}
	else {
	    CLOG_LOGMSG(send1, tag, rank-1, (int) MPI_COMM_WORLD, DSIZE);
	    MPI_Recv(data, DSIZE, MPI_INT, rank-1, tag, MPI_COMM_WORLD, &status);
	    CLOG_LOGMSG(send2, tag, rank-1, (int) MPI_COMM_WORLD, DSIZE);

	    CLOG_LOGMSG(recv1, tag, (rank+1) % size, (int) MPI_COMM_WORLD, DSIZE);
	    MPI_Send(data, DSIZE, MPI_INT, (rank+1) % size, tag, MPI_COMM_WORLD);
	    CLOG_LOGMSG(recv2, tag, (rank+1) % size, (int) MPI_COMM_WORLD, DSIZE);
	}
    }

    CLOG_Finalize();
    CLOG_mergelogs(CMERGE_SHIFT, "testlog.clog", ALOG_LOG);

    MPI_Finalize();
}

void clog_setup()
{
    send1     = CLOG_get_new_event();
    send2     = CLOG_get_new_event();
    sendstate = CLOG_get_new_state();
    CLOG_LOGEVENT(send1, "start send");
    CLOG_LOGEVENT(send2, "end send");
    CLOG_LOGSTATE(sendstate,send1,send2,"green","sending");

    recv1     = CLOG_get_new_event();
    recv2     = CLOG_get_new_event();
    recvstate = CLOG_get_new_state();
    CLOG_LOGEVENT(recv1, "start recv");
    CLOG_LOGEVENT(recv2, "end recv");
    CLOG_LOGSTATE(recvstate,recv1,recv2,"red","recving");

    comp1     = CLOG_get_new_event();
    comp2     = CLOG_get_new_event();
    compstate = CLOG_get_new_state();
    CLOG_LOGEVENT(comp1, "start comp");
    CLOG_LOGEVENT(comp2, "end comp");
    CLOG_LOGSTATE(compstate,comp1,comp2,"blue","computing");

    redu1     = CLOG_get_new_event();
    redu2     = CLOG_get_new_event();
    redustate = CLOG_get_new_state();
    CLOG_LOGEVENT(redu1, "start redu");
    CLOG_LOGEVENT(redu2, "end redu");
    CLOG_LOGSTATE(redustate,redu1,redu2,"purple","reducing");

    barr1     = CLOG_get_new_event();
    barr2     = CLOG_get_new_event();
    barrstate = CLOG_get_new_state();
    CLOG_LOGEVENT(barr1, "start barr");
    CLOG_LOGEVENT(barr2, "end barr");
    CLOG_LOGSTATE(barrstate,barr1,barr2,"yellow","barrier");
}










