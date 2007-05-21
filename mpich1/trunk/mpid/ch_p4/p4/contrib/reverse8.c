#include <stdio.h>

void main () {
  unsigned char a[8];
  int i, c;

  i = 0;

  while ((c=getchar()) != EOF) {
    a[i] = c;
/*    fprintf(stderr," read %d in a[%d]\n", (int)a[i], i ); */
    i++;
    if (i==8) {
      for (i=7; i>=0; i--) {
/*        fprintf(stderr,"write %d from a[%d]\n", (int)a[i], i ); */
	putchar( a[i] );
      }
      i = 0;
    }
  }
}
