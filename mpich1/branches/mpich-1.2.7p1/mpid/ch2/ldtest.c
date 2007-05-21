#include <stdio.h>
int main( int argc, char ** argv )
{
    long double d, *dp;
    long l[4], *lout;
    char *p;
    int i;
    
    printf( "Sizeof long double is %d\n", sizeof(long double) );
    printf( "Sizeof long is %d\n", sizeof(long) );
    l[0] = 0x01020304;
    l[1] = 0x05060708;
    l[2] = 0x09101112;
    l[3] = 0x13141516;
    dp = (long double *)l;
    d = *dp;
    lout = (long *)dp;
    p = (char *)dp;
    for (i=0; i<4; i++) {
	printf( "%x%x%x%x ", p[0], p[1], p[2], p[3] );
	p += 4;
    }
    printf( "\n" );
    return 0;
}
