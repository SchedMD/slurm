#include "mpi.h"
#include "mpicxxbase.h"

// This file implements the support routines for the C++ wrappers

namespace MPI {

  const Datatype MPI::CHAR(MPI_CHAR);
  const Datatype MPI::UNSIGNED_CHAR(MPI_UNSIGNED_CHAR);
  const Datatype MPI::BYTE(MPI_BYTE);
  const Datatype MPI::SHORT(MPI_SHORT);
  const Datatype MPI::UNSIGNED_SHORT(MPI_UNSIGNED_SHORT);
  const Datatype MPI::INT(MPI_INT);
  const Datatype MPI::UNSIGNED(MPI_UNSIGNED);
  const Datatype MPI::LONG(MPI_LONG);
  const Datatype MPI::UNSIGNED_LONG(MPI_UNSIGNED_LONG);
  const Datatype MPI::FLOAT(MPI_FLOAT);
  const Datatype MPI::DOUBLE(MPI_DOUBLE);
  const Datatype MPI::LONG_DOUBLE(MPI_LONG_DOUBLE);
  const Datatype MPI::LONG_LONG_INT(MPI_LONG_LONG_INT);
  const Datatype MPI::LONG_LONG(MPI_LONG_LONG);
  const Datatype MPI::PACKED(MPI_PACKED);
  const Datatype MPI::LB(MPI_LB);
  const Datatype MPI::UB(MPI_UB);
  const Datatype MPI::FLOAT_INT(MPI_FLOAT_INT);
  const Datatype MPI::DOUBLE_INT(MPI_DOUBLE_INT);
  const Datatype MPI::LONG_INT(MPI_LONG_INT);
  const Datatype MPI::SHORT_INT(MPI_SHORT_INT);
  const Datatype MPI::TWOINT(MPI_2INT);
  const Datatype MPI::LONG_DOUBLE_INT(MPI_LONG_DOUBLE_INT);

  Intracomm MPI::COMM_WORLD(MPI_COMM_WORLD);
  Intracomm MPI::COMM_SELF(MPI_COMM_SELF);
  const Comm MPI::COMM_NULL;

  const Group MPI::GROUP_EMPTY(MPI_GROUP_EMPTY);

  const Op MPI::MAX(MPI_MAX);
  const Op MPI::MIN(MPI_MIN);
  const Op MPI::SUM(MPI_SUM);
  const Op MPI::PROD(MPI_PROD);
  const Op MPI::LAND(MPI_LAND);
  const Op MPI::BAND(MPI_BAND);
  const Op MPI::LOR(MPI_LOR);
  const Op MPI::BOR(MPI_BOR);
  const Op MPI::LXOR(MPI_LXOR);
  const Op MPI::BXOR(MPI_BXOR);
  const Op MPI::MINLOC(MPI_MINLOC);
  const Op MPI::MAXLOC(MPI_MAXLOC);

  const int   MPI::IDENT(MPI_IDENT);
  const int   MPI::CONGRUENT(MPI_CONGRUENT);
  const int   MPI::SIMILAR(MPI_SIMILAR);
  const int   MPI::UNEQUAL(MPI_UNEQUAL);

void MPI_CXX_Init( void )
{
}

  // Functions that are not class members must be defined here

void Init(int& argc, char**& argv) { 
  MPI_Init( &argc, &argv ); 
  MPI_CXX_Init();
}

int Get_error_class(int errorcode) {
  int ec;
  MPIX_CALL(MPI_Error_class( errorcode, &ec ) ); return ec; }

inline double Wtime() { return MPI_Wtime(); }
inline double Wtick() { return MPI_Wtick(); }

void Finalize() { MPI_Finalize(); }

bool Is_initialized() {int i; MPI_Initialized( &i ); return (i==1);}

} // namespace MPI
