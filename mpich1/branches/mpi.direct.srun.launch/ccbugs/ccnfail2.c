#include <stdio.h>
int main(argc,argv)
int argc;
char **argv;
{
double x, y;
x = 0.2;
y =  ((0.5/3.0)*x)-((0.5/3.0)*x);
if (y != 0.0) {
    printf( "x*y-y*x = %e\n", y );
    return 1;
    }
return 0;
}
