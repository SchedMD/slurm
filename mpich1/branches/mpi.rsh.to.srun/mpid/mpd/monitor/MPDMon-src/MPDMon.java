public class MPDMon extends javax.swing.JFrame
{
    private int port;
    private String host;
    public String mpd_id;
    private String selecttype;
    private String nodesstring;
    public int jobid;
    private String[] load_data;
    private String[] mem_data;
    private javax.swing.JCheckBox load_indep;
    private javax.swing.JLabel load_num;
    private javax.swing.JLabel time_num;
    private javax.swing.JPanel nodepanel;
    private javax.swing.JPanel load_output;
    private javax.swing.JPanel mem_output;
    private javax.swing.JPanel myr_output;
    private javax.swing.JPanel options;
    private javax.swing.JPanel load_options;
    private javax.swing.JPanel mem_options;
    private javax.swing.JPanel myr_options;
    private javax.swing.JPanel sys_options;
    private javax.swing.JPanel type_options;
    private javax.swing.JScrollPane load_scroller;
    private javax.swing.JScrollPane mem_scroller;
    private javax.swing.JScrollPane options_scroller;
    private javax.swing.JTextField rangetext;
    private javax.swing.JTextField jobidtext;
    private javax.swing.JButton nodes_jobid_button;
    private javax.swing.JButton nodes_range_button;
    private MyrTable myrtable;
    private MyrTable memtable;
    private myr_counter_box myrboxes;
    private mem_counter_box memboxes;
    private int updatetime;
    private javax.swing.JTabbedPane main_tabpane;
    private mpd_procs procs;
    private java.net.Socket mpdsock;
    private java.io.InputStreamReader in;
    private java.io.OutputStreamWriter out;
    private java.util.LinkedList datatypes;
    private NodeParser nparser;
    public static void main(String[] args)
    {
	long time;
	MPDMon MonWin;

	if (args.length < 2)
	    {
		System.out.println("Usage: java MPDMon <host> <port>");
		System.exit(1);
	    }
	MonWin = new MPDMon(args);
	MonWin.pack();
	MonWin.setVisible(true);
	for (;;)
	    {
		time = System.currentTimeMillis();
		while (System.currentTimeMillis() - time < MonWin.getUpdateTime())
		    {
		    }
		MonWin.update(MonWin.mpd_id, MonWin.jobid);
		MonWin.repaint();
	    }
    }
    public MPDMon(String[] args)
    {
	super("MPD Remote Monitor");
	// System.out.println("started constructor");
	this.addWindowListener(new WindowCloser());
	this.addComponentListener(new WindowResizer());
	Object[] sockstuff;
	int load_length;
	int mem_length;
	CheckBoxListener clisten;
	javax.swing.JSlider load_slide;
	javax.swing.JSlider time_slide;
	javax.swing.JCheckBox load_type_box;
	javax.swing.JCheckBox mem_type_box;
	javax.swing.JCheckBox myr_type_box;
	javax.swing.JButton mem_types_aux;
	javax.swing.JButton myr_types_aux;
	javax.swing.JButton myr_reset;
	javax.swing.JRadioButton nodes_range;
	javax.swing.JRadioButton nodes_jobid;
	javax.swing.JRadioButton nodes_all;
	javax.swing.JPanel rangepanel;
	javax.swing.JPanel jobidpanel;
	javax.swing.JPanel radiobuttonpanel;
	javax.swing.JPanel textpanel;
	javax.swing.JLabel rangelabel;
	javax.swing.JLabel jobidlabel;
	javax.swing.ButtonGroup bgroup;
	NodeSelectListener nslisten;
	ReqTypeOKButtonListener rtoblisten;
	mpd_id = "";
	selecttype = "all";
	nodesstring = "";
	myrboxes = new myr_counter_box();
	memboxes = new mem_counter_box();
	myrtable = new MyrTable();
	memtable = new MyrTable();
	nodepanel = new javax.swing.JPanel();
	load_options = new javax.swing.JPanel();
	mem_options = new javax.swing.JPanel();
	myr_options = new javax.swing.JPanel();
	sys_options = new javax.swing.JPanel();
	rangepanel = new javax.swing.JPanel();
	jobidpanel = new javax.swing.JPanel();
	textpanel = new javax.swing.JPanel();
	radiobuttonpanel = new javax.swing.JPanel();
	nslisten = new NodeSelectListener();
	rtoblisten = new ReqTypeOKButtonListener();
	nparser = new NodeParser();
	datatypes = new java.util.LinkedList();
	load_options.setBorder(new javax.swing.border.TitledBorder(javax.swing.BorderFactory.createTitledBorder(javax.swing.BorderFactory.createEtchedBorder(), "Load average options")));
	mem_options.setBorder(new javax.swing.border.TitledBorder(javax.swing.BorderFactory.createTitledBorder(javax.swing.BorderFactory.createEtchedBorder(), "Memory use options")));
	myr_options.setBorder(new javax.swing.border.TitledBorder(javax.swing.BorderFactory.createTitledBorder(javax.swing.BorderFactory.createEtchedBorder(), "Myrinet counter options")));
	sys_options.setBorder(new javax.swing.border.TitledBorder(javax.swing.BorderFactory.createTitledBorder(javax.swing.BorderFactory.createEtchedBorder(), "System options")));
	javax.swing.JLabel load_options_label = new javax.swing.JLabel("Load average history:");
	javax.swing.JLabel mem_options_label = new javax.swing.JLabel("Memory use history:");
	javax.swing.JLabel time_options_label = new javax.swing.JLabel("Update frequency(ms)");
	rangelabel = new javax.swing.JLabel("Range:");
	jobidlabel = new javax.swing.JLabel("Job ID:");
	bgroup = new javax.swing.ButtonGroup();
	host = args[0];
	port = Integer.parseInt(args[1]);
	procs = new mpd_procs();
	//***********************
	updatetime = 3000;
	load_indep = new javax.swing.JCheckBox("Graphs scale independently", true);
	load_num = new javax.swing.JLabel(Integer.toString(100));
	load_length = 100;
	mem_length = 100;
	//***********************
	nodes_all = new javax.swing.JRadioButton("All nodes", true);
	nodes_range = new javax.swing.JRadioButton("Range/List of nodes");
	nodes_jobid = new javax.swing.JRadioButton("Job ID");
	nodes_all.setActionCommand("nodes_all");
	nodes_range.setActionCommand("nodes_range");
	nodes_jobid.setActionCommand("nodes_jobid");
	nodes_all.addActionListener(nslisten);
	nodes_range.addActionListener(nslisten);
	nodes_jobid.addActionListener(nslisten);
	rangetext = new javax.swing.JTextField(10);
	jobidtext = new javax.swing.JTextField(4);
	//	rangetext.setMaximumSize(new java.awt.Dimension(30,10));
	//	jobidtext.setMaximumSize(new java.awt.Dimension(30,10));
	rangetext.setEnabled(false);
	jobidtext.setEnabled(false);
	time_num = new javax.swing.JLabel(Integer.toString(updatetime));
	load_slide = new javax.swing.JSlider(10, 999, load_length);
	time_slide = new javax.swing.JSlider(500, 5000, updatetime);
	load_type_box = new javax.swing.JCheckBox("Load Averages", true);
	mem_type_box = new javax.swing.JCheckBox("Memory Statistics", false);
	myr_type_box = new javax.swing.JCheckBox("Myrinet Statistics", false);
	clisten = new CheckBoxListener(load_type_box, mem_type_box, myr_type_box);
	load_type_box.addItemListener(clisten);
	mem_type_box.addItemListener(clisten);
	myr_type_box.addItemListener(clisten);
	mem_types_aux = new javax.swing.JButton("Memory Statistics");
	mem_types_aux.addMouseListener(new MemButtonListener());
	myr_types_aux = new javax.swing.JButton("Myrinet Counters");
	myr_types_aux.addMouseListener(new MyrButtonListener());
	myr_reset = new javax.swing.JButton("Reset Counters");
	myr_reset.addMouseListener(new ResetListener());
	nodes_jobid_button = new javax.swing.JButton("OK");
	nodes_range_button = new javax.swing.JButton("OK");
	nodes_jobid_button.setActionCommand("jobid");
	nodes_range_button.setActionCommand("range");
	nodes_jobid_button.addActionListener(rtoblisten);
	nodes_range_button.addActionListener(rtoblisten);
	nodes_jobid_button.setEnabled(false);
	nodes_range_button.setEnabled(false);
	mpd_id = args[0] + "_" + Integer.toString(port);
	// System.out.println("about to try connect...");
	sockstuff = connect(host, port, mpd_id);
	// System.out.println("connected!");
	mpdsock = (java.net.Socket)sockstuff[0];
	in = (java.io.InputStreamReader)sockstuff[1];
	out = (java.io.OutputStreamWriter)sockstuff[2];
	load_output = new javax.swing.JPanel();
	mem_output = new javax.swing.JPanel();
	myr_output = new javax.swing.JPanel();
	options = new javax.swing.JPanel();
	type_options = new javax.swing.JPanel();
	main_tabpane = new javax.swing.JTabbedPane();
	load_output.setLayout(new java.awt.GridLayout(0,10));
	mem_output.setLayout(new java.awt.FlowLayout());
	nodepanel.setLayout(new java.awt.GridLayout(0,5));
	//	rangepanel.setLayout(new java.awt.GridLayout(1,2));
	//	jobidpanel.setLayout(new java.awt.GridLayout(1,2));
	radiobuttonpanel.setLayout(new java.awt.GridLayout(3,1));
	textpanel.setLayout(new java.awt.GridLayout(2,1));
	myr_output.setLayout(new javax.swing.BoxLayout(myr_output, javax.swing.BoxLayout.Y_AXIS));
	mem_output.setLayout(new javax.swing.BoxLayout(mem_output, javax.swing.BoxLayout.Y_AXIS));
	myr_output.add(myr_reset);
	myr_output.add(myrtable);
	mem_output.add(memtable);
	options.setLayout(new javax.swing.BoxLayout(options, javax.swing.BoxLayout.Y_AXIS));
	type_options.setLayout(new javax.swing.BoxLayout(type_options, javax.swing.BoxLayout.Y_AXIS));
	bgroup.add(nodes_all);
	bgroup.add(nodes_range);
	bgroup.add(nodes_jobid);
	radiobuttonpanel.add(nodes_all);
	radiobuttonpanel.add(nodes_range);
	radiobuttonpanel.add(nodes_jobid);
	rangepanel.add(rangelabel);
	rangepanel.add(rangetext);
	rangepanel.add(nodes_range_button);
	jobidpanel.add(jobidlabel);
	jobidpanel.add(jobidtext);
	jobidpanel.add(nodes_jobid_button);
	textpanel.add(rangepanel);
	textpanel.add(jobidpanel);
	load_options.add(load_options_label);
	load_options.add(load_slide);
	load_options.add(load_num);
	//	load_options.add(load_indep);
	mem_options.add(mem_options_label);
	mem_options.add(mem_types_aux);
	myr_options.add(myr_types_aux);
	sys_options.add(time_options_label);
	sys_options.add(time_slide);
	sys_options.add(time_num);
	sys_options.add(radiobuttonpanel);
	sys_options.add(textpanel);
	type_options.add(load_type_box);
	type_options.add(mem_type_box);
	type_options.add(myr_type_box);
	load_slide.addChangeListener(new SliderListener(load_output, load_num));
	//mem_slide.addChangeListener(new SliderListener());
	time_slide.addChangeListener(new SliderListener(time_num));
	//load_indep.addItemListener(new CheckBoxListener());
	load_scroller = new javax.swing.JScrollPane(load_output, javax.swing.JScrollPane.VERTICAL_SCROLLBAR_AS_NEEDED, javax.swing.JScrollPane.HORIZONTAL_SCROLLBAR_AS_NEEDED);
        mem_scroller = new javax.swing.JScrollPane(mem_output, javax.swing.JScrollPane.VERTICAL_SCROLLBAR_AS_NEEDED, javax.swing.JScrollPane.HORIZONTAL_SCROLLBAR_AS_NEEDED);
	options_scroller = new javax.swing.JScrollPane(options, javax.swing.JScrollPane.VERTICAL_SCROLLBAR_AS_NEEDED, javax.swing.JScrollPane.HORIZONTAL_SCROLLBAR_AS_NEEDED);
	options.add(sys_options);
	//***********************
    	toggleVal("loadavg", true);
	//toggleVal("memusage", true);
	//toggleVal("myrinfo", true);
	//***********************
	main_tabpane.addTab("Nodes", null, nodepanel, "Node list overview");
	main_tabpane.addTab("Options", null, options_scroller, "Display options");
	main_tabpane.addTab("Display Types", null, type_options, "Types of data to display");
	this.getContentPane().add(main_tabpane);
	repaint();
	// System.out.println("finished constructor");
    }   
    
    public void toggleVal(String val, boolean enabled)
    {
	if (enabled)
	    {
		datatypes.add(val);
		if (val.equals("loadavg"))
		    {
			main_tabpane.addTab("System Load", null, load_scroller, "System load levels");
			options.add(load_options);
		    }
		else if (val.equals("memusage"))
		    {
			main_tabpane.addTab("Memory Usage", null, mem_scroller, "Memory Statistics");
			options.add(mem_options);
		    }
		else if (val.equals("myrinfo"))
		    {
			main_tabpane.addTab("Myrinet Counters", null, myr_output, "Myrinet card *_cnt numbers");
			options.add(myr_options);
		    }
	    }
	else
	    {
		datatypes.remove(val);
		if (val.equals("loadavg"))
		    {
			main_tabpane.remove(load_scroller);
			options.remove(load_options);
		    }
		else if (val.equals("memusage"))
		    {
			main_tabpane.remove(mem_scroller);
			options.remove(mem_options);
		    }
		else if (val.equals("myrinfo"))
		    {
			main_tabpane.remove(myr_output);
			options.remove(myr_options);
		    }
	    }
	repaint();
    }
    
    public void update(String id, int jobid)
    {
	int tindex;
	double tdata;
	Double tDouble;
	String tString;
	java.util.LinkedList removed = getData(id, jobid);
	java.util.ArrayList procVector = procs.toArrayList();
	if (datatypes.indexOf("loadavg") != -1)
	    {
		java.awt.Component[] load_comps = load_output.getComponents();
		String load_huge = "";
		for (int i = 0; i < load_comps.length; i++)
		    {
			load_huge = load_huge.concat(((LoadGraph)load_comps[i]).getId());
		    }
		if (nodepanel.getComponentCount() > load_comps.length)
		    {
			for (int i = 0; i < nodepanel.getComponentCount(); i++)
			    {
				if (load_huge.indexOf(((javax.swing.JCheckBox)nodepanel.getComponent(i)).getText()) == -1)
				    {
					load_output.add(new LoadGraph(((javax.swing.JCheckBox)nodepanel.getComponent(i)).getText(), 100, true));
				    }
			    }
		    }
		for (int i = 0; i < load_comps.length; i++)
		    {
			if (removed.indexOf(((LoadGraph)load_comps[i]).getId()) != -1)
			    {
				load_output.remove((LoadGraph)load_comps[i]);
			    }
			else
			    {
				for (tindex = 0; tindex < procVector.size(); tindex++)
				    {
					if (((Node)procVector.get(tindex)).name.equals(((LoadGraph)load_comps[i]).getId()))
					    break;
				    }
				tString = (String)(((Node)procVector.get(tindex)).loadavg);
				tDouble = new Double(Double.parseDouble(tString));
				tdata = tDouble.doubleValue();
				((LoadGraph)load_comps[i]).update_data(tdata);
				for (int j = 0; j < nodepanel.getComponentCount(); j++)
				    {
					if (((javax.swing.JCheckBox)nodepanel.getComponent(j)).getText().equals(((LoadGraph)load_comps[i]).getId()))
					    {
						load_comps[i].setVisible(((javax.swing.JCheckBox)nodepanel.getComponent(j)).isSelected());
						load_comps[i].invalidate();
					    }
				    }
			    }
		    }
	    }
	if (datatypes.indexOf("memusage") != -1)
	    {
		java.util.LinkedList columnnames = new java.util.LinkedList();
		java.util.LinkedList data = new java.util.LinkedList();
		java.util.LinkedList tempv = new java.util.LinkedList();
		java.util.LinkedList memtypes = new java.util.LinkedList();
		Node tempnode;
		int index;
		columnnames.add("Node");
		javax.swing.JPanel mempanel = (javax.swing.JPanel)memboxes.getContentPane().getComponent(0);
		for (int i = 0; i < mempanel.getComponentCount() - 1; i++)
		    {
			if (((javax.swing.JCheckBox)mempanel.getComponent(i)).isSelected())
			    {
				memtypes.add(((javax.swing.JCheckBox)mempanel.getComponent(i)).getText());
			    }
		    }
		for (int i = 0; i < nodepanel.getComponentCount(); i++)
		    {
			if (((javax.swing.JCheckBox)nodepanel.getComponent(i)).isSelected())
			    {
				index = procs.indexOf(((javax.swing.JCheckBox)nodepanel.getComponent(i)).getText());
				tempv.add(procs.getData(index, "name"));
				for (int j = 0; j < memtypes.size(); j++)
				    {
					tempv.add(procs.getData(index, (String)memtypes.get(j)));
				    }
				data.add(tempv.clone());
				tempv.clear();
			    }
		    }
		memtypes.addFirst("Node");
		memtable.update_data(data, memtypes);
	    }
	    if (datatypes.indexOf("myrinfo") != -1)
		{
		    java.util.LinkedList columnnames = new java.util.LinkedList();
		    java.util.LinkedList data = new java.util.LinkedList();
		    java.util.LinkedList tempv = new java.util.LinkedList();
		    java.util.LinkedList myrtypes = new java.util.LinkedList();
		    Node tempnode;
		    int index;
		    columnnames.add("Node");
		    javax.swing.JPanel myrpanel = (javax.swing.JPanel)myrboxes.getContentPane().getComponent(0);
		    for (int i = 0; i < myrpanel.getComponentCount() - 1; i++)
			{
			    if (((javax.swing.JCheckBox)myrpanel.getComponent(i)).isSelected())
				{
				    myrtypes.add(((javax.swing.JCheckBox)myrpanel.getComponent(i)).getText());
				}
			}
		    for (int i = 0; i < nodepanel.getComponentCount(); i++)
			{
			    if (((javax.swing.JCheckBox)nodepanel.getComponent(i)).isSelected())
				{
				    index = procs.indexOf(((javax.swing.JCheckBox)nodepanel.getComponent(i)).getText());
				    tempv.add(procs.getData(index, "name"));
				    for (int j = 0; j < myrtypes.size(); j++)
					{
					    tempv.add(procs.getData(index, (String)myrtypes.get(j)));
					}
				    data.add(tempv.clone());
				    tempv.clear();
				}
			}
		    myrtypes.addFirst("Node");
		    myrtable.update_data(data, myrtypes);
		}
	    repaint();
	    }
    private int findPort()
	{
	    return Integer.parseInt(javax.swing.JOptionPane.showInputDialog(null, "Input MPD local port number", "Port input", javax.swing.JOptionPane.QUESTION_MESSAGE));
	}
    private java.util.LinkedList getData(String id, int jobid)
	{
	    Node tempNode = null;
	    String writestring = "src=jmon cmd=moninfo_req vals=";
	    java.util.LinkedList nodenames = new java.util.LinkedList();
	    java.util.LinkedList removed = new java.util.LinkedList();
	    java.util.TreeSet checkboxes = new java.util.TreeSet();
	    for (int i = 0; i < datatypes.size(); i++)
		{
		    writestring = writestring.concat((String)datatypes.get(i));
		}
	    if (selecttype.equals("all"))
		writestring = writestring.concat(" monwhat=all");
	    else if (selecttype.equals("range"))
		writestring = writestring.concat(" monwhat=" + nodesstring);
	    else
		writestring = writestring.concat(" monwhat=" + nodesstring);
	    try 
		{
		    out.write(writestring + "\n");
		    out.flush();
		}
	    catch (Exception e)
		{
		    System.err.println(e);
		}
	    tempNode = nparser.parse(response(in));
	    while (!(tempNode.name).equals(id))
		{
		    nodenames.add(tempNode.name);
		    if (!procs.contains(tempNode.name))
			{
			    procs.addProc(tempNode);
			    checkboxes.add(new NodeCheckBox(tempNode.name, true));
			    // System.out.println("adding " + tempNode.name + " to checkboxes");
			}
		    else
			{
			    procs.replaceProc(tempNode.name, tempNode);
			    for (int i = 0; i < nodepanel.getComponentCount(); i++)
				{
				    if (((NodeCheckBox)nodepanel.getComponent(i)).getText().equals(tempNode.name))
					{
					    checkboxes.add((NodeCheckBox)nodepanel.getComponent(i));
					}
				}
			}
		    tempNode = nparser.parse(response(in));
		}
	    if (!tempNode.emptyLastNode)
		{
		    nodenames.add((String)tempNode.getProperty("name"));
		    if (!procs.contains(tempNode.name))
			{
			    procs.addProc(tempNode);
			    checkboxes.add(new NodeCheckBox(tempNode.name, true));	    
			    // System.out.println("adding " + tempNode.getProperty("name") + " to checkboxes");
			}
		    else
			{
			    procs.replaceProc(tempNode.name, tempNode);
			    for (int i = 0; i < nodepanel.getComponentCount(); i++)
				{
				    if (((NodeCheckBox)nodepanel.getComponent(i)).getText().equals(tempNode.name))
					{
					    checkboxes.add((NodeCheckBox)nodepanel.getComponent(i));
					}
				}
			}
		}
	    if (procs.getSize() > nodenames.size())
		{
		    // System.out.println("procs.getSize(" + procs.getSize() + " is greater than nodenames.size(" + nodenames.size() + ")");
		    java.util.ArrayList procVector = procs.toArrayList();
		    for (int i = 0; i < procs.getSize(); i++)
			{
			    // System.out.println("at index " + i + " in procs.getSize()");
			    if (!nodenames.contains(((Node)procVector.get(i)).name))
				{
				    // System.out.println(((Node)procVector.get(i)).name + " is not in nodenames so adding to removed");
				    removed.add(((Node)procVector.get(i)).name);
				    // System.out.println("nodepanel.getComponentCount() is " + nodepanel.getComponentCount());
				    for (int j = 0; j < nodepanel.getComponentCount(); j++)
					{
					    // System.out.println("at index " + j + " in nodepanel");
					    if (((javax.swing.JCheckBox)nodepanel.getComponent(j)).getText().equals(((Node)procVector.get(i)).name))
						{
						    nodepanel.remove(nodepanel.getComponent(j));
						}
					}
				    procs.remProc(((Node)procVector.get(i)).name);
				}
			}
		}
	    nodepanel.removeAll();
	    java.util.Iterator it = checkboxes.iterator();
	    while (it.hasNext())
		nodepanel.add((NodeCheckBox)it.next());
	    repaint();
	    if (removed.size() > 0)
		{
		    // System.out.println("REMOVED A NODE!!");
		}
	    return removed;
	}
    private String getHostName()
	{
	    return javax.swing.JOptionPane.showInputDialog(null, "Hostname?", "Hostname input", javax.swing.JOptionPane.QUESTION_MESSAGE);
	}
    public String response(java.io.InputStreamReader buf_in)
	{
	    String retstring = "";
	    int bytes_read;
	    char[] buf = new char[1];
	    boolean isReady = false;
	    buf[0] = '\0';
	    try
		{
		    isReady = buf_in.ready();
		}
	    catch (java.io.IOException e)
		{
		    System.err.println("response: Communication error! aborting...");
		    System.exit(2);
		}
	    while (!isReady) 
		{
		    try
			{
			    isReady = buf_in.ready();
			}
		    catch (java.io.IOException e)
			{
			    System.err.println("response: Communcation error! aborting...");
			    System.exit(2);
			}
		}
	    while (isReady && buf[0] != '\n')
		{
		  try
		    {
			    bytes_read = buf_in.read(buf);
			    retstring = retstring.concat(new String(buf, 0, bytes_read));
			}
		    catch (java.io.IOException e)
			{
			    System.err.println("response: Communication error! aborting...");
			    System.exit(2);
			}
		    try
			{
			    isReady = buf_in.ready();
			}
		    catch (java.io.IOException e)
			{
			    System.err.println("response: Communication error! aborting...");
			    System.exit(2);
			}
		}
	    return retstring;
	}
    private class WindowCloser extends java.awt.event.WindowAdapter
    {
	public void windowClosing(java.awt.event.WindowEvent e)
	{
	    // System.out.println("WindowCloser::windowClosing: starting...");
	    try
		{
		    out.write("src=jmon cmd=moninfo_conn_close\n");
		    out.flush();
		    in.close();
		    out.close();
		    mpdsock.close();
		}
	    catch (java.io.IOException i)
		{
		    System.err.println("WindowCloser::windowClosing: Could not close streams!");
		}
	    System.exit(0);
	}
    }
    private class WindowResizer extends java.awt.event.ComponentAdapter
    {
	public void componentResized(java.awt.event.ComponentEvent e)
	{
	    javax.swing.JFrame source = (javax.swing.JFrame)e.getSource();
	    java.awt.Dimension d = source.getSize();
	    java.awt.Insets i1 = source.getInsets();
	    java.awt.Dimension f = new java.awt.Dimension((int)d.getWidth() - (i1.left + i1.right) - 0, (int)d.getHeight() - (i1.top + i1.bottom) - 0);
	    // System.out.println("got resize call, setting preferred size to " + f);
	    main_tabpane.setPreferredSize(f);
	    source.pack();
	}
    }
    private class SliderListener implements javax.swing.event.ChangeListener
    {
	javax.swing.JLabel label;
	javax.swing.JPanel panel;
	public SliderListener(javax.swing.JPanel p, javax.swing.JLabel l)
	{
	    panel = p;
	    label = l;
	}
	public SliderListener(javax.swing.JLabel l)
	{
	    label = l;
	}
	public void stateChanged(javax.swing.event.ChangeEvent e)
	{
	    javax.swing.JSlider slider = (javax.swing.JSlider)e.getSource();
	    label.setText(Integer.toString(slider.getValue()));
	    if (!slider.getValueIsAdjusting())
		{
		    if (panel != null)
			{
			    for (int i = 0; i < panel.getComponentCount(); i++)
				{
				    ((LoadGraph)panel.getComponent(i)).setSize(slider.getValue());
				}
			}
		    else 
			{
			    updatetime = slider.getValue();
			}
		}
	}
    }
    private class NodeSelectListener implements java.awt.event.ActionListener
    {
	public void actionPerformed(java.awt.event.ActionEvent e)
	{
	    String command = e.getActionCommand();
	    if (command.equals("nodes_all"))
		{
		    rangetext.setEnabled(false);
		    jobidtext.setEnabled(false);
		    nodes_jobid_button.setEnabled(false);
		    nodes_range_button.setEnabled(false);
		    selecttype = "all";
		}
	    else if (command.equals("nodes_range"))
		{
		    jobidtext.setEnabled(false);
		    rangetext.setEnabled(true);
		    nodes_jobid_button.setEnabled(false);
		    nodes_range_button.setEnabled(true);
		}
	    else if (command.equals("nodes_jobid"))
		{
		    jobidtext.setEnabled(true);
		    rangetext.setEnabled(false);
		    nodes_jobid_button.setEnabled(true);
		    nodes_range_button.setEnabled(false);
		}
	}
    }
    private class ReqTypeOKButtonListener implements java.awt.event.ActionListener
    {
	public void actionPerformed(java.awt.event.ActionEvent e)
	{
	    String command = e.getActionCommand();
	    if (command.equals("range"))
	    {
		selecttype = "range";
		nodesstring = rangetext.getText();
	    }
	    else
	    {
		selecttype = "jobid";
		nodesstring = jobidtext.getText();
	    }
	    // System.out.println("selecttype set to " + selecttype);
	}
    }
    private class CheckBoxListener implements java.awt.event.ItemListener
    {
	private javax.swing.JCheckBox load;
	private javax.swing.JCheckBox mem;
	private javax.swing.JCheckBox myr;
	public CheckBoxListener(javax.swing.JCheckBox a, javax.swing.JCheckBox b, javax.swing.JCheckBox c)
	{
	    load = a;
	    mem = b;
	    myr = c;
	}
	public void itemStateChanged(java.awt.event.ItemEvent e)
	{
	    javax.swing.JCheckBox source = (javax.swing.JCheckBox)e.getItemSelectable();
	    if (source == load)
		{
		    toggleVal("loadavg", source.isSelected());
		    // System.out.println("loadavg toggled to " + source.isSelected());
		}
	    else if (source == mem)
		{
		    toggleVal("memusage", source.isSelected());
		    // System.out.println("memusage toggled to " + source.isSelected());
		}
	    else if (source == myr)
		{
		    toggleVal("myrinfo", source.isSelected());
		    // System.out.println("myrinfo toggled to " + source.isSelected());
		}
	}
    }
    private class NodeCheckBox extends javax.swing.JCheckBox implements Comparable
    {
	public int compareTo(Object o)
	{
	    NodeCheckBox n = (NodeCheckBox)o;
	    String myhost = this.getText().substring(0, this.getText().indexOf("_"));
	    String otherhost = n.getText().substring(0, n.getText().indexOf("_"));
	    if (myhost.equals("cct1m"))
		return -1;
	    if (otherhost.equals("cct1m"))
		return -1;
	    if (myhost.equals(otherhost))
		{
		    int myport = Integer.parseInt(this.getText().substring(this.getText().indexOf("_") + 1));
		    int otherport = Integer.parseInt(n.getText().substring(n.getText().indexOf("_") + 1));
		    if (myport > otherport)
			return 1;
		    else
			return -1;
		}
	    String[] myhostsplit = stripnums(myhost);
	    String[] otherhostsplit = stripnums(otherhost);
	    if (!(myhostsplit[0].equals(otherhostsplit[0])))
		{
		    return myhostsplit[0].compareTo(otherhostsplit[0]);
		}
	    else
		{
		    int myhostnum = Integer.parseInt(myhostsplit[1]);
		    int otherhostnum = Integer.parseInt(otherhostsplit[1]);
		    if (myhostnum > otherhostnum)
			return 1;
		    else
			return -1;
		}
	}
	public String[] stripnums(String str)
	{
	    StringBuffer buf = new StringBuffer(str);
	    String nums = "";
	    String chars = "";
	    int i;
	    boolean indigits = false;
	    testfornums: for (i = buf.length() - 1; i >= 0; i--)
		{
		    if (Character.isDigit(buf.charAt(i)))
			indigits = true;
		    else
			{
			    if (indigits)
				{
				    chars = buf.substring(0,i);
				    nums = buf.substring(i + 1);
				    break testfornums;
				}
			    if (i == 0)
				{
				    chars = str;
				    nums = "";
				    break testfornums;
				}
			}
		}
	    String[] retbuf = {chars,nums};
	    return retbuf;
	}
	public boolean equals(Object o)
	{
	    NodeCheckBox n = (NodeCheckBox)o;
	    return this.getText().equals(n.getText());
	}
	public NodeCheckBox(String n, boolean i)
	{
	    super(n,i);
	}
    }
    
    private class MyrButtonListener extends java.awt.event.MouseAdapter
    {
	public void mouseClicked(java.awt.event.MouseEvent e)
	{
	    myrboxes.setVisible(true);
	}
    }

    private class MemButtonListener extends java.awt.event.MouseAdapter
    {
	public void mouseClicked(java.awt.event.MouseEvent e)
	{
	    memboxes.setVisible(true);
	}
    }
    
    private class ResetListener extends java.awt.event.MouseAdapter
    {
	public void mouseClicked(java.awt.event.MouseEvent e)
	{
	    myrtable.reset();
	}
    }
    
    private class myr_counter_box extends javax.swing.JFrame
    {
        public javax.swing.JCheckBox bad_header_cnt;
        public javax.swing.JCheckBox bad_length_cnt;
        public javax.swing.JCheckBox netsend_cnt;
        public javax.swing.JCheckBox netrecv_cnt;
        public javax.swing.JCheckBox bad_type_cnt;
        public javax.swing.JCheckBox badcrc_cnt;
        public javax.swing.JCheckBox badroute_cnt;
        public javax.swing.JCheckBox bogus_header_cnt;
        public javax.swing.JCheckBox drop_cnt;
        public javax.swing.JCheckBox handle_connection_reset_request_cnt;
        public javax.swing.JCheckBox misrouted_cnt;
        public javax.swing.JCheckBox nack_cnt;
        public javax.swing.JCheckBox nack_down_cnt;
        public javax.swing.JCheckBox nack_ignore_close_connection_cnt;
        public javax.swing.JCheckBox nack_received_cnt;
        public javax.swing.JCheckBox nack_reject_cnt;
        public javax.swing.JCheckBox nack_ignore_open_connection_cnt;
        public javax.swing.JCheckBox nack_ignored_cnt;
	public javax.swing.JCheckBox nack_normal_cnt;
        public javax.swing.JCheckBox nack_receive_close_connection_cnt;
        public javax.swing.JCheckBox nack_receive_open_connection_cnt;
        public javax.swing.JCheckBox nack_send_nothing1_cnt;
        public javax.swing.JCheckBox nack_send_nothing2_cnt;
        public javax.swing.JCheckBox nack_send_open_connection_cnt;
        public javax.swing.JCheckBox nack_send_close_connection_cnt;
        public javax.swing.JCheckBox nack_down_recv_cnt;
	public javax.swing.JCheckBox nack_down_send_cnt;
	public javax.swing.JCheckBox no_match_for_datagram_recv_cnt;
        public javax.swing.JCheckBox no_match_for_ether_recv_cnt;
        public javax.swing.JCheckBox no_match_for_reliable_recv_cnt;
        public javax.swing.JCheckBox no_match_for_raw_recv_cnt;
        public javax.swing.JCheckBox out_of_sequence_cnt;
        public javax.swing.JCheckBox progress_cnt;
        public javax.swing.JCheckBox resend_cnt;
        public javax.swing.JCheckBox short_mapper_config_packet_cnt;
        public javax.swing.JCheckBox short_mapper_packet_cnt;
        public javax.swing.JCheckBox short_mapper_scout_packet_cnt;
        public javax.swing.JCheckBox short_packet_cnt;
        public javax.swing.JCheckBox used_bogus_send_cnt;
        public javax.swing.JCheckBox used_bogus_recv_cnt;
        public javax.swing.JCheckBox zero_len_cnt;
        private javax.swing.JPanel panel;
	private javax.swing.JButton OK;
	public myr_counter_box()
        {
	    super("Myrinet Counters");
	    panel = new javax.swing.JPanel();
	    OK = new javax.swing.JButton("OK");
	    bad_header_cnt = new javax.swing.JCheckBox("bad_header_cnt",false);
	    bad_length_cnt = new javax.swing.JCheckBox("bad_length_cnt",false);
            netsend_cnt = new javax.swing.JCheckBox("netsend_cnt",true);
            netrecv_cnt = new javax.swing.JCheckBox("netrecv_cnt",true);
            bad_type_cnt = new javax.swing.JCheckBox("bad_type_cnt",false);
            badcrc_cnt = new javax.swing.JCheckBox("badcrc_cnt",true);
            badroute_cnt = new javax.swing.JCheckBox("badroute_cnt",true);
            bogus_header_cnt = new javax.swing.JCheckBox("bogus_header_cnt",false);
            drop_cnt = new javax.swing.JCheckBox("drop_cnt",true);
            handle_connection_reset_request_cnt = new javax.swing.JCheckBox("handle_connection_reset_request_cnt",false);
            misrouted_cnt = new javax.swing.JCheckBox("misrouted_cnt",false);
            nack_cnt = new javax.swing.JCheckBox("nack_cnt",false);
            nack_down_cnt = new javax.swing.JCheckBox("nack_down_cnt",false);
            nack_ignore_close_connection_cnt = new javax.swing.JCheckBox("nack_ignore_close_connection_cnt",false);
            nack_received_cnt = new javax.swing.JCheckBox("nack_received_cnt",false);
            nack_reject_cnt = new javax.swing.JCheckBox("nack_reject_cnt",false);
            nack_ignore_open_connection_cnt = new javax.swing.JCheckBox("nack_ignore_open_connection_cnt",false);
            nack_ignored_cnt = new javax.swing.JCheckBox("nack_ignored_cnt",false);
	    nack_normal_cnt = new javax.swing.JCheckBox("nack_normal_cnt",false);
	    nack_receive_close_connection_cnt = new javax.swing.JCheckBox("nack_receive_close_connection_cnt",false);
	    nack_receive_open_connection_cnt = new javax.swing.JCheckBox("nack_receive_open_connection_cnt",false);
	    nack_send_nothing1_cnt = new javax.swing.JCheckBox("nack_send_nothing1_cnt",false);
	    nack_send_nothing2_cnt = new javax.swing.JCheckBox("nack_send_nothing2_cnt",false);
	    nack_send_open_connection_cnt = new javax.swing.JCheckBox("nack_send_open_connection_cnt",false);
	    nack_send_close_connection_cnt = new javax.swing.JCheckBox("nack_send_close_connection_cnt",false);
	    nack_down_send_cnt = new javax.swing.JCheckBox("nack_down_send_cnt",false);
	    nack_down_recv_cnt = new javax.swing.JCheckBox("nack_down_recv_cnt",false);
	    no_match_for_datagram_recv_cnt = new javax.swing.JCheckBox("no_match_for_datagram_recv_cnt",false);
	    no_match_for_ether_recv_cnt = new javax.swing.JCheckBox("no_match_for_ether_recv_cnt",false);
	    no_match_for_reliable_recv_cnt = new javax.swing.JCheckBox("no_match_for_reliable_recv_cnt",false);
	    no_match_for_raw_recv_cnt = new javax.swing.JCheckBox("no_match_for_raw_recv_cnt",false);
	    out_of_sequence_cnt = new javax.swing.JCheckBox("out_of_sequence_cnt",false);
	    progress_cnt = new javax.swing.JCheckBox("progress_cnt",false);
	    resend_cnt = new javax.swing.JCheckBox("resend_cnt",true);
	    short_mapper_config_packet_cnt = new javax.swing.JCheckBox("short_mapper_config_packet_cnt",false);
	    short_mapper_packet_cnt = new javax.swing.JCheckBox("short_mapper_packet_cnt",false);
	    short_mapper_scout_packet_cnt = new javax.swing.JCheckBox("short_mapper_scout_packet_cnt",false);
	    short_packet_cnt = new javax.swing.JCheckBox("short_packet_cnt",false);
	    used_bogus_send_cnt = new javax.swing.JCheckBox("used_bogus_send_cnt",false);
	    used_bogus_recv_cnt = new javax.swing.JCheckBox("used_bogus_recv_cnt",false);
	    zero_len_cnt = new javax.swing.JCheckBox("zero_len_cnt",false);
	    this.getContentPane().add(panel);
	    panel.setLayout(new java.awt.GridLayout(21,2));
	    panel.add(bad_header_cnt);
	    panel.add(bad_length_cnt);
	    panel.add(netsend_cnt);
	    panel.add(netrecv_cnt);
	    panel.add(bad_type_cnt);
	    panel.add(badcrc_cnt);
	    panel.add(badroute_cnt);
	    panel.add(bogus_header_cnt);
	    panel.add(drop_cnt);
	    panel.add(handle_connection_reset_request_cnt);
	    panel.add(misrouted_cnt);
	    panel.add(nack_cnt);
	    panel.add(nack_down_cnt);
	    panel.add(nack_ignore_close_connection_cnt);
	    panel.add(nack_received_cnt);
	    panel.add(nack_reject_cnt);
	    panel.add(nack_ignore_open_connection_cnt);
	    panel.add(nack_ignored_cnt);
	    panel.add(nack_normal_cnt);
	    panel.add(nack_receive_close_connection_cnt);
	    panel.add(nack_receive_open_connection_cnt);
	    panel.add(nack_send_nothing1_cnt);
	    panel.add(nack_send_nothing2_cnt);
	    panel.add(nack_send_open_connection_cnt);
	    panel.add(nack_send_close_connection_cnt);
	    panel.add(nack_down_send_cnt);
	    panel.add(nack_down_recv_cnt);
	    panel.add(no_match_for_datagram_recv_cnt);
	    panel.add(no_match_for_ether_recv_cnt);
	    panel.add(no_match_for_reliable_recv_cnt);
	    panel.add(no_match_for_raw_recv_cnt);
	    panel.add(out_of_sequence_cnt);
	    panel.add(progress_cnt);
	    panel.add(resend_cnt);
	    panel.add(short_mapper_config_packet_cnt);
	    panel.add(short_mapper_packet_cnt);
	    panel.add(short_mapper_scout_packet_cnt);
	    panel.add(short_packet_cnt);
	    panel.add(used_bogus_send_cnt);
	    panel.add(used_bogus_recv_cnt);
	    panel.add(zero_len_cnt);
	    panel.add(OK);
	    this.getRootPane().setDefaultButton(OK);
	    OK.addActionListener(new ButtonListener());
	    this.pack();
	}
	private class ButtonListener implements java.awt.event.ActionListener
	{
	    public void actionPerformed(java.awt.event.ActionEvent e)
	    {
		close();
	    }
	}
	private void close()
	{
	    this.setVisible(false);
	}
    }
    private class mem_counter_box extends javax.swing.JFrame
    {
	private javax.swing.JPanel panel;
	private javax.swing.JButton OK;
	private javax.swing.JCheckBox MemTotal;
	private javax.swing.JCheckBox MemFree;
	private javax.swing.JCheckBox MemShared;
	private javax.swing.JCheckBox Buffers;
	private javax.swing.JCheckBox Cached;
	private javax.swing.JCheckBox Active;
	private javax.swing.JCheckBox Inact_dirty;
	private javax.swing.JCheckBox Inact_clean;
	private javax.swing.JCheckBox Inact_target;
	private javax.swing.JCheckBox HighTotal;
	private javax.swing.JCheckBox HighFree;
	private javax.swing.JCheckBox LowTotal;
	private javax.swing.JCheckBox LowFree;
	private javax.swing.JCheckBox SwapTotal;
	private javax.swing.JCheckBox SwapFree;
	public mem_counter_box()
	{
	    panel = new javax.swing.JPanel();
	    OK = new javax.swing.JButton("OK");
	    MemTotal = new javax.swing.JCheckBox("MemTotal", true);
	    MemFree = new javax.swing.JCheckBox("MemFree", true);
	    MemShared = new javax.swing.JCheckBox("MemShared", true);
	    Buffers = new javax.swing.JCheckBox("Buffers", true);
	    Cached = new javax.swing.JCheckBox("Cached", true);
	    Active = new javax.swing.JCheckBox("Active", false);
	    Inact_dirty = new javax.swing.JCheckBox("Inact_dirty", false);
	    Inact_clean = new javax.swing.JCheckBox("Inact_clean", false);
	    Inact_target = new javax.swing.JCheckBox("Inact_target", false);
	    HighTotal = new javax.swing.JCheckBox("HighTotal", false);
	    HighFree = new javax.swing.JCheckBox("HighFree", false);
	    LowTotal = new javax.swing.JCheckBox("LowTotal", false);
	    LowFree = new javax.swing.JCheckBox("LowFree", false);
	    SwapTotal = new javax.swing.JCheckBox("SwapTotal", true);
	    SwapFree = new javax.swing.JCheckBox("SwapFree", true);
	    this.getContentPane().add(panel);
	    panel.setLayout(new java.awt.GridLayout(4, 4));
	    panel.add(MemTotal);
	    panel.add(MemFree);
	    panel.add(MemShared);
	    panel.add(Buffers);
	    panel.add(Cached);
	    panel.add(Active);
	    panel.add(Inact_dirty);
	    panel.add(Inact_clean);
	    panel.add(HighTotal);
	    panel.add(HighFree);
	    panel.add(LowTotal);
	    panel.add(LowFree);
	    panel.add(SwapTotal);
	    panel.add(SwapFree);
	    panel.add(OK);
	    this.pack();
	    this.getRootPane().setDefaultButton(OK);
	    OK.addActionListener(new ButtonListener());
	}
	private class ButtonListener implements java.awt.event.ActionListener
	{
	    public void actionPerformed(java.awt.event.ActionEvent e)
	    {
		close();
	    }
	}
	private void close()
	{
	    this.setVisible(false);
	}
    }
    public int getUpdateTime()
	{
	    return updatetime;
	}
    private Object[] connect(String host, int port, String mpd_id)
	{
	    String resp;
	    int rand;
	    java.net.Socket sock = null;
	    java.io.InputStreamReader i = null;
	    java.io.OutputStreamWriter out = null;
	    java.io.BufferedReader file_in = null;
	    try
		{
		    file_in = new java.io.BufferedReader(new java.io.FileReader(System.getProperty("user.home") + "/.mpdpasswd"));
		}
	    catch (java.io.FileNotFoundException e)
		{
		    System.err.println("connect: Couldn't find ~/.mpdpasswd file! aborting...");
		    System.exit(3);
		}
	    try
		{
		    sock = new java.net.Socket(host, port);
		}
	    catch (java.net.UnknownHostException e)
		{
		    System.err.println("connect: Unknown host! aborting...");
		    System.exit(1);
		}
	    catch (java.net.ConnectException e)
		{
		    System.err.println("connect: Can't connect to " + host + " on specified port " + port + ", aborting...");
		    System.exit(1);
		}
	    catch (java.net.NoRouteToHostException e)
		{
		    System.err.println("connect: No route to host! aborting...");
		    System.exit(1);
		}
	    catch (java.io.IOException e)
		{
		    System.err.println("connect: Connect error! aborting...");
		    System.exit(1);
		}
	    try
		{
		    i = new java.io.InputStreamReader(sock.getInputStream());
		}
	    catch (java.io.IOException e)
		{
		    System.err.println("connect: Can't open input stream for reading! aborting...");
		    System.exit(1);
		}
	    try
		{
		    out = new java.io.OutputStreamWriter(sock.getOutputStream());
		}
	    catch (java.io.IOException e)
		{
		    System.err.println("connect: Can't open output stream for writing! aborting...");
		    System.exit(1);
		}
	    try
		{
		    out.write("cmd=moninfo_conn_req src=jmon version=2 dest=" + mpd_id + "\n");
		    out.flush();
		    resp = response(i);
		    rand = Integer.parseInt(resp.substring(resp.indexOf("rand=") + 5, resp.indexOf(" ", resp.indexOf("rand="))));
		    String pass = file_in.readLine();
		    String encoded = pass + "\n" + Integer.toString(rand);
		    out.write("src=jmon dest=" + mpd_id + " cmd=new_moninfo_conn encoded_num=" + jcrypt.crypt("el", encoded) + "\n");
		    out.flush();
		    resp = response(i);
		    if (resp.indexOf("moninfo_conn_ok") == -1)
			{
			    System.err.println("connect: got wrong message?  This is what I got: " + resp);
			}
		}
	    catch (java.io.IOException e)
		{
		    System.err.println(e);
		    System.err.println("connect: IO exception! This is bad! aborting...");
		    System.exit(3);
		}
	    Object[] returnvals = {sock, i, out};
	    return returnvals;
	}
    }
