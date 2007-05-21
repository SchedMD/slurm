
#include "confdefs.h"
int main(int argc, char *argv[])
{
    return 0;
}
#ifdef HAVE_LONG_DOUBLE
long double f( int n, long double b[], long double c[] )
{
int i;
long double a[100];

for (i=0; i<n; i++) 
    a[i] = ((b[i])>(c[i]))?(b[i]):(c[i]);
return (int)a[10];
}
#endif
