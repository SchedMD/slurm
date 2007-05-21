/* This file contains code to generate a variety of MPI datatypes for testing
   the various MPI routines. 

 To simplify the test code, this generates an array of datatypes, buffers with
 data and buffers with no data (0 bits) for use in send and receive 
 routines of various types.

  In addition, this doesn't even test all of the possibilities.  For example,
  there is currently no test of sending more than one item defined with 
  MPI_Type_contiguous .

  This routine should be extended as time permits.

  Note also that this test assumes that the sending and receive types are
  the same.  MPI requires only that the type signatures match, which is
  a weaker requirement.

  THIS CODE IS FROM mpich/tsuite AND SHOULD BE CHANGED THERE ONLY
 */

#include "mpi.h"
#include <stdlib.h>
#include <stdio.h>
#include "dtypes.h"

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

/* Change this to test only the basic, predefined types */
static int basic_only = 0;

/* 
   Arrays types, inbufs, outbufs, and counts are allocated by the
   CALLER.  n on input is the maximum number; on output, it is the
   number defined .
   names contains a string identifying the test

   See AllocateForData below for a routine to allocate these arrays.

   We may want to add a routine to call to check that the proper data
   has been received.
 */
/* TYPECNT is the number of instances of each type in a test */
#define TYPECNT 10
#define SETUPBASICTYPE(mpi,c,name) { int i; c *a; \
if (cnt > *n) {*n = cnt; return; }\
types[cnt] = mpi; \
inbufs[cnt] = (void *)calloc( TYPECNT,sizeof(c) ); \
outbufs[cnt] = (void *)malloc( sizeof(c) * TYPECNT ); \
a = (c *)inbufs[cnt]; for (i=0; i<TYPECNT; i++) a[i] = i; \
a = (c *)outbufs[cnt]; for (i=0; i<TYPECNT; i++) a[i] = 0; \
names[cnt] = (char *)malloc(100);\
sprintf( names[cnt], "Basic type %s", name );\
counts[cnt]  = TYPECNT; bytesize[cnt] = sizeof(c) * TYPECNT; cnt++; }

#define SETUPCONTIGTYPE(mpi,c,name) { int i; c *a; \
if (cnt > *n) {*n = cnt; return; }\
MPI_Type_contiguous( TYPECNT, mpi, types + cnt );\
MPI_Type_commit( types + cnt );\
inbufs[cnt] = (void *)calloc( TYPECNT, sizeof(c) ); \
outbufs[cnt] = (void *)malloc( sizeof(c) * TYPECNT ); \
a = (c *)inbufs[cnt]; for (i=0; i<TYPECNT; i++) a[i] = i; \
a = (c *)outbufs[cnt]; for (i=0; i<TYPECNT; i++) a[i] = 0; \
names[cnt] = (char *)malloc(100);\
sprintf( names[cnt], "Contig type %s", name );\
counts[cnt]  = 1;  bytesize[cnt] = sizeof(c) * TYPECNT; cnt++; }

/* These are vectors of block length one.  */
#define STRIDE 9
#define SETUPVECTORTYPE(mpi,c,name) { int i; c *a; \
if (cnt > *n) {*n = cnt; return; }\
MPI_Type_vector( TYPECNT, 1, STRIDE, mpi, types + cnt );\
MPI_Type_commit( types + cnt );\
inbufs[cnt] = (void *)calloc( sizeof(c) * TYPECNT * STRIDE,1); \
outbufs[cnt] = (void *)calloc( sizeof(c) * TYPECNT * STRIDE,1); \
a = (c *)inbufs[cnt]; for (i=0; i<TYPECNT; i++) a[i*STRIDE] = i; \
a = (c *)outbufs[cnt]; for (i=0; i<TYPECNT; i++) a[i*STRIDE] = 0; \
names[cnt] = (char *)malloc(100);\
sprintf( names[cnt], "Vector type %s", name );\
counts[cnt]  = 1;  bytesize[cnt] = sizeof(c) * TYPECNT * STRIDE ;cnt++; }

/* This indexed type is setup like a contiguous type .
   Note that systems may try to convert this to contiguous, so we'll
   eventually need a test that has holes in it */
#define SETUPINDEXTYPE(mpi,c,name) { int i; int *lens, *disp; c *a; \
if (cnt > *n) {*n = cnt; return; }\
lens = (int *)malloc( TYPECNT * sizeof(int) ); \
disp = (int *)malloc( TYPECNT * sizeof(int) ); \
for (i=0; i<TYPECNT; i++) { lens[i] = 1; disp[i] = i; } \
MPI_Type_indexed( TYPECNT, lens, disp, mpi, types + cnt );\
free( lens ); free( disp ); \
MPI_Type_commit( types + cnt );\
inbufs[cnt] = (void *)calloc( TYPECNT, sizeof(c) ); \
outbufs[cnt] = (void *)malloc( sizeof(c) * TYPECNT ); \
a = (c *)inbufs[cnt]; for (i=0; i<TYPECNT; i++) a[i] = i; \
a = (c *)outbufs[cnt]; for (i=0; i<TYPECNT; i++) a[i] = 0; \
names[cnt] = (char *)malloc(100);\
sprintf( names[cnt], "Index type %s", name );\
counts[cnt]  = 1;  bytesize[cnt] = sizeof(c) * TYPECNT; cnt++; }

/* This defines a structure of two basic members; by chosing things like
   (char, double), various packing and alignment tests can be made */
#define SETUPSTRUCT2TYPE(mpi1,c1,mpi2,c2,name,tname) { int i; \
MPI_Datatype b[3]; int cnts[3]; \
struct name { c1 a1; c2 a2; } *a, samp; \
MPI_Aint disp[3]; \
b[0] = mpi1; b[1] = mpi2; b[2] = MPI_UB;\
cnts[0] = 1; cnts[1] = 1; cnts[2] = 1;\
MPI_Address( &(samp.a2), &disp[1] ); \
MPI_Address( &(samp.a1), &disp[0] ); \
MPI_Address( &(samp) + 1, &disp[2] ); \
disp[1] = disp[1] - disp[0]; disp[2] = disp[2] - disp[0]; disp[0] = 0; \
if (cnt > *n) {*n = cnt; return; }\
MPI_Type_struct( 3, cnts, disp, b, types + cnt );\
MPI_Type_commit( types + cnt );\
inbufs[cnt] = (void *)calloc( sizeof(struct name) * TYPECNT,1); \
outbufs[cnt] = (void *)calloc( sizeof(struct name) * TYPECNT,1); \
a = (struct name *)inbufs[cnt]; for (i=0; i<TYPECNT; i++) { a[i].a1 = i; \
 a[i].a2 = i; } \
a = (struct name *)outbufs[cnt]; for (i=0; i<TYPECNT; i++) { a[i].a1 = 0; \
 a[i].a2 = 0; } \
names[cnt] = (char *)malloc(100);\
sprintf( names[cnt], "Struct type %s", tname );\
counts[cnt]  = TYPECNT;  bytesize[cnt] = sizeof(struct name) * TYPECNT;cnt++; }

/* This accomplished the same effect as VECTOR, but allow a count of > 1 */
#define SETUPSTRUCTTYPEUB(mpi,c,name) { int i; c *a; \
int blens[2];  MPI_Aint disps[2]; MPI_Datatype mtypes[2]; \
if (cnt > *n) {*n = cnt; return; }\
blens[0] = 1; blens[1] = 1; disps[0] = 0; disps[1] = STRIDE * sizeof(c); \
mtypes[0] = mpi; mtypes[1] = MPI_UB; \
MPI_Type_struct( 2, blens, disps, mtypes, types + cnt );\
MPI_Type_commit( types + cnt );\
inbufs[cnt] = (void *)calloc( sizeof(c) * TYPECNT * STRIDE,1); \
outbufs[cnt] = (void *)calloc( sizeof(c) * TYPECNT * STRIDE,1); \
a = (c *)inbufs[cnt]; for (i=0; i<TYPECNT; i++) a[i*STRIDE] = i; \
a = (c *)outbufs[cnt]; for (i=0; i<TYPECNT; i++) a[i*STRIDE] = 0; \
names[cnt] = (char *)malloc(100);\
sprintf( names[cnt], "Struct (MPI_UB) type %s", name );\
counts[cnt]  = TYPECNT;  bytesize[cnt] = sizeof(c) * TYPECNT * STRIDE;cnt++; }

/* 
 * Set whether only the basic types should be generated
 */
void BasicDatatypesOnly( void )
{
    basic_only = 1;
}

static int nbasic_types = 0;
/* On input, n is the size of the various buffers.  On output, 
   it is the number available types 
 */
void GenerateData( MPI_Datatype *types, void **inbufs, void **outbufs, 
		   int *counts, int *bytesize, char **names, int *n )
{
int cnt = 0;   /* Number of defined types */

/* First, generate an element of each basic type */
SETUPBASICTYPE(MPI_CHAR,char,"MPI_CHAR");
SETUPBASICTYPE(MPI_SHORT,short,"MPI_SHORT");
SETUPBASICTYPE(MPI_INT,int,"MPI_INT");
SETUPBASICTYPE(MPI_LONG,long,"MPI_LONG");
SETUPBASICTYPE(MPI_UNSIGNED_CHAR,unsigned char,"MPI_UNSIGNED_CHAR");
SETUPBASICTYPE(MPI_UNSIGNED_SHORT,unsigned short,"MPI_UNSIGNED_SHORT");
SETUPBASICTYPE(MPI_UNSIGNED,unsigned,"MPI_UNSIGNED");
SETUPBASICTYPE(MPI_UNSIGNED_LONG,unsigned long,"MPI_UNSIGNED_LONG");
SETUPBASICTYPE(MPI_FLOAT,float,"MPI_FLOAT");
SETUPBASICTYPE(MPI_DOUBLE,double,"MPI_DOUBLE");
SETUPBASICTYPE(MPI_BYTE,char,"MPI_BYTE");
#ifdef HAVE_LONG_LONG_INT
SETUPBASICTYPE(MPI_LONG_LONG_INT,long long,"MPI_LONG_LONG_INT");
#endif
#ifdef HAVE_LONG_DOUBLE
SETUPBASICTYPE(MPI_LONG_DOUBLE,long double,"MPI_LONG_DOUBLE");
#endif
nbasic_types = cnt;

 if (basic_only) {
     *n = cnt;
     return;
 }
/* Generate contiguous data items */
SETUPCONTIGTYPE(MPI_CHAR,char,"MPI_CHAR");
SETUPCONTIGTYPE(MPI_SHORT,short,"MPI_SHORT");
SETUPCONTIGTYPE(MPI_INT,int,"MPI_INT");
SETUPCONTIGTYPE(MPI_LONG,long,"MPI_LONG");
SETUPCONTIGTYPE(MPI_UNSIGNED_CHAR,unsigned char,"MPI_UNSIGNED_CHAR");
SETUPCONTIGTYPE(MPI_UNSIGNED_SHORT,unsigned short,"MPI_UNSIGNED_SHORT");
SETUPCONTIGTYPE(MPI_UNSIGNED,unsigned,"MPI_UNSIGNED");
SETUPCONTIGTYPE(MPI_UNSIGNED_LONG,unsigned long,"MPI_UNSIGNED_LONG");
SETUPCONTIGTYPE(MPI_FLOAT,float,"MPI_FLOAT");
SETUPCONTIGTYPE(MPI_DOUBLE,double,"MPI_DOUBLE");
SETUPCONTIGTYPE(MPI_BYTE,char,"MPI_BYTE");
#ifdef HAVE_LONG_LONG_INT
SETUPCONTIGTYPE(MPI_LONG_LONG_INT,long long,"MPI_LONG_LONG_INT");
#endif
#ifdef HAVE_LONG_DOUBLE
SETUPCONTIGTYPE(MPI_LONG_DOUBLE,long double,"MPI_LONG_DOUBLE");
#endif

/* Generate vector items */
SETUPVECTORTYPE(MPI_CHAR,char,"MPI_CHAR");
SETUPVECTORTYPE(MPI_SHORT,short,"MPI_SHORT");
SETUPVECTORTYPE(MPI_INT,int,"MPI_INT");
SETUPVECTORTYPE(MPI_LONG,long,"MPI_LONG");
SETUPVECTORTYPE(MPI_UNSIGNED_CHAR,unsigned char,"MPI_UNSIGNED_CHAR");
SETUPVECTORTYPE(MPI_UNSIGNED_SHORT,unsigned short,"MPI_UNSIGNED_SHORT");
SETUPVECTORTYPE(MPI_UNSIGNED,unsigned,"MPI_UNSIGNED");
SETUPVECTORTYPE(MPI_UNSIGNED_LONG,unsigned long,"MPI_UNSIGNED_LONG");
SETUPVECTORTYPE(MPI_FLOAT,float,"MPI_FLOAT");
SETUPVECTORTYPE(MPI_DOUBLE,double,"MPI_DOUBLE");
SETUPVECTORTYPE(MPI_BYTE,char,"MPI_BYTE");
#ifdef HAVE_LONG_LONG_INT
SETUPVECTORTYPE(MPI_LONG_LONG_INT,long long,"MPI_LONG_LONG_INT");
#endif
#ifdef HAVE_LONG_DOUBLE
SETUPVECTORTYPE(MPI_LONG_DOUBLE,long double,"MPI_LONG_DOUBLE");
#endif

/* Generate indexed items */
SETUPINDEXTYPE(MPI_CHAR,char,"MPI_CHAR");
SETUPINDEXTYPE(MPI_SHORT,short,"MPI_SHORT");
SETUPINDEXTYPE(MPI_INT,int,"MPI_INT");
SETUPINDEXTYPE(MPI_LONG,long,"MPI_LONG");
SETUPINDEXTYPE(MPI_UNSIGNED_CHAR,unsigned char,"MPI_UNSIGNED_CHAR");
SETUPINDEXTYPE(MPI_UNSIGNED_SHORT,unsigned short,"MPI_UNSIGNED_SHORT");
SETUPINDEXTYPE(MPI_UNSIGNED,unsigned,"MPI_UNSIGNED");
SETUPINDEXTYPE(MPI_UNSIGNED_LONG,unsigned long,"MPI_UNSIGNED_LONG");
SETUPINDEXTYPE(MPI_FLOAT,float,"MPI_FLOAT");
SETUPINDEXTYPE(MPI_DOUBLE,double,"MPI_DOUBLE");
SETUPINDEXTYPE(MPI_BYTE,char,"MPI_BYTE");
#ifdef HAVE_LONG_LONG_INT
SETUPINDEXTYPE(MPI_LONG_LONG_INT,long long,"MPI_LONG_LONG_INT");
#endif
#ifdef HAVE_LONG_DOUBLE
SETUPINDEXTYPE(MPI_LONG_DOUBLE,long double,"MPI_LONG_DOUBLE");
#endif

/* Generate struct items */ 
SETUPSTRUCT2TYPE(MPI_CHAR,char,MPI_DOUBLE,double,d1,"char-double")
SETUPSTRUCT2TYPE(MPI_DOUBLE,double,MPI_CHAR,char,d2,"double-char")
SETUPSTRUCT2TYPE(MPI_UNSIGNED,unsigned,MPI_DOUBLE,double,d3,"unsigned-double")
SETUPSTRUCT2TYPE(MPI_FLOAT,float,MPI_LONG,long,d4,"float-long")
SETUPSTRUCT2TYPE(MPI_UNSIGNED_CHAR,unsigned char,MPI_CHAR,char,d5,
  "unsigned char-char")
SETUPSTRUCT2TYPE(MPI_UNSIGNED_SHORT,unsigned short,MPI_DOUBLE,double,d6,
  "unsigned short-double")

/* Generate struct using MPI_UB */
SETUPSTRUCTTYPEUB(MPI_CHAR,char,"MPI_CHAR");
SETUPSTRUCTTYPEUB(MPI_SHORT,short,"MPI_SHORT");
SETUPSTRUCTTYPEUB(MPI_INT,int,"MPI_INT");
SETUPSTRUCTTYPEUB(MPI_LONG,long,"MPI_LONG");
SETUPSTRUCTTYPEUB(MPI_UNSIGNED_CHAR,unsigned char,"MPI_UNSIGNED_CHAR");
SETUPSTRUCTTYPEUB(MPI_UNSIGNED_SHORT,unsigned short,"MPI_UNSIGNED_SHORT");
SETUPSTRUCTTYPEUB(MPI_UNSIGNED,unsigned,"MPI_UNSIGNED");
SETUPSTRUCTTYPEUB(MPI_UNSIGNED_LONG,unsigned long,"MPI_UNSIGNED_LONG");
SETUPSTRUCTTYPEUB(MPI_FLOAT,float,"MPI_FLOAT");
SETUPSTRUCTTYPEUB(MPI_DOUBLE,double,"MPI_DOUBLE");
SETUPSTRUCTTYPEUB(MPI_BYTE,char,"MPI_BYTE");

/* 60 different entries to this point + 4 for long long and 
   4 for long double */
*n = cnt;
}

/* 
   MAX_TEST should be 1 + actual max (allows us to check that it was, 
   indeed, large enough) 
 */
#define MAX_TEST 70
void AllocateForData( MPI_Datatype **types, void ***inbufs, void ***outbufs, 
		      int **counts, int **bytesize, char ***names, int *n )
{
    *types    = (MPI_Datatype *)malloc( MAX_TEST * sizeof(MPI_Datatype) );
    *inbufs   = (void **) malloc( MAX_TEST * sizeof(void *) );
    *outbufs  = (void **) malloc( MAX_TEST * sizeof(void *) );
    *names    = (char **) malloc( MAX_TEST * sizeof(char *) );
    *counts   = (int *)   malloc( MAX_TEST * sizeof(int) );
    *bytesize = (int *)   malloc( MAX_TEST * sizeof(int) );
    *n	      = MAX_TEST;
}

int CheckData( void *inbuf, void *outbuf, int size_bytes )
{
    char *in = (char *)inbuf, *out = (char *)outbuf;
    int  i;
    for (i=0; i<size_bytes; i++) {
	if (in[i] != out[i]) {
	    return i + 1;
	}
    }
    return 0;
}

/* 
 * This is a version of CheckData that prints error messages
 */
int CheckDataAndPrint( void *inbuf, void *outbuf, int size_bytes, 
		       char *typename, int typenum )
{
    int errloc, world_rank;
    
    if ((errloc = CheckData( inbuf, outbuf, size_bytes ))) {
	char *p1, *p2;
	MPI_Comm_rank( MPI_COMM_WORLD, &world_rank );
	fprintf( stderr, 
	 "Error in data with type %s (type %d on %d) at byte %d of %d\n", 
		 typename, typenum, world_rank, errloc - 1, size_bytes );
	p1 = (char *)inbuf;
	p2 = (char *)outbuf;
	fprintf( stderr, 
		 "Got %x expected %x\n", p2[errloc-1], p1[errloc-1] );
#if 0
	MPIR_PrintDatatypeUnpack( stderr, counts[j], types[j], 
				  0, 0 );
#endif
    }
    return errloc;
}

void FreeDatatypes( MPI_Datatype *types, void **inbufs, void **outbufs, 
		    int *counts, int *bytesize, char **names, int n )
{
    int i;
    for (i=0; i<n; i++) {
	if (inbufs[i]) 
	    free( inbufs[i] );
	if (outbufs[i]) 
	    free( outbufs[i] );
	free( names[i] );
	/* Only if not basic ... */
	if (i >= nbasic_types) 
	    MPI_Type_free( types + i );
    }
    free( inbufs );
    free( outbufs );
    free( names );
    free( counts );
    free( bytesize );
}
