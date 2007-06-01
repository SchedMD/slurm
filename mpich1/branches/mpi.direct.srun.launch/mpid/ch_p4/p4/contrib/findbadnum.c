#include <stdio.h>

main () {
  float x, y;
  char c[50];
  int i=0;
  struct float_format {
#ifdef FREEBSD
    unsigned int mant : 23;
    unsigned int exp : 8;
    unsigned int sign : 1;
#else
    unsigned int sign : 1;
    unsigned int exp : 8;
    unsigned int mant : 23;
#endif
  };

  union readable_float {
    float f;
    struct float_format s;
    unsigned char c[ sizeof( float ) ];
  } z;

/*
  printf( "struct = %d, float = %d, union = %d\n",
	  sizeof( struct float_format ), sizeof( float ),
	  sizeof( union readable_float ) );
  z.f = 25.0;
  printf( "z=%f, %u %u %u | %u %u %u %u\n", z.f, z.s.sign, z.s.exp, z.s.mant,
	  (int)((unsigned char *)&z)[0],
	  (int)((unsigned char *)&z)[1],
	  (int)((unsigned char *)&z)[2],
	  (int)((unsigned char *)&z)[3] );
  z.f = -25.0;
  printf( "z=%f, %u %u %u | %u %u %u %u\n", z.f, z.s.sign, z.s.exp, z.s.mant,
	  (int)((unsigned char *)&z)[0],
	  (int)((unsigned char *)&z)[1],
	  (int)((unsigned char *)&z)[2],
	  (int)((unsigned char *)&z)[3] );
  z.s.exp = 132;
  printf( "z=%f, %u %u %u | %u %u %u %u\n", z.f, z.s.sign, z.s.exp, z.s.mant,
	  (int)((unsigned char *)&z)[0],
	  (int)((unsigned char *)&z)[1],
	  (int)((unsigned char *)&z)[2],
	  (int)((unsigned char *)&z)[3] );
  z.c[0] = 0x01;
  z.c[1] = 0x00;
  z.c[2] = 0x80;
  z.c[3] = 0x7f;
  printf( "z=%f, %u %u %u | %u %u %u %u\n", z.f, z.s.sign, z.s.exp, z.s.mant,
	  (int)((unsigned char *)&z)[0],
	  (int)((unsigned char *)&z)[1],
	  (int)((unsigned char *)&z)[2],
	  (int)((unsigned char *)&z)[3] );
*/

  while (fread( &z, sizeof( float ), 1, stdin)) {
    if (z.s.exp == 255 && z.s.mant!=0) {
      fprintf( stderr,  "%d doesn't look like a number (%u %u %u).\n", i,
	       z.s.sign, z.s.exp, z.s.mant);
      z.s.mant = 0;
    } else {
      /* fprintf( stderr, "(%u %u) ", z.s.exp, z.s.mant ); */
    }
    fwrite( &z, sizeof( float ), 1, stdout );
    i++;
  }

}
