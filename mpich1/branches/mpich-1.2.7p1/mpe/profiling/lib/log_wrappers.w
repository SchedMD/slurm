#ifdef MPI_BUILD_PROFILING
#undef MPI_BUILD_PROFILING
#endif
#include <stdio.h>
#include "mpi.h"
#include "mpe.h"

{{forallfn fn_name MPI_Init MPI_Finalize}}static int {{fn_name}}_stateid_{{fileno}},{{fn_name}}_ncalls_{{fileno}}=0;
{{endforallfn}}

static int procid_{{fileno}};
static char logFileName_{{fileno}}[256];

{{fn fn_name MPI_Init}}
  {{vardecl int stateid}}
  {{callfn}}

  MPE_Init_log();
  MPI_Comm_rank( MPI_COMM_WORLD, &procid_{{fileno}} );
  {{stateid}}=1;
  {{forallfn funcs MPI_Init MPI_Finalize}}{{funcs}}_stateid_{{fileno}} = {{stateid}}++;
  {{endforallfn}}
  sprintf( logFileName_{{fileno}}, "%s_profile.log", (*{{argv}})[0] );

  MPE_Start_log();
{{endfn}}


{{fnall fn_name MPI_Init MPI_Finalize}}
/*
    {{fn_name}} - prototyping replacement for {{fn_name}}
    Log the beginning and ending of the time spent in {{fn_name}} calls.
*/

  ++{{fn_name}}_ncalls_{{fileno}};
  MPE_Log_event( {{fn_name}}_stateid_{{fileno}}*2,
	         {{fn_name}}_ncalls_{{fileno}}, (char *)0 );
  {{callfn}}
  MPE_Log_event( {{fn_name}}_stateid_{{fileno}}*2+1,
	         {{fn_name}}_ncalls_{{fileno}}, (char *)0 );

{{endfnall}}



{{fn fn_name MPI_Finalize}}
/*
    MPI_Finalize - prototyping replacement for MPI_Finalize
*/

  if (procid_{{fileno}} == 0) {
    fprintf( stderr, "Writing logfile.\n");
    {{forallfn fn_name2 MPI_Init MPI_Finalize}}MPE_Describe_state( {{fn_name2}}_stateid_{{fileno}}*2,
	                            {{fn_name2}}_stateid_{{fileno}}*2+1,
      				    "{{fn_name2}}", ":" );
    {{endforallfn}}
  }
  MPE_Finish_log( logFileName_{{fileno}} );
  if (procid_{{fileno}} == 0)
    fprintf( stderr, "Finished writing logfile.\n");

  {{callfn}}
{{endfn}}

