#include "mpi.h"
#include "mpe.h"

#ifdef MPI_BUILD_PROFILING
#undef MPI_BUILD_PROFILING
#endif

static char *logname_{{fileno}};

int prof_send( sender, receiver, tag, size, note )
int sender, receiver, tag, size;
char *note;
{
  MPE_Log_send( receiver, tag, size );
}

int prof_recv( receiver, sender, tag, size, note )
int sender, receiver, tag, size;
char *note;
{
  MPE_Log_receive( sender, tag, size );
}

{{fn fn_name MPI_Init}}
  {{callfn}}
  MPE_Init_log();
  logname_{{fileno}} = (char *)malloc( strlen({{argv}}[0]) + 10 );
  strcpy( logname_{{fileno}}, {{argv}}[0] );
  strcat( logname_{{fileno}}, ".log" );
{{endfn}}

{{fn fn_name MPI_Finalize}}
  MPE_Finish_log( logname_{{fileno}} );
  {{callfn}}
{{endfn}}
