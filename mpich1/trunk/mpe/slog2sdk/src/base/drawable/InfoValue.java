/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

package base.drawable;

import base.io.MixedDataInput;
import base.io.MixedDataOutput;
import base.io.MixedDataIO;

public class InfoValue implements MixedDataIO
{
    private InfoType  type;
    private Object    value;

    public InfoValue()
    {
        type   = null;
        value  = null;
    }

    public InfoValue( final InfoType in_type )
    {
        type   = in_type;
        value  = null;
    }

    public InfoValue( final InfoType in_type, final Object in_value )
    {
        type   = in_type;
        value  = in_value;
    }

    //  for slog2print
    public InfoType  getType()
    {
        return type;
    }

    public Object getValue()
    {
        return value;
    }

    public void setValue( final Object in_value )
    throws ClassCastException
    {
        if ( type == null || in_value == null ) {
            value  = in_value;
            return;
        }

        if ( isValueConsistentWithType( this.type, in_value ) )
            value  = in_value;
        else
            throw new ClassCastException( "Unmatched InfoType " + this.type
                                        + " for the value " + in_value );
    }

    private static boolean isValueConsistentWithType( final InfoType aType,
                                                      final Object aValue )
    {
        if ( aType == null || aValue == null )
            return true;

        if ( aType.equals( InfoType.STR ) )
            return ( aValue instanceof String );
        if ( aType.equals( InfoType.INT2 ) )
            return ( aValue instanceof Short );
        if ( aType.equals( InfoType.INT4 ) || aType.equals( InfoType.BYTE4 ) )
            return ( aValue instanceof Integer );
        if ( aType.equals( InfoType.INT8 ) || aType.equals( InfoType.BYTE8 ) )
            return ( aValue instanceof Long );
        if ( aType.equals( InfoType.FLT4 ) )
            return ( aValue instanceof Float );
        if ( aType.equals( InfoType.FLT8 ) )
            return ( aValue instanceof Double );

        return false;
    }

    public int getByteSize()
    {
        if ( value != null ) {
            if ( value instanceof String )
                return InfoType.BYTESIZE + 2 + ( (String) value ).length();
            else if ( value instanceof Short )
                return InfoType.BYTESIZE + 2;
            else if ( value instanceof Integer )
                return InfoType.BYTESIZE + 4;
            else if ( value instanceof Long )
                return InfoType.BYTESIZE + 8;
            else if ( value instanceof Float )
                return InfoType.BYTESIZE + 4;
            else if ( value instanceof Double )
                return InfoType.BYTESIZE + 8;
        }
        return 0;
    }

    public void writeValue( MixedDataOutput outs )
    throws java.io.IOException
    {
        if ( type.equals( InfoType.STR ) )
            outs.writeString( (String) value );
        else if (    type.equals( InfoType.INT8 )
                  || type.equals( InfoType.BYTE8 ) )
            outs.writeLong( ( (Long) value ).longValue() );
        else if (    type.equals( InfoType.INT4 )
                  || type.equals( InfoType.BYTE4 ) )
            outs.writeInt( ( (Integer) value ).intValue() );
        else if ( type.equals( InfoType.INT2 ) )
            outs.writeShort( ( (Short) value ).shortValue() );
        else if ( type.equals( InfoType.FLT4 ) )
            outs.writeFloat( ( (Float) value ).floatValue() );
        else if ( type.equals( InfoType.FLT8 ) )
            outs.writeDouble( ( (Double) value ).doubleValue() );
        else
            throw new java.io.IOException( "Unknown InfoType = " + type );
    }

    public void readValue( MixedDataInput ins )
    throws java.io.IOException
    {
        if ( type.equals( InfoType.STR ) )
            value = ins.readString();
        else if (    type.equals( InfoType.INT8 )
                  || type.equals( InfoType.BYTE8 ) )
            value = new Long( ins.readLong() );
        else if (    type.equals( InfoType.INT4 )
                  || type.equals( InfoType.BYTE4 ) )
            value = new Integer( ins.readInt() );
        else if ( type.equals( InfoType.INT2 ) )
            value = new Short( ins.readShort() );
        else if ( type.equals( InfoType.FLT4 ) )
            value = new Float( ins.readFloat() );
        else if ( type.equals( InfoType.FLT8 ) )
            value = new Double( ins.readDouble() );
        else
            throw new java.io.IOException( "Unknown InfoType = " + type );
    }

    public void writeObject( MixedDataOutput outs )
    throws java.io.IOException
    {
        type.writeObject( outs );
        this.writeValue( outs );
    }

    public InfoValue( MixedDataInput ins )
    throws java.io.IOException
    {
        this();
        this.readObject( ins );
    }

    public void readObject( MixedDataInput ins )
    throws java.io.IOException
    {
        type = new InfoType( ins );
        this.readValue( ins );
    }

    public String toString()
    {
        if ( type.equals( InfoType.STR ) )
            return (String) value;
        else if ( type.equals( InfoType.BYTE4 ) )
            return Integer.toHexString( ( (Integer) value ).intValue() );
        else if ( type.equals( InfoType.BYTE8 ) )
            return Long.toHexString( ( (Long) value ).longValue() );
        else
            return value.toString();
    }
}
