#ifndef __MPI_BINDINGS
#define __MPI_BINDINGS
#define _MPI_INCLUDE
#include "mpi.h"
//
// To do:  make non-static functions virtual.  Note that the standard
// requires this (see "Class Member Functions for MPI").
// We may want to make this optional so that we can measure the difference
// in performance.  The macro MPICH_NO_VIRTUAL_CXX could turn this off if
// we used MPICH_virtual instead of virtual, and #defined MPICH_virtual
// as either virtual or empty (or inline!)
//

// C++ information 
#include "mpicxxconf.h"
// Namespace support is required for this implementation
namespace MPI {

  // Basic types
  typedef MPI_Aint   Aint;
  typedef MPI_Offset Offset;
  typedef MPI_Fint   Fint;

  // Special implementation macros
#ifdef HAVE_CXX_EXCEPTIONS
  // This is not completely correct yet; we need to ensure that an error code 
  // is returned.  What do we do for other error handlers?
  // Should there be a special invocation switch?  Should we avoid inlining
  // to maintain separation from the user's code (we can then use the internal
  // structure of the objects within the implementation of the C++ wrappers,
  // much as we could within the Fortran wrappers.
  // An alternative is to have a function that gets the error behavior to
  // use before calling the C version of the routine; this routine can 
  // know details of the internal structures.
  // 
  // If we adopt the layered call handling, then this is just another 
  // instance of a nested call
  // Note also that we want to use the MPI, not the PMPI, routines, to 
  // allow the use of MPI tracing to work with the C++ calls (as allowed 
  // by the standard), rather than by implementing the pmpi names in C++
#define MPIX_CALL( fnc ) \
{int err; err = fnc; if (err) throw Exception(err);}
#else
#define MPIX_CALL( fnc ) (void)fnc
#endif

  // MPI classes

  class Exception { 
  protected:
    int the_real_code;
  public: 
    // new/delete
    inline Exception()        { the_real_code = MPI_SUCCESS; }
    inline Exception(int err) { the_real_code = err; }
    inline ~Exception(){};

    int Get_error_code() const { return the_real_code; }; 
    int Get_error_class() const { 
      int err_class; MPI_Error_class( the_real_code, &err_class ); 
      return err_class; }; 
    const char *Get_error_string() const {
      char *str = new char[MPI_MAX_ERROR_STRING]; int len;
      MPI_Error_string( the_real_code, str, &len ); return str;}; 
  }; 

  class Datatype {
    friend class Comm;    // Many functions
    friend class Status;  // Needed for Get_count and Get_elements
  protected:
    MPI_Datatype the_real_dtype;
  public:
    // new/delete
    inline Datatype(MPI_Datatype dtype) { the_real_dtype = dtype; }
    inline Datatype(void) {the_real_dtype = MPI_DATATYPE_NULL;}
    virtual ~Datatype() {}
    // copy/assignment
    Datatype :: Datatype(const Datatype &dtype) {
      the_real_dtype = dtype.the_real_dtype; }
    Datatype& Datatype::operator=(const Datatype&dtype) {
      the_real_dtype = dtype.the_real_dtype; return *this; }
    // logical
    bool operator== (const Datatype &dtype) {
      return (the_real_dtype == dtype.the_real_dtype); }
    bool operator!= (const Datatype &dtype) {
      return (the_real_dtype != dtype.the_real_dtype); }
    // C/C++ cast and assignment
    inline operator MPI_Datatype*() { return &the_real_dtype; }
    inline operator MPI_Datatype() { return the_real_dtype; }
    Datatype& Datatype::operator=(const MPI_Datatype& dtype) {
      the_real_dtype = dtype; return *this; }

    // MPI 1 functions
#include "mpi1cppdtype.h"
    // MPI 2 functions
#include "mpi2cppdtype.h"
  }; // Datatype

  class Info {
  protected:
    MPI_Info the_real_info;
  public:
    // new/delete
    inline Info(MPI_Info info) { the_real_info = info; }
    inline Info(void) {the_real_info = MPI_INFO_NULL;}
    virtual ~Info() {}
    // copy/assignment
    Info :: Info(const Info &info) {
      the_real_info = info.the_real_info; }
    Info& Info::operator=(const Info& info) {
      the_real_info = info.the_real_info; return *this; }
    // logical
    bool operator== (const Info &info) {
      return (the_real_info == info.the_real_info); }
    bool operator!= (const Info &info) {
      return (the_real_info != info.the_real_info); }
    // C/C++ cast and assignment
    inline operator MPI_Info*() { return &the_real_info; }
    inline operator MPI_Info() { return the_real_info; }
    Info& Info::operator=(const MPI_Info& info) {
      the_real_info = info; return *this; }
    // MPI 2 functions
#include "mpi2cppinfo.h"
  }; // Info

  class Status {
    friend class Comm;
    friend class File;
    friend class Request;
  protected: 
    MPI_Status the_real_status;
  public:
    // new/delete
    inline Status(MPI_Status status) { the_real_status = status; }
    inline Status(void) {}
    virtual ~Status() {}
    // copy/assignment
    Status :: Status(const Status &status) {
      the_real_status = status.the_real_status; }
    Status& Status::operator=(const Status& status) {
      the_real_status = status.the_real_status; return *this; }
    // logical
    // No comparision of status because it is not an opaque structure
    // C/C++ cast and assignment
    inline operator MPI_Status() { return the_real_status; }
    inline operator MPI_Status*() { return &the_real_status; }
    Status& Status::operator=(const MPI_Status& status) {
      the_real_status = status; return *this; }

    // MPI 1 functions
#include "mpi1cppst.h"
    // MPI 2 functions
#include "mpi2cppst.h"
  }; // Status

  class Group {
    friend class Comm; 
  protected:
    MPI_Group the_real_group;
  public:
    // new/delete
    inline Group(MPI_Group group) { the_real_group = group; }
    inline Group(void) {the_real_group = MPI_GROUP_NULL;}
    virtual ~Group(){}
    // copy/assignment
    Group :: Group(const Group &group) {
      the_real_group = group.the_real_group; }
    Group& Group::operator=(const Group& group) {
      the_real_group = group.the_real_group; return *this; }
    // logical
    bool operator== (const Group &group) {
      return (the_real_group == group.the_real_group);}
    bool operator!= (const Group &group) {
      return (the_real_group != group.the_real_group);}
    // C/C++ cast and assignment
    inline operator MPI_Group*() { return &the_real_group; }
    inline operator MPI_Group() { return the_real_group; }
    Group& Group::operator=(const MPI_Group& group) {
      the_real_group = group; return *this; }

    // MPI 1 functions
#include "mpi1cppgroup.h"
  }; // Group

  class Op {
  protected:
    MPI_Op the_real_op;
  public:
    // new/delete
    inline Op(MPI_Op op) { the_real_op = op; }
    inline Op(void) {the_real_op = MPI_OP_NULL;}
    // copy/assignment
    Op :: Op(const Op &op) { the_real_op = op.the_real_op; }
    Op& Op::operator=(const Op&op) {
      the_real_op = op.the_real_op; return *this; }
    // logical
    bool operator== (const Op &op) {
      return (the_real_op == op.the_real_op);}
    bool operator!= (const Op &op) {
      return (the_real_op != op.the_real_op);}
    // C/C++ cast and assignment
    inline operator MPI_Op*() { return &the_real_op; }
    inline operator MPI_Op() { return the_real_op; }
    Op& Op::operator=(const MPI_Op& op) {
      the_real_op = op; return *this; }

    // MPI 1 functions
    // A complication here is that the MPI implementation knows how to call
    // C routines, not C++ routines.
#include "mpi1cppop.h"
  }; // Op

  class Errhandler {
  protected:
    MPI_Errhandler the_real_errhandler;
  public:
    // new/delete
    inline Errhandler( MPI_Errhandler eh ) { the_real_errhandler = eh; }
    inline Errhandler (void) { the_real_errhandler = MPI_ERRHANDLER_NULL; }
    virtual ~Errhandler() {}
    // copy/assignment
    Errhandler :: Errhandler(const Errhandler &eh) {
      the_real_errhandler = eh.the_real_errhandler; }
    Errhandler& Errhandler::operator=(const Errhandler&eh) {
      the_real_errhandler = eh.the_real_errhandler; return *this; }
    // logical
    bool operator== (const Errhandler &eh) {
      return (the_real_errhandler == eh.the_real_errhandler);}
    bool operator!= (const Errhandler &eh) {
      return (the_real_errhandler != eh.the_real_errhandler);}
    // C/C++ cast and assignment
    inline operator MPI_Errhandler*() { return &the_real_errhandler; }
    inline operator MPI_Errhandler() { return the_real_errhandler; }
    Errhandler& Errhandler::operator=(const MPI_Errhandler& eh) {
      the_real_errhandler = eh; return *this; }

    // MPI 1 functions
#include "mpi1cpperrh.h"
  }; // Errhandler

  class Request {
  protected:
    MPI_Request the_real_req;
    // Need Comm for pack/unpack/packsize
    friend class Comm;
  public:
    // new/delete
    inline Request(MPI_Request req) { the_real_req = req; }
    inline Request(void) {the_real_req = MPI_REQUEST_NULL; }
    virtual ~Request() {}
    // copy/assignment
    Request :: Request( const Request &req ) {
      the_real_req = req.the_real_req; }
    Request& Request::operator=(const Request&req) { 
      the_real_req = req.the_real_req; return *this; }
    // logical
    bool operator== (const Request &req) {
      return (the_real_req == req.the_real_req); }
    bool operator!= (const Request &req) {
      return (the_real_req != req.the_real_req); }
    // C/C++ cast and assignment
    inline operator MPI_Request*() { return &the_real_req; }
    inline operator MPI_Request() { return the_real_req; }
    Request& Request::operator=(const MPI_Request&req) { 
      the_real_req = req; return *this; }

    // MPI 1 functions
#include "mpi1cppreq.h"
    // MPI 2 functions
#include "mpi2cppreq.h"
  };  // Request

  class Prequest : public Request {
  protected:
    MPI_Request the_real_req;
  public:
    // MPI  1 functions
#include "mpi1cpppreq.h"
  }; // Prequest

  class Comm { 
      friend class Cartcomm;
      friend class Intercomm;
      friend class Intracomm;
      friend class Graphcomm;
  protected:
    MPI_Comm the_real_comm;

  public:
    // new/delete
    inline Comm(void) { the_real_comm = MPI_COMM_NULL; }
    inline Comm(MPI_Comm comm) { the_real_comm = comm; }
    // copy/assignment
    Comm :: Comm(const Comm &comm) {
      the_real_comm = comm.the_real_comm; }
    // logical
    // C/C++ cast and assignment
    inline operator MPI_Comm*() { return &the_real_comm; }
    inline operator MPI_Comm() { return the_real_comm; }
    Comm& Comm::operator=(const MPI_Comm& comm) {
      the_real_comm = comm; return *this; }

    // MPI 1 functions
#include "mpi1cppcomm.h"
    // MPI 2 functions
#include "mpi2cppcomm.h"
  };  // Comm

  class Intercomm: public Comm {
    // Need Comm for Merge
    friend class Intracomm;
  protected:
    MPI_Comm the_real_comm;
  public:
    // new/delete
    // copy/assignment
    // logical
    // C/C++ cast and assignment
    inline Intercomm(MPI_Comm comm) { the_real_comm = comm; }
    inline Intercomm(void) {the_real_comm = MPI_COMM_NULL;}
    // MPI 1 functions
#include "mpi1cppinter.h"
    // MPI 2 functions
#include "mpi2cppinter.h"
  }; // Intercomm

  class Intracomm : public Comm {
  public: 
    // For the create_cart/graph routines
    friend class Cartcomm;
    friend class Graphcomm;

    // new/delete
    // copy/assignment
    // logical
    // C/C++ cast and assignment
    inline Intracomm(MPI_Comm comm) { the_real_comm = comm; }
    inline Intracomm(void) {the_real_comm = MPI_COMM_NULL;}
    inline operator MPI_Comm*() { return &the_real_comm; }
    inline operator MPI_Comm() { return the_real_comm; }
    // MPI 1 functions
#include "mpi1cppintra.h"
    // MPI 2 functions
#include "mpi2cppintra.h"
  }; // Intracomm

  class Grequest: public Request {
    // MPI 2 functions
  public:
    // new/delete
    // copy/assignment
    // logical
    // C/C++ cast and assignment
#include "mpi2cppgreq.h"
  }; // Grequest;

#ifdef HAVE_MPI_WIN_CREATE
  class Win { 
  protected:
    MPI_Win the_real_win;
  public:
    // new/delete
    inline Win(MPI_Win win) { the_real_win = win; }
    inline Win(void) {the_real_win = MPI_WIN_NULL;}
    // copy/assignment
    Win :: Win(const Win &win) {
      the_real_win = win.the_real_win; }
    Win& Win::operator=(const Win&win) {
      the_real_win = win.the_real_win; return *this; }
    // logical
    bool operator== (const Win &win) {
      return (the_real_win == win.the_real_win); }
    bool operator!= (const Win &win) {
      return (the_real_win != win.the_real_win); }
    // C/C++ cast and assignment
    inline operator MPI_Win*() { return &the_real_win; }
    inline operator MPI_Win() { return the_real_win; }
    Win& Win::operator=(const MPI_Win& the_real_win);

    // MPI 2 functions
#include "mpi2cppwin.h"
  }; // Win;
#endif

  class File {
  protected:
    MPI_File the_real_file;
  public:
    // new/delete
    inline File(MPI_File file) { the_real_file = file; }
    inline File(void) {the_real_file = MPI_FILE_NULL;}
    virtual ~File() {};
    // copy/assignment
    File :: File(const File &file) {
      the_real_file = file.the_real_file; }
    File& File::operator=(const File&file) {
      the_real_file = file.the_real_file; return *this; }
    // logical
    bool operator== (const File &file) {
      return (the_real_file == file.the_real_file); }
    bool operator!= (const File &file) {
      return (the_real_file != file.the_real_file); }
    // C/C++ cast and assignment
    inline operator MPI_File*() { return &the_real_file; }
    inline operator MPI_File() { return the_real_file; }
    File& File::operator=(const MPI_File& file) {
      the_real_file = file; return *this; }

    // MPI 2 functions
#include "mpi2cppfile.h"
  }; // File;

  class Graphcomm : public Intracomm {
  protected:
    MPI_Comm the_real_comm;
  public:
    // new/delete
    // copy/assignment
    // logical
    // C/C++ cast and assignment

    // MPI 1 functions
#include "mpi1cppgraph.h"
  }; // Graphcomm;

  class Cartcomm  : public Intracomm {
  protected:
    MPI_Comm the_real_comm;
  public:
    // new/delete
    inline Cartcomm(void) {the_real_comm = MPI_COMM_NULL;}
    inline Cartcomm(MPI_Comm comm) { the_real_comm = comm; }
    virtual ~Cartcomm() {}
    // copy/assignment
    Cartcomm :: Cartcomm(const Comm &comm) {
      the_real_comm = comm.the_real_comm; }
    // logical
    // C/C++ cast and assignment
    inline operator MPI_Comm*() { return &the_real_comm; }
    inline operator MPI_Comm() { return the_real_comm; }
    Cartcomm& Cartcomm::operator=(const MPI_Comm& the_real_comm);
// MPI 1 functions
#include "mpi1cppcart.h"
  }; // Cartcomm;
  
  // General MPI namespace
  void MPI_CXX_Init( void );

  // Predefined communicators (not const - see 10.1.5)
  extern Intracomm COMM_WORLD;
  extern Intracomm COMM_SELF;

  // Predefined datatypes
  // Can these be const in MPI-2 (set_name?)
  extern const Datatype CHAR;
  extern const Datatype UNSIGNED_CHAR;
  extern const Datatype BYTE;
  extern const Datatype SHORT;
  extern const Datatype UNSIGNED_SHORT;
  extern const Datatype INT;
  extern const Datatype UNSIGNED;
  extern const Datatype LONG;
  extern const Datatype UNSIGNED_LONG;
  extern const Datatype FLOAT;
  extern const Datatype DOUBLE;
  extern const Datatype LONG_DOUBLE;
  extern const Datatype COMPLEX;
  extern const Datatype DOUBLE_COMPLEX;
  extern const Datatype LONG_DOUBLE_COMPLEX;
  extern const Datatype LONG_LONG_INT;
  extern const Datatype LONG_LONG;
  extern const Datatype UNSIGNED_LONG_LONG;
  extern const Datatype PACKED;
  extern const Datatype LB;
  extern const Datatype UB;
  extern const Datatype FLOAT_INT;
  extern const Datatype DOUBLE_INT;
  extern const Datatype LONG_INT;
  extern const Datatype TWOINT;
  extern const Datatype SHORT_INT;
  extern const Datatype LONG_DOUBLE_INT;

  // C++ only datatypes
  extern const Datatype BOOL;

  // C++ names for Fortran datatypes
  extern const Datatype CHARACTER;
  extern const Datatype INTEGER;
  extern const Datatype REAL;
  extern const Datatype DOUBLE_PRECISION;
  extern const Datatype LOGICAL;
  extern const Datatype F_COMPLEX;

  extern const Datatype TWOREAL;
  extern const Datatype TWODOUBLE_PRECISION;
  extern const Datatype TWOINTEGER;
  extern const Datatype F_DOUBLE_COMPLEX;
  // C++ names for optional Fortran types
  extern const Datatype INTEGER1;
  extern const Datatype INTEGER2;
  extern const Datatype INTEGER4;
  extern const Datatype INTEGER8;
  extern const Datatype REAL4;
  extern const Datatype REAL8;
  extern const Datatype REAL16;

  // Functions part of MPI but no particular class

#include "mpi1cppgen.h"
#include "mpi2cppgen.h"

  // Static values and "execution-time" constants
  extern const Comm COMM_NULL;

  // Results of compare
  extern const int IDENT;
  extern const int CONGRUENT;
  extern const int SIMILAR;
  extern const int UNEQUAL;

  // Error Classes
  extern const int SUCCESS;
  extern const int ERR_BUFFER;
  //etc.

  // Point-to-point constants
  extern const int ANY_TAG;
  extern const int ANY_SOURCE;
  extern const int PROC_NULL;
  extern const int UNDEFINED;
  extern const int BSEND_OVERHEAD;

  // Misc constants
  extern const int KEYVAL_INVALID;

  // String sizes
  extern const int MAX_PROCESSOR_NAME;
  extern const int MAX_ERROR_STRING;
  //??  extern const int MAX_OBJECT_NAME;

};  // MPI namespace

#endif

