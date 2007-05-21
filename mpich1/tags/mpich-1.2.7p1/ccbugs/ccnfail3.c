#include <stdio.h>
int main(argc,argv)
int argc;
char **argv;
{
char b;
int  i, a[10];

for (i=0; i<10; i++) {
    ((char *)a)[i] = 0x80;
}
b = 0x80;
if ( ((char *)a)[0] != b) { return 1; }
return 0;
}



