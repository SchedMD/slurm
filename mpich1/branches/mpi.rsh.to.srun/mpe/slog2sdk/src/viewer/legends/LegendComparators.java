/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */
package viewer.legends;

import java.util.Comparator;

import base.drawable.Topology;
import base.drawable.Category;
import viewer.common.Parameters;

public class LegendComparators
{
    public  static final Comparator INDEX_ORDER
                                    = new IndexOrder();
    private static final Comparator PREVIEW_ORDER
                                    = new PreviewOrder();
    private static final Comparator TOPOLOGY_ORDER
                                    = new TopologyOrder();
    public  static final Comparator CASE_SENSITIVE_ORDER
                                    = new CaseSensitiveOrder();
    public  static final Comparator CASE_INSENSITIVE_ORDER
                                    = new CaseInsensitiveOrder();

    public static class IndexOrder implements Comparator
    {
        public int compare( Object o1, Object o2 )
        {
            Category type1 = (Category) o1;
            Category type2 = (Category) o2;
            return type1.getIndex() - type2.getIndex();
        }
    }

    /*
       This comparator gives preference over Preview drawable
       All Preview object's category indexes are negative as defined in
       logformat/clogTOdrawable/InputLog.java & logformat/trace/InputLog.java.
    */
    public static class PreviewOrder implements Comparator
    {
        public int compare( Object o1, Object o2 )
        {
            Category type1  = (Category) o1;
            Category type2  = (Category) o2;
            int      pview1 = ( type1.getIndex() < 0 ? 0 : 1 );
            int      pview2 = ( type2.getIndex() < 0 ? 0 : 1 );
            return pview1 - pview2;
        }
    }

    public static class TopologyOrder implements Comparator
    {
        public int compare( Object o1, Object o2 )
        {
            Category type1      = (Category) o1;
            Category type2      = (Category) o2;
            return   type2.getTopology().hashCode()
                   - type1.getTopology().hashCode();
            // intentionally reversed, so arrow < state < event
        }
    }

    public static class CaseSensitiveOrder implements Comparator
    {
        public int compare( Object o1, Object o2 )
        {
            Category type1      = (Category) o1;
            Category type2      = (Category) o2;
            int      diff       = 0;
            if ( Parameters.LEGEND_TOPOLOGY_ORDER ) {
                diff = TOPOLOGY_ORDER.compare( type1, type2 );
                if ( diff != 0 )
                    return diff;
            }
            if ( Parameters.LEGEND_PREVIEW_ORDER ) {
                diff = PREVIEW_ORDER.compare( type1, type2 );
                if ( diff != 0 )
                    return diff;
            }
            return type1.getName().compareTo( type2.getName() );
        }
    }

    public static class CaseInsensitiveOrder implements Comparator
    {
        public int compare( Object o1, Object o2 )
        {
            Category type1      = (Category) o1;
            Category type2      = (Category) o2;
            int      diff       = 0;
            if ( Parameters.LEGEND_TOPOLOGY_ORDER ) {
                diff = TOPOLOGY_ORDER.compare( type1, type2 );
                if ( diff != 0 )
                    return diff;
            }
            if ( Parameters.LEGEND_PREVIEW_ORDER ) {
                diff = PREVIEW_ORDER.compare( type1, type2 );
                if ( diff != 0 )
                    return diff;
            }
            return type1.getName().compareToIgnoreCase( type2.getName() );
        }
    }
}
