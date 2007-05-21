typedef void Errhandler_fn(File &, int *, ... );
void Call_errhandler(int errorcode) const;
Errhandler Get_errhandler() const;
static Errhandler Create_errhandler(Errhandler_fn* function);
void Set_errhandler(const Errhandler& errhandler);
Aint Get_type_extent(const Datatype& datatype) const;
Group Get_group() const;
Info Get_info() const;
Offset Get_byte_offset(const Offset disp) const;
Offset Get_position() const;
Offset Get_position_shared() const;
Offset Get_size() const { 
    MPI_Offset size; 
    MPIX_CALL( MPI_File_get_size( the_real_file, &size ) ); return size; };
Request Iread(void* buf, int count, const Datatype& datatype);
Request Iread_at(Offset offset, void* buf, int count, 
		 const Datatype& datatype);
Request Iread_shared(void* buf, int count, const Datatype& datatype);
Request Iwrite(const void* buf, int count, const Datatype& datatype);
Request Iwrite_at(Offset offset, const void* buf, int count, 
		  const Datatype& datatype);
Request Iwrite_shared(const void* buf, int count, const Datatype& datatype);
bool Get_atomicity() const;
int Get_amode() const;
static File Open(const Intracomm& comm, const char* filename, 
		 int amode, const Info& info) { 
    File *f = new(File);
    MPIX_CALL( MPI_File_open( (MPI_Comm)(Comm) comm, (char *)filename, amode, 
			      (MPI_Info)(Info)info, &f->the_real_file) ); 
    return *f;
}
static void Delete(const char* filename, const Info& info);
void Close() { MPIX_CALL( MPI_File_close( &the_real_file ) ); }
void Get_view(Offset& disp, Datatype& etype, Datatype& filetype,
	      char* datarep) const;
void Preallocate(Offset size) { 
  MPIX_CALL( MPI_File_preallocate( the_real_file, (MPI_Offset)size ) ); }
void Read(void* buf, int count, const Datatype& datatype)
{ MPIX_CALL( MPI_File_read( the_real_file, buf, count, 
		       (MPI_Datatype)(Datatype)datatype, MPI_STATUS_IGNORE ));}
void Read(void* buf, int count, const Datatype& datatype, Status& status)
{ MPIX_CALL( MPI_File_read( the_real_file, buf, count, 
			    (MPI_Datatype)(Datatype)datatype, 
			    &status.the_real_status ));}
void Read_all(void* buf, int count, const Datatype& datatype);
void Read_all(void* buf, int count, const Datatype& datatype, Status& status);
void Read_all_begin(void* buf, int count, const Datatype& datatype);
void Read_all_end(void* buf);
void Read_all_end(void* buf, Status& status);
void Read_at(Offset offset, void* buf, int count, const Datatype& datatype);
void Read_at(Offset offset, void* buf, int count, const Datatype& datatype, 
	     Status& status);
void Read_at_all(Offset offset, void* buf, int count, 
		 const Datatype& datatype);
void Read_at_all(Offset offset, void* buf, int count, 
		 const Datatype& datatype, Status& status);
void Read_at_all_begin(Offset offset, void* buf, int count,
		       const Datatype& datatype);
void Read_at_all_end(void* buf);
void Read_at_all_end(void* buf, Status& status);
void Read_ordered(void* buf, int count, const Datatype& datatype);
void Read_ordered(void* buf, int count, const Datatype& datatype, 
		  Status& status);
void Read_ordered_begin(void* buf, int count, const Datatype& datatype);
void Read_ordered_end(void* buf);
void Read_ordered_end(void* buf, Status& status);
void Read_shared(void* buf, int count, const Datatype& datatype);
void Read_shared(void* buf, int count, const Datatype& datatype,
		 Status& status);
void Seek(Offset offset, int whence);
void Seek_shared(Offset offset, int whence);
void Set_atomicity(bool flag);
void Set_info(const Info& info);
void Set_size(Offset size);
void Set_view(Offset disp, const Datatype& etype, const Datatype& filetype, 
	      const char* datarep, const Info& info) { 
    MPIX_CALL( MPI_File_set_view( the_real_file, disp, 
				  (MPI_Datatype)(Datatype)etype, 
				  (MPI_Datatype)(Datatype)filetype, 
				  (char *)datarep, (MPI_Info)(Info)info ) ); }
void Sync();
void Write(const void* buf, int count, const Datatype& datatype);
void Write(const void* buf, int count, const Datatype& datatype, 
	   Status& status);
void Write_all(const void* buf, int count, const Datatype& datatype);
void Write_all(const void* buf, int count, const Datatype& datatype, 
	       Status& status);
void Write_all_begin(const void* buf, int count, const Datatype& datatype);
void Write_all_end(const void* buf);
void Write_all_end(const void* buf, Status& status);
void Write_at(Offset offset, const void* buf, int count, 
	      const Datatype& datatype);
void Write_at(Offset offset, const void* buf, int count, 
	      const Datatype& datatype, Status& status);
void Write_at_all(Offset offset, const void* buf, int count, 
		  const Datatype& datatype);
void Write_at_all(Offset offset, const void* buf, int count, 
		  const Datatype& datatype, Status& status);
void Write_at_all_begin(Offset offset, const void* buf, int count, 
			const Datatype& datatype);
void Write_at_all_end(const void* buf);
void Write_at_all_end(const void* buf, Status& status);
void Write_ordered(const void* buf, int count, const Datatype& datatype);
void Write_ordered(const void* buf, int count, const Datatype& datatype, 
		   Status& status);
void Write_ordered_begin(const void* buf, int count, const Datatype& datatype);
void Write_ordered_end(const void* buf);
void Write_ordered_end(const void* buf, Status& status);
void Write_shared(const void* buf, int count, const Datatype& datatype);
void Write_shared(const void* buf, int count, const Datatype& datatype, 
		  Status& status);
