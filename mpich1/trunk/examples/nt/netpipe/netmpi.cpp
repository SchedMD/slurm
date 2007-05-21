#include <stdio.h>
#include <stdlib.h>
#include "mpi.h"
#include "GetOpt.h"
#include <string.h>

#define  DEFPORT			5002
#define  TRIALS 			7
#define  REPEAT 			1000
int 	 g_NSAMP =			150;
#define  PERT				3
int      g_LATENCYREPS =		1000;
#define  LONGTIME			1e99
#define  CHARSIZE			8
#define  PATIENCE			50
#define  RUNTM				0.25
double	 g_STOPTM = 		0.1;
#define  MAXINT 			2147483647

#define 	ABS(x)	   (((x) < 0)?(-(x)):(x))
#define 	MIN(x, y)	(((x) < (y))?(x):(y))
#define 	MAX(x, y)	(((x) > (y))?(x):(y))

int g_nIproc = 0, g_nNproc = 0;

typedef struct protocolstruct ProtocolStruct;
struct protocolstruct
{
	int nbor, iproc;
};

typedef struct argstruct ArgStruct;
struct argstruct 
{
	/* This is the common information that is needed for all tests			*/
	char	 *host;			/* Name of receiving host						*/
	short	 port;			/* Port used for connection 					*/
	char	 *buff;			/* Transmitted buffer							*/
	char	 *buff1;		/* Transmitted buffer							*/
	int 	 bufflen,		/* Length of transmitted buffer 				*/
			 tr, 			/* Transmit flag 								*/
			 nbuff;			/* Number of buffers to transmit 				*/
	
	/* Now we work with a union of information for protocol dependent stuff  */
	ProtocolStruct prot;	/* Structure holding necessary info for TCP 	 */
};

typedef struct data Data;
struct data
{
	double t;
	double bps;
	double variance;
	int    bits;
	int    repeat;
};

double When();
int Setup(ArgStruct *p);
void Sync(ArgStruct *p);
void SendData(ArgStruct *p);
void RecvData(ArgStruct *p);
void SendRecvData(ArgStruct *p);
void SendTime(ArgStruct *p, double *t, int *rpt);
void RecvTime(ArgStruct *p, double *t, int *rpt);
int Establish(ArgStruct *p);
int  CleanUp(ArgStruct *p);
double TestLatency(ArgStruct *p);
double TestSyncTime(ArgStruct *p);
void PrintOptions(void);
int DetermineLatencyReps(ArgStruct *p);

void PrintOptions()
{
	printf("\n");
	printf("Usage: netpipe flags\n");
	printf(" flags:\n");
	printf("       -reps #iterations\n");
	printf("       -time stop_time\n");
	printf("       -start initial_msg_size\n");
	printf("       -end final_msg_size\n");
	printf("       -out outputfile\n");
	printf("       -nocache\n");
	printf("       -headtohead\n");
	printf("       -pert\n");
	printf("       -noprint\n");
	printf("Requires exactly two processes\n");
	printf("\n");
}

void main(int argc, char *argv[])
{
	FILE		*out;				/* Output data file 						 */
	char		s[255]; 			/* Generic string							 */
	char		*memtmp;
	char		*memtmp1;
	
	int i, j, n, nq,				/* Loop indices								*/
		bufoffset = 0,				/* Align buffer to this						*/
		bufalign = 16*1024,			/* Boundary to align buffer to				*/
		nrepeat,					/* Number of time to do the transmission	*/
		nzero = 0,
		len,						/* Number of bytes to be transmitted		*/
		inc = 1,					/* Increment value							*/
		detailflag = 0,				/* Set to examine the signature curve detail*/
		bufszflag = 0,				/* Set to change the TCP socket buffer size */
		pert,						/* Perturbation value						*/
		ipert,
		start = 1,					/* Starting value for signature curve 		*/
		end = MAXINT,				/* Ending value for signature curve			*/
		streamopt = 0,				/* Streaming mode flag						*/
		printopt = 0;				/* Debug print statements flag				*/
	
	ArgStruct	args;				/* Argumentsfor all the calls				*/
	
	double		t, t0, t1, t2,		/* Time variables							*/
		tlast,						/* Time for the last transmission			*/
		tzero = 0,
		latency,					/* Network message latency					*/
		synctime;					/* Network synchronization time 			*/
	
	Data		*bwdata;			/* Bandwidth curve data 					*/
	bwdata = new Data[g_NSAMP];
	
	short		port = DEFPORT;		/* Port number for connection 				*/
	bool bNoCache = false;
	bool bHeadToHead = false;
	bool bSavePert = false;
	
	MPI_Init(&argc, &argv);
	
	MPI_Comm_size(MPI_COMM_WORLD, &g_nNproc);
	MPI_Comm_rank(MPI_COMM_WORLD, &g_nIproc);

	if (g_nNproc != 2)
	{
		if (g_nIproc == 0)
			PrintOptions();
		MPI_Finalize();
		exit(0);
	}

	GetOpt(argc, argv, "-time", &g_STOPTM);
	GetOpt(argc, argv, "-reps", &g_NSAMP);
	GetOpt(argc, argv, "-start", &start);
	GetOpt(argc, argv, "-end", &end);
	bNoCache = GetOpt(argc, argv, "-nocache");
	bHeadToHead = GetOpt(argc, argv, "-headtohead");
	if (GetOpt(argc, argv, "-noprint"))
	    printopt = 0;
	bSavePert = GetOpt(argc, argv, "-pert");
	
	if (g_nIproc == 0)
		strcpy(s, "Netpipe.out");
	GetOpt(argc, argv, "-out", s);
	
	printopt = 1;
	if (start > end)
	{
		fprintf(stdout, "Start MUST be LESS than end\n");
		exit(420132);
	}
	
	args.nbuff = TRIALS;
	args.port = port;
	
	Setup(&args);
	Establish(&args);
	
	if (args.tr)
	{
		if ((out = fopen(s, "w")) == NULL)
		{
			fprintf(stdout,"Can't open %s for output\n", s);
			exit(1);
		}
	}
	
    latency = TestLatency(&args);
    synctime = TestSyncTime(&args);

	
	
	if (args.tr)
	{
		SendTime(&args, &latency, &nzero);
	}
	else
	{
		RecvTime(&args, &latency, &nzero);
	}
	if (args.tr && printopt)
	{
		fprintf(stdout,"Latency: %lf\n", latency);
		fprintf(stdout,"Sync Time: %lf\n", synctime);
		fprintf(stdout,"Now starting main loop\n");
		fflush(stdout);
	}
	tlast = latency;
	inc = (start > 1 && !detailflag) ? start/2: inc;
	args.bufflen = start;
	
	/* Main loop of benchmark */
	for (nq = n = 0, len = start; 
	n < g_NSAMP && tlast < g_STOPTM && len <= end; 
	len = len + inc, nq++)
	{
		if (nq > 2 && !detailflag)
			inc = ((nq % 2))? inc + inc: inc;
		
		/* This is a perturbation loop to test nearby values */
		for (ipert = 0, pert = (!detailflag && inc > PERT + 1)? -PERT: 0;
		pert <= PERT; 
		ipert++, n++, pert += (!detailflag && inc > PERT + 1)? PERT: PERT + 1)
		{
			
			/* Calculate howmany times to repeat the experiment. */
			if (args.tr)
			{
				nrepeat = (int)(MAX((RUNTM / ((double)args.bufflen /
					(args.bufflen - inc + 1.0) * tlast)), TRIALS));
				SendTime(&args, &tzero, &nrepeat);
			}
			else
			{
				nrepeat = 1; /* Just needs to be greater than zero */
				RecvTime(&args, &tzero, &nrepeat);
			}
			
			/* Allocate the buffer */
			args.bufflen = len + pert;
			/* printf("allocating %d bytes\n", args.bufflen * nrepeat + bufalign); */
			if (bNoCache)
			{
				if ((args.buff = (char *)malloc(args.bufflen * nrepeat + bufalign)) == (char *)NULL)
				{
					fprintf(stdout,"Couldn't allocate memory\n");
					break;
				}
			}
			else
			{
				if ((args.buff = (char *)malloc(args.bufflen + bufalign)) == (char *)NULL)
				{
					fprintf(stdout,"Couldn't allocate memory\n");
					break;
				}
			}
			/* if ((args.buff1 = (char *)malloc(args.bufflen * nrepeat + bufalign)) == (char *)NULL) */
			if ((args.buff1 = (char *)malloc(args.bufflen + bufalign)) == (char *)NULL)
			{
				fprintf(stdout,"Couldn't allocate memory\n");
				break;
			}
			/* Possibly align the data buffer */
			memtmp = args.buff;
			memtmp1 = args.buff1;
			
			if (!bNoCache)
			{
				if (bufalign != 0)
				{
					args.buff += (bufalign - ((int)args.buff % bufalign) + bufoffset) % bufalign;
					/* args.buff1 += (bufalign - ((int)args.buff1 % bufalign) + bufoffset) % bufalign; */
				}
			}
			args.buff1 += (bufalign - ((int)args.buff1 % bufalign) + bufoffset) % bufalign;

			if (args.tr && printopt)
			{
				fprintf(stdout,"%3d: %9d bytes %4d times --> ",
					n, args.bufflen, nrepeat);
				fflush(stdout);
			}
			
			/* Finally, we get to transmit or receive and time */
			if (args.tr)
			{
				bwdata[n].t = LONGTIME;
				t2 = t1 = 0;
				for (i = 0; i < TRIALS; i++)
				{
					if (bNoCache)
					{
						if (bufalign != 0)
						{
							args.buff = memtmp + ((bufalign - ((int)args.buff % bufalign) + bufoffset) % bufalign);
							/* args.buff1 = memtmp1 + ((bufalign - ((int)args.buff1 % bufalign) + bufoffset) % bufalign); */
						}
						else
						{
							args.buff = memtmp;
							/* args.buff1 = memtmp1; */
						}
					}
			
					Sync(&args);
					t0 = When();
					for (j = 0; j < nrepeat; j++)
					{
						if (bHeadToHead)
							SendRecvData(&args);
						else
						{
							SendData(&args);
							if (!streamopt)
							{
								RecvData(&args);
							}
						}
						if (bNoCache)
						{
							args.buff += args.bufflen;
							/* args.buff1 += args.bufflen; */
						}
					}
					t = (When() - t0)/((1 + !streamopt) * nrepeat);
					
					if (!streamopt)
					{
						t2 += t*t;
						t1 += t;
						bwdata[n].t = MIN(bwdata[n].t, t);
					}
				}
				if (!streamopt)
					SendTime(&args, &bwdata[n].t, &nzero);
				else
					RecvTime(&args, &bwdata[n].t, &nzero);
				
				if (!streamopt)
					bwdata[n].variance = t2/TRIALS - t1/TRIALS * t1/TRIALS;
				
			}
			else
			{
				bwdata[n].t = LONGTIME;
				t2 = t1 = 0;
				for (i = 0; i < TRIALS; i++)
				{
					if (bNoCache)
					{
						if (bufalign != 0)
						{
							args.buff = memtmp + ((bufalign - ((int)args.buff % bufalign) + bufoffset) % bufalign);
							/* args.buff1 = memtmp1 + ((bufalign - ((int)args.buff1 % bufalign) + bufoffset) % bufalign); */
						}
						else
						{
							args.buff = memtmp;
							/* args.buff1 = memtmp1; */
						}
					}
			
					Sync(&args);
					t0 = When();
					for (j = 0; j < nrepeat; j++)
					{
						if (bHeadToHead)
							SendRecvData(&args);
						else
						{
							RecvData(&args);
							if (!streamopt)
								SendData(&args);
						}
						if (bNoCache)
						{
							args.buff += args.bufflen;
							/* args.buff1 += args.bufflen; */
						}
					}
					t = (When() - t0)/((1 + !streamopt) * nrepeat);
					
					if (streamopt)
					{
						t2 += t*t;
						t1 += t;
						bwdata[n].t = MIN(bwdata[n].t, t);
					}
				}
				if (streamopt)
					SendTime(&args, &bwdata[n].t, &nzero);
				else
					RecvTime(&args, &bwdata[n].t, &nzero);
				
				if (streamopt)
					bwdata[n].variance = t2/TRIALS - t1/TRIALS * t1/TRIALS;
				
			}
			tlast = bwdata[n].t;
			bwdata[n].bits = args.bufflen * CHARSIZE;
			bwdata[n].bps = bwdata[n].bits / (bwdata[n].t * 1024 * 1024);
			bwdata[n].repeat = nrepeat;
			
			if (args.tr)
			{
			    if (bSavePert)
			    {
				/* fprintf(out,"%lf\t%lf\t%d\t%d\t%lf\n", bwdata[n].t, bwdata[n].bps,
				    bwdata[n].bits, bwdata[n].bits / 8, bwdata[n].variance); */
				fprintf(out,"%d\t%lf\t%lf\n", bwdata[n].bits / 8, bwdata[n].bps, bwdata[n].t);
				fflush(out);
			    }
			}
			
			free(memtmp);
			free(memtmp1);
			
			if (args.tr && printopt)
			{
				fprintf(stdout," %6.2lf Mbps in %lf sec\n", bwdata[n].bps, tlast);
				fflush(stdout);
			}
		} /* End of perturbation loop */
		if (!bSavePert && args.tr)
		{
		    /* if we didn't save all of the perturbation loops, find the max and save it */
		    int index = 1;
		    double dmax = bwdata[n-1].bps;
		    for (; ipert > 1; ipert--)
		    {
			if (bwdata[n-ipert].bps > dmax)
			{
			    index = ipert;
			    dmax = bwdata[n-ipert].bps;
			}
		    }
		    fprintf(out,"%d\t%f\t%f\n", bwdata[n-index].bits / 8, bwdata[n-index].bps, bwdata[n-index].t);
		    fflush(out);
		}
	} /* End of main loop  */
	
	if (args.tr)
		fclose(out);
/* THE_END:		 */
	CleanUp(&args);
	delete bwdata;
}


/* Return the current time in seconds, using a double precision number. 	 */
double When()
{
	return MPI_Wtime();
}

int Setup(ArgStruct *p)
{
	int nproc;
	char s[255];
	int len = 255;
	
	MPI_Comm_rank(MPI_COMM_WORLD, &p->prot.iproc);
	MPI_Comm_size(MPI_COMM_WORLD, &nproc);
	
	MPI_Get_processor_name(s, &len);
	printf("%d: %s\n", p->prot.iproc, s);
	fflush(stdout);
	
	if (p->prot.iproc == 0)
		p->prot.nbor = 1;
	else
		p->prot.nbor = 0;
	
	if (nproc < 2)
	{
		printf("Need two processes\n");
		printf("nproc: %i\n", nproc);
		exit(-2);
	}
	
	if (p->prot.iproc == 0)
		p->tr = 1;
	else
		p->tr = 0;
	return 1;	
}	

void Sync(ArgStruct *p)
{
	char ch;
	MPI_Status status;
	if (p->tr)
	{
		MPI_Send(&ch, 1, MPI_BYTE, p->prot.nbor, 1, MPI_COMM_WORLD);
		MPI_Recv(&ch, 1, MPI_BYTE, p->prot.nbor, 1, MPI_COMM_WORLD, &status);
		MPI_Send(&ch, 1, MPI_BYTE, p->prot.nbor, 1, MPI_COMM_WORLD);
	}
	else
	{
		MPI_Recv(&ch, 1, MPI_BYTE, p->prot.nbor, 1, MPI_COMM_WORLD, &status);
		MPI_Send(&ch, 1, MPI_BYTE, p->prot.nbor, 1, MPI_COMM_WORLD);
		MPI_Recv(&ch, 1, MPI_BYTE, p->prot.nbor, 1, MPI_COMM_WORLD, &status);
	}
}

int DetermineLatencyReps(ArgStruct *p)
{
    MPI_Status status;
    double t0, duration = 0;
    int reps = 1;
    int i;

    /* prime the send/receive pipes */
    Sync(p);
    Sync(p);
    Sync(p);

    /* test how long it takes to send n messages 
     * where n = 1, 2, 4, 8, 16, 32, ...
     */
    while ( (duration < 0.1) ||
	    (duration < 0.3 && reps < 1000))
    {
	t0 = When();
	t0 = When();
	t0 = When();
	t0 = When();
	for (i=0; i<reps; i++)
	{
	    Sync(p);
	}
	duration = When() - t0;
	reps = reps * 2;

	/* use duration from the root only */
	if (p->prot.iproc == 0)
	    MPI_Send(&duration, 1, MPI_DOUBLE, p->prot.nbor, 2, MPI_COMM_WORLD);
	else
	    MPI_Recv(&duration, 1, MPI_DOUBLE, p->prot.nbor, 2, MPI_COMM_WORLD, &status);
    }

    return reps;
}

double TestLatency(ArgStruct *p)
{
    double latency, t0;
    int i;

    g_LATENCYREPS = DetermineLatencyReps(p);
    if (g_LATENCYREPS < 1024 && p->prot.iproc == 0)
    {
	printf("Using %d reps to determine latency\n", g_LATENCYREPS);
	fflush(stdout);
    }

    p->bufflen = 1;
    p->buff = (char *)malloc(p->bufflen);
    p->buff1 = (char *)malloc(p->bufflen);
    Sync(p);
    t0 = When();
    t0 = When();
    t0 = When();
    t0 = When();
    for (i = 0; i < g_LATENCYREPS; i++)
    {
	if (p->tr)
	{
	    SendData(p);
	    RecvData(p);
	}
	else
	{
	    RecvData(p);
	    SendData(p);
	}
    }
    latency = (When() - t0)/(2 * g_LATENCYREPS);
    free(p->buff);
    free(p->buff1);

    return latency;
}

double TestSyncTime(ArgStruct *p)
{
    double synctime, t0;
    int i;

    t0 = When();
    t0 = When();
    t0 = When();
    t0 = When();
    t0 = When();
    t0 = When();
    for (i = 0; i < g_LATENCYREPS; i++)
	Sync(p);
    synctime = (When() - t0)/g_LATENCYREPS;

    return synctime;
}

void SendRecvData(ArgStruct *p)
{
	MPI_Status status;
	
	MPI_Sendrecv(p->buff, p->bufflen, MPI_BYTE, p->prot.nbor, 1, p->buff1, p->bufflen, MPI_BYTE, p->prot.nbor, 1, MPI_COMM_WORLD, &status);

	/*
	//MPI_Request request;
	//MPI_Irecv(p->buff1, p->bufflen, MPI_BYTE, p->prot.nbor, 1, MPI_COMM_WORLD, &request);
	//MPI_Send(p->buff, p->bufflen, MPI_BYTE, p->prot.nbor, 1, MPI_COMM_WORLD);
	//MPI_Wait(&request, &status);

	//MPI_Send(p->buff, p->bufflen, MPI_BYTE, p->prot.nbor, 1, MPI_COMM_WORLD);
	//MPI_Recv(p->buff1, p->bufflen, MPI_BYTE, p->prot.nbor, 1, MPI_COMM_WORLD, &status);
	*/
}

void SendData(ArgStruct *p)
{
	MPI_Send(p->buff, p->bufflen, MPI_BYTE, p->prot.nbor, 1, MPI_COMM_WORLD);
}

void RecvData(ArgStruct *p)
{
	MPI_Status status;
	MPI_Recv(p->buff1, p->bufflen, MPI_BYTE, p->prot.nbor, 1, MPI_COMM_WORLD, &status);
}


void SendTime(ArgStruct *p, double *t, int *rpt)
{
	if (*rpt > 0)
		MPI_Send(rpt, 1, MPI_INT, p->prot.nbor, 2, MPI_COMM_WORLD);
	else
		MPI_Send(t, 1, MPI_DOUBLE, p->prot.nbor, 2, MPI_COMM_WORLD);
}

void RecvTime(ArgStruct *p, double *t, int *rpt)
{
	MPI_Status status;
	if (*rpt > 0)
		MPI_Recv(rpt, 1, MPI_INT, p->prot.nbor, 2, MPI_COMM_WORLD, &status);
	else
		MPI_Recv(t, 1, MPI_DOUBLE, p->prot.nbor, 2, MPI_COMM_WORLD, &status);
}

int Establish(ArgStruct *p)
{
	return 1;
}

int  CleanUp(ArgStruct *p)
{
	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Finalize();
	return 1;
}

