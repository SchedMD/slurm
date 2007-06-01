 Graphcomm Dup() const;
 void Get_dims(int nnodes[], int nedges[]) const ;
 void Get_topo(int maxindex, int maxedges,   int index[], int edges[]) const ;
  int Get_neighbors_count(int rank) const;
 void Get_neighbors(int rank, int maxneighbors,   int neighbors[]) const ;
  int Map(int nnodes, const int index[],   const int edges[]) const ;
