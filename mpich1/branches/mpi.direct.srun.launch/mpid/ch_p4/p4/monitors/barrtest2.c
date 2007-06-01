#include "p4.h"

double maxbar(), sumbar(), minbar();
main(argc,argv)
int argc;
char **argv;
{
    int i, j, n, myid, start, end;
    double x, ymin, ymax, ysum;
    unsigned long int starttime, endtime, onetime, manytime, manyp4time;
    unsigned long int maxtime, mintime, sumtime;
    void *bar;

    p4_initenv(&argc,argv);

    if (argc != 2)
        p4_error("must indicate total # procs on cmd line",(-1));
    else
        n = atoi(argv[1]);

    bar = initbar(&n);

    p4_create_procgroup();
    myid = p4_get_my_id();
    x = (double) myid;
    pidbar(bar,&myid);
    if (n != p4_num_cluster_ids())
        p4_error("number of procs mismatch",(-1));

    if (p4_get_my_id() == 100)
     sleep(5);

    starttime = p4_ustimer();
    for (i = 0; i < 1000; i++)
      waitbar(bar);
    endtime = p4_ustimer();
    manytime = endtime - starttime;

    starttime = p4_ustimer();
    for (i = 0; i < 1000; i++)
      ysum = sumbar(bar,&x);
    endtime = p4_ustimer();
    sumtime = endtime - starttime;

    p4_wait_for_end();

    if (myid == 0)
      {
        printf("time for 1000 barriers = %d microseconds\n",manytime);
	printf("time for 1000 sums (%f) =  %d microseconds\n",ysum,sumtime);
/*
	printf("time for 1000 maxs (%f) =  %d microseconds\n",ymax,maxtime);
	printf("time for 1000 mins (%f) =  %d microseconds\n",ymin,mintime);
*/
      }

}





