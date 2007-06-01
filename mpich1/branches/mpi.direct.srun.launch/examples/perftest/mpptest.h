#ifndef _MPPTEST
#define _MPPTEST

#include "mpptestconf.h"

/* Definitions for pair-wise communication testing */
typedef double (*TimeFunction)( int, int, void * );

typedef struct _PairData *PairData;
/* Structure for the collective communication testing */
typedef struct {
    MPI_Comm pset;       /* Procset to test over */
    int     src;         /* Source (for scatter) */
    } GOPctx;

#define NO_NBR -1

/* size of the job and my rank in MPI_COMM_WORLD */
extern int __NUMNODES, __MYPROCID;

/* Function prototypes */
PairData PairInit( int, int );
PairData BisectInit( int );
void PrintPairInfo( PairData );
void PairChange( int, PairData );
void BisectChange( int, PairData );
int set_vector_stride( int );

void *GOPInit( int *, char ** );
void RunATest( int, int*, int*, double *, double *, int *, 
	       double (*)(int,int, void *),  
	       int, int, int, int, void *, void *);
void CheckTimeLimit( void );


double (*GetPairFunction( int *, char *[], char * ))(int, int, void *);
double (*GetGOPFunction( int*, char *[], char *, char *))(int, int, void *);
double (*GetHaloFunction( int *, char *[], void *, char * ))(int, int, void *);
int GetHaloPartners( void * );
void PrintHaloHelp( void );

/* copy.c : memcpy test */
double memcpy_rate( int, int, void *);
double memcpy_rate_int( int, int, void *);
double memcpy_rate_double( int, int, void *);
double memcpy_rate_long_long( int, int, void *);
double memcpy_rate_double_vector( int, int, void *);
double memcpy_rate_long_long_vector( int, int, void *);

/* Overlap testing */
typedef struct {
    int    proc1, proc2;
    int    MsgSize;                 /* Size of message in bytes */
    int    OverlapSize,             /* */
           OverlapLen,              /* */
           OverlapPos;              /* Location in buffers */
    double *Overlap1, *Overlap2;    /* Buffers */
    } OverlapData;

double round_trip_nb_overlap( int, int, void *);
double round_trip_b_overlap( int, int, void *);
void *OverlapInit( int, int, int );
void OverlapSizes( int, int [3], void *);

/* Graphics routines */
typedef struct _GraphData *GraphData;
/* Routine to generate graphics context */
GraphData SetupGraph( int *, char *[] );
void PrintGraphHelp( void );
void HeaderGraph( GraphData ctx, char *protocol_name, char *title_string, 
		  char *units );
void DrawGraph( GraphData ctx, int first, int last, double s, double r );
void RateoutputGraph( GraphData ctx, double sumlen, double sumtime, 
		      double sumlentime, double sumlen2, double sumtime2, 
		      int ntest, double *S, double *R );
void EndPageGraph( GraphData ctx );
void EndGraph( GraphData ctx );
void DataoutGraph( GraphData ctx, int proc1, int proc2, int distance, 
		   int len, double t, double mean_time, double rate,
		   double tmean, double tmax );
void DataScale( GraphData, int );
void DrawGraphGop( GraphData ctx, int first, int last, double s, double r, 
		   int nsizes, int *sizelist );
void HeaderForGopGraph( GraphData ctx, char *protocol_name, 
			char *title_string, char *units );
void DataoutGraphForGop( GraphData ctx, int len, double t, double mean_time, 
			 double rate, double tmean, double tmax );
void DataendForGop( GraphData ctx );
void DatabeginForGop( GraphData ctx, int np );

/* Global operations */
void PrintGOPHelp( void );

/* Patterns */
void PrintPatternHelp( void );
int GetNeighbor( int, int, int );
void SetPattern( int *, char *[] );
int GetMaxIndex( void );
int GetDestination( int, int, int );
int GetSource( int, int, int );

/* Prototypes */
double RunSingleTest( double (*)(int,int,void *), int, int, void *,
		      double *, double * );
void time_function( int, int, int, int, int, int, 
		    double (*)( int, int, void * ), void *,
		    int, int, double, void *);
void ClearTimes(void);

/* Rate */
void PIComputeRate( double sumlen, double sumtime, double sumlentime, 
		    double sumlen2, int ntest, double *s, double *r );

/* MPE Seq */
void MPE_Seq_begin( MPI_Comm, int );
void MPE_Seq_end( MPI_Comm, int );

/* gopf.c (goptest) */
void *GOPInit( int *, char ** );

#endif
