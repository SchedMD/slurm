import java.util.*;
import javax.xml.parsers.*;
import org.xml.sax.*;
import org.xml.sax.helpers.*;
import java.io.*;

public class NodeParser 
{
    private Node p;
    private SAXParser parser;
    private handler Handler;
    private LinkedList current;
    public NodeParser()
    {
	current = new LinkedList();
	parser = null;
	SAXParserFactory factory = null;
	factory = SAXParserFactory.newInstance();
	try
	{
	    parser = factory.newSAXParser(); //create the SAX XML parser
	}
	catch (ParserConfigurationException e)
	{
	    System.err.println(e);
	}
	catch (SAXException e)
	{
	    System.err.println(e);
	}
	; //create the custom SAX handler
    }
    public Node parse(String in)
    {
	handler Handler = new handler();
	p = new Node();
	try
	{
	    parser.parse(new InputSource(new StringReader(in)), Handler);
	}
	catch (SAXException e)
	{
	    System.err.println(e);
	}
	catch (IOException e)
	{
	    System.err.println(e);
	}
	return p;
    }
    private class handler extends DefaultHandler
    {
	public void startElement(String uri, String localName, String qName, Attributes attr)
	{
	    current.add(new String(localName)); //add localName to path tree
	    if (localName.equals("node")) //the main tag is a special case because it has metadata
	    {
		// System.out.println("got node " + attr.getValue(0));
		p.setProperty("name", attr.getValue(0));
	    }
	}
	public void ignorableWhitespace(char[] ch, int start, int length)
	{
	}
	public void characters(char[] ch, int start, int length)
	{
	    if (!((String)current.getLast()).equals("node"))
	    {
	    p.setProperty((String)current.getLast(), new String(ch, start, length)); //setProperty on the Node object to the tag name and the characters we got
	    //if (localName.equals("node")) endDocument(); //end parsing if this is the end of the top-level <node> tag
	    }
	    else p.emptyLastNode = true;
	}
	public void endElement(String uri, String localName, String qName)
	{
	    current.remove(current.getLast()); //remove this element from the path tree
	}
	public void endDocument()
	{
	}
    }
}
	    
