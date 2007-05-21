#include <stdio.h>
#include "mpi.h"
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#else
extern char *malloc();
#endif
#include "test.h"

typedef struct _table {
  int references;
  int length;
  int *value;
} Table;

/* Prototypes for picky compilers */
int copy_table ( MPI_Comm, int, void *, void *, void *, int * );
void create_table ( int, int *, Table ** );
int delete_table ( MPI_Comm, int, void *, void * );

/* These are incorrect...*/
int copy_table ( MPI_Comm oldcomm, int keyval, void *extra_state, 
		 void *attr_in, void *attr_out, int *flag)
{
  Table *table = (Table *)attr_in;;

  table->references++;
  (*(void **)attr_out) = attr_in;
  (*flag) = 1;
  (*(int *)extra_state)++;
  return (MPI_SUCCESS);
}

void create_table ( int num, int *values, Table **table_out )
{
  int i;
  (*table_out) = (Table *)malloc(sizeof(Table));
  (*table_out)->references = 1;
  (*table_out)->length = num;
  (*table_out)->value = (int *)malloc(sizeof(int)*num);
  for (i=0;i<num;i++) 
    (*table_out)->value[i] = values[i];
}

int delete_table ( MPI_Comm comm, int keyval, 
		   void *attr_val, void *extra_state)
{
  Table *table = (Table *)attr_val;

  if ( table->references == 1 )
	free(table);
  else
	table->references--;
  (*(int *)extra_state)--;
  return MPI_SUCCESS;
}

int main ( int argc, char **argv )
{
  int rank, size;
  Table *table;
  MPI_Comm new_comm;
  int table_key;
  int values[3]; 
  int table_copies = 1;
  int found;
  int errors = 0;

  MPI_Init ( &argc, &argv );
  MPI_Comm_rank ( MPI_COMM_WORLD, &rank );
  MPI_Comm_size ( MPI_COMM_WORLD, &size );

  values[0] = 1; values[1] = 2; values[2] = 3;
  create_table(3,values,&table);
  
  MPI_Keyval_create ( copy_table, delete_table, &table_key, 
		      (void *)&table_copies );
  MPI_Attr_put ( MPI_COMM_WORLD, table_key, (void *)table );
  MPI_Comm_dup ( MPI_COMM_WORLD, &new_comm );
  MPI_Attr_get ( new_comm, table_key, (void **)&table, &found );

  if (!found) {
      printf( "did not find attribute on new comm\n" );
      errors++;
  }

  if ((table_copies != 2) && (table->references != 2)) {
      printf( "table_copies != 2 (=%d) and table->references != 2 (=%d)\n",
	      table_copies, table->references );
      errors++;
  }

  MPI_Comm_free ( &new_comm );

  if ((table_copies != 1) && (table->references != 1)) {
      printf( "table_copies != 1 (=%d) and table->references != 1 (=%d)\n",
	      table_copies, table->references );
      errors++;
  }

  MPI_Attr_delete ( MPI_COMM_WORLD, table_key );

  if ( table_copies != 0 ) {
      printf( "table_copies != 0 (=%d)\n", table_copies );
      errors++;
  }
  if (errors)
    printf("[%d] OOPS.  %d errors!\n",rank,errors);

  MPI_Keyval_free ( &table_key );
  Test_Waitforall( );
  MPI_Finalize();
  return 0;
}
