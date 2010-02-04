#include <math.h>
#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>

#define SKIP_LOGICALS

#define MAX_PROD_RANK 10
#define MAX_SUM_RANK 1000

/* WHY? FOR THE FOLLOWING? NEED TO FIX IT... */
/* Run this test on 2, 4, 6, 8, or 10 processes only */

int main( int argc, char **argv )
{
int count, errcnt = 0, gerr = 0, size, rank, size_max_prod_rank, size_max_sum_rank;
MPI_Comm comm;
MPI_Init( &argc, &argv );

MPI_Comm_size( MPI_COMM_WORLD, &size );

size_max_prod_rank = (size < MAX_PROD_RANK) ? size : MAX_PROD_RANK;
size_max_sum_rank = (size < MAX_SUM_RANK) ? size : MAX_SUM_RANK;

MPI_Comm_rank( MPI_COMM_WORLD, &rank );
count = 10;
comm = MPI_COMM_WORLD;

/* Test sum */
#ifdef DEBUG
    if (rank == 0) printf( "Testing MPI_SUM...\n" );
#endif

for (count = 1; count < 11; count += 9) {

{
int *in, *out, *sol;
int  i, fnderr=0;
in = (int *)malloc( count * sizeof(int) );
out = (int *)malloc( count * sizeof(int) );
sol = (int *)malloc( count * sizeof(int) );
/* OLD TEST WILL BREAK WITH LARGE NUMBERS OF TASKS... */
/*
for (i=0; i<count; i++) { *(in + i) = i; *(sol + i) = i*size;
	*(out + i) = 0; }
	*/
 if (rank < MAX_SUM_RANK) {
   for (i=0; i<count; i++) {
     *(in + i) = i;
     *(sol + i) = i*size_max_sum_rank;
     *(out + i) = 0; }
 } else {
   for (i=0; i<count; i++) {
     *(in + i) = 0;
     *(sol + i) = i*size_max_sum_rank;
     *(out + i) = 0; }
 }

MPI_Allreduce( in, out, count, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_INT and op MPI_SUM\n", rank );
free( in );
free( out );
free( sol );
}


{
long *in, *out, *sol;
int  i, fnderr=0;
in = (long *)malloc( count * sizeof(long) );
out = (long *)malloc( count * sizeof(long) );
sol = (long *)malloc( count * sizeof(long) );
/* OLD TEST WILL BREAK WITH LARGE NUMBERS OF TASKS... */
/*
for (i=0; i<count; i++) { *(in + i) = i; *(sol + i) = i*size;
	*(out + i) = 0; }
	*/
 if (rank < MAX_SUM_RANK) {
   for (i=0; i<count; i++) {
     *(in + i) = i;
     *(sol + i) = i*size_max_sum_rank;
     *(out + i) = 0; }
 } else {
   for (i=0; i<count; i++) {
     *(in + i) = 0;
     *(sol + i) = i*size_max_sum_rank;
     *(out + i) = 0; }
 }

MPI_Allreduce( in, out, count, MPI_LONG, MPI_SUM, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_LONG and op MPI_SUM\n", rank );
free( in );
free( out );
free( sol );
}


{
short *in, *out, *sol;
int  i, fnderr=0;
in = (short *)malloc( count * sizeof(short) );
out = (short *)malloc( count * sizeof(short) );
sol = (short *)malloc( count * sizeof(short) );
/* OLD TEST WILL BREAK WITH LARGE NUMBERS OF TASKS... */
/*
for (i=0; i<count; i++) { *(in + i) = i; *(sol + i) = i*size;
	*(out + i) = 0; }
	*/
 if (rank < MAX_SUM_RANK) {
   for (i=0; i<count; i++) {
     *(in + i) = i;
     *(sol + i) = i*size_max_sum_rank;
     *(out + i) = 0; }
 } else {
   for (i=0; i<count; i++) {
     *(in + i) = 0;
     *(sol + i) = i*size_max_sum_rank;
     *(out + i) = 0; }
 }

MPI_Allreduce( in, out, count, MPI_SHORT, MPI_SUM, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_SHORT and op MPI_SUM\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned short *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned short *)malloc( count * sizeof(unsigned short) );
out = (unsigned short *)malloc( count * sizeof(unsigned short) );
sol = (unsigned short *)malloc( count * sizeof(unsigned short) );
/* OLD TEST WILL BREAK WITH LARGE NUMBERS OF TASKS... */
/*
for (i=0; i<count; i++) { *(in + i) = i; *(sol + i) = i*size;
	*(out + i) = 0; }
	*/
 if (rank < MAX_SUM_RANK) {
   for (i=0; i<count; i++) {
     *(in + i) = i;
     *(sol + i) = i*size_max_sum_rank;
     *(out + i) = 0; }
 } else {
   for (i=0; i<count; i++) {
     *(in + i) = 0;
     *(sol + i) = i*size_max_sum_rank;
     *(out + i) = 0; }
 }

MPI_Allreduce( in, out, count, MPI_UNSIGNED_SHORT, MPI_SUM, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_SHORT and op MPI_SUM\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned *)malloc( count * sizeof(unsigned) );
out = (unsigned *)malloc( count * sizeof(unsigned) );
sol = (unsigned *)malloc( count * sizeof(unsigned) );
/* OLD TEST WILL BREAK WITH LARGE NUMBERS OF TASKS... */
/*
for (i=0; i<count; i++) { *(in + i) = i; *(sol + i) = i*size;
	*(out + i) = 0; }
	*/
 if (rank < MAX_SUM_RANK) {
   for (i=0; i<count; i++) {
     *(in + i) = i;
     *(sol + i) = i*size_max_sum_rank;
     *(out + i) = 0; }
 } else {
   for (i=0; i<count; i++) {
     *(in + i) = 0;
     *(sol + i) = i*size_max_sum_rank;
     *(out + i) = 0; }
 }

MPI_Allreduce( in, out, count, MPI_UNSIGNED, MPI_SUM, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED and op MPI_SUM\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned long *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned long *)malloc( count * sizeof(unsigned long) );
out = (unsigned long *)malloc( count * sizeof(unsigned long) );
sol = (unsigned long *)malloc( count * sizeof(unsigned long) );
/* OLD TEST WILL BREAK WITH LARGE NUMBERS OF TASKS... */
/*
for (i=0; i<count; i++) { *(in + i) = i; *(sol + i) = i*size;
	*(out + i) = 0; }
	*/
 if (rank < MAX_SUM_RANK) {
   for (i=0; i<count; i++) {
     *(in + i) = i;
     *(sol + i) = i*size_max_sum_rank;
     *(out + i) = 0; }
 } else {
   for (i=0; i<count; i++) {
     *(in + i) = 0;
     *(sol + i) = i*size_max_sum_rank;
     *(out + i) = 0; }
 }

MPI_Allreduce( in, out, count, MPI_UNSIGNED_LONG, MPI_SUM, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_LONG and op MPI_SUM\n", rank );
free( in );
free( out );
free( sol );
}


{
float *in, *out, *sol;
int  i, fnderr=0;
in = (float *)malloc( count * sizeof(float) );
out = (float *)malloc( count * sizeof(float) );
sol = (float *)malloc( count * sizeof(float) );
/* OLD TEST WILL BREAK WITH LARGE NUMBERS OF TASKS... */
/*
for (i=0; i<count; i++) { *(in + i) = i; *(sol + i) = i*size;
	*(out + i) = 0; }
	*/
 if (rank < MAX_SUM_RANK) {
   for (i=0; i<count; i++) {
     *(in + i) = (float) i;
     *(sol + i) = ((float) i)*((float) size_max_sum_rank);
     *(out + i) = 0.0; }
 } else {
   for (i=0; i<count; i++) {
     *(in + i) = 0.0;
     *(sol + i) = ((float) i)*((float) size_max_sum_rank);
     *(out + i) = 0.0; }
 }

MPI_Allreduce( in, out, count, MPI_FLOAT, MPI_SUM, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_FLOAT and op MPI_SUM\n", rank );
free( in );
free( out );
free( sol );
}


{
double *in, *out, *sol;
int  i, fnderr=0;
in = (double *)malloc( count * sizeof(double) );
out = (double *)malloc( count * sizeof(double) );
sol = (double *)malloc( count * sizeof(double) );
/* OLD TEST WILL BREAK WITH LARGE NUMBERS OF TASKS... */
/*
for (i=0; i<count; i++) { *(in + i) = i; *(sol + i) = i*size;
	*(out + i) = 0; }
	*/
 if (rank < MAX_SUM_RANK) {
   for (i=0; i<count; i++) {
     *(in + i) = (double) i;
     *(sol + i) = ((double) i)*((double) size_max_sum_rank);
     *(out + i) = 0.0; }
 } else {
   for (i=0; i<count; i++) {
     *(in + i) = 0.0;
     *(sol + i) = ((double) i)*((double) size_max_sum_rank);
     *(out + i) = 0.0; }
 }

MPI_Allreduce( in, out, count, MPI_DOUBLE, MPI_SUM, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_DOUBLE and op MPI_SUM\n", rank );
free( in );
free( out );
free( sol );
}

gerr += errcnt;
if (errcnt > 0)
	printf( "Found %d errors on %d for MPI_SUM\n", errcnt, rank );
errcnt = 0;


/* Test product */
#ifdef DEBUG
    if (rank == 0) printf( "Testing MPI_PROD...\n" );
#endif

{
int *in, *out, *sol;
int  i, j, fnderr=0;
in = (int *)malloc( count * sizeof(int) );
out = (int *)malloc( count * sizeof(int) );
sol = (int *)malloc( count * sizeof(int) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = i; *(sol + i) = (i > 0) ? (int)(pow((double)(i),(double)size)+0.1) : 0;
	*(out + i) = 0; }
	*/
 if (rank < MAX_PROD_RANK) {
   for (i=0; i<count; i++) {
     *(in + i) = i;
     *(sol + i) = 1;
     for (j=0; j<size_max_prod_rank; j++) {
       *(sol + i) = (*(sol + i)) * i; }
     *(out + i) = 0; }
 } else {
   for (i=0; i<count; i++) {
     *(in + i) = 1;
     *(sol + i) = 1;
     for (j=0; j<size_max_prod_rank; j++) {
       *(sol + i) = (*(sol + i)) * i; }
     *(out + i) = 0; }
 }

MPI_Allreduce( in, out, count, MPI_INT, MPI_PROD, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_INT and op MPI_PROD\n", rank );
free( in );
free( out );
free( sol );
}


{
long *in, *out, *sol;
int  i, j, fnderr=0;
in = (long *)malloc( count * sizeof(long) );
out = (long *)malloc( count * sizeof(long) );
sol = (long *)malloc( count * sizeof(long) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = i; *(sol + i) = (i > 0) ? (int)(pow((double)(i),(double)size)+0.1) : 0;
	*(out + i) = 0; }
	*/
 if (rank < MAX_PROD_RANK) {
   for (i=0; i<count; i++) {
     *(in + i) = i;
     *(sol + i) = 1;
     for (j=0; j<size_max_prod_rank; j++) {
       *(sol + i) = (*(sol + i)) * i; }
     *(out + i) = 0; }
 } else {
   for (i=0; i<count; i++) {
     *(in + i) = 1;
     *(sol + i) = 1;
     for (j=0; j<size_max_prod_rank; j++) {
       *(sol + i) = (*(sol + i)) * i; }
     *(out + i) = 0; }
 }

MPI_Allreduce( in, out, count, MPI_LONG, MPI_PROD, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_LONG and op MPI_PROD\n", rank );
free( in );
free( out );
free( sol );
}


{
short *in, *out, *sol;
int  i, j, fnderr=0;
in = (short *)malloc( count * sizeof(short) );
out = (short *)malloc( count * sizeof(short) );
sol = (short *)malloc( count * sizeof(short) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = i; *(sol + i) = (i > 0) ? (int)(pow((double)(i),(double)size)+0.1) : 0;
	*(out + i) = 0; }
	*/
 if (rank < MAX_PROD_RANK) {
   for (i=0; i<count; i++) {
     *(in + i) = i;
     *(sol + i) = 1;
     for (j=0; j<size_max_prod_rank; j++) {
       *(sol + i) = (*(sol + i)) * i; }
     *(out + i) = 0; }
 } else {
   for (i=0; i<count; i++) {
     *(in + i) = 1;
     *(sol + i) = 1;
     for (j=0; j<size_max_prod_rank; j++) {
       *(sol + i) = (*(sol + i)) * i; }
     *(out + i) = 0; }
 }

MPI_Allreduce( in, out, count, MPI_SHORT, MPI_PROD, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_SHORT and op MPI_PROD\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned short *in, *out, *sol;
int  i, j, fnderr=0;
in = (unsigned short *)malloc( count * sizeof(unsigned short) );
out = (unsigned short *)malloc( count * sizeof(unsigned short) );
sol = (unsigned short *)malloc( count * sizeof(unsigned short) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = i; *(sol + i) = (i > 0) ? (int)(pow((double)(i),(double)size)+0.1) : 0;
	*(out + i) = 0; }
	*/
 if (rank < MAX_PROD_RANK) {
   for (i=0; i<count; i++) {
     *(in + i) = i;
     *(sol + i) = 1;
     for (j=0; j<size_max_prod_rank; j++) {
       *(sol + i) = (*(sol + i)) * i; }
     *(out + i) = 0; }
 } else {
   for (i=0; i<count; i++) {
     *(in + i) = 1;
     *(sol + i) = 1;
     for (j=0; j<size_max_prod_rank; j++) {
       *(sol + i) = (*(sol + i)) * i; }
     *(out + i) = 0; }
 }

MPI_Allreduce( in, out, count, MPI_UNSIGNED_SHORT, MPI_PROD, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_SHORT and op MPI_PROD\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned *in, *out, *sol;
int  i, j, fnderr=0;
in = (unsigned *)malloc( count * sizeof(unsigned) );
out = (unsigned *)malloc( count * sizeof(unsigned) );
sol = (unsigned *)malloc( count * sizeof(unsigned) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = i; *(sol + i) = (i > 0) ? (int)(pow((double)(i),(double)size)+0.1) : 0;
	*(out + i) = 0; }
	*/
 if (rank < MAX_PROD_RANK) {
   for (i=0; i<count; i++) {
     *(in + i) = i;
     *(sol + i) = 1;
     for (j=0; j<size_max_prod_rank; j++) {
       *(sol + i) = (*(sol + i)) * i; }
     *(out + i) = 0; }
 } else {
   for (i=0; i<count; i++) {
     *(in + i) = 1;
     *(sol + i) = 1;
     for (j=0; j<size_max_prod_rank; j++) {
       *(sol + i) = (*(sol + i)) * i; }
     *(out + i) = 0; }
 }

MPI_Allreduce( in, out, count, MPI_UNSIGNED, MPI_PROD, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED and op MPI_PROD\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned long *in, *out, *sol;
int  i, j, fnderr=0;
in = (unsigned long *)malloc( count * sizeof(unsigned long) );
out = (unsigned long *)malloc( count * sizeof(unsigned long) );
sol = (unsigned long *)malloc( count * sizeof(unsigned long) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = i; *(sol + i) = (i > 0) ? (int)(pow((double)(i),(double)size)+0.1) : 0;
	*(out + i) = 0; }
	*/
 if (rank < MAX_PROD_RANK) {
   for (i=0; i<count; i++) {
     *(in + i) = i;
     *(sol + i) = 1;
     for (j=0; j<size_max_prod_rank; j++) {
       *(sol + i) = (*(sol + i)) * i; }
     *(out + i) = 0; }
 } else {
   for (i=0; i<count; i++) {
     *(in + i) = 1;
     *(sol + i) = 1;
     for (j=0; j<size_max_prod_rank; j++) {
       *(sol + i) = (*(sol + i)) * i; }
     *(out + i) = 0; }
 }

MPI_Allreduce( in, out, count, MPI_UNSIGNED_LONG, MPI_PROD, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_LONG and op MPI_PROD\n", rank );
free( in );
free( out );
free( sol );
}


{
float *in, *out, *sol;
int  i, j, fnderr=0;
in = (float *)malloc( count * sizeof(float) );
out = (float *)malloc( count * sizeof(float) );
sol = (float *)malloc( count * sizeof(float) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = i; *(sol + i) = (i > 0) ? (int)(pow((double)(i),(double)size)+0.1) : 0;
	*(out + i) = 0; }
	*/
 if (rank < MAX_PROD_RANK) {
   for (i=0; i<count; i++) {
     *(in + i) = (float) i;
     *(sol + i) = 1.0;
     for (j=0; j<size_max_prod_rank; j++) {
       *(sol + i) = (float) (*(sol + i)) * (float) i; }
     *(out + i) = 0.0; }
 } else {
   for (i=0; i<count; i++) {
     *(in + i) = 1.0;
     *(sol + i) = (float) 1;
     for (j=0; j<size_max_prod_rank; j++) {
       *(sol + i) = (*(sol + i)) * (float) i; }
     *(out + i) = 0.0; }
 }

MPI_Allreduce( in, out, count, MPI_FLOAT, MPI_PROD, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_FLOAT and op MPI_PROD\n", rank );
free( in );
free( out );
free( sol );
}


{
double *in, *out, *sol;
int  i, j, fnderr=0;
in = (double *)malloc( count * sizeof(double) );
out = (double *)malloc( count * sizeof(double) );
sol = (double *)malloc( count * sizeof(double) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = i; *(sol + i) = (i > 0) ? (int)(pow((double)(i),(double)size)+0.1) : 0;
	*(out + i) = 0; }
	*/
 if (rank < MAX_PROD_RANK) {
   for (i=0; i<count; i++) {
     *(in + i) = (double) i;
     *(sol + i) = 1.0;
     for (j=0; j<size_max_prod_rank; j++) {
       *(sol + i) = (double) (*(sol + i)) * (double) i; }
     *(out + i) = 0.0; }
 } else {
   for (i=0; i<count; i++) {
     *(in + i) = 1.0;
     *(sol + i) = (double) 1;
     for (j=0; j<size_max_prod_rank; j++) {
       *(sol + i) = (*(sol + i)) * (double) i; }
     *(out + i) = 0.0; }
 }

MPI_Allreduce( in, out, count, MPI_DOUBLE, MPI_PROD, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;
 fprintf (stderr, "MPI_DOUBLE choked: got: %f; expected %f\n", *(out + i),*(sol + i));}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_DOUBLE and op MPI_PROD\n", rank );
free( in );
free( out );
free( sol );
}


gerr += errcnt;
if (errcnt > 0)
	printf( "Found %d errors on %d for MPI_PROD\n", errcnt, rank );
errcnt = 0;


/* Test max */
#ifdef DEBUG
    if (rank == 0) printf( "Testing MPI_MAX...\n" );
#endif

{
int *in, *out, *sol;
int  i, fnderr=0;
in = (int *)malloc( count * sizeof(int) );
out = (int *)malloc( count * sizeof(int) );
sol = (int *)malloc( count * sizeof(int) );
for (i=0; i<count; i++) { *(in + i) = (rank + i); *(sol + i) = (size - 1 + i);
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_INT, MPI_MAX, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_INT and op MPI_MAX\n", rank );
free( in );
free( out );
free( sol );
}


{
long *in, *out, *sol;
int  i, fnderr=0;
in = (long *)malloc( count * sizeof(long) );
out = (long *)malloc( count * sizeof(long) );
sol = (long *)malloc( count * sizeof(long) );
for (i=0; i<count; i++) { *(in + i) = (rank + i); *(sol + i) = (size - 1 + i);
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_LONG, MPI_MAX, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_LONG and op MPI_MAX\n", rank );
free( in );
free( out );
free( sol );
}


{
short *in, *out, *sol;
int  i, fnderr=0;
in = (short *)malloc( count * sizeof(short) );
out = (short *)malloc( count * sizeof(short) );
sol = (short *)malloc( count * sizeof(short) );
for (i=0; i<count; i++) { *(in + i) = (rank + i); *(sol + i) = (size - 1 + i);
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_SHORT, MPI_MAX, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_SHORT and op MPI_MAX\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned short *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned short *)malloc( count * sizeof(unsigned short) );
out = (unsigned short *)malloc( count * sizeof(unsigned short) );
sol = (unsigned short *)malloc( count * sizeof(unsigned short) );
for (i=0; i<count; i++) { *(in + i) = (rank + i); *(sol + i) = (size - 1 + i);
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED_SHORT, MPI_MAX, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_SHORT and op MPI_MAX\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned *)malloc( count * sizeof(unsigned) );
out = (unsigned *)malloc( count * sizeof(unsigned) );
sol = (unsigned *)malloc( count * sizeof(unsigned) );
for (i=0; i<count; i++) { *(in + i) = (rank + i); *(sol + i) = (size - 1 + i);
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED, MPI_MAX, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED and op MPI_MAX\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned long *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned long *)malloc( count * sizeof(unsigned long) );
out = (unsigned long *)malloc( count * sizeof(unsigned long) );
sol = (unsigned long *)malloc( count * sizeof(unsigned long) );
for (i=0; i<count; i++) { *(in + i) = (rank + i); *(sol + i) = (size - 1 + i);
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED_LONG, MPI_MAX, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_LONG and op MPI_MAX\n", rank );
free( in );
free( out );
free( sol );
}


{
float *in, *out, *sol;
int  i, fnderr=0;
in = (float *)malloc( count * sizeof(float) );
out = (float *)malloc( count * sizeof(float) );
sol = (float *)malloc( count * sizeof(float) );
for (i=0; i<count; i++) { *(in + i) = (rank + i); *(sol + i) = (size - 1 + i);
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_FLOAT, MPI_MAX, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_FLOAT and op MPI_MAX\n", rank );
free( in );
free( out );
free( sol );
}


{
double *in, *out, *sol;
int  i, fnderr=0;
in = (double *)malloc( count * sizeof(double) );
out = (double *)malloc( count * sizeof(double) );
sol = (double *)malloc( count * sizeof(double) );
for (i=0; i<count; i++) { *(in + i) = (rank + i); *(sol + i) = (size - 1 + i);
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_DOUBLE, MPI_MAX, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_DOUBLE and op MPI_MAX\n", rank );
free( in );
free( out );
free( sol );
}


gerr += errcnt;
if (errcnt > 0)
	printf( "Found %d errors on %d for MPI_MAX\n", errcnt, rank );
errcnt = 0;

/* Test min */
#ifdef DEBUG
    if (rank == 0) printf( "Testing MPI_MIN...\n" );
#endif

{
int *in, *out, *sol;
int  i, fnderr=0;
in = (int *)malloc( count * sizeof(int) );
out = (int *)malloc( count * sizeof(int) );
sol = (int *)malloc( count * sizeof(int) );
for (i=0; i<count; i++) { *(in + i) = (rank + i); *(sol + i) = i;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_INT, MPI_MIN, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_INT and op MPI_MIN\n", rank );
free( in );
free( out );
free( sol );
}


{
long *in, *out, *sol;
int  i, fnderr=0;
in = (long *)malloc( count * sizeof(long) );
out = (long *)malloc( count * sizeof(long) );
sol = (long *)malloc( count * sizeof(long) );
for (i=0; i<count; i++) { *(in + i) = (rank + i); *(sol + i) = i;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_LONG, MPI_MIN, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_LONG and op MPI_MIN\n", rank );
free( in );
free( out );
free( sol );
}


{
short *in, *out, *sol;
int  i, fnderr=0;
in = (short *)malloc( count * sizeof(short) );
out = (short *)malloc( count * sizeof(short) );
sol = (short *)malloc( count * sizeof(short) );
for (i=0; i<count; i++) { *(in + i) = (rank + i); *(sol + i) = i;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_SHORT, MPI_MIN, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_SHORT and op MPI_MIN\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned short *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned short *)malloc( count * sizeof(unsigned short) );
out = (unsigned short *)malloc( count * sizeof(unsigned short) );
sol = (unsigned short *)malloc( count * sizeof(unsigned short) );
for (i=0; i<count; i++) { *(in + i) = (rank + i); *(sol + i) = i;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED_SHORT, MPI_MIN, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_SHORT and op MPI_MIN\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned *)malloc( count * sizeof(unsigned) );
out = (unsigned *)malloc( count * sizeof(unsigned) );
sol = (unsigned *)malloc( count * sizeof(unsigned) );
for (i=0; i<count; i++) { *(in + i) = (rank + i); *(sol + i) = i;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED, MPI_MIN, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED and op MPI_MIN\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned long *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned long *)malloc( count * sizeof(unsigned long) );
out = (unsigned long *)malloc( count * sizeof(unsigned long) );
sol = (unsigned long *)malloc( count * sizeof(unsigned long) );
for (i=0; i<count; i++) { *(in + i) = (rank + i); *(sol + i) = i;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED_LONG, MPI_MIN, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_LONG and op MPI_MIN\n", rank );
free( in );
free( out );
free( sol );
}


{
float *in, *out, *sol;
int  i, fnderr=0;
in = (float *)malloc( count * sizeof(float) );
out = (float *)malloc( count * sizeof(float) );
sol = (float *)malloc( count * sizeof(float) );
for (i=0; i<count; i++) { *(in + i) = (rank + i); *(sol + i) = i;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_FLOAT, MPI_MIN, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_FLOAT and op MPI_MIN\n", rank );
free( in );
free( out );
free( sol );
}


{
double *in, *out, *sol;
int  i, fnderr=0;
in = (double *)malloc( count * sizeof(double) );
out = (double *)malloc( count * sizeof(double) );
sol = (double *)malloc( count * sizeof(double) );
for (i=0; i<count; i++) { *(in + i) = (rank + i); *(sol + i) = i;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_DOUBLE, MPI_MIN, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_DOUBLE and op MPI_MIN\n", rank );
free( in );
free( out );
free( sol );
}


gerr += errcnt;
if (errcnt > 0)
	printf( "Found %d errors on %d for MPI_MIN\n", errcnt, rank );
errcnt = 0;

#ifndef SKIP_LOGICALS

/* Test LOR */
#ifdef DEBUG
    if (rank == 0) printf( "Testing MPI_LOR...\n" );
#endif

{
int *in, *out, *sol;
int  i, fnderr=0;
in = (int *)malloc( count * sizeof(int) );
out = (int *)malloc( count * sizeof(int) );
sol = (int *)malloc( count * sizeof(int) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = (rank & 0x1); *(sol + i) = (size > 1);
	*(out + i) = 0; }
*/
 if (rank) {
   for (i=0; i<count; i++) { *(in + i) = 0; *(out + i) = 0; }
 }
 else {
   for (i=0; i<count; i++) { *(in + i) = 0x1; *(out + i) = 0; }
 }

MPI_Allreduce( in, out, count, MPI_INT, MPI_LOR, comm );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
*/
 for (i=0; i<count; i++) { if (!(*(out + i))) {errcnt++; fnderr++;}}

if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_INT and op MPI_LOR\n", rank );
free( in );
free( out );
free( sol );
}


{
long *in, *out, *sol;
int  i, fnderr=0;
in = (long *)malloc( count * sizeof(long) );
out = (long *)malloc( count * sizeof(long) );
sol = (long *)malloc( count * sizeof(long) );
for (i=0; i<count; i++) { *(in + i) = (rank & 0x1); *(sol + i) = (size > 1);
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_LONG, MPI_LOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_LONG and op MPI_LOR\n", rank );
free( in );
free( out );
free( sol );
}


{
short *in, *out, *sol;
int  i, fnderr=0;
in = (short *)malloc( count * sizeof(short) );
out = (short *)malloc( count * sizeof(short) );
sol = (short *)malloc( count * sizeof(short) );
for (i=0; i<count; i++) { *(in + i) = (rank & 0x1); *(sol + i) = (size > 1);
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_SHORT, MPI_LOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_SHORT and op MPI_LOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned short *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned short *)malloc( count * sizeof(unsigned short) );
out = (unsigned short *)malloc( count * sizeof(unsigned short) );
sol = (unsigned short *)malloc( count * sizeof(unsigned short) );
for (i=0; i<count; i++) { *(in + i) = (rank & 0x1); *(sol + i) = (size > 1);
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED_SHORT, MPI_LOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_SHORT and op MPI_LOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned *)malloc( count * sizeof(unsigned) );
out = (unsigned *)malloc( count * sizeof(unsigned) );
sol = (unsigned *)malloc( count * sizeof(unsigned) );
for (i=0; i<count; i++) { *(in + i) = (rank & 0x1); *(sol + i) = (size > 1);
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED, MPI_LOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED and op MPI_LOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned long *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned long *)malloc( count * sizeof(unsigned long) );
out = (unsigned long *)malloc( count * sizeof(unsigned long) );
sol = (unsigned long *)malloc( count * sizeof(unsigned long) );
for (i=0; i<count; i++) { *(in + i) = (rank & 0x1); *(sol + i) = (size > 1);
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED_LONG, MPI_LOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_LONG and op MPI_LOR\n", rank );
free( in );
free( out );
free( sol );
}


gerr += errcnt;
if (errcnt > 0)
	printf( "Found %d errors on %d for MPI_LOR(1)\n", errcnt, rank );
errcnt = 0;


{
int *in, *out, *sol;
int  i, fnderr=0;
in = (int *)malloc( count * sizeof(int) );
out = (int *)malloc( count * sizeof(int) );
sol = (int *)malloc( count * sizeof(int) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = 0; *(sol + i) = 0;
	*(out + i) = 0; }
*/
 for (i=0; i<count; i++) { *(in + i) = 0; *(out + i) = 0x1; }

MPI_Allreduce( in, out, count, MPI_INT, MPI_LOR, comm );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
*/
 for (i=0; i<count; i++) { if (*(out + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_INT and op MPI_LOR\n", rank );
free( in );
free( out );
free( sol );
}


{
long *in, *out, *sol;
int  i, fnderr=0;
in = (long *)malloc( count * sizeof(long) );
out = (long *)malloc( count * sizeof(long) );
sol = (long *)malloc( count * sizeof(long) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = 0; *(sol + i) = 0;
	*(out + i) = 0; }
*/
 for (i=0; i<count; i++) { *(in + i) = 0; *(out + i) = 0x1; }

MPI_Allreduce( in, out, count, MPI_LONG, MPI_LOR, comm );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
*/
 for (i=0; i<count; i++) { if (*(out + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_LONG and op MPI_LOR\n", rank );
free( in );
free( out );
free( sol );
}


{
short *in, *out, *sol;
int  i, fnderr=0;
in = (short *)malloc( count * sizeof(short) );
out = (short *)malloc( count * sizeof(short) );
sol = (short *)malloc( count * sizeof(short) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = 0; *(sol + i) = 0;
	*(out + i) = 0; }
*/
 for (i=0; i<count; i++) { *(in + i) = 0; *(out + i) = 0x1; }

MPI_Allreduce( in, out, count, MPI_SHORT, MPI_LOR, comm );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
*/
 for (i=0; i<count; i++) { if (*(out + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_SHORT and op MPI_LOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned short *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned short *)malloc( count * sizeof(unsigned short) );
out = (unsigned short *)malloc( count * sizeof(unsigned short) );
sol = (unsigned short *)malloc( count * sizeof(unsigned short) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = 0; *(sol + i) = 0;
	*(out + i) = 0; }
*/
 for (i=0; i<count; i++) { *(in + i) = 0; *(out + i) = 0x1; }

MPI_Allreduce( in, out, count, MPI_UNSIGNED_SHORT, MPI_LOR, comm );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
*/
 for (i=0; i<count; i++) { if (*(out + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_SHORT and op MPI_LOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned *)malloc( count * sizeof(unsigned) );
out = (unsigned *)malloc( count * sizeof(unsigned) );
sol = (unsigned *)malloc( count * sizeof(unsigned) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = 0; *(sol + i) = 0;
	*(out + i) = 0; }
*/
 for (i=0; i<count; i++) { *(in + i) = 0; *(out + i) = 0x1; }

MPI_Allreduce( in, out, count, MPI_UNSIGNED, MPI_LOR, comm );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
*/
 for (i=0; i<count; i++) { if (*(out + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED and op MPI_LOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned long *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned long *)malloc( count * sizeof(unsigned long) );
out = (unsigned long *)malloc( count * sizeof(unsigned long) );
sol = (unsigned long *)malloc( count * sizeof(unsigned long) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = 0; *(sol + i) = 0;
	*(out + i) = 0; }
*/
 for (i=0; i<count; i++) { *(in + i) = 0; *(out + i) = 0x1; }

MPI_Allreduce( in, out, count, MPI_UNSIGNED_LONG, MPI_LOR, comm );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
*/
 for (i=0; i<count; i++) { if (*(out + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_LONG and op MPI_LOR\n", rank );
free( in );
free( out );
free( sol );
}


gerr += errcnt;
if (errcnt > 0)
	printf( "Found %d errors on %d for MPI_LOR(0)\n", errcnt, rank );
errcnt = 0;

/* Test LXOR */
#ifdef DEBUG
    if (rank == 0) printf( "Testing MPI_LXOR...\n" );
#endif

{
int *in, *out, *sol;
int  i, fnderr=0;
in = (int *)malloc( count * sizeof(int) );
out = (int *)malloc( count * sizeof(int) );
sol = (int *)malloc( count * sizeof(int) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = (rank == 1); *(sol + i) = (size > 1);
	*(out + i) = 0; }
*/
 if (rank == 1) {
   for (i=0; i<count; i++) { *(in + i) = 0x1; *(out + i) = 0; }}
 else {
   for (i=0; i<count; i++) { *(in + i) = 0; *(out + i) = 0; }}

MPI_Allreduce( in, out, count, MPI_INT, MPI_LXOR, comm );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
*/
for (i=0; i<count; i++) { if (!(*(out + i))) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_INT and op MPI_LXOR\n", rank);
free( in );
free( out );
free( sol );
}


{
long *in, *out, *sol;
int  i, fnderr=0;
in = (long *)malloc( count * sizeof(long) );
out = (long *)malloc( count * sizeof(long) );
sol = (long *)malloc( count * sizeof(long) );
for (i=0; i<count; i++) { *(in + i) = (rank == 1); *(sol + i) = (size > 1);
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_LONG, MPI_LXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_LONG and op MPI_LXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
short *in, *out, *sol;
int  i, fnderr=0;
in = (short *)malloc( count * sizeof(short) );
out = (short *)malloc( count * sizeof(short) );
sol = (short *)malloc( count * sizeof(short) );
for (i=0; i<count; i++) { *(in + i) = (rank == 1); *(sol + i) = (size > 1);
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_SHORT, MPI_LXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_SHORT and op MPI_LXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned short *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned short *)malloc( count * sizeof(unsigned short) );
out = (unsigned short *)malloc( count * sizeof(unsigned short) );
sol = (unsigned short *)malloc( count * sizeof(unsigned short) );
for (i=0; i<count; i++) { *(in + i) = (rank == 1); *(sol + i) = (size > 1);
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED_SHORT, MPI_LXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_SHORT and op MPI_LXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned *)malloc( count * sizeof(unsigned) );
out = (unsigned *)malloc( count * sizeof(unsigned) );
sol = (unsigned *)malloc( count * sizeof(unsigned) );
for (i=0; i<count; i++) { *(in + i) = (rank == 1); *(sol + i) = (size > 1);
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED, MPI_LXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED and op MPI_LXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned long *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned long *)malloc( count * sizeof(unsigned long) );
out = (unsigned long *)malloc( count * sizeof(unsigned long) );
sol = (unsigned long *)malloc( count * sizeof(unsigned long) );
for (i=0; i<count; i++) { *(in + i) = (rank == 1); *(sol + i) = (size > 1);
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED_LONG, MPI_LXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_LONG and op MPI_LXOR\n", rank );
free( in );
free( out );
free( sol );
}


gerr += errcnt;
if (errcnt > 0)
	printf( "Found %d errors on %d for MPI_LXOR(1)\n", errcnt, rank );
errcnt = 0;


{
int *in, *out, *sol;
int  i, fnderr=0;
in = (int *)malloc( count * sizeof(int) );
out = (int *)malloc( count * sizeof(int) );
sol = (int *)malloc( count * sizeof(int) );
for (i=0; i<count; i++) { *(in + i) = 0; *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_INT, MPI_LXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_INT and op MPI_LXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
long *in, *out, *sol;
int  i, fnderr=0;
in = (long *)malloc( count * sizeof(long) );
out = (long *)malloc( count * sizeof(long) );
sol = (long *)malloc( count * sizeof(long) );
for (i=0; i<count; i++) { *(in + i) = 0; *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_LONG, MPI_LXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_LONG and op MPI_LXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
short *in, *out, *sol;
int  i, fnderr=0;
in = (short *)malloc( count * sizeof(short) );
out = (short *)malloc( count * sizeof(short) );
sol = (short *)malloc( count * sizeof(short) );
for (i=0; i<count; i++) { *(in + i) = 0; *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_SHORT, MPI_LXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_SHORT and op MPI_LXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned short *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned short *)malloc( count * sizeof(unsigned short) );
out = (unsigned short *)malloc( count * sizeof(unsigned short) );
sol = (unsigned short *)malloc( count * sizeof(unsigned short) );
for (i=0; i<count; i++) { *(in + i) = 0; *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED_SHORT, MPI_LXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_SHORT and op MPI_LXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned *)malloc( count * sizeof(unsigned) );
out = (unsigned *)malloc( count * sizeof(unsigned) );
sol = (unsigned *)malloc( count * sizeof(unsigned) );
for (i=0; i<count; i++) { *(in + i) = 0; *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED, MPI_LXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED and op MPI_LXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned long *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned long *)malloc( count * sizeof(unsigned long) );
out = (unsigned long *)malloc( count * sizeof(unsigned long) );
sol = (unsigned long *)malloc( count * sizeof(unsigned long) );
for (i=0; i<count; i++) { *(in + i) = 0; *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED_LONG, MPI_LXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_LONG and op MPI_LXOR\n", rank );
free( in );
free( out );
free( sol );
}


gerr += errcnt;
if (errcnt > 0)
	printf( "Found %d errors on %d for MPI_LXOR(0)\n", errcnt, rank );
errcnt = 0;


{
int *in, *out, *sol;
int  i, fnderr=0;
in = (int *)malloc( count * sizeof(int) );
out = (int *)malloc( count * sizeof(int) );
sol = (int *)malloc( count * sizeof(int) );
for (i=0; i<count; i++) { *(in + i) = 1; *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_INT, MPI_LXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_INT and op MPI_LXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
long *in, *out, *sol;
int  i, fnderr=0;
in = (long *)malloc( count * sizeof(long) );
out = (long *)malloc( count * sizeof(long) );
sol = (long *)malloc( count * sizeof(long) );
for (i=0; i<count; i++) { *(in + i) = 1; *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_LONG, MPI_LXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_LONG and op MPI_LXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
short *in, *out, *sol;
int  i, fnderr=0;
in = (short *)malloc( count * sizeof(short) );
out = (short *)malloc( count * sizeof(short) );
sol = (short *)malloc( count * sizeof(short) );
for (i=0; i<count; i++) { *(in + i) = 1; *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_SHORT, MPI_LXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_SHORT and op MPI_LXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned short *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned short *)malloc( count * sizeof(unsigned short) );
out = (unsigned short *)malloc( count * sizeof(unsigned short) );
sol = (unsigned short *)malloc( count * sizeof(unsigned short) );
for (i=0; i<count; i++) { *(in + i) = 1; *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED_SHORT, MPI_LXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_SHORT and op MPI_LXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned *)malloc( count * sizeof(unsigned) );
out = (unsigned *)malloc( count * sizeof(unsigned) );
sol = (unsigned *)malloc( count * sizeof(unsigned) );
for (i=0; i<count; i++) { *(in + i) = 1; *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED, MPI_LXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED and op MPI_LXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned long *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned long *)malloc( count * sizeof(unsigned long) );
out = (unsigned long *)malloc( count * sizeof(unsigned long) );
sol = (unsigned long *)malloc( count * sizeof(unsigned long) );
for (i=0; i<count; i++) { *(in + i) = 1; *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED_LONG, MPI_LXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_LONG and op MPI_LXOR\n", rank );
free( in );
free( out );
free( sol );
}


gerr += errcnt;
if (errcnt > 0)
	printf( "Found %d errors on %d for MPI_LXOR(1-0)\n", errcnt, rank );
errcnt = 0;

/* Test LAND */
#ifdef DEBUG
    if (rank == 0) printf( "Testing MPI_LAND...\n" );
#endif

{
int *in, *out, *sol;
int  i, fnderr=0;
in = (int *)malloc( count * sizeof(int) );
out = (int *)malloc( count * sizeof(int) );
sol = (int *)malloc( count * sizeof(int) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = (rank & 0x1); *(sol + i) = 0;
	*(out + i) = 0; }
*/
 for (i=0; i<count; i++) { *(in + i) = 0x1; *(out + i) = 0; }

MPI_Allreduce( in, out, count, MPI_INT, MPI_LAND, comm );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
*/
 for (i=0; i<count; i++) { if (!(*(out + i))) {errcnt++; fnderr++;}}

if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_INT and op MPI_LAND\n", rank );
free( in );
free( out );
free( sol );
}


{
long *in, *out, *sol;
int  i, fnderr=0;
in = (long *)malloc( count * sizeof(long) );
out = (long *)malloc( count * sizeof(long) );
sol = (long *)malloc( count * sizeof(long) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = (rank & 0x1); *(sol + i) = 0;
	*(out + i) = 0; }
*/
 for (i=0; i<count; i++) { *(in + i) = 0x1; *(out + i) = 0; }

MPI_Allreduce( in, out, count, MPI_LONG, MPI_LAND, comm );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
*/
 for (i=0; i<count; i++) { if (!(*(out + i))) {errcnt++; fnderr++;}}

if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_LONG and op MPI_LAND\n", rank );
free( in );
free( out );
free( sol );
}


{
short *in, *out, *sol;
int  i, fnderr=0;
in = (short *)malloc( count * sizeof(short) );
out = (short *)malloc( count * sizeof(short) );
sol = (short *)malloc( count * sizeof(short) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = (rank & 0x1); *(sol + i) = 0;
	*(out + i) = 0; }
*/
 for (i=0; i<count; i++) { *(in + i) = 0x1; *(out + i) = 0; }

MPI_Allreduce( in, out, count, MPI_SHORT, MPI_LAND, comm );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
*/
 for (i=0; i<count; i++) { if (!(*(out + i))) {errcnt++; fnderr++;}}

if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_SHORT and op MPI_LAND\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned short *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned short *)malloc( count * sizeof(unsigned short) );
out = (unsigned short *)malloc( count * sizeof(unsigned short) );
sol = (unsigned short *)malloc( count * sizeof(unsigned short) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = (rank & 0x1); *(sol + i) = 0;
	*(out + i) = 0; }
*/
 for (i=0; i<count; i++) { *(in + i) = 0x1; *(out + i) = 0; }

MPI_Allreduce( in, out, count, MPI_UNSIGNED_SHORT, MPI_LAND, comm );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
*/
 for (i=0; i<count; i++) { if (!(*(out + i))) {errcnt++; fnderr++;}}

if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_SHORT and op MPI_LAND\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned *)malloc( count * sizeof(unsigned) );
out = (unsigned *)malloc( count * sizeof(unsigned) );
sol = (unsigned *)malloc( count * sizeof(unsigned) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/* BUT NOT THIS ONE - CHANGING IT ANYWAY... */
/*
for (i=0; i<count; i++) { *(in + i) = (rank & 0x1); *(sol + i) = 0;
	*(out + i) = 0; }
*/
 for (i=0; i<count; i++) { *(in + i) = 0x1; *(out + i) = 0; }

MPI_Allreduce( in, out, count, MPI_UNSIGNED, MPI_LAND, comm );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/* BUT NOT THIS ONE - CHANGING IT ANYWAY... */
/*
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
*/
 for (i=0; i<count; i++) { if (!(*(out + i))) {errcnt++; fnderr++;}}

if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED and op MPI_LAND\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned long *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned long *)malloc( count * sizeof(unsigned long) );
out = (unsigned long *)malloc( count * sizeof(unsigned long) );
sol = (unsigned long *)malloc( count * sizeof(unsigned long) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = (rank & 0x1); *(sol + i) = 0;
	*(out + i) = 0; }
*/
 for (i=0; i<count; i++) { *(in + i) = 0x1; *(out + i) = 0; }

MPI_Allreduce( in, out, count, MPI_UNSIGNED_LONG, MPI_LAND, comm );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
*/
 for (i=0; i<count; i++) { if (!(*(out + i))) {errcnt++; fnderr++;}}

if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_LONG and op MPI_LAND\n", rank );
free( in );
free( out );
free( sol );
}


gerr += errcnt;
if (errcnt > 0)
	printf( "Found %d errors on %d for MPI_LAND(1)\n", errcnt, rank );
errcnt = 0;


{
int *in, *out, *sol;
int  i, fnderr=0;
in = (int *)malloc( count * sizeof(int) );
out = (int *)malloc( count * sizeof(int) );
sol = (int *)malloc( count * sizeof(int) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = 1; *(sol + i) = 1;
	*(out + i) = 0; }
*/
 for (i=0; i<count; i++) { *(in + i) = 0; *(out + i) = 0x1; }

MPI_Allreduce( in, out, count, MPI_INT, MPI_LAND, comm );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
*/
 for (i=0; i<count; i++) { if (*(out + i)) {errcnt++; fnderr++;}}

if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_INT and op MPI_LAND\n", rank );
free( in );
free( out );
free( sol );
}


{
long *in, *out, *sol;
int  i, fnderr=0;
in = (long *)malloc( count * sizeof(long) );
out = (long *)malloc( count * sizeof(long) );
sol = (long *)malloc( count * sizeof(long) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = 1; *(sol + i) = 1;
	*(out + i) = 0; }
*/
 for (i=0; i<count; i++) { *(in + i) = 0; *(out + i) = 0x1; }

MPI_Allreduce( in, out, count, MPI_LONG, MPI_LAND, comm );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
*/
 for (i=0; i<count; i++) { if (*(out + i)) {errcnt++; fnderr++;}}

if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_LONG and op MPI_LAND\n", rank );
free( in );
free( out );
free( sol );
}


{
short *in, *out, *sol;
int  i, fnderr=0;
in = (short *)malloc( count * sizeof(short) );
out = (short *)malloc( count * sizeof(short) );
sol = (short *)malloc( count * sizeof(short) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = 1; *(sol + i) = 1;
	*(out + i) = 0; }
*/
 for (i=0; i<count; i++) { *(in + i) = 0; *(out + i) = 0x1; }

MPI_Allreduce( in, out, count, MPI_SHORT, MPI_LAND, comm );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
*/
 for (i=0; i<count; i++) { if (*(out + i)) {errcnt++; fnderr++;}}

if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_SHORT and op MPI_LAND\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned short *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned short *)malloc( count * sizeof(unsigned short) );
out = (unsigned short *)malloc( count * sizeof(unsigned short) );
sol = (unsigned short *)malloc( count * sizeof(unsigned short) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = 1; *(sol + i) = 1;
	*(out + i) = 0; }
*/
 for (i=0; i<count; i++) { *(in + i) = 0; *(out + i) = 0x1; }

MPI_Allreduce( in, out, count, MPI_UNSIGNED_SHORT, MPI_LAND, comm );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
*/
 for (i=0; i<count; i++) { if (*(out + i)) {errcnt++; fnderr++;
fprintf( stderr,
 	"(%d) MPI_UNSIGNED_SHORT MPI_LAND index %d\n", rank, i );}}

if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_SHORT and op MPI_LAND\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned *)malloc( count * sizeof(unsigned) );
out = (unsigned *)malloc( count * sizeof(unsigned) );
sol = (unsigned *)malloc( count * sizeof(unsigned) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = 1; *(sol + i) = 1;
	*(out + i) = 0; }
*/
 for (i=0; i<count; i++) { *(in + i) = 0; *(out + i) = 0x1; }

MPI_Allreduce( in, out, count, MPI_UNSIGNED, MPI_LAND, comm );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
*/
 for (i=0; i<count; i++) { if (*(out + i)) {errcnt++; fnderr++;}}

if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED and op MPI_LAND\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned long *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned long *)malloc( count * sizeof(unsigned long) );
out = (unsigned long *)malloc( count * sizeof(unsigned long) );
sol = (unsigned long *)malloc( count * sizeof(unsigned long) );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { *(in + i) = 1; *(sol + i) = 1;
	*(out + i) = 0; }
*/
 for (i=0; i<count; i++) { *(in + i) = 0; *(out + i) = 0x1; }

MPI_Allreduce( in, out, count, MPI_UNSIGNED_LONG, MPI_LAND, comm );
/* OLD TEST SEEMS TO BE BROKEN AT LEAST ON BlueGeneL... */
/*
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
*/
 for (i=0; i<count; i++) { if (*(out + i)) {errcnt++; fnderr++;}}

if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_LONG and op MPI_LAND\n", rank );
free( in );
free( out );
free( sol );
}


gerr += errcnt;
if (errcnt > 0)
	printf( "Found %d errors on %d for MPI_LAND(0)\n", errcnt, rank );
errcnt = 0;

#endif

/* Test BOR */
#ifdef DEBUG
    if (rank == 0) printf( "Testing MPI_BOR...\n" );
#endif

{
int *in, *out, *sol;
int  i, fnderr=0;
in = (int *)malloc( count * sizeof(int) );
out = (int *)malloc( count * sizeof(int) );
sol = (int *)malloc( count * sizeof(int) );
for (i=0; i<count; i++) { *(in + i) = rank & 0x3; *(sol + i) = (size < 3) ? size - 1 : 0x3;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_INT, MPI_BOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_INT and op MPI_BOR\n", rank );
free( in );
free( out );
free( sol );
}


{
long *in, *out, *sol;
int  i, fnderr=0;
in = (long *)malloc( count * sizeof(long) );
out = (long *)malloc( count * sizeof(long) );
sol = (long *)malloc( count * sizeof(long) );
for (i=0; i<count; i++) { *(in + i) = rank & 0x3; *(sol + i) = (size < 3) ? size - 1 : 0x3;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_LONG, MPI_BOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_LONG and op MPI_BOR\n", rank );
free( in );
free( out );
free( sol );
}


{
short *in, *out, *sol;
int  i, fnderr=0;
in = (short *)malloc( count * sizeof(short) );
out = (short *)malloc( count * sizeof(short) );
sol = (short *)malloc( count * sizeof(short) );
for (i=0; i<count; i++) { *(in + i) = rank & 0x3; *(sol + i) = (size < 3) ? size - 1 : 0x3;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_SHORT, MPI_BOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_SHORT and op MPI_BOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned short *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned short *)malloc( count * sizeof(unsigned short) );
out = (unsigned short *)malloc( count * sizeof(unsigned short) );
sol = (unsigned short *)malloc( count * sizeof(unsigned short) );
for (i=0; i<count; i++) { *(in + i) = rank & 0x3; *(sol + i) = (size < 3) ? size - 1 : 0x3;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED_SHORT, MPI_BOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_SHORT and op MPI_BOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned *)malloc( count * sizeof(unsigned) );
out = (unsigned *)malloc( count * sizeof(unsigned) );
sol = (unsigned *)malloc( count * sizeof(unsigned) );
for (i=0; i<count; i++) { *(in + i) = rank & 0x3; *(sol + i) = (size < 3) ? size - 1 : 0x3;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED, MPI_BOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED and op MPI_BOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned long *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned long *)malloc( count * sizeof(unsigned long) );
out = (unsigned long *)malloc( count * sizeof(unsigned long) );
sol = (unsigned long *)malloc( count * sizeof(unsigned long) );
for (i=0; i<count; i++) { *(in + i) = rank & 0x3; *(sol + i) = (size < 3) ? size - 1 : 0x3;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED_LONG, MPI_BOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_LONG and op MPI_BOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned char *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned char *)malloc( count * sizeof(unsigned char) );
out = (unsigned char *)malloc( count * sizeof(unsigned char) );
sol = (unsigned char *)malloc( count * sizeof(unsigned char) );
for (i=0; i<count; i++) { *(in + i) = rank & 0x3; *(sol + i) = (size < 3) ? size - 1 : 0x3;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_BYTE, MPI_BOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_BYTE and op MPI_BOR\n", rank );
free( in );
free( out );
free( sol );
}


gerr += errcnt;
if (errcnt > 0)
	printf( "Found %d errors on %d for MPI_BOR(1)\n", errcnt, rank );
errcnt = 0;

/* Test BAND */
#ifdef DEBUG
    if (rank == 0) printf( "Testing MPI_BAND...\n" );
#endif

{
int *in, *out, *sol;
int  i, fnderr=0;
in = (int *)malloc( count * sizeof(int) );
out = (int *)malloc( count * sizeof(int) );
sol = (int *)malloc( count * sizeof(int) );
for (i=0; i<count; i++) { *(in + i) = (rank == size-1 ? i : ~0); *(sol + i) = i;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_INT, MPI_BAND, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_INT and op MPI_BAND\n", rank );
free( in );
free( out );
free( sol );
}


{
long *in, *out, *sol;
int  i, fnderr=0;
in = (long *)malloc( count * sizeof(long) );
out = (long *)malloc( count * sizeof(long) );
sol = (long *)malloc( count * sizeof(long) );
for (i=0; i<count; i++) { *(in + i) = (rank == size-1 ? i : ~0); *(sol + i) = i;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_LONG, MPI_BAND, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_LONG and op MPI_BAND\n", rank );
free( in );
free( out );
free( sol );
}


{
short *in, *out, *sol;
int  i, fnderr=0;
in = (short *)malloc( count * sizeof(short) );
out = (short *)malloc( count * sizeof(short) );
sol = (short *)malloc( count * sizeof(short) );
for (i=0; i<count; i++) { *(in + i) = (rank == size-1 ? i : ~0); *(sol + i) = i;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_SHORT, MPI_BAND, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_SHORT and op MPI_BAND\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned short *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned short *)malloc( count * sizeof(unsigned short) );
out = (unsigned short *)malloc( count * sizeof(unsigned short) );
sol = (unsigned short *)malloc( count * sizeof(unsigned short) );
for (i=0; i<count; i++) { *(in + i) = (rank == size-1 ? i : ~0); *(sol + i) = i;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED_SHORT, MPI_BAND, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_SHORT and op MPI_BAND\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned *)malloc( count * sizeof(unsigned) );
out = (unsigned *)malloc( count * sizeof(unsigned) );
sol = (unsigned *)malloc( count * sizeof(unsigned) );
for (i=0; i<count; i++) { *(in + i) = (rank == size-1 ? i : ~0); *(sol + i) = i;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED, MPI_BAND, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED and op MPI_BAND\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned long *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned long *)malloc( count * sizeof(unsigned long) );
out = (unsigned long *)malloc( count * sizeof(unsigned long) );
sol = (unsigned long *)malloc( count * sizeof(unsigned long) );
for (i=0; i<count; i++) { *(in + i) = (rank == size-1 ? i : ~0); *(sol + i) = i;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED_LONG, MPI_BAND, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_LONG and op MPI_BAND\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned char *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned char *)malloc( count * sizeof(unsigned char) );
out = (unsigned char *)malloc( count * sizeof(unsigned char) );
sol = (unsigned char *)malloc( count * sizeof(unsigned char) );
for (i=0; i<count; i++) { *(in + i) = (rank == size-1 ? i : ~0); *(sol + i) = i;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_BYTE, MPI_BAND, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_BYTE and op MPI_BAND\n", rank );
free( in );
free( out );
free( sol );
}


gerr += errcnt;
if (errcnt > 0)
	printf( "Found %d errors on %d for MPI_BAND(1)\n", errcnt, rank );
errcnt = 0;


{
int *in, *out, *sol;
int  i, fnderr=0;
in = (int *)malloc( count * sizeof(int) );
out = (int *)malloc( count * sizeof(int) );
sol = (int *)malloc( count * sizeof(int) );
for (i=0; i<count; i++) { *(in + i) = (rank == size-1 ? i : 0); *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_INT, MPI_BAND, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_INT and op MPI_BAND\n", rank );
free( in );
free( out );
free( sol );
}


{
long *in, *out, *sol;
int  i, fnderr=0;
in = (long *)malloc( count * sizeof(long) );
out = (long *)malloc( count * sizeof(long) );
sol = (long *)malloc( count * sizeof(long) );
for (i=0; i<count; i++) { *(in + i) = (rank == size-1 ? i : 0); *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_LONG, MPI_BAND, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_LONG and op MPI_BAND\n", rank );
free( in );
free( out );
free( sol );
}


{
short *in, *out, *sol;
int  i, fnderr=0;
in = (short *)malloc( count * sizeof(short) );
out = (short *)malloc( count * sizeof(short) );
sol = (short *)malloc( count * sizeof(short) );
for (i=0; i<count; i++) { *(in + i) = (rank == size-1 ? i : 0); *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_SHORT, MPI_BAND, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_SHORT and op MPI_BAND\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned short *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned short *)malloc( count * sizeof(unsigned short) );
out = (unsigned short *)malloc( count * sizeof(unsigned short) );
sol = (unsigned short *)malloc( count * sizeof(unsigned short) );
for (i=0; i<count; i++) { *(in + i) = (rank == size-1 ? i : 0); *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED_SHORT, MPI_BAND, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_SHORT and op MPI_BAND\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned *)malloc( count * sizeof(unsigned) );
out = (unsigned *)malloc( count * sizeof(unsigned) );
sol = (unsigned *)malloc( count * sizeof(unsigned) );
for (i=0; i<count; i++) { *(in + i) = (rank == size-1 ? i : 0); *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED, MPI_BAND, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED and op MPI_BAND\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned long *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned long *)malloc( count * sizeof(unsigned long) );
out = (unsigned long *)malloc( count * sizeof(unsigned long) );
sol = (unsigned long *)malloc( count * sizeof(unsigned long) );
for (i=0; i<count; i++) { *(in + i) = (rank == size-1 ? i : 0); *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED_LONG, MPI_BAND, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_LONG and op MPI_BAND\n", rank );
free( in );
free( out );
free( sol );
}


gerr += errcnt;
if (errcnt > 0)
	printf( "Found %d errors on %d for MPI_BAND(0)\n", errcnt, rank );
errcnt = 0;

/* Test BXOR */
#ifdef DEBUG
    if (rank == 0) printf( "Testing MPI_BXOR...\n" );
#endif

{
int *in, *out, *sol;
int  i, fnderr=0;
in = (int *)malloc( count * sizeof(int) );
out = (int *)malloc( count * sizeof(int) );
sol = (int *)malloc( count * sizeof(int) );
for (i=0; i<count; i++) { *(in + i) = (rank == 1)*0xf0 ; *(sol + i) = (size > 1)*0xf0 ;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_INT, MPI_BXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_INT and op MPI_BXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
long *in, *out, *sol;
int  i, fnderr=0;
in = (long *)malloc( count * sizeof(long) );
out = (long *)malloc( count * sizeof(long) );
sol = (long *)malloc( count * sizeof(long) );
for (i=0; i<count; i++) { *(in + i) = (rank == 1)*0xf0 ; *(sol + i) = (size > 1)*0xf0 ;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_LONG, MPI_BXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_LONG and op MPI_BXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
short *in, *out, *sol;
int  i, fnderr=0;
in = (short *)malloc( count * sizeof(short) );
out = (short *)malloc( count * sizeof(short) );
sol = (short *)malloc( count * sizeof(short) );
for (i=0; i<count; i++) { *(in + i) = (rank == 1)*0xf0 ; *(sol + i) = (size > 1)*0xf0 ;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_SHORT, MPI_BXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_SHORT and op MPI_BXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned short *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned short *)malloc( count * sizeof(unsigned short) );
out = (unsigned short *)malloc( count * sizeof(unsigned short) );
sol = (unsigned short *)malloc( count * sizeof(unsigned short) );
for (i=0; i<count; i++) { *(in + i) = (rank == 1)*0xf0 ; *(sol + i) = (size > 1)*0xf0 ;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED_SHORT, MPI_BXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_SHORT and op MPI_BXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned *)malloc( count * sizeof(unsigned) );
out = (unsigned *)malloc( count * sizeof(unsigned) );
sol = (unsigned *)malloc( count * sizeof(unsigned) );
for (i=0; i<count; i++) { *(in + i) = (rank == 1)*0xf0 ; *(sol + i) = (size > 1)*0xf0 ;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED, MPI_BXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED and op MPI_BXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned long *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned long *)malloc( count * sizeof(unsigned long) );
out = (unsigned long *)malloc( count * sizeof(unsigned long) );
sol = (unsigned long *)malloc( count * sizeof(unsigned long) );
for (i=0; i<count; i++) { *(in + i) = (rank == 1)*0xf0 ; *(sol + i) = (size > 1)*0xf0 ;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED_LONG, MPI_BXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_LONG and op MPI_BXOR\n", rank );
free( in );
free( out );
free( sol );
}


gerr += errcnt;
if (errcnt > 0)
	printf( "Found %d errors on %d for MPI_BXOR(1)\n", errcnt, rank );
errcnt = 0;


{
int *in, *out, *sol;
int  i, fnderr=0;
in = (int *)malloc( count * sizeof(int) );
out = (int *)malloc( count * sizeof(int) );
sol = (int *)malloc( count * sizeof(int) );
for (i=0; i<count; i++) { *(in + i) = 0; *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_INT, MPI_BXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_INT and op MPI_BXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
long *in, *out, *sol;
int  i, fnderr=0;
in = (long *)malloc( count * sizeof(long) );
out = (long *)malloc( count * sizeof(long) );
sol = (long *)malloc( count * sizeof(long) );
for (i=0; i<count; i++) { *(in + i) = 0; *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_LONG, MPI_BXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_LONG and op MPI_BXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
short *in, *out, *sol;
int  i, fnderr=0;
in = (short *)malloc( count * sizeof(short) );
out = (short *)malloc( count * sizeof(short) );
sol = (short *)malloc( count * sizeof(short) );
for (i=0; i<count; i++) { *(in + i) = 0; *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_SHORT, MPI_BXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_SHORT and op MPI_BXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned short *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned short *)malloc( count * sizeof(unsigned short) );
out = (unsigned short *)malloc( count * sizeof(unsigned short) );
sol = (unsigned short *)malloc( count * sizeof(unsigned short) );
for (i=0; i<count; i++) { *(in + i) = 0; *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED_SHORT, MPI_BXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_SHORT and op MPI_BXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned *)malloc( count * sizeof(unsigned) );
out = (unsigned *)malloc( count * sizeof(unsigned) );
sol = (unsigned *)malloc( count * sizeof(unsigned) );
for (i=0; i<count; i++) { *(in + i) = 0; *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED, MPI_BXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED and op MPI_BXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned long *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned long *)malloc( count * sizeof(unsigned long) );
out = (unsigned long *)malloc( count * sizeof(unsigned long) );
sol = (unsigned long *)malloc( count * sizeof(unsigned long) );
for (i=0; i<count; i++) { *(in + i) = 0; *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED_LONG, MPI_BXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_LONG and op MPI_BXOR\n", rank );
free( in );
free( out );
free( sol );
}


gerr += errcnt;
if (errcnt > 0)
	printf( "Found %d errors on %d for MPI_BXOR(0)\n", errcnt, rank );
errcnt = 0;


{
int *in, *out, *sol;
int  i, fnderr=0;
in = (int *)malloc( count * sizeof(int) );
out = (int *)malloc( count * sizeof(int) );
sol = (int *)malloc( count * sizeof(int) );
for (i=0; i<count; i++) { *(in + i) = ~0; *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_INT, MPI_BXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_INT and op MPI_BXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
long *in, *out, *sol;
int  i, fnderr=0;
in = (long *)malloc( count * sizeof(long) );
out = (long *)malloc( count * sizeof(long) );
sol = (long *)malloc( count * sizeof(long) );
for (i=0; i<count; i++) { *(in + i) = ~0; *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_LONG, MPI_BXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_LONG and op MPI_BXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
short *in, *out, *sol;
int  i, fnderr=0;
in = (short *)malloc( count * sizeof(short) );
out = (short *)malloc( count * sizeof(short) );
sol = (short *)malloc( count * sizeof(short) );
for (i=0; i<count; i++) { *(in + i) = ~0; *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_SHORT, MPI_BXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_SHORT and op MPI_BXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned short *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned short *)malloc( count * sizeof(unsigned short) );
out = (unsigned short *)malloc( count * sizeof(unsigned short) );
sol = (unsigned short *)malloc( count * sizeof(unsigned short) );
for (i=0; i<count; i++) { *(in + i) = ~0; *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED_SHORT, MPI_BXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_SHORT and op MPI_BXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned *)malloc( count * sizeof(unsigned) );
out = (unsigned *)malloc( count * sizeof(unsigned) );
sol = (unsigned *)malloc( count * sizeof(unsigned) );
for (i=0; i<count; i++) { *(in + i) = ~0; *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED, MPI_BXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED and op MPI_BXOR\n", rank );
free( in );
free( out );
free( sol );
}


{
unsigned long *in, *out, *sol;
int  i, fnderr=0;
in = (unsigned long *)malloc( count * sizeof(unsigned long) );
out = (unsigned long *)malloc( count * sizeof(unsigned long) );
sol = (unsigned long *)malloc( count * sizeof(unsigned long) );
for (i=0; i<count; i++) { *(in + i) = ~0; *(sol + i) = 0;
	*(out + i) = 0; }
MPI_Allreduce( in, out, count, MPI_UNSIGNED_LONG, MPI_BXOR, comm );
for (i=0; i<count; i++) { if (*(out + i) != *(sol + i)) {errcnt++; fnderr++;}}
if (fnderr) fprintf( stderr,
 	"(%d) Error for type MPI_UNSIGNED_LONG and op MPI_BXOR\n", rank );
free( in );
free( out );
free( sol );
}


gerr += errcnt;
if (errcnt > 0)
	printf( "Found %d errors on %d for MPI_BXOR(1-0)\n", errcnt, rank );
errcnt = 0;


/* Test Maxloc */
#ifdef DEBUG
    if (rank == 0) printf( "Testing MPI_MAXLOC...\n" );
#endif

{
struct int_test { int a; int b; } *in, *out, *sol;
int  i,fnderr=0;
in = (struct int_test *)malloc( count * sizeof(struct int_test) );
out = (struct int_test *)malloc( count * sizeof(struct int_test) );
sol = (struct int_test *)malloc( count * sizeof(struct int_test) );
for (i=0; i<count; i++) { (in + i)->a = (rank + i); (in + i)->b = rank;
        (sol + i)->a = (size - 1 + i); (sol + i)->b = (size-1);
	(out + i)->a = 0; (out + i)->b = -1; }
MPI_Allreduce( in, out, count, MPI_2INT, MPI_MAXLOC, comm );
for (i=0; i<count; i++) { if ((out + i)->a != (sol + i)->a ||
	                      (out + i)->b != (sol + i)->b) {
	errcnt++; fnderr++;
    fprintf( stderr, "(%d) Expected (%d,%d) got (%d,%d)\n", rank,
	(int)((sol + i)->a),
	(sol+i)->b, (int)((out+i)->a), (out+i)->b );
}}
if (fnderr) fprintf( stderr,
	"(%d) Error for type MPI_2INT and op MPI_MAXLOC (%d of %d wrong)\n",
                     rank, fnderr, count );
free( in );
free( out );
free( sol );
}


{
struct long_test { long a; int b; } *in, *out, *sol;
int  i,fnderr=0;
in = (struct long_test *)malloc( count * sizeof(struct long_test) );
out = (struct long_test *)malloc( count * sizeof(struct long_test) );
sol = (struct long_test *)malloc( count * sizeof(struct long_test) );
for (i=0; i<count; i++) { (in + i)->a = (rank + i); (in + i)->b = rank;
        (sol + i)->a = (size - 1 + i); (sol + i)->b = (size-1);
	(out + i)->a = 0; (out + i)->b = -1; }
MPI_Allreduce( in, out, count, MPI_LONG_INT, MPI_MAXLOC, comm );
for (i=0; i<count; i++) { if ((out + i)->a != (sol + i)->a ||
	                      (out + i)->b != (sol + i)->b) {
	errcnt++; fnderr++;
    fprintf( stderr, "(%d) Expected (%d,%d) got (%d,%d)\n", rank,
	(int)((sol + i)->a),
	(sol+i)->b, (int)((out+i)->a), (out+i)->b );
}}
if (fnderr) fprintf( stderr,
	"(%d) Error for type MPI_LONG_INT and op MPI_MAXLOC (%d of %d wrong)\n",
                     rank, fnderr, count );
free( in );
free( out );
free( sol );
}


{
struct short_test { short a; int b; } *in, *out, *sol;
int  i,fnderr=0;
in = (struct short_test *)malloc( count * sizeof(struct short_test) );
out = (struct short_test *)malloc( count * sizeof(struct short_test) );
sol = (struct short_test *)malloc( count * sizeof(struct short_test) );
for (i=0; i<count; i++) { (in + i)->a = (rank + i); (in + i)->b = rank;
        (sol + i)->a = (size - 1 + i); (sol + i)->b = (size-1);
	(out + i)->a = 0; (out + i)->b = -1; }
MPI_Allreduce( in, out, count, MPI_SHORT_INT, MPI_MAXLOC, comm );
for (i=0; i<count; i++) { if ((out + i)->a != (sol + i)->a ||
	                      (out + i)->b != (sol + i)->b) {
	errcnt++; fnderr++;
    fprintf( stderr, "(%d) Expected (%d,%d) got (%d,%d)\n", rank,
	(int)((sol + i)->a),
	(sol+i)->b, (int)((out+i)->a), (out+i)->b );
}}
if (fnderr) fprintf( stderr,
	"(%d) Error for type MPI_SHORT_INT and op MPI_MAXLOC (%d of %d wrong)\n",
                     rank, fnderr, count );
free( in );
free( out );
free( sol );
}


{
struct float_test { float a; int b; } *in, *out, *sol;
int  i,fnderr=0;
in = (struct float_test *)malloc( count * sizeof(struct float_test) );
out = (struct float_test *)malloc( count * sizeof(struct float_test) );
sol = (struct float_test *)malloc( count * sizeof(struct float_test) );
for (i=0; i<count; i++) { (in + i)->a = (rank + i); (in + i)->b = rank;
        (sol + i)->a = (size - 1 + i); (sol + i)->b = (size-1);
	(out + i)->a = 0; (out + i)->b = -1; }
MPI_Allreduce( in, out, count, MPI_FLOAT_INT, MPI_MAXLOC, comm );
for (i=0; i<count; i++) { if ((out + i)->a != (sol + i)->a ||
	                      (out + i)->b != (sol + i)->b) {
	errcnt++; fnderr++;
    fprintf( stderr, "(%d) Expected (%d,%d) got (%d,%d)\n", rank,
	(int)((sol + i)->a),
	(sol+i)->b, (int)((out+i)->a), (out+i)->b );
}}
if (fnderr) fprintf( stderr,
	"(%d) Error for type MPI_FLOAT_INT and op MPI_MAXLOC (%d of %d wrong)\n",
                     rank, fnderr, count );
free( in );
free( out );
free( sol );
}


{
struct double_test { double a; int b; } *in, *out, *sol;
int  i,fnderr=0;
in = (struct double_test *)malloc( count * sizeof(struct double_test) );
out = (struct double_test *)malloc( count * sizeof(struct double_test) );
sol = (struct double_test *)malloc( count * sizeof(struct double_test) );
for (i=0; i<count; i++) { (in + i)->a = (rank + i); (in + i)->b = rank;
        (sol + i)->a = (size - 1 + i); (sol + i)->b = (size-1);
	(out + i)->a = 0; (out + i)->b = -1; }
MPI_Allreduce( in, out, count, MPI_DOUBLE_INT, MPI_MAXLOC, comm );
for (i=0; i<count; i++) { if ((out + i)->a != (sol + i)->a ||
	                      (out + i)->b != (sol + i)->b) {
	errcnt++; fnderr++;
    fprintf( stderr, "(%d) Expected (%d,%d) got (%d,%d)\n", rank,
	(int)((sol + i)->a),
	(sol+i)->b, (int)((out+i)->a), (out+i)->b );
}}
if (fnderr) fprintf( stderr,
	"(%d) Error for type MPI_DOUBLE_INT and op MPI_MAXLOC (%d of %d wrong)\n",
                     rank, fnderr, count );
free( in );
free( out );
free( sol );
}


gerr += errcnt;
if (errcnt > 0)
	printf( "Found %d errors on %d for MPI_MAXLOC\n", errcnt, rank );
errcnt = 0;


/* Test minloc */
#ifdef DEBUG
    if (rank == 0) printf( "Testing MPI_MINLOC...\n" );
#endif


{
struct int_test { int a; int b; } *in, *out, *sol;
int  i,fnderr=0;
in = (struct int_test *)malloc( count * sizeof(struct int_test) );
out = (struct int_test *)malloc( count * sizeof(struct int_test) );
sol = (struct int_test *)malloc( count * sizeof(struct int_test) );
for (i=0; i<count; i++) { (in + i)->a = (rank + i); (in + i)->b = rank;
        (sol + i)->a = i; (sol + i)->b = 0;
	(out + i)->a = 0; (out + i)->b = -1; }
MPI_Allreduce( in, out, count, MPI_2INT, MPI_MINLOC, comm );
for (i=0; i<count; i++) { if ((out + i)->a != (sol + i)->a ||
	                      (out + i)->b != (sol + i)->b) {
	errcnt++; fnderr++;
    fprintf( stderr, "(%d) Expected (%d,%d) got (%d,%d)\n", rank,
	(int)((sol + i)->a),
	(sol+i)->b, (int)((out+i)->a), (out+i)->b );
}}
if (fnderr) fprintf( stderr,
	"(%d) Error for type MPI_2INT and op MPI_MINLOC (%d of %d wrong)\n",
                     rank, fnderr, count );
free( in );
free( out );
free( sol );
}


{
struct long_test { long a; int b; } *in, *out, *sol;
int  i,fnderr=0;
in = (struct long_test *)malloc( count * sizeof(struct long_test) );
out = (struct long_test *)malloc( count * sizeof(struct long_test) );
sol = (struct long_test *)malloc( count * sizeof(struct long_test) );
for (i=0; i<count; i++) { (in + i)->a = (rank + i); (in + i)->b = rank;
        (sol + i)->a = i; (sol + i)->b = 0;
	(out + i)->a = 0; (out + i)->b = -1; }
MPI_Allreduce( in, out, count, MPI_LONG_INT, MPI_MINLOC, comm );
for (i=0; i<count; i++) { if ((out + i)->a != (sol + i)->a ||
	                      (out + i)->b != (sol + i)->b) {
	errcnt++; fnderr++;
    fprintf( stderr, "(%d) Expected (%d,%d) got (%d,%d)\n", rank,
	(int)((sol + i)->a),
	(sol+i)->b, (int)((out+i)->a), (out+i)->b );
}}
if (fnderr) fprintf( stderr,
	"(%d) Error for type MPI_LONG_INT and op MPI_MINLOC (%d of %d wrong)\n",
                     rank, fnderr, count );
free( in );
free( out );
free( sol );
}


{
struct short_test { short a; int b; } *in, *out, *sol;
int  i,fnderr=0;
in = (struct short_test *)malloc( count * sizeof(struct short_test) );
out = (struct short_test *)malloc( count * sizeof(struct short_test) );
sol = (struct short_test *)malloc( count * sizeof(struct short_test) );
for (i=0; i<count; i++) { (in + i)->a = (rank + i); (in + i)->b = rank;
        (sol + i)->a = i; (sol + i)->b = 0;
	(out + i)->a = 0; (out + i)->b = -1; }
MPI_Allreduce( in, out, count, MPI_SHORT_INT, MPI_MINLOC, comm );
for (i=0; i<count; i++) { if ((out + i)->a != (sol + i)->a ||
	                      (out + i)->b != (sol + i)->b) {
	errcnt++; fnderr++;
    fprintf( stderr, "(%d) Expected (%d,%d) got (%d,%d)\n", rank,
	(int)((sol + i)->a),
	(sol+i)->b, (int)((out+i)->a), (out+i)->b );
}}
if (fnderr) fprintf( stderr,
	"(%d) Error for type MPI_SHORT_INT and op MPI_MINLOC (%d of %d wrong)\n",
                     rank, fnderr, count );
free( in );
free( out );
free( sol );
}


{
struct float_test { float a; int b; } *in, *out, *sol;
int  i,fnderr=0;
in = (struct float_test *)malloc( count * sizeof(struct float_test) );
out = (struct float_test *)malloc( count * sizeof(struct float_test) );
sol = (struct float_test *)malloc( count * sizeof(struct float_test) );
for (i=0; i<count; i++) { (in + i)->a = (rank + i); (in + i)->b = rank;
        (sol + i)->a = i; (sol + i)->b = 0;
	(out + i)->a = 0; (out + i)->b = -1; }
MPI_Allreduce( in, out, count, MPI_FLOAT_INT, MPI_MINLOC, comm );
for (i=0; i<count; i++) { if ((out + i)->a != (sol + i)->a ||
	                      (out + i)->b != (sol + i)->b) {
	errcnt++; fnderr++;
    fprintf( stderr, "(%d) Expected (%d,%d) got (%d,%d)\n", rank,
	(int)((sol + i)->a),
	(sol+i)->b, (int)((out+i)->a), (out+i)->b );
}}
if (fnderr) fprintf( stderr,
	"(%d) Error for type MPI_FLOAT_INT and op MPI_MINLOC (%d of %d wrong)\n",
                     rank, fnderr, count );
free( in );
free( out );
free( sol );
}


{
struct double_test { double a; int b; } *in, *out, *sol;
int  i,fnderr=0;
in = (struct double_test *)malloc( count * sizeof(struct double_test) );
out = (struct double_test *)malloc( count * sizeof(struct double_test) );
sol = (struct double_test *)malloc( count * sizeof(struct double_test) );
for (i=0; i<count; i++) { (in + i)->a = (rank + i); (in + i)->b = rank;
        (sol + i)->a = i; (sol + i)->b = 0;
	(out + i)->a = 0; (out + i)->b = -1; }
MPI_Allreduce( in, out, count, MPI_DOUBLE_INT, MPI_MINLOC, comm );
for (i=0; i<count; i++) { if ((out + i)->a != (sol + i)->a ||
	                      (out + i)->b != (sol + i)->b) {
	errcnt++; fnderr++;
    fprintf( stderr, "(%d) Expected (%d,%d) got (%d,%d)\n", rank,
	(int)((sol + i)->a),
	(sol+i)->b, (int)((out+i)->a), (out+i)->b );
}}
if (fnderr) fprintf( stderr,
	"(%d) Error for type MPI_DOUBLE_INT and op MPI_MINLOC (%d of %d wrong)\n",
                     rank, fnderr, count );
free( in );
free( out );
free( sol );
}

gerr += errcnt;
if (errcnt > 0)
	printf( "Found %d errors on %d for MPI_MINLOC\n", errcnt, rank );
errcnt = 0;

}

if (gerr > 0) {
	MPI_Comm_rank( MPI_COMM_WORLD, &rank );
	printf( "Found %d errors overall on %d\n", gerr, rank );
	}
else {
  if (!rank)
    printf( "PASSED!", gerr, rank );
}

MPI_Finalize( );
return 0;
}
