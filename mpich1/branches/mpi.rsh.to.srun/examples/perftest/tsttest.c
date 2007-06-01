/* Sample program to test the automatic test routine */

#include <math.h>
#include <stdio.h>

double f( x, result, ctx )
double x, *result;
void  *ctx;
{
double fv;

fv = sin(x);
result[0] = x;
result[1] = fv;
return fv;
}

double f2( x, result, ctx )
double x, *result;
void  *ctx;
{
double f;

f = sin(x);
f += floor(x);
result[0] = x;
result[1] = f;
return f;
}

int main( argc, argv )
int  argc;
char **argv;
{
int    nvals, rmax, i;
double *results, *r;
double rtol = 1.0e-2;

rmax    = 1000;
results = (double *)malloc( rmax * 2 * sizeof(double) );

nvals   = TSTAuto1d( 0.0, 7.0, 0.01, 0.2, rtol, 1.0e-10,
                     results, 2*sizeof(double), rmax, f, (void *)0 );
if (nvals == rmax) printf( "Underresolved (increase rmax)\n" );

TSTRSort( results, 2*sizeof(double), nvals );
r   = results;
fprintf( stdout, "title top 'sin(x)'\n" );
for (i=0; i<nvals; i++) {
    fprintf( stdout, "%f %f\n", r[0], r[1] );
    r += 2;
    }        

nvals   = TSTAuto1d( 0.0, 7.0, 0.01, 0.2, rtol, 1.0e-10,
                     results, 2*sizeof(double), rmax, f2, (void *)0 );
if (nvals == rmax) printf( "Underresolved (increase rmax)\n" );

TSTRSort( results, 2*sizeof(double), nvals );
r   = results;
fprintf( stdout, "title top 'sin(x)+int(x)'\n" );
for (i=0; i<nvals; i++) {
    fprintf( stdout, "%f %f\n", r[0], r[1] );
    r += 2;
    }        

return 0;
}
