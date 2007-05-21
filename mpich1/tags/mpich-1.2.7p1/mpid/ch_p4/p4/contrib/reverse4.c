#include <stdio.h>

#define NBYTES 4

void main () {
  unsigned char a[NBYTES];
  int i, c;

  i = 0;

  while ((c=getchar()) != EOF) {
    a[i] = c;
/*    fprintf(stderr," read %d in a[%d]\n", (int)a[i], i ); */
    i++;
    if (i==NBYTES) {
      for (i=(NBYTES-1); i>=0; i--) {
/*        fprintf(stderr,"write %d from a[%d]\n", (int)a[i], i ); */
	putchar( a[i] );
      }
      i = 0;
    }
  }
}
