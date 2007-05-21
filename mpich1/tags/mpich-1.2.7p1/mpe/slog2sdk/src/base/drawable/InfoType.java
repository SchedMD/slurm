/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.drawable;

import java.io.DataInput;
import java.io.DataOutput;

import base.io.DataIO;

public class InfoType implements DataIO
{
    public static final int       BYTESIZE = 1;

    public static final InfoType  STR   = new InfoType( 's' );
    public static final InfoType  INT2  = new InfoType( 'h' );
    public static final InfoType  INT4  = new InfoType( 'd' );
    public static final InfoType  INT8  = new InfoType( 'l' );
    public static final InfoType  BYTE4 = new InfoType( 'x' );
    public static final InfoType  BYTE8 = new InfoType( 'X' );
    public static final InfoType  FLT4  = new InfoType( 'e' );
    public static final InfoType  FLT8  = new InfoType( 'E' );

    private byte  type;

    public InfoType()
    {
        type  = (byte) ' ';
    }

    public InfoType( char chr )
    {
        type  = (byte) chr;
    }

    public boolean equals( final InfoType aType )
    {
        return this.type == aType.type;
    }

    public boolean isValid()
    {
        return (    this.equals( STR )   || this.equals( INT2 )
                 || this.equals( INT4 )  || this.equals( INT8 )
                 || this.equals( BYTE4 ) || this.equals( BYTE8 )
                 || this.equals( FLT4 )  || this.equals( FLT8 ) );
    }

    public void writeObject( DataOutput outs )
    throws java.io.IOException
    {
        outs.writeByte( (int) type );
    }

    public InfoType( DataInput ins )
    throws java.io.IOException
    {
        this.readObject( ins );
    }

    public void readObject( DataInput ins )
    throws java.io.IOException
    {
        type = ins.readByte();
    }

    public String toString()
    {
        return "%" + String.valueOf( (char) type );
    }

    public static final void main( String[] args )
    {
        System.out.println( "STR = " + STR );
        System.out.println( "INT2 = " + INT2 );
        System.out.println( "INT4 = " + INT4 );
        System.out.println( "INT8 = " + INT8 );
        System.out.println( "BYTE4 = " + BYTE4 );
        System.out.println( "BYTE8 = " + BYTE8 );
        System.out.println( "FLT4 = " + FLT4 );
        System.out.println( "FLT8 = " + FLT8 );

        InfoType atype = new InfoType( 'a' );
        System.out.println( atype.isValid() );
        InfoType btype = new InfoType( 'd' );
        System.out.println( btype.isValid() );
        System.out.println( atype.equals( InfoType.INT2 ) );
        System.out.println( btype.equals( InfoType.INT4 ) );
    }
}
