#include <stdio.h>
#include <slurm/pmi2.h>


int main(int argc, char **argv)
{
  int spawned, size, rank, appnum;
  int ret;

  ret = PMI2_Init(&spawned, &size, &rank, &appnum);
  printf("spawned=%d, size=%d, rank=%d, appnum=%d\n", spawned, size, rank, appnum);
  PMI2_Finalize();

  return 0;
}
