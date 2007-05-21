#include <stdio.h>
#include "../usc.h"
#include "ccell.c7.h"
#include "../alog.h"

#define SLOOP_START 1
#define SLOOP_END   2
#define ILOOP_START 3
#define ILOOP_END   4

cell_main()
{
	int cellid;
	
	cellid = getcid();

	if (cellid == 0)
	{
	    ALOG_MASTER(0,ALOG_TRUNCATE);
	}
	else
	{
	    ALOG_SETUP(cellid,ALOG_TRUNCATE);
	}

	usc_init();
	printf("\nRollover Value = %lu\n", usc_rollover_val());

	printf("\n\nShort Loop Test:\n");
	printf("================\n\n");
	ALOG_LOG(cellid,SLOOP_START,0,"");
	short_loop();
	ALOG_LOG(cellid,SLOOP_END,0,"");

	printf("\n\nInfinite Loop Test (measures 5 second intervals):\n");
	printf("=================================================\n");
	printf("(***** Type ^C to terminate this test *****)\n\n");
	ALOG_LOG(cellid,ILOOP_START,0,"");
	infinite_loop();
	ALOG_LOG(cellid,ILOOP_END,0,"");

	ALOG_OUTPUT;
}


short_loop()
{
	usc_time_t t1, t2, t3;
	usc_time_t t[100];
	int i;

	for (i=0; i<100; i++)
		t[i] = usc_clock();
	for (i=0; i<100; i++)
		printf("Clock Reading %2d:  %lu\n", i+1, t[i]);

	printf("\nThree additional readings...just for the heck of it\n");
	t1 = usc_clock();
	t2 = usc_clock();
	t3 = usc_clock();
	printf("time1 = %lu,  time2 = %lu,  time3 = %lu\n", t1, t2, t3);
}


infinite_loop()
{
	usc_time_t t1, t2;
	int i,j;

	for(j=0;j < 10; j++)
	{
		t1 = usc_clock();
#if !defined(CAP2_CELL)

		sleep(5);
#else
		{
		double dgettime();
		double dinit;

		dinit = dgettime();
		while (dgettime() < dinit + 5)
		    ;
		}
#endif

		t2 = usc_clock();
		printf("Start_time = %lu    End_time = %lu\n\n", t1, t2);
		printf("---> Interval = %lu microsecs <---\n\n", t2-t1);
	}
}
