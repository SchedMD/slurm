/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.drawable;

public class YCoordMap
{
    private int        num_rows;
    private int        num_columns;
    private String     title_name;
    private String[]   column_names;   /* of length num_columns - 1 */
    private int[]      map_elems;      /* of length num_rows * num_columns */
    private Method[]   methods;

    public YCoordMap( int Nrows, int Ncolumns,
                      String title, String[] col_names,
                      int[] ielems, int[] methodIDs )
    {
        num_rows     = Nrows;
        num_columns  = Ncolumns;
        title_name   = title;
        column_names = col_names;
        map_elems    = ielems;
        this.setMethodIDs( methodIDs );
    }

    private void setMethodIDs( int[] methodIDs )
    {
        if ( methodIDs != null && methodIDs.length > 0 ) {
            methods = new Method[ methodIDs.length ];
            for ( int idx = 0; idx < methodIDs.length; idx++ )
                methods[ idx ] = new Method( methodIDs[ idx ] );
        }
        else
            methods = null;
    }

    public void setMethods( Method[] in_methods )
    {
        methods  = in_methods;
    }

    public int getNumOfRows()
    {
        return num_rows;
    }

    public int getNumOfColumns()
    {
        return num_columns;
    }

    public String getTitleName()
    {
        return title_name;
    }

    public String[] getColumnNames()
    {
        return column_names;
    }

    public Method[] getMethods()
    {
        return methods;
    }

    public int[] getMapElems()
    {
        return map_elems;
    }

    public String toString()
    {
        int num_columns_minus_one;
        int irow, icol, idx;
        StringBuffer rep = new StringBuffer( "Title: " + title_name + "\n" );
        rep.append( "Column Labels: LineID -> " );
        num_columns_minus_one = num_columns - 1;
        for ( icol = 0; icol < num_columns_minus_one; icol++ )
            rep.append( column_names[ icol ] + " " );
        rep.append( "\n" );
        idx = 0;
        for ( irow = 0; irow < num_rows; irow++ ) {
            rep.append( "\t" + map_elems[ idx++ ] + " -> " );
            for ( icol = 0; icol < num_columns_minus_one; icol++ )
                rep.append( " " + map_elems[ idx++ ] );
            rep.append( "\n" );
        }
        if ( methods != null && methods.length > 0 ) {
            rep.append( "Methods=< " );
            for ( idx = 0; idx < methods.length; idx++ )
                rep.append( methods[ idx ] + " " );
            rep.append( ">\n" );
        }
        return rep.toString();
    }
}
