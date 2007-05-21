#include "mpi.h"

{{foreachfn fn_name MPI_Send MPI_Bsend MPI_Isend}}
static int {{fn_name}}_nsends_{{fileno}}; {{endforeachfn}}

{{forallfn fn_name MPI_Init MPI_Finalize MPI_Wtime}}int {{fn_name}}_ncalls_{{fileno}};
{{endforallfn}}

{{fnall this_fn_name MPI_Finalize}}
  {{vardecl int i}}
  printf( "{{this_fn_name}} is being called.\n" );

  {{callfn}}

  {{this_fn_name}}_ncalls_{{fileno}}++;
  printf( "{{i}} unused (%d).\n", {{i}} );
{{endfnall}}

{{fn fn_name MPI_Send MPI_Bsend MPI_Isend}}
  {{vardecl double i}}
  {{vardecl int typesize}}

  {{callfn}}

  MPI_Type_size( {{datatype}}, &{{typesize}} );
  MPE_Log_send( {{dest}}, {{tag}}, {{typesize}}*{{count}} );
  printf( "first argument is {{0}} and {{i}} went unused (%lf)\n", {{i}} );
  {{fn_name}}_nsends_{{fileno}}++;

{{endfn}}
