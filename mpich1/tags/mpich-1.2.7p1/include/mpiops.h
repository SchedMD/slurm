#ifndef MPIR_OP_COOKIE
/* MPI combination function */
#define MPIR_OP_COOKIE 0xca01beaf
struct MPIR_OP {
  MPI_User_function *op;
  MPIR_COOKIE 
  int               commute;
  int               permanent;
};

#endif
