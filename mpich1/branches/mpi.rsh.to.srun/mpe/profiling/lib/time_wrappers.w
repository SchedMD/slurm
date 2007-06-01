#ifdef MPI_BUILD_PROFILING
#undef MPI_BUILD_PROFILING
#endif
#include <stdio.h>
#include "mpi.h"

static char *output_filename_{{fileno}};

{{forallfn fn_name}}static int ncalls_{{fn_name}}_{{fileno}}=0;
static double time_{{fn_name}}_{{fileno}}=0.0;
{{endforallfn}}


{{fn fn_name MPI_Init}}
  {{vardecl int myid, str_len}}
  {{vardecl char *progName}}
  
  {{callfn}}

  MPI_Comm_rank( MPI_COMM_WORLD, &{{myid}} );
  if ({{myid}} == 0) {
    {{progName}} = (*{{argv}})[0];
    {{str_len}} = strlen( {{progName}} ) + 1;
  }
  MPI_Bcast( &{{str_len}}, 1, MPI_INT, 0, MPI_COMM_WORLD );
  if ({{myid}}) {{progName}} = (char *) malloc( {{str_len}} );
  MPI_Bcast( {{progName}}, {{str_len}}, MPI_CHAR, 0, MPI_COMM_WORLD );
  output_filename_{{fileno}} = (char *) malloc( {{str_len}} + 20 );
  sprintf( output_filename_{{fileno}}, "%s_%d.prof", {{progName}}, {{myid}} );
{{endfn}}


{{fnall fn_name MPI_Init MPI_Finalize MPI_Wtime}}
  {{vardecl double startTime}}

  ncalls_{{fn_name}}_{{fileno}}++;
  {{startTime}} = MPI_Wtime();
  {{callfn}}
  time_{{fn_name}}_{{fileno}} += MPI_Wtime() - {{startTime}};
{{endfnall}}


{{fn fn_name MPI_Finalize}}
  {{vardecl FILE *outf}}

  {{outf}} = fopen( output_filename_{{fileno}}, "w" );
  if ({{outf}}) {
  {{forallfn dis_fn}}
    if (ncalls_{{dis_fn}}_{{fileno}}) {
      fprintf( outf, "{{dis_fn}}: %d calls, %.6lf sec.\n",
               ncalls_{{dis_fn}}_{{fileno}}, time_{{dis_fn}}_{{fileno}} );
    }
  {{endforallfn}}
    fclose( {{outf}} );
  }

  {{callfn}}

{{endfn}}
