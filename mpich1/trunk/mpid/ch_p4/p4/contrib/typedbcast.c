#include "p4.h"

#define FINAL_BARRIER 4
#define MESSAGE       6
    
main(argc,argv)
int argc;
char **argv;
{
    
    p4_initenv(&argc,argv);
    if (p4_get_my_id() == 0)
        p4_create_procgroup();
    slave();
    p4_global_barrier(FINAL_BARRIER); /* broadcasts may be in progress */
    p4_wait_for_end();
}

slave()	
{
    int my_num, work_num;
    int mtyp = MESSAGE, dummy, len, i;
    int tempfrom;
    float flt;
    double dbl;
    long lng;

    char hname[100];
    int hlen = 100;

    gethostname(hname,hlen);
    my_num = p4_get_my_id();
    work_num = p4_num_total_ids();

    dummy = my_num;
    p4_broadcastx(mtyp, (char *) &dummy, sizeof(int), P4INT);
    CheckReceives( my_num, work_num );
    my_broadcastx( mtyp, (char *)&dummy, sizeof(int), P4INT);
    CheckReceives( my_num, work_num );
}


my_broadcastx( tag, buf, size, type )
int tag, size, type;
char *buf;
{
  int myid, np, i;

  myid = p4_get_my_id();
  np = p4_num_total_ids();

  for (i=0; i<np; i++) {
    if (i!=myid) {
      p4_sendx( tag, i, buf, size, type );
    }
  }
}


CheckReceives( my_num, work_num )
int my_num, work_num;
{
  int mtyp = MESSAGE, i, len;
  char *msg;

  for (i=0; i<work_num; i++)
    if (i != my_num)
    {
      msg = NULL;
      p4_recv(&mtyp, &i, &msg, &len);
      if (*((int *)msg) != i) {
	p4_dprintf( "%d received from %d %d, not %d.\n", my_num, i,
		   *((int *)msg), i );
      } else {
	p4_dprintf( "%d received from %d correctly.\n", my_num, i );
      }
    }
}
  
