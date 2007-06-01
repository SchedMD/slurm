#include <stdio.h>
#include "../usc.h"

#if defined(CAP2_HOST)
#include <chost.c7.h>
host_main()
#else
#if defined(CAP2_CELL)
cell_main()
#else
main()
#endif
#endif
{
	usc_init();
	printf("\nRollover Value = %lu\n", usc_rollover_val());

	printf("\n\nShort Loop Test:\n");
	printf("================\n\n");
	short_loop();

	printf("\n\nInfinite Loop Test (measures 5 second intervals):\n");
	printf("=================================================\n");
	printf("(***** Type ^C to terminate this test *****)\n\n");
	infinite_loop();
}


/* #define	MAX_LOOP	1000 */
#define	MAX_LOOP	50

short_loop()
{
	usc_time_t t1, t2, t3;
	usc_time_t t[MAX_LOOP];
	int i;

	for (i=0; i<MAX_LOOP; i++)
		t[i] = usc_clock();
	for (i=0; i<MAX_LOOP; i++)
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
