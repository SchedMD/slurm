public class mpd_procs
{
    private java.util.TreeSet procs;
    public mpd_procs()
    {
	procs = new java.util.TreeSet();
    }
    public int getSize()
    {
	return procs.size();
    }
    public void addProc(Node newnode)
    {
	procs.add(newnode);
    }
    public void remProc(String name)
    {
	java.util.Iterator it = procs.iterator();
	while (it.hasNext())
	    {
		if (((Node)it.next()).name.equals(name))
		    it.remove();
	    }
    }
    public void replaceProc(String oldname, Node newnode)
    {
	java.util.Iterator it = procs.iterator();
	while (it.hasNext())
	    {
		if (((Node)it.next()).name.equals(oldname))
		    it.remove();
	    }
	procs.add(newnode);
    }
    public int indexOf(String name)
    {
	java.util.Iterator it = procs.iterator();
	int index = 0;
	while (it.hasNext())
	    {
		if (((Node)it.next()).name.equals(name))
		    return index;
		else
		    index++;
	    }
	return -1;
    }
    public java.util.ArrayList toArrayList()
    {
	java.util.ArrayList retVector = new java.util.ArrayList();
	java.util.Iterator it = procs.iterator();
	while (it.hasNext())
	    {
		retVector.add(it.next());
	    }
	return retVector;
    }
    public Object getData(int idx, String property)
    {
	int index = 0;
	java.util.Iterator it = procs.iterator();
	Node n;
	while (it.hasNext())
	    {
		n = (Node)it.next();
		if (index == idx)
		    {
			return n.getProperty(property);
		    }
		else
		    index++;
	    }
	return "ERROR ERROR BAD THINGS";
    }
    public boolean contains(String name)
    {
	java.util.Iterator it = procs.iterator();
	while (it.hasNext())
	    {
		if (((Node)it.next()).name.equals(name))
		    return true;
	    }
	return false;
    }
    /*
      public Object getData(int idx, String property)
      {
      java.util.Iterator it = procs.iterator();
      int i = 0;
      while (i < idx)
      {
      it.next();
      i++;
      }
      return ((Node)it.next()).getProperty(property);
      }*/
}
