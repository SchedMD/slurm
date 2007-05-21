int Get_size() const { 
  int s; MPIX_CALL( MPI_Group_size( the_real_group, &s )); return s; }
int Get_rank() const {
  int r; MPIX_CALL( MPI_Group_rank( the_real_group, &r )); return r; }
static void Translate_ranks (const Group& group1, int n, const int ranks1[], 
			     const Group& group2, int ranks2[]);
static int Compare(const Group& group1, const Group& group2) {
  int c; MPIX_CALL( MPI_Group_compare( (MPI_Group)group1.the_real_group, 
				       (MPI_Group)group2.the_real_group,
				       &c )); return c; }
static Group Union(const Group& group1, const Group& group2) {
   Group *group = new Group;
   MPIX_CALL( MPI_Group_union( (MPI_Group)group1.the_real_group,
			       (MPI_Group)group2.the_real_group,
			       (MPI_Group*)&group->the_real_group ) );
   return *group;
}
static Group Intersect(const Group& group1, const Group& group2);
static Group Difference(const Group& group1, const Group& group2);
Group Incl(int n, const int ranks[]) const;
Group Excl(int n, const int ranks[]) const;
Group Range_incl(int n, const int ranges[][3]) const;
Group Range_excl(int n, const int ranges[][3]) const;
void Free() { MPIX_CALL( MPI_Group_free( &the_real_group ));
    if (the_real_group == MPI_GROUP_NULL) { delete this; }}
