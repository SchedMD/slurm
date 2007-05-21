/*
 * Validate a proposed cluster description
 * 
 * Assumptions:
 * 1. A cluster is either a collection of (smaller) clusters or a collection
 * of individual machines.  
 * 2. An individual node is in exactly one cluster of individual machines.
 * That cluster, of course, may be listed in many clusters.
 * 3. The performance of communication within a cluster is unaffected by
 * communication outside of the cluster.
 * 4. The performance of a link within a cluster may be bounded per process
 * or cluster, or may have a maximum aggregate that may be shared.
 * 5. ? Need some limit on the graph of clusters?  Is it a tree?  Must it
 * be acyclic?
 * 
 * Consequences:
 * Assumption 3 allows communication measurements to take place concurrently
 * among disjoint clusters.
 * Assumption 4 requires the performance tests to take into account shared
 * communication resources
 *
 * Description of cluster
 * A file containing:
 *
 * name [ number of processors ]
 * clustername name name name ...
 * clustername clustername clustername ...
 *
 * The first name is being defined.  If there are additional names, then
 * the name is defining a cluster, containing the named nodes or clusters (but
 * not both).
 * If the name is either alone or listed with an integer, then the 
 * name describes a single node.
 * 
 * This system describes a hierarchy or completely connected nodes.  However,
 * there need not be a cluster that contains all nodes.  For example, for a
 * system with only nearest-neighbor links, a different cluster description 
 * could be used for each 
 *
 * Special cases:
 * 1. Define a collection of nodes with similar names made of a name and a
 * range of numbers from n1 to n2:
 * name%d n1 n2 [ number of processors ]
 * 2. Define a cluster of names with similar names as in 1:
 * cluster name%d n1 n2 
 *    or
 * cluster clustername%d n1 n2
 * 
 * This makes it eaiser to define large systems such as Chiba.
 * (This could be managed by a separate step that created a full file from
 * an abbreviated version.)
 *
 * Definitions:
 * Cluster depth is defined recursively.  The cluster depth of a node is zero.
 * The depth of a cluster is one greater than the maximum depth of any member
 * of the cluster.
 *
 * Algorithm for validating description:
 *
 * for depth = 1, ..., maxdepth
 *    for each cluster at this depth
 *        Measure performance within cluster (with other clusters silent)
 *    endfor
 *    Measure performance within cluster (all clusters at this level at
 *       the same time)
 *    Compare results; if measureable difference, report failure of cluster
 *       description at this depth.
 * endfor    
 * 
 * To measure performance within a cluster:
 *    If cluster is made up of nodes,
 *       measure bisection bandwidth for several patterns.
 *    If cluster of cluster, ditto on a cluster by cluster basis (
 *       for each link, take min(number in each cluster) and then 
 *       have that many processes in each cluster exchange with the 
 *       partner cluster).
 * 
 * Issues for discussion:
 *   (From Rusty):
I read the draft, and my only concern is that it treat CPU's as the
fundamental unit.  The language seems to treat "node" as the fundamental
unit.  We need to deal with single multi-cpu nodes as clusters, even though
it is awkward to create names for the individual cpu's.  Well, not to create
them, but perhaps to apply them.
 *
 *    (From Bill):
 *    The assumption is that within any cluster, all links are the same.
 *    This makes it hard to describe things like mesh-connected machines,
 *    where there are neighbors, but full performance is available to only
 *    one neighbor at a time.  What is missing from the above description is
 *    the notion of a gateway for the network at the "surface" of the cluster.
 *    All traffic from the cluster passes through the appropriate gateway;
 *    there may be many or few gateways.
 *    
 *    Question: does this suggest that we want a more complex description?
 *    For example, we could allow a graph, with the clique a special node type
 *    that would allow graphs of clusters to be described?
 */
