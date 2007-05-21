#include "mpi.h"
#include "test.h"
#include <stdio.h>

/* The original version of this was sent by  
   empierce@tribble.llnl.gov (Elsie M. Pierce) 
   I've modified it to fit the automated tests requirements
 */
/* Prototypes for picky compilers */
int iinit ( int *, int, int );
int ilist1 ( int *, int, int, int );
void Build_vect ( MPI_Datatype * );
void Build_ctg ( int, MPI_Datatype *, MPI_Datatype * );
void Get_d5 ( int );

int iinit(a, value, l)
int *a, value, l;
{
  int i;
  
  for (i=0; i<l; i++)
    a[i] = value;
  return(0);
}


int ilist1(a, mype, pe_out, l)
int *a, mype, pe_out, l;
{
  int i;
  
  if (mype == pe_out){
    for (i=0; i<l; i++)
      printf("%d ",a[i]);
    printf("\n");
  }
  return(0);
}


void Build_vect(mess_ptr) 
MPI_Datatype* mess_ptr;
{
  int count, bllen, gap, str;
  
/*   Define an MPI type with two blocks of 3 integers each, separated */
/*   by one integer. */
  count	= 2;
  bllen	= 3;
  gap	= 1;
  str	= bllen + gap;

  MPI_Type_vector(count, bllen, str, MPI_INT, mess_ptr);
  MPI_Type_commit(mess_ptr);
  
}


void   Build_ctg(big_offset,messtyp, messtyp2)
int big_offset;
MPI_Datatype *messtyp, *messtyp2;
{
  int count;
  MPI_Aint ext;
    
  count=3;
  MPI_Type_extent(*messtyp, &ext);
  MPI_Type_hvector(count, 1, ext+big_offset, *messtyp, messtyp2);
  MPI_Type_commit(messtyp2);
  /*printf( "pack is:\n" );
  MPIR_PrintDatatypePack( stdout, 1, *messtyp2, 0, 0 );
  printf( "unpack is:\n" );
  MPIR_PrintDatatypeUnpack( stdout, 1, *messtyp2, 0, 0 ); */
}
  


void Get_d5(my_rank)
int my_rank;
{
  MPI_Datatype messtyp, messtyp2;
  int root=0;
  int count=1;
  int i, big_offset;
  int intlen;
#define DL 32
  
  int dar[DL];
     
  i=iinit(dar, my_rank, DL);
  Build_vect(&messtyp);
  MPI_Bcast(dar, count, messtyp, root, MPI_COMM_WORLD);
  if (my_rank==1)
    printf("  0 = Sent, 1 = Not Sent \n%s",
	   "  Vector Type with Gap : \n");
  i=ilist1(dar, my_rank, 1, DL);

  intlen = sizeof(int);
  for (big_offset = -intlen; big_offset<=2*intlen; 
       big_offset += intlen){
    if (my_rank==1)
     printf("\n Three of above vector types combined, with offset = %i ints\n",
	     big_offset/(int)sizeof(int));
    i=iinit(dar, my_rank, DL);
    Build_ctg(big_offset, &messtyp, &messtyp2);
    MPI_Bcast(dar, count, messtyp2, root, MPI_COMM_WORLD);
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Type_free(&messtyp2);
    i=ilist1(dar, my_rank, 1, DL);
  }
  MPI_Type_free( &messtyp );
}



int main( int argc, char *argv[]) 
{
  int my_rank;
    
  MPI_Init (&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

  Get_d5(my_rank);
  
  MPI_Finalize();
  return 0;
}
