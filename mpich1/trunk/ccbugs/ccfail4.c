#define OFF  3
#include <math.h>
/* We need some way to include malloc definition for this program */
#include <stdlib.h> 

typedef struct {
  void **array;
} test_struct;

int main( argc, argv )
int argc;
char **argv;
{
    int i,b;
    void *a,*c;
    test_struct *joe;

    joe = (test_struct *) malloc( sizeof(test_struct));
    joe->array = (void **) malloc( 100*sizeof(void*));
  
    if (argc > 1)   b = atof(argv[1]);  else b = 1;
    for ( i=0; i<10; i++ ) {
	joe->array[i] = (void *) malloc( 10*sizeof(int));;
	printf("%d %p\n",i,joe->array[i]);}

    a = joe->array[OFF+b+1];
    c = joe->array[OFF+1+b];

    printf("%p\n",a);
    printf("%p\n",c);
    return a != c;
}
