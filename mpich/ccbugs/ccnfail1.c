#include <stdio.h>
/* 
 * This program breaks the Convex C compiler at higher levels of optimization
 * because it fails to properly truncate the value of f.
 * Thanks to Gary Applegate@convex.com for this example
 * 
 * The example has been modified slightly to produce a failure in the
 * return code.
 */
int tstrange( f )
int f;
{
return (f > 255);
}

main () { 

  int j;
  unsigned char f;
  int rc = 0;

  for(j=704; j< 1025; j+=16) {
     f = (unsigned char) j;
     /* printf("%d: %d  %d\n", j, f, (unsigned char) j); */
     if  (tstrange( (int)f )) rc = 1;
  }
 return rc;
}
