Cartcomm Dup() const;
int Get_dim() const;
void Get_topo(int maxdims, int dims[], bool periods[],   int coords[]) const ;
int Get_cart_rank(const int coords[]) const ;
void Get_coords(int rank, int maxdims, int coords[])   const ;
void Shift(int direction, int disp,   int& rank_source, int& rank_dest) const ;
Cartcomm Sub(const bool remain_dims[]) const;
int Map(int ndims, const int dims[], const bool periods[]) const ;
