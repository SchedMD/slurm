#include <stdio.h>
 
 
#define MAX_NUM_STATES 1000
#define MAX_NUM_OVERLAPPING_STATES 100
#define MAX_LINE_LEN 1000
#define MAX_STATE_NAME_LEN 1000
 
#define NPROCS_TYPE -3
#define STATEDEF_TYPE -13
 
#define DEBUG 0
 
typedef struct {
  char name[MAX_STATE_NAME_LEN];
  int start, end;
} state_def;
 
typedef struct {
  int stateNum;
  double startTime;
} state_list_item;
 
ReadHeader( fp, nprocs, line, stateDefs, nstates )
FILE *fp;
int *nprocs;
state_def *stateDefs;
int *nstates;
char *line;
{
  int type, data;
 
  type = -1;
  while( type<0 && fgets( line, MAX_LINE_LEN, fp ) ) {
    sscanf( line, "%d %*d %*d %d", &type, &data );
#if DEBUG >1
    fprintf( stderr, "line read: %s\n", line );
#endif
    if (type == NPROCS_TYPE) {
      *nprocs = data;
#if DEBUG
      fprintf( stderr, "# of processes: %d\n", *nprocs );
#endif
    } else if (type == STATEDEF_TYPE) {
      if (*nstates==MAX_NUM_STATES) {
	fprintf( stderr, "Too many state definitions.\n" );
      } else {
	sscanf( line, "%*d %*d %d %d %*d %*d %*s %[^\n]",
	      &(stateDefs[*nstates].start), &(stateDefs[*nstates].end),
	      stateDefs[*nstates].name );
#if DEBUG
	fprintf( stderr, "State: %s, %d %d\n", stateDefs[*nstates].name,
		stateDefs[*nstates].start, stateDefs[*nstates].end );
#endif
	(*nstates)++;
      }
    }
  }
}
 
 
StrContainsNonWhite( str )
char *str;
{
  while (*str) {
    if (!isspace( *str )) return 1;
    (*str)++;
  }
  return 0;
}
 
 
 
IsStateEvt( stateDefs, nstates, type, isStartEvt, stateNum )
state_def *stateDefs;
int type, *isStartEvt, *stateNum;
{
  int i;
  i = 0;
  while (i < nstates &&
	 stateDefs[i].start != type && stateDefs[i].end != type) i++;
  if (i==nstates) {
    return 0;
  } else {
    *isStartEvt = (stateDefs[i].start == type);
    *stateNum = i;
    return 1;
  }
}
  
    
 
 
ClearStates( states, nprocs )
state_list_item *states;
int nprocs;
{
  int n, i;
 
  n = nprocs * MAX_NUM_OVERLAPPING_STATES;
  for (i=0; i<n; i++) {
    states[i].stateNum = -1;
  }
}
 
 
StartState( procNum, stateNum, time, states )
int procNum, stateNum;
double time;
state_list_item *states;
{
  int i, offset;
 
  offset = procNum*MAX_NUM_OVERLAPPING_STATES;
  i=0;
  while (i<MAX_NUM_OVERLAPPING_STATES &&
	 states[offset+i].stateNum!=-1) {
#if DEBUG
    fprintf( stderr, "Slot %d full with %d\n", offset+i,
	    states[offset+i].stateNum );
#endif
    i++;
  }
  if (i==MAX_NUM_OVERLAPPING_STATES) {
    fprintf( stderr, "Too many overlapping states on process %d at %lf sec.\n",
	    procNum, time/1000000 );
  } else {
    i += offset;
    states[i].stateNum = stateNum;
    states[i].startTime = time;
#if DEBUG
    fprintf( stderr, "Put state %d on proc %d in slot %d\n", stateNum,
	    procNum, i );
#endif
  }
}
 
 
EndState( procNum, stateNum, time, states, stateTimes )
int procNum, stateNum;
double time, *stateTimes;
state_list_item *states;
{
  int i, offset, latestIdx;
  double latestTime;
 
  offset = procNum*MAX_NUM_OVERLAPPING_STATES;
  latestTime = -1;
 
  for( i=0; i<MAX_NUM_OVERLAPPING_STATES; i++) {
    if (states[offset+i].stateNum == stateNum) {
      if (states[offset+i].startTime > latestTime) {
	latestIdx = i;
	latestTime = states[offset+i].startTime;
      }
    }
  }
  if (latestTime==-1) {		/* no time was found for the start event */
    fprintf( stderr, "End of state without beginning on proc %d at %lf sec.\n",
	    procNum, time/1000000 );
  } else {
    i = offset+latestIdx;
    stateTimes[stateNum] += time-states[i].startTime;
    states[i].stateNum = -1;
#if DEBUG
    fprintf( stderr, "Removed state %d on proc %d from slot %d,\n", stateNum,
	    procNum, i );
    fprintf( stderr, "adding %lf seconds to the %d counter\n",
	    time-states[i].startTime, stateNum );
#endif
  }
}
 
 
PrintTimes( stateDefs, nstates, times )
state_def *stateDefs;
int nstates;
double *times;
{
  int i, longestName, ndigits;
  double total, temp;
  char formatString[100];
 
  total = 0;
 
  if (!nstates) {
    fprintf( stderr, "No states defined.\n" );
    exit( -3 );
  }
  longestName = 6;		/* "State:" */
  for (i=0; i<nstates; i++) {
    if (strlen( stateDefs[i].name ) > longestName) {
      longestName = strlen( stateDefs[i].name );
    }
    total += times[i];
  }
 
  ndigits = 8;
  temp = total;
  while (temp > 10000000.0) {
    temp /= 10;
    ndigits++;
  }
  
  sprintf( formatString, "%%-%ds  Time:\n", longestName );
  printf( formatString, "State:" );
  sprintf( formatString, "%%-%ds  %%%d.6lf\n", longestName, ndigits );
  for (i=0; i<nstates; i++) {
      if (times[i] > 0.0)
	  printf( formatString, stateDefs[i].name, times[i]/1000000 );
  }
  putchar( '\n' );
  printf( formatString, "Total:", total/1000000 );
}
 
 
 
ReadBody( fp, nprocs, line, stateDefs, nstates )
FILE *fp;
int nprocs;
char *line;
state_def *stateDefs;
int nstates;
{
  state_list_item *states;
  int i, type, procNum, isStartEvt, stateNum;
  double time, *stateTimes;
 
  stateTimes = (double *) malloc( nstates );
  states = (state_list_item *) malloc( sizeof( state_list_item ) *
				      MAX_NUM_OVERLAPPING_STATES * nprocs );
  ClearStates( states, nprocs );
  for (i=0; i<nstates; i++) stateTimes[i] = 0; /* clear times */
 
  do {
#if DEBUG > 1
    fprintf( stderr, "Read line: %s\n", line );
#endif
    if (StrContainsNonWhite( line )) {
      sscanf( line, "%d %d %*d %*d %*d %lf", &type, &procNum, &time );
      if (IsStateEvt( stateDefs, nstates, type, &isStartEvt, &stateNum )) {
#if DEBUG
	fprintf( stderr, "state defining type of %d at %lf\n", type, time );
#endif
	if (isStartEvt) {	/* start event */
	  StartState( procNum, stateNum, time, states );
	} else {		/* end event */
	  EndState( procNum, stateNum, time, states, stateTimes );
	}
      }
    }
  } while (fgets( line, MAX_LINE_LEN, fp ));
 
  PrintTimes( stateDefs, nstates, stateTimes );
}
 
 
main( argc, argv )
int argc;
char **argv;
{
  state_def stateDefs[MAX_NUM_STATES];
  int nstates, nprocs;
  FILE *fp;
  char line[MAX_LINE_LEN];
 
  if (argc!=2) {
    fprintf( stderr, "Syntax:\n    %s <log filename>\n\n", argv[0] );
    exit( -1 );
  }
 
  fp = fopen( argv[1], "r" );
  if (!fp) {
    fprintf( stderr, "Could not open %s.\n", argv[1] );
    exit( -2 );
  }
 
  ReadHeader( fp, &nprocs, line, stateDefs, &nstates );
  ReadBody( fp, nprocs, line, stateDefs, nstates );
}
 
