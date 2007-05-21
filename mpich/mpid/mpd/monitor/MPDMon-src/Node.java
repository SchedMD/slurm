import java.lang.reflect.*;

public class Node implements Comparable
{
    public String name;
    public String loadavg;
    public String bad_header_cnt ;
    public String bad_length_cnt ;
    public String netsend_cnt;
    public String netrecv_cnt;
    public String bad_type_cnt ;
    public String badcrc_cnt ;
    public String badroute_cnt ;
    public String bogus_header_cnt ;
    public String drop_cnt ;
    public String handle_connection_reset_request_cnt ;
    public String misrouted_cnt ;
    public String nack_cnt ;
    public String nack_down_cnt ;
    public String nack_ignore_close_connection_cnt ;
    public String nack_received_cnt ;
    public String nack_reject_cnt ;
    public String nack_ignore_open_connection_cnt ;
    public String nack_ignored_cnt ;
    public String nack_normal_cnt ;
    public String nack_receive_close_connection_cnt ;
    public String nack_receive_open_connection_cnt ;
    public String nack_send_nothing1_cnt ;
    public String nack_send_nothing2_cnt ;
    public String nack_send_open_connection_cnt ;
    public String nack_send_close_connection_cnt ;
    public String nack_down_recv_cnt;
    public String nack_down_send_cnt;
    public String no_match_for_datagram_recv_cnt ;
    public String no_match_for_ether_recv_cnt ;
    public String no_match_for_reliable_recv_cnt ;
    public String no_match_for_raw_recv_cnt ;
    public String out_of_sequence_cnt ;
    public String progress_cnt ;
    public String resend_cnt ;
    public String short_mapper_config_packet_cnt ;
    public String short_mapper_packet_cnt ;
    public String short_mapper_scout_packet_cnt ;
    public String short_packet_cnt ;
    public String used_bogus_send_cnt ;
    public String used_bogus_recv_cnt ;
    public String zero_len_cnt ;
    public String MemTotal ;
    public String MemFree ;
    public String MemShared ;
    public String Buffers ;
    public String Cached ;
    public String Active ;
    public String Inact_dirty ;
    public String Inact_clean ;
    public String Inact_target ;
    public String HighTotal ;
    public String HighFree ;
    public String LowTotal ;
    public String LowFree ;
    public String SwapTotal ;
    public String SwapFree;
    public boolean emptyLastNode;
    public int compareTo(Object o)
    {
	Node n = (Node)o;
	String myhost = name.substring(0, name.indexOf("_"));
	String otherhost = n.name.substring(0, n.name.indexOf("_"));
	int myport = Integer.parseInt(name.substring(name.indexOf("_") + 1));
	int otherport = Integer.parseInt(n.name.substring(n.name.indexOf("_") + 1));
	if (myhost.equals(otherhost))
	    {
		if (myport > otherport)
		    return 1;
		else
		    return -1;
	    }
	return myhost.compareTo(otherhost);
    }
    public boolean equals(Object o)
    {
	return name.equals((String)o);
    }
    public String getProperty(String property)
    {
	String str;
	try
	{
	    str = (String)(Node.class.getField(property).get(this));
	    if ( str != null ) return str;
	    else return "0";
	}
	catch (NoSuchFieldException e)
	{
	    System.err.println(e);
	    System.err.println("tried to get a property that doesn't exist!  You tried to get: " + property);
	    System.exit(4);
	}
	catch (IllegalAccessException e)
	{
	    System.err.println(e);
	}
	return new String();
    }
    public void setProperty(String property, String value)
    {
	try
	{
	    Node.class.getField(property).set(this, value);
	}
	catch (NoSuchFieldException e)
	{
	    System.err.println(e);
	    System.err.println("tried to set a property that doesn't exist!  Yo tried to set: " + property);
	}
	catch (IllegalAccessException e)
	{
	    System.err.println(e);
	}
    }
}




