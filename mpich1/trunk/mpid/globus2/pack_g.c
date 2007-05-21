#include "chconfig.h"
#include "globdev.h"
/* #include "mpid.h" */

/*
 * there are 3 data items that are passed as args to many of these
 * pack/unpack routines.  they are:
 *     struct MPIR_COMMUNICATOR *comm - communicator in which this
 *                                      message is intended
 *     int partner                    - rank within 'comm' for message
 *     MPID_Msg_pack_t msgact         - ??? don't know ???
 * these 3 args are sometimes useful for other devices but are all 
 * ignored in the globus2 device, i.e., we pack/unpack data independent
 * of the comm+partner.
 * there is a 4th data item, MPID_Msgrep_t msgrep, that is also 
 * passed.  it is essentially 'format'.  
 */


void
MPID_Pack(
    void *				src,			/* src args */
    int					count,
    struct MPIR_DATATYPE *		datatype,
    void *				dest_buff_start,	/* dest args */
    int					maxcount,		/* ignored */
    int *				position,
    struct MPIR_COMMUNICATOR *		comm,			/* ignored */
    int					partner,		/* ignored */
    MPID_Msgrep_t			msgrep,			/* ignored */
    MPID_Msg_pack_t			msgact,			/* ignored */
    int *				error_code)
{
    if (*position == 0)
    {
	unsigned char *			buf;

	buf = (unsigned char *) dest_buff_start;
	*buf = GLOBUS_DC_FORMAT_LOCAL;
	*position += 1;
    }

    mpich_globus2_pack_data(src, count, datatype,
			    dest_buff_start, position, error_code);
}


/* 
 * mpich_globus2_pack_data()
 *
 * the destination buffer is decribed by 'dest_buff_start' (beginning of dest
 * buff, but not necessarily where to begin packing), 'maxcount' (size in bytes
 * of dest buff), and 'position' (diplacement, in bytes, into 'dest_buff_start'
 * where to start packing).  we must update 'position' as we pack more data.
 *
 * NOTE: struct MPIR_COMMUNICATOR *comm, int partner, and 
 *       MPID_Msg_pack_t msgact are ignored for reasons stated 
 *       at top of file.  
 *       MPID_Msgrep_t msgrep is ignored during packing for the globus2 
 *       device because of our reader-makes-right model, i.e. we always 
 *       pack/send data in its native format.
 *
 * NOTE: there is one more datatype found in datatype.h ... MPIR_FORT_INT
 *       it has been explained to me by bill that we do not have to
 *       support an explicit case for that type because it is a
 *       synonym for one of the other types we already have a case
 *       statement for (which type it is a synonym for is architecture 
 *       dependent and determined during mpich configuration).
 *
 * NICK: because this is an MPID routine as opposed to a MPI routine,
 *       it is assumed that there is enough room ('maxcount' bytes)
 *       to pack the data into 'src'.  we do not bother checking
 *       that here, and therefore we essentially ignore maxcount also 
 *       .... maybe that's an assumption we're not allowed to make?
 */
void
mpich_globus2_pack_data(
    void *				src,			/* src args */
    int					count,
    struct MPIR_DATATYPE *		datatype,
    void *				dest_buff_start,	/* dest args */
    int *				position,
    int *				error_code)
{
    globus_byte_t *dest_before_put, *dst;

    *error_code = 0;

    if (count == 0 || datatype->size == 0)
	return ;

    dst = dest_before_put = ((globus_byte_t *) dest_buff_start) + *position;
    switch (datatype->dte_type)
    {
      case MPIR_CHAR:   globus_dc_put_char(&dst,(char *)src,count);       break;
      case MPIR_UCHAR:  globus_dc_put_u_char(&dst,(u_char *)src,count);   break;
      case MPIR_PACKED: /* THIS MUST BE A MEMCPY, i.e., NOT CONVERTED */
	memcpy((void *) dst, src, count); 
	dst += count;                                                     break;
      case MPIR_BYTE:   /* THIS MUST BE A MEMCPY, i.e., NOT CONVERTED */
	memcpy((void *) dst, src, count); 
	dst += count;                                                     break;
      case MPIR_SHORT:  globus_dc_put_short(&dst,(short *)src,count);     break;
      case MPIR_USHORT: globus_dc_put_u_short(&dst,(u_short *)src,count); break;
      case MPIR_LOGICAL: /* 'logical' in FORTRAN is always same as 'int' */
      case MPIR_INT:    globus_dc_put_int(&dst,(int *)src,count);         break;
      case MPIR_UINT:   globus_dc_put_u_int(&dst,(u_int *)src,count);     break;
      case MPIR_LONG:   globus_dc_put_long(&dst,(long *)src,count);       break;
      case MPIR_LONGLONGINT: 
		    globus_dc_put_long_long(&dst,(long long *)src,count); break;
      case MPIR_ULONG:  globus_dc_put_u_long(&dst,(u_long *)src,count);   break;
      case MPIR_FLOAT:  globus_dc_put_float(&dst,(float *)src,count);     break;
      case MPIR_DOUBLE: globus_dc_put_double(&dst,(double *)src,count);   break;
      case MPIR_LONGDOUBLE: *error_code = MPI_ERR_TYPE;                   break;
      case MPIR_UB:
      case MPIR_LB:                                                       break;
      case MPIR_COMPLEX: globus_dc_put_float(&dst,(float *)src,2*count);  break;
      case MPIR_DOUBLE_COMPLEX: 
	globus_dc_put_double(&dst,(double *)src,2*count);                 break;
      /*
       * rest are complex data types requiring special care
       * by decomposing down to their basic types
       */
      case MPIR_CONTIG:
        mpich_globus2_pack_data(src,
				count * datatype->count,
				datatype->old_type,
				dest_buff_start,
				position,
				error_code);
        break;
      case MPIR_VECTOR:
      case MPIR_HVECTOR:
        {
          globus_byte_t *tmp = (globus_byte_t *) src; 
	  int i, j;
	  for (i = 0; *error_code == 0  && i < count; i++)
	  {
	    src = (void *) tmp;
	    for (j = 0; *error_code == 0  && j < datatype->count; j++)
	    {
              mpich_globus2_pack_data(src,
				      datatype->blocklen,
				      datatype->old_type,
				      dest_buff_start,
				      position,
				      error_code);
	      src = (void *) (((globus_byte_t *) src) + datatype->stride);
	    } /* endfor */
	    tmp += datatype->extent;
	  } /* endfor */
        }
        break;
      case MPIR_INDEXED:
      case MPIR_HINDEXED:
        {
	  void *tmp;
          int i, j;
	  for (i = 0; *error_code == 0 && i < count; i++)
	  {
	    for (j = 0; *error_code == 0 && j < datatype->count; j++)
	    {
	      tmp = (void *) (((char *) src) + datatype->indices[j]);
              mpich_globus2_pack_data(tmp,
				      datatype->blocklens[j],
				      datatype->old_type,
				      dest_buff_start,
				      position,
				      error_code);
	    } /* endfor */
	    src = (void *) (((globus_byte_t *) src) + datatype->extent);
	  } /* endfor */
        }
        break;
      case MPIR_STRUCT:
        {
	  void *tmp;
          int i, j;
	  for (i = 0; *error_code == 0 && i < count; i++)
	  {
	    for (j = 0; *error_code == 0 && j < datatype->count; j++)
	    {
	      tmp = (void *) (((char *) src) + datatype->indices[j]);
              mpich_globus2_pack_data(tmp,
				      datatype->blocklens[j],
				      datatype->old_types[j],
				      dest_buff_start,
				      position,
				      error_code);
	    } /* endfor */
	    src = (void *) (((globus_byte_t *) src) + datatype->extent);
	  } /* endfor */
        }
        break;
      default:
        globus_libc_fprintf(stderr,
            "ERROR: MPID_Pack: encountered unrecognizable MPIR type %d\n", 
            datatype->dte_type);
        *error_code = MPI_ERR_INTERN;
	return;
        break;
    } /* end switch() */

    /* updating 'position' for basic data types */
    if (*error_code == 0)
    {
	switch (datatype->dte_type)
	{
	  case MPIR_CHAR:    case MPIR_UCHAR:          case MPIR_PACKED: 
	  case MPIR_BYTE:    case MPIR_SHORT:          case MPIR_USHORT: 
	  case MPIR_LOGICAL: case MPIR_INT:            case MPIR_UINT: 
	  case MPIR_LONG:    case MPIR_LONGLONGINT:    case MPIR_ULONG:
	  case MPIR_FLOAT:   case MPIR_DOUBLE:         case MPIR_LONGDOUBLE:
	  case MPIR_COMPLEX: case MPIR_DOUBLE_COMPLEX: 
	    (*position) += (dst - dest_before_put); break;
	  default:                                  break;
	} /* end switch() */
    } /* endif */

} /* end mpich_globus2_pack_data() */


void
MPID_Unpack(
    void *				src_buff_start,		/* src args */
    int					maxcount,
    MPID_Msgrep_t			msgrep,
    int *				in_position,
    void *				dest_buff_start,	/* dest args */
    int					count,
    struct MPIR_DATATYPE *		datatype,
    int *				out_position,
    struct MPIR_COMMUNICATOR *		comm,			/* ignored */
    int					partner,		/* ignored */
    int *				error_code)
{
    unsigned char *			buf;
    int					src_format;

    buf = (unsigned char *) src_buff_start;
    src_format = *buf;
    if (*in_position == 0)
    {
	*in_position += 1;
    }
    
    mpich_globus2_unpack_data(src_buff_start, in_position, src_format,
		dest_buff_start, count, datatype, out_position, error_code);
}

/* 
 * mpich_globus2_unpack_data
 *
 * the source buffer is decribed by 'src_buff_start' (beginning of src buff, 
 * but not necessarily where to begin unpacking from), 'maxcount' (size in 
 * bytes), 'msgrep' (dataorigin format), and 'in_position' (diplacement, 
 * in bytes, into 'dest_buff_start' from where to start unpacking).
 * similarly, the dest buffer is described by 'dest_buff_start' and
 * 'out_position'.  we must update 'in_position' and 'out_position'
 * as we unpack the data.
 *
 * NICK: it is assumed that there is enouch data in the source to
 *       completely fill the dest buff as described.  futher, any
 *       'extra' data left in the src after filling the dest is ignored.
 *       .... maybe that's an assumption we're not allowed to make?
 *
 * NOTE: there is one more datatype found in datatype.h ... MPIR_FORT_INT
 *       it has been explained to me by bill that we do not have to
 *       support an explicit case for that type because it is a
 *       synonym for one of the other types we already have a case
 *       statement for (which type it is a synonym for is architecture 
 *       dependent and determined during mpich configuration).
 */
void
mpich_globus2_unpack_data(
    void *				src_buff_start,		/* src args */
    int *				in_position,
    int					src_format,
    void *				dest_buff_start,	/* dest args */
    int					count,
    struct MPIR_DATATYPE *		datatype,
    int *				out_position,
    int *				error_code)
{
    *error_code = 0;

    switch (datatype->dte_type)
    {
	case MPIR_CHAR:    case MPIR_UCHAR:       case MPIR_PACKED:
	case MPIR_BYTE:    case MPIR_SHORT:       case MPIR_USHORT:
	case MPIR_LOGICAL: case MPIR_INT:         case MPIR_UINT: 
	case MPIR_LONG:    case MPIR_LONGLONGINT: case MPIR_ULONG: 
	case MPIR_FLOAT:   case MPIR_DOUBLE:      case MPIR_UB:    
	case MPIR_LB:      case MPIR_COMPLEX:     case MPIR_DOUBLE_COMPLEX: 
	{
	    globus_byte_t *src_after_unpack  =
		((globus_byte_t *)src_buff_start) + *in_position;
	    globus_byte_t *src_before_unpack = src_after_unpack;
	    int nbytes_rcvd = 0;

	    if (extract_complete_from_buff(&src_after_unpack, 
					   ((globus_byte_t *) dest_buff_start) 
					   + *out_position, 
					   count, 
					   datatype, 
					   src_format,
					   &nbytes_rcvd))
	    {
		*error_code = MPI_ERR_INTERN;
	    }
	    else
	    {
		(*in_position)  += (src_after_unpack  - src_before_unpack);
		(*out_position) += nbytes_rcvd;
	    } /* endif */
	}
	break;
	case MPIR_LONGDOUBLE: *error_code = MPI_ERR_TYPE; break;
	/*
	* rest are complex data types requiring special care
	* by decomposing down to their basic types
	*/
	case MPIR_CONTIG:
	    mpich_globus2_unpack_data(src_buff_start,
				      in_position,
				      src_format,
				      dest_buff_start,
				      count * datatype->count,
				      datatype->old_type,
				      out_position,
				      error_code);
	break;
	case MPIR_VECTOR:
	case MPIR_HVECTOR:
	{
	    globus_byte_t *tmp = (globus_byte_t *) dest_buff_start; 
	    int i, j, tmp_int;
	    for (i = 0; *error_code == 0  && i < count; i++)
	    {
		dest_buff_start = (void *) tmp;
		for (j = 0; *error_code == 0  && j < datatype->count; j++)
		{
		    tmp_int = 0;

		    mpich_globus2_unpack_data(src_buff_start,
					      in_position,
					      src_format,
					      dest_buff_start,
					      datatype->blocklen,
					      datatype->old_type,
					      &tmp_int,
					      error_code);

		    *out_position += tmp_int;
		    dest_buff_start = 
			(void *) (((globus_byte_t *) dest_buff_start) 
			    + datatype->stride);
		} /* endfor */
		tmp += datatype->extent;
	    } /* endfor */
	}
	break;
	case MPIR_INDEXED:
	case MPIR_HINDEXED:
	{
	    globus_byte_t *tmp;
	    int i, j, tmp_int;
	    for (i = 0; *error_code == 0 && i < count; i++)
	    {
		for (j = 0; *error_code == 0 && j < datatype->count; j++)
		{
		    tmp_int = 0;

		    tmp = (globus_byte_t *) dest_buff_start 
			    + datatype->indices[j];
		    mpich_globus2_unpack_data(src_buff_start,
					      in_position,
					      src_format,
					      tmp,
					      datatype->blocklens[j],
					      datatype->old_type,
					      &tmp_int,
					      error_code);
		    *out_position += tmp_int;
		} /* endfor */
		dest_buff_start = (void *) (((globus_byte_t *) dest_buff_start)
				    + datatype->extent);
	    } /* endfor */
	}
	break;
	case MPIR_STRUCT:
	{
	    globus_byte_t *tmp;
	    int i, j, tmp_int;
	    for (i = 0; *error_code == 0 && i < count; i++)
	    {
		for (j = 0; *error_code == 0 && j < datatype->count; j++)
		{
		    tmp_int = 0;

		    tmp = (globus_byte_t *) dest_buff_start 
			    + datatype->indices[j];
		    mpich_globus2_unpack_data(src_buff_start,
					      in_position,
					      src_format,
					      tmp,
					      datatype->blocklens[j],
					      datatype->old_types[j],
					      &tmp_int,
					      error_code);
		    *out_position += tmp_int;
		} /* endfor */
		dest_buff_start = (void *) (((globus_byte_t *) dest_buff_start)
				    + datatype->extent);
	    } /* endfor */
	}
	break;
	default:
	    globus_libc_fprintf(stderr,
		"ERROR: MPID_Unpack: encountered unrecognizable MPIR type %d\n",
		datatype->dte_type);
	    *error_code = MPI_ERR_INTERN;
	break;
    } /* end switch() */

}
/* end mpich_globus2_unpack_data() */

/*
 * MPID_Pack_size
 *
 * NOTE: MPID_Msg_pack_t msgact ignored for reasons stated at top of file
 *
 * NOTE: there's no way for me to report an error condition.
 *       where's the *error_code arg?
 *       in case of an error, i will return pass 0 and print an error
 *       message to stdout.
 */
void MPID_Pack_size(int count,
		    struct MPIR_DATATYPE *datatype,
		    MPID_Msg_pack_t msgact, /* ignored */
		    int *size)
{
    int					tmp_size;

    tmp_size = local_size(count, datatype);
    
    if (tmp_size < 0)
    {
	globus_libc_fprintf(stderr,
	"ERROR: MPID_Pack_size could not calculate pack size, returning 0\n");
	*size = 0;
    } /* endif */

    *size = tmp_size + sizeof(unsigned char);
}
/* end MPID_Pack_size() */

/*
 * local_size
 *
 * return -1 when there's problems
 *
 * NOTE: there is one more datatype found in datatype.h ... MPIR_FORT_INT
 *       it has been explained to me by bill that we do not have to
 *       support an explicit case for that type because it is a
 *       synonym for one of the other types we already have a case
 *       statement for (which type it is a synonym for is architecture 
 *       dependent and determined during mpich configuration).
 *
 */
int local_size(int count, struct MPIR_DATATYPE *datatype)
{
    int rc;

    if (count < 0)
    {
	globus_libc_fprintf(stderr,
	    "ERROR: local_size: passed count %d .... must be >= 0\n", 
	    count);
	return -1;
    } /* endif */

    switch(datatype->dte_type)
    {
        case MPIR_CHAR:           rc = globus_dc_sizeof_char(count);      break;
        case MPIR_UCHAR:          rc = globus_dc_sizeof_u_char(count);    break;
	/* MPIR_PACKED are always raw bytes and are never converted */
        case MPIR_PACKED:         rc = count;                             break;
        case MPIR_BYTE:           rc = count ;                            break;
        case MPIR_SHORT:          rc = globus_dc_sizeof_short(count);     break;
        case MPIR_USHORT:         rc = globus_dc_sizeof_u_short(count);   break;
        case MPIR_LOGICAL: /* 'logical' in FORTRAN is always same as 'int' */
        case MPIR_INT:            rc = globus_dc_sizeof_int(count);       break;
        case MPIR_UINT:           rc = globus_dc_sizeof_u_int(count);     break;
        case MPIR_LONG:           rc = globus_dc_sizeof_long(count);      break;
        case MPIR_LONGLONGINT:    rc = globus_dc_sizeof_long_long(count); break;
        case MPIR_ULONG:          rc = globus_dc_sizeof_u_long(count);    break;
        case MPIR_FLOAT:          rc = globus_dc_sizeof_float(count);     break;
        case MPIR_DOUBLE:         rc = globus_dc_sizeof_double(count);    break;
        case MPIR_LONGDOUBLE: /* not supported by Globus */ rc = 0;       break;
        case MPIR_UB:             
        case MPIR_LB:             rc = 0;                                break;
        case MPIR_COMPLEX:        rc = globus_dc_sizeof_float(2*count);  break;
        case MPIR_DOUBLE_COMPLEX: rc = globus_dc_sizeof_double(2*count); break;
        case MPIR_CONTIG:         
            rc = local_size(count*datatype->count, datatype->old_type);
            break;
        case MPIR_VECTOR:         
        case MPIR_HVECTOR:        
	    {
		int tmp = local_size(datatype->blocklen, datatype->old_type);
		rc = (tmp == -1 ? -1 : tmp*count*datatype->count);
	    }
            break;
        case MPIR_INDEXED:        
        case MPIR_HINDEXED:       
	    {
		int i, tmp, tmp2;
		for (rc = tmp = tmp2 = i = 0; 
		    tmp2 != -1 && i < datatype->count; 
			i++)
		{
		    tmp2 = local_size(datatype->blocklens[i], 
					datatype->old_type);
		    if (tmp2 == -1)
			tmp = -1;
		    else
			tmp += tmp2;
		} /* endfor */
		if (tmp != -1)
		    rc = tmp*count;
		else
		    rc = -1;
	    }
	    break;
        case MPIR_STRUCT:
	    {
		int i, tmp, tmp2;
		for (rc = tmp = tmp2 = i = 0; 
		    tmp2 != -1 && i < datatype->count; 
			i++)
		{
		    tmp2 = local_size(datatype->blocklens[i], 
					datatype->old_types[i]);
		    if (tmp2 == -1)
			tmp = -1;
		    else
			tmp += tmp2;
		} /* endfor */
		if (tmp != -1)
		    rc = tmp*count;
		else
		    rc = -1;
	    }
	    break;
        default:        
            globus_libc_fprintf(stderr,
                "ERROR: local_size: encountered unrecognizable MPIR type %d\n", 
		    datatype->dte_type);
            rc = -1;
	    break;
    } /* end switch */

    return rc;

} /* end local_size() */
