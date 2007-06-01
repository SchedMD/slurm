public class MyrTable extends javax.swing.JScrollPane
{
    private MJTable table;
    private MyrTableModel model;
    public MyrTable()
    {
	super();
	table = new MJTable();
	this.setViewportView(table);
    }
    public void update_data(java.util.LinkedList newinfo, java.util.LinkedList columns)
    {
	if ((columns.size() != table.getColumnCount()) || (newinfo.size() != table.getRowCount()))
	{
	    model = new MyrTableModel(newinfo, columns);
	    table = new MJTable(model);
	    this.setViewportView(table);
	}
	else
	{
	    for (int i = 0; i < newinfo.size(); i++)
	    {
		for (int j = 0; j < ((java.util.LinkedList)newinfo.get(i)).size(); j++)
		{
		    table.setValueAt(((java.util.LinkedList)newinfo.get(i)).get(j), i, j);
		}
	    }
	}
    }
    public void reset()
    {
	model.reset();
    }
    private class MyrTableModel extends javax.swing.table.AbstractTableModel
    {
	private java.util.LinkedList columnnames;
	private java.util.LinkedList data;
	private java.util.Vector reset_cnts;
	public MyrTableModel(java.util.LinkedList rowdata, java.util.LinkedList columns)
	{
	    columnnames = columns;
	    data = rowdata;
	    reset_cnts = new java.util.Vector();
	    reset_cnts.setSize(data.size());
	    
	    for (int i = 0; i < reset_cnts.size(); i++)
	    {
		reset_cnts.set(i, new java.util.Vector());
		((java.util.Vector)reset_cnts.get(i)).setSize(columns.size());
		for (int j = 0; j < ((java.util.Vector)reset_cnts.get(i)).size(); j++)
		{
		    ((java.util.Vector)reset_cnts.get(i)).set(j, new Integer(0));
		}
	    }
	}
	public Object getValueAt(int row, int column)
	{
	    String dataInt = (String)(((java.util.LinkedList)data.get(row)).get(column));
	    if (column == 0) return dataInt;
	    else 
	    {
		Integer resetInt = (Integer)(((java.util.Vector)reset_cnts.get(row)).get(column));
		return new Integer(Integer.decode(dataInt).intValue() - resetInt.intValue());
	    }
	}
	public int getRowCount()
	{
	    return data.size();
	}
	public int getColumnCount()
	{
	    return columnnames.size();
	}
	public void setValueAt(Object value, int row, int column)
	{
	    java.util.LinkedList temp = (java.util.LinkedList)data.get(row);
	    temp.set(column, value);
	    fireTableCellUpdated(row,column);
	}
	public String getColumnName(int c)
	{
	    return (String)columnnames.get(c);
	}
	public void reset()
	{
	    for (int i = 0; i < reset_cnts.size(); i++)
	    {
		for (int j = 1; j <  ((java.util.Vector)reset_cnts.get(i)).size(); j++)
		{
		    Integer temp = Integer.decode((String)(((java.util.LinkedList)data.get(i)).get(j)));
		    ((java.util.Vector)reset_cnts.get(i)).set(j, Integer.decode((String)(((java.util.LinkedList)data.get(i)).get(j))));
		}
	    }
	}
    }
    private class MJTable extends javax.swing.JTable
    {
	public MJTable()
	{
	    super();
	}
	public MJTable(javax.swing.table.TableModel m)
	{
	    super(m);
	}
	public Class getColumnClass(int column)
	{
	    Class returnclass = null;
	    if (column == 0)
		{
		    try
			{
			    returnclass = Class.forName("java.lang.String");
			}
		    catch (ClassNotFoundException e) {}
		}
	    else
		{
		    try
			{
			    returnclass =  Class.forName("java.lang.Integer");
			}
		    catch (ClassNotFoundException e) {}
		}
	    return returnclass;
	}
    }
}
