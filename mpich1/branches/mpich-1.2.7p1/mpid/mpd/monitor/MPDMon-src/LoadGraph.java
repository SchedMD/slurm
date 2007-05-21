import java.awt.*;
import javax.swing.*;

public class LoadGraph extends JComponent 
{
    private double[] values;
    private float scale;
    private int shiftcount;
    private String header;
    private double total;
    private int history;
    private boolean indep;
    private double maxval;
    public LoadGraph(String head, int length, boolean in)
    {
	history = length;
	maxval = 0.0;
	scale = 1;
	shiftcount = 0;
	indep = in;
	values = new double[999];
	for (int i = 0; i < values.length; i++)
	{
	    values[i] = 0;
	}
	this.setPreferredSize(new Dimension(30 + length,150));
	this.setVisible(true);
	header = head;
    }
    public void paintComponent(Graphics g)
    {
	g.setColor(Color.white);
	g.fillRect(25, 25, history, 100);
	g.setColor(Color.red);
	g.drawString(Float.toString(scale), 0, 25);
	g.drawString(Float.toString(scale/2), 0, 75);
	g.setColor(Color.black);
	g.drawRect(25, 25, history, 100);
	g.drawLine(25, 75, 25 + history, 75);
	for (int i = values.length - history; i < values.length; i++)
	{
	    g.drawLine(i - (values.length - history) + 25, 125, i - (values.length - history) + 25, 125 - (int)((values[i]/scale)*100));
	}
	g.drawString(header, 25, 140);
    }
    public double shift(double newval)
    {
	for (int i = 1; i < values.length; i++)
	{
	    values[i-1] = values[i];
	}
	total += newval;
	values[values.length - 1] = newval;
	shiftcount++;
	return (total/shiftcount);
    }
    public void update_data(double newval)
    {
	shift(newval);
	if (newval > maxval) 
		maxval = newval;
	if ((newval > scale) && (indep)) 
	{
	    while (newval > scale) 
	    {
		scale *= 2;
	    }
	}
	else if (canScaleDown() && indep) 
	{
	    while (maxval < scale/2)
	    {
		// System.out.println("scaling down(from update_data)");
		scale *= 0.5;
	    }
	}
	this.repaint();
    }
    public String getId()
    {
	return header;
    }
    public int getCount()
    {
	return shiftcount;
    }
    public void setSize(int size)
    {
	boolean canScaleDown = true;
	history = size;
	this.setPreferredSize(new Dimension(30 + history, 150));
	this.repaint();
	this.invalidate();
	for (int i = values.length - history; i < values.length; i++)
	{
	    if (values[i] > scale/2)
	    {
		canScaleDown = false;
		if (values[i] > scale)
		{
		    while (values[i] > scale)
		    {
			// System.out.println("scaling up (from setSize())");
			scale *= 2;
		    }
		}
	    }
	}
	if (canScaleDown) 
	{
	    while (canScaleDown())
	    {
		// System.out.println("scaling down (from setSize())");
		scale *= 0.5;
	    }
	}
    }
    public void setScale(float s)
    {
	if (indep) System.err.println("You are trying to set the scale on an independant graph!  This should not happen! Andrew can't write code!");
	else scale = s;
    }
    public boolean canScaleDown()
    {
	boolean allzero = true;
	boolean can = true;
	boolean retval;
	for (int i = values.length - history; i < values.length; i++)
	{
	    if (values[i] > (scale/2)) can = false;
	    if (values[i] != 0) allzero = false;
	}
	retval = (!can && allzero);
	return retval;
    }
    public float getScale()
    {
	return scale;
    }
    public void setIndep(boolean in)
    {
	indep = in;
    }
}
