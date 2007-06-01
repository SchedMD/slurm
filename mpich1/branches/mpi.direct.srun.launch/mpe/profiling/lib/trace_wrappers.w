#ifdef MPI_BUILD_PROFILING
#undef MPI_BUILD_PROFILING
#endif
#include <stdio.h>
#include "mpi.h"

{{fnall fn_name}}
/*
    {{fn_name}} - prototyping replacement for {{fn_name}}
    Trace the beginning and ending of {{fn_name}}.
*/

  {{vardecl int llrank}}
  PMPI_Comm_rank( MPI_COMM_WORLD, &llrank );
  printf( "[%d] Starting {{fn_name}}...\n", llrank ); fflush( stdout );
  {{callfn}}
  printf( "[%d] Ending {{fn_name}}\n", llrank ); fflush( stdout );

{{endfnall}}



