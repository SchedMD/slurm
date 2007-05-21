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

int main( int argc, char **argv )
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
	  case 2:
	    Ring();      break;
/*
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

static void Ring()       /* Time passing a message round a ring */
{
    int nproc, me;
    MPI_Status status;
    int nproc = p4_num_total_ids();
    int type = 4;
    int left = (me + nproc - 1) % nproc;
    int right = (me + 1) % nproc;
    char *buffer, *msg;
    int start, lenbuf, used, max_len,  msg_len;
    double rate, us_rate;
    double start_ustime, end_ustime, used_ustime;

    MPI_Comm_rank(MPI_COMM_WORLD, &me);
    MPI_Comm_size(MPI_COMM_WORLD, &nproc);
    left = (me + nproc - 1) % nproc;
    right = (me + 1) % nproc;

    /* Find out how big a message to use */

    if (me == 0) {
	(void) printf("\nRing test...time network performance\n---------\n\n");
	(void) printf("Input maximum message size: ");
	(void) fflush(stdout);
    }
    max_len = GlobalReadInteger();
    if ( (max_len <= 0) || (max_len >= 4*1024*1024) )
	max_len = 512*1024;
    if ( (buffer = malloc((unsigned) max_len)) == (char *) NULL) {
	printf("process %d could not allocate buffer of size %d\n",me,max_len);
	MPI_Abort(MPI_COMM_WORLD,7777);
    }

    lenbuf = 1;
    while (lenbuf <= max_len) {
	start_ustime = MPI_Wtime();
	if (me == 0) {
	    MPI_Send(buffer,lenbuf,MPI_CHAR,left, type,MPI_COMM_WORLD);
	    MPI_Recv(buffer,lenbuf,MPI_CHAR,right,type,MPI_COMM_WORLD,&status);
	}
	else {
	    MPI_Recv(buffer,lenbuf,MPI_CHAR,right,type,MPI_COMM_WORLD,&status);
	    MPI_Send(buffer,lenbuf,MPI_CHAR,left, type,MPI_COMM_WORLD);
	}
	used_ustime = MPI_Wtime() - start_ustime;

	if (used_ustime > 0)
	    us_rate = 1.0 * (double) (nproc * lenbuf) / (double) used_ustime;
	else
	    us_rate = 0.0;
	if (me == 0)
	    printf("len=%d bytes, used= %d us, rate=%f Mbytes/sec\n",
		   lenbuf, used_ustime, us_rate);
    
	lenbuf *= 2;
    }
    free(buffer);
}

double ranf()
/* Returns ran # uniform in (0,1) ... probably rather bad statistics. */
{
  static unsigned long seed = 54321;

  seed = seed * 1812433253 + 12345;
  return (seed & 0x7fffffff) * 4.6566128752458e-10;
}

static void RandList(lo, hi, list, n)
     int lo, hi, *list, n;
/*
  Fill list with n random integers between lo & hi inclusively
*/
{
  int i, ran;
  double dran;

  for (i=0; i<n; i++) {
    dran = ranf();
    ran = lo + (int) (dran * (double) (hi-lo+1));
    if (ran < lo)
      ran = lo;
    if (ran > hi)
      ran = hi;
    list[i] = ran;
  }
}

static void Stress()
/*
  Stress the system by passing messages between a randomly selected
  list of nodes
*/
{
#define N_LEN 10
#ifdef NCUBE
  /* ncube does not handle msgs larger than 
     32K at present (see nwrite) */
  static int len[N_LEN] = {0,1,2,4,8,4096,8192,16384,32768,32768};
#else
  static int len[N_LEN] = {0,1,2,4,8,4096,8192,16384,32768,65536};
#endif
  int me = p4_get_my_id();
  int nproc = p4_num_total_ids();
  int zero = 0;
  int type, lenbuf, i, j, from, to;
  int *list_i, *list_j, *list_n;
  char *buffer;
  int n_stress, mod, *msg, msg_len;


  type = 6;
  if (me == 0) {
    (void) printf("\nStress test ... randomly exchange messages\n-----------");
    (void) printf("\n\nInput no. of messages: ");
    (void) fflush(stdout);
  }
  n_stress = GlobalReadInteger();
  if ( (n_stress <= 0) || (n_stress > 100000) )
    n_stress = 1000;
  p4_dprintfl(00,"n_stress=%d\n",n_stress);

  lenbuf = n_stress * sizeof(int);

  if (!(buffer = p4_shmalloc((unsigned) len[N_LEN-1])))
    p4_error("Stress: failed to allocate buffer", len[N_LEN-1]);

  type = 7;
  if (me == 0) { /* Make random list of pairs and message lengths */
    if (!(list_i = (int *) p4_shmalloc((unsigned) lenbuf)))
      p4_error("Stress: failed to allocate list_i",lenbuf);
    if (!(list_j = (int *) p4_shmalloc((unsigned) lenbuf)))
      p4_error("Stress: failed to allocate list_j",lenbuf);
    if (!(list_n = (int *) p4_shmalloc((unsigned) lenbuf)))
      p4_error("Stress: failed to allocate list_n",lenbuf);

    RandList((int) 0, nproc-1, list_i, n_stress);
    RandList((int) 0, nproc-1, list_j, n_stress);
    RandList((int) 0, N_LEN-1, list_n, n_stress);
    for (i=0; i<n_stress; i++)
      list_n[i] = len[list_n[i]];
    p4_broadcastx(type, (char *) list_i, lenbuf, P4INT);
    p4_broadcastx(type, (char *) list_j, lenbuf, P4INT);
    p4_broadcastx(type, (char *) list_n, lenbuf, P4INT);
  }
  else {
    list_i = (int *) NULL;
    (void) p4_recv(&type, &zero, (char **) &list_i, &msg_len);
    list_j = (int *) NULL;
    (void) p4_recv(&type, &zero, (char **) &list_j, &msg_len);
    list_n = (int *) NULL;
    (void) p4_recv(&type, &zero, (char **) &list_n, &msg_len);
  }

  type = 8;

  j = 0;
  mod = (n_stress-1)/10 + 1;
  for (i=0; i < n_stress; i++) {

    from   = list_i[i];
    to     = list_j[i];
    lenbuf = list_n[i];

    /* P4 can send to self 
    if (from == to)
      continue; */

    if ( (me == 0) && (j%mod == 0) ) {
      (void) printf("Stress: test=%ld: from=%ld, to=%ld, len=%ld\n",
		    i, from, to, lenbuf);
      (void) fflush(stdout);
    }

    j++;  /* Needed when skipping send to self */

    if (from == me)
      (void) p4_send(type, to, buffer, lenbuf);

    if (to == me) {
      msg = (int *) NULL;
      (void) p4_recv(&type, &from, (char **) &msg, &msg_len);
      p4_msg_free((char *) msg);
      if (msg_len != lenbuf)
	p4_error("Stress: invalid message length on receive",lenbuf);
    }
  }

  (void) p4_shfree(buffer);
  if (me == 0) {
    (void) p4_shfree((char *) list_n);
    (void) p4_shfree((char *) list_j);
    (void) p4_shfree((char *) list_i);
  }
  else {
    (void) p4_msg_free((char *) list_n);
    (void) p4_msg_free((char *) list_j);
    (void) p4_msg_free((char *) list_i);
  }
}

static int CompareVectors(n, a, b)
     int n;
     double *a, *b;
/*
  Return the no. of differences in two vectors allowing for
  numerical roundoff.
*/
{
#define ABS(a)   (((a)>=0 ) ? (a) : -(a))
  int nerrs = 0;
  double diff;

  while (n--) {
    diff = *a++ - *b++;
    if (ABS(diff) > 1.0e-8)
      nerrs++;
  }
  
  return nerrs;
}

static void Globals()
/*
  Test out functioning of the global operations.
*/
{
  int nproc = p4_num_total_ids();
  int me = p4_get_my_id();
  int n, i, start, used, nerrs;
  double *a, *b, rate;

#define DO(string, op) \
  start = p4_clock(); \
  if (p4_global_op(33, (char *) a, n, sizeof(double), op, P4DBL)) \
    p4_error("p4_global_op failed",n); \
  used = p4_clock()-start; \
  rate = (used>0) ? n/(1.0e+3 * used) : 0.0; \
  nerrs = CompareVectors(n, a, b); \
  if (me == 0) \
    (void) printf("%s, len=%d, used=%d ms, rate=%f Mop/sec, nerrs=%d\n",\
                   string, n, used, rate, nerrs);

  if (me == 0) {
    (void) printf("\nGlobal operations test\n----------------------");
    (void) printf("\n\nInput vector length ");
    (void) fflush(stdout);
  }
  n = GlobalReadInteger();
  if ( (n < 0) || (n > 1000000) )
    n = 1000;

  if (!(a = (double *) p4_shmalloc((unsigned) (n*sizeof(double)))))
    p4_error("failed to create work space (a)",n);
  if (!(b = (double *) p4_shmalloc((unsigned) (n*sizeof(double)))))
    p4_error("failed to create work space (b)",n);

  /* Summation */

  for (i=0; i<n; i++) {
    a[i] = i+me;
    b[i] = nproc*i + (nproc*(nproc-1))/2;
  }
  DO("Summation", p4_dbl_sum_op);

  /* Maximum */

  for (i=0; i<n; i++) {
    a[i] = i+me;
    b[i] = i+nproc-1;
  }
  DO("Maximum", p4_dbl_max_op);

  /* Abs Maximum */

  for (i=0; i<n; i++) {
    a[i] = i+me - n/2;
    b[i] = MAX2(n/2-i, i+nproc-1-n/2);
  }
  DO("Abs Maximum", p4_dbl_absmax_op);

  /* Tidy up */

  p4_shfree((char *) b);
  p4_shfree((char *) a);
}


void synchronize(type)
     int type;
/*
  Processes block until all have checked in with process 0
  with a message of specified type .. a barrier.
*/
{
  int me = p4_get_my_id();
  int nproc = p4_num_total_ids();
  int zero = 0;
  int *msg;
  int msg_len, node, dummy = type;

  if (me == zero) {
    for (node=1; node<nproc; node++){       /* Check in */
      msg = (int *) NULL;
      if (p4_recv(&type, &node, (char **) &msg, &msg_len))
	p4_error("synchronize: recv 1 failed", (int) msg);
      p4_msg_free((char *) msg);
    }
    if (p4_broadcast(type, (char *) &dummy, sizeof dummy))
      p4_error("synchronize: broadcast failed",type);
  }
  else {
    if (p4_send(type, zero, (char *) &me, sizeof me))
      p4_error("synchronize: send failed", type);
    msg = (int *) NULL;
    if (p4_recv(&type, &zero, (char **) &msg, &msg_len))
      p4_error("synchronize: recv 2 failed", (int) msg);
    p4_msg_free((char *) msg);
  }
}    
