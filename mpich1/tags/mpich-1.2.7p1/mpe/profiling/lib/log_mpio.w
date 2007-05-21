/*
    This file should be INCLUDED into log_wrap.c when adding the IO routines
    to the profiling list

    Also set MPE_MAX_STATES to 180
 */

{{forallfn fn_name MPI_Init MPI_Finalize}}#define {{fn_name}}_ID {{fn_num}}
{{endforallfn}}

void MPE_Init_MPIIO( void )
{
  MPE_State *state;
  {{forallfn fn_name MPI_Init MPI_Finalize}}
  state = &states[{{fn_name}}_ID];
  state->kind_mask = MPE_KIND_FILE;
  state->name = "{{fn_name}}";
  state->color = "brown:gray2";
  {{endforallfn}}
}

{{fnall fn_name MPI_Init MPI_Finalize MPI_Register_datarep }}
/*
    {{fn_name}} - prototyping replacement for {{fn_name}}
    Log the beginning and ending of the time spent in {{fn_name}} calls.
*/
  MPE_LOG_STATE_DECL;

  MPE_LOG_STATE_BEGIN( {{fn_name}}_ID,MPI_COMM_NULL);
  {{callfn}}
  MPE_LOG_STATE_END( MPI_COMM_NULL );

{{endfnall}}
