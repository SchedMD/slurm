#include "mpeconf.h"
#include "clog_time.h"
#include "mpi.h"
#include <stdio.h>

static double clog_time_offset;

extern int MPE_Log_procid;

void CLOG_timeinit()
{
    double   local_time;
    int      flag, *is_globalp;

    PMPI_Initialized(&flag);
    if (!flag)
        PMPI_Init(0,0);

    PMPI_Attr_get( MPI_COMM_WORLD, MPI_WTIME_IS_GLOBAL, &is_globalp, &flag );

	/*
    printf("TaskID = %d : flag = %d, is_globalp = %d, *is_globalp = %d\n",
		   MPE_Log_procid, flag, is_globalp, *is_globalp);
	*/

    if ( !flag || (is_globalp && !*is_globalp) ) {
        /*  Clocks are NOT synchronized  */
        clog_time_offset = PMPI_Wtime();
		/*
        printf( "TaskID = %d : clog_time_offset = %.20E\n",
				MPE_Log_procid, clog_time_offset );
		*/
    }
    else {
        /*  Clocks are synchronized  */
        local_time = PMPI_Wtime();
        PMPI_Allreduce( &local_time, &clog_time_offset, 1, MPI_DOUBLE,
                        MPI_MAX, MPI_COMM_WORLD );
        /*  clog_time_offset should be a globally known value  */
		/*
        printf( "TaskID  = %d : "
				"local_time = %.20E,  clog_time_offset = %.20E\n",
			   	MPE_Log_procid, local_time, clog_time_offset );
		*/
    }
        
}

double CLOG_timestamp()
{
    return ( PMPI_Wtime() - clog_time_offset );
}
