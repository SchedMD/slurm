/*
 * bits_bytes.c  - Tools for manipulating bitmaps and strings
 * See slurm.h for documentation on external functions and data structures
 *
 * Author: Moe Jette, jette@llnl.gov
 */

#define DEBUG_SYSTEM  1

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "list.h"
#include "slurm.h"

#define BUF_SIZE 1024
#define SEPCHARS " \n\t"
/* Tag fields of data structures dumped to API (jobs, partitions, nodes, etc.) */
/* This is useful for API debugging, especially when changing formats */
#define TAG_SIZE 8

int	Node_Record_Count = 0;		/* Count of records in the Node Record Table */

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
main(int argc, char * argv[]) {
    char In_Line[128];
    char *Out_Line;
    int  Error_Code, Int_Found, i, size;
    char *String_Found;
    unsigned *Map1, *Map2, *Map3;
    char *Buffer;
    int Buffer_Offset, Buffer_Size;

    printf("Testing string manipulation functions...\n");
    strcpy(In_Line, "Test1=UNLIMITED Test2=1234 Test3 LeftOver Test4=My,String");

    Error_Code = Load_Integer(&Int_Found, "Test1=", In_Line);
    if (Error_Code) printf("Load_Integer error on Test1\n");
    if (Int_Found != -1)
	printf("Load_Integer parse error on Test1, got %d\n", Int_Found);

    Error_Code = Load_Integer(&Int_Found, "Test2=", In_Line);
    if (Error_Code) printf("Load_Integer error on Test2\n");
    if (Int_Found != 1234) 
	printf("Load_Integer parse error on Test2, got %d\n", Int_Found);

    Error_Code = Load_Integer(&Int_Found, "Test3", In_Line);
    if (Error_Code) printf("Load_Integer error on Test3\n");
    if (Int_Found != 1) 
	printf("Load_Integer parse error on Test3, got %d\n", Int_Found);

    String_Found = NULL;	/* NOTE: arg1 of Load_String is freed if set */
    Error_Code = Load_String(&String_Found, "Test4=", In_Line);
    if (Error_Code) printf("Load_String error on Test4\n");
    if (strcmp(String_Found,"My,String") != 0) 
	printf("Load_String parse error on Test4, got :%s:\n",String_Found);
    free(String_Found);

    printf("NOTE: We expect this to print \"Leftover\"\n");
    Report_Leftover(In_Line, 0);

    printf("\n\nTesting bitmap manipulation functions...\n");
    Node_Record_Count = 97;
    size = (Node_Record_Count + (sizeof(unsigned)*8) - 1) / (sizeof(unsigned)*8);
    size *= (sizeof(unsigned)*8);
    Map1 = malloc(size);
    memset(Map1, 0, size);
    BitMapSet(Map1, 23);
    BitMapSet(Map1, 71);
    Out_Line = BitMapPrint(Map1);
    printf("BitMapPrint #1 Map1 shows %s\n", Out_Line);
    free(Out_Line);
    Map2 = BitMapCopy(Map1);
    Out_Line = BitMapPrint(Map2);
    printf("BitMapPrint #2 Map2 shows %s\n", Out_Line);
    free(Out_Line);
    Map3 = BitMapCopy(Map1);
    BitMapClear(Map2, 23);
    if (BitMapIsSuper(Map2,Map1) != 1) printf("ERROR: BitMapIsSuper error 1\n");
    if (BitMapIsSuper(Map1,Map2) != 0) printf("ERROR: BitMapIsSuper error 2\n");
    BitMapOR(Map3, Map2);
    if (BitMapValue(Map3, 23) != 1) printf("ERROR: BitMapOR error 1\n");
    if (BitMapValue(Map3, 71) != 1) printf("ERROR: BitMapOR error 2\n");
    if (BitMapValue(Map3, 93) != 0) printf("ERROR: BitMapOR error 3\n");
    BitMapAND(Map3, Map2);
    if (BitMapValue(Map3, 23) != 0) printf("ERROR: BitMapAND error 1\n");
    if (BitMapValue(Map3, 71) != 1) printf("ERROR: BitMapAND error 2\n");
    if (BitMapValue(Map3, 93) != 0) printf("ERROR: BitMapAND error 3\n");
    Out_Line = BitMapPrint(Map3);
    printf("BitMapPrint #3 Map3 shows %s\n", Out_Line);
    free(Out_Line);

    BitMapFill(Map1);
    Out_Line = BitMapPrint(Map1);
    if (BitMapValue(Map1, 34) != 1) printf("ERROR: BitMapFill error 1\n");
    printf("BitMapPrint #4 Map1 shows %s\n", Out_Line);
    free(Out_Line);

    memset(Map1, 0, size);
    for (i=0; i<10; i++) {
	BitMapSet(Map1, (i+35));
	if (i>0) BitMapSet(Map1, (i+65));
    } /* for */
    Out_Line = BitMapPrint(Map1);
    printf("BitMapPrint #6 Map1 shows %s\n", Out_Line);
    size = BitMapCount(Map1);
    if (size != 19) printf("ERROR: BitMapCount error, %d\n", size);

    printf("\n\nTesting buffer I/O functions...\n");
    Buffer = NULL;
    Buffer_Offset = Buffer_Size= 0;
    size = 123;
    Error_Code = Write_Value(&Buffer, &Buffer_Offset, &Buffer_Size, "Val1", &size, sizeof(int));
    if (Error_Code) printf("Write_Value error on Test1\n");
    Error_Code = Write_Value(&Buffer, &Buffer_Offset, &Buffer_Size, "Val2", Map1, 8);
    if (Error_Code) printf("Write_Value error on Test2\n");
    size = 45;
    Error_Code = Write_Value(&Buffer, &Buffer_Offset, &Buffer_Size, "Val3", &size, sizeof(int));
    if (Error_Code) printf("Write_Value error on Test4\n");
    Error_Code = Write_Value(&Buffer, &Buffer_Offset, &Buffer_Size, "EndRec", &size, 0);
    if (Error_Code) printf("Write_Value error on Test4\n");
    size = 124;
    Error_Code = Write_Value(&Buffer, &Buffer_Offset, &Buffer_Size, "Val1", &size, sizeof(int));
    if (Error_Code) printf("Write_Value error on Test5\n");
    Buffer_Offset = 0;
    Error_Code = Read_Value(Buffer, &Buffer_Offset, Buffer_Size, "Val1", &size);
    if (Error_Code || (size != 123)) printf("Read_Value error on Test1\n");
    Error_Code = Read_Array(Buffer, &Buffer_Offset, Buffer_Size, "Val2", (void**)&Map2, &i);
    if (Error_Code || (strncmp((char *)Map1, (char *)Map2,1) != 0) || (i != 8)) 
	printf("Read_Array error on Test1\n");
    Error_Code = Read_Tag(Buffer, &Buffer_Offset, Buffer_Size, "EndRec");
    if (Error_Code) printf("Read_Tag error on Test4\n");
    Error_Code = Read_Value(Buffer, &Buffer_Offset, Buffer_Size, "Val1", &size);
    if (Error_Code || (size != 124)) printf("Read_Value error on Test2\n");
    exit(0);
} /* main */
#endif


/*
 * BitMapAND - AND two bitmaps together
 * Input: BitMap1 and BitMap2 - The bitmaps to AND
 * Output: BitMap1 is set to the value of BitMap1 & BitMap2
 */
void BitMapAND(unsigned *BitMap1, unsigned *BitMap2) {
    int i, size;

    if ((BitMap1 == NULL) || (BitMap2 == NULL)) {
#if DEBUG_SYSTEM
	fprintf(stderr, "BitMapAND: BitMap pointer is NULL\n");
#else
	syslog(LOG_ALERT, "BitMapAND: BitMap pointer is NULL\n");
#endif
	abort();
    } /* if */

    size = (Node_Record_Count + (sizeof(unsigned)*8) - 1) / (sizeof(unsigned)*8);
    for (i=0; i<size; i++) {
	BitMap1[i] &= BitMap2[i];
    } /* for (i */
} /* BitMapAND */


/*
 * BitMapClear - Clear the specified bit in the specified bitmap
 * Input: BitMap - The bit map to manipulate
 *        Position - Postition to clear
 * Output: BitMap - Updated value
 */
void BitMapClear(unsigned *BitMap, int Position) {
    int val, bit;
    unsigned mask;

    if (BitMap == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "BitMapClear: BitMap pointer is NULL\n");
#else
	syslog(LOG_ALERT, "BitMapClear: BitMap pointer is NULL\n");
#endif
	abort();
    } /* if */

    val  = Position / (sizeof(unsigned)*8);
    bit  = Position % (sizeof(unsigned)*8);
    mask = ~(0x1 << ((sizeof(unsigned)*8)-1-bit));

    BitMap[val] &= mask;
} /* BitMapClear */


/*
 * BitMapCopy - Create a copy of a bitmap
 * Input: BitMap - The bitmap create a copy of
 * Output: Returns pointer to copy of BitMap or NULL if error (no memory)
 * NOTE:  The returned value MUST BE FREED by the calling routine
 */
unsigned *BitMapCopy(unsigned *BitMap) {
    int size;
    unsigned *Output;

    if (BitMap == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "BitMapCopy: BitMap pointer is NULL\n");
#else
	syslog(LOG_ALERT, "BitMapCopy: BitMap pointer is NULL\n");
#endif
	abort();
    } /* if */

    size = (Node_Record_Count + (sizeof(unsigned)*8) - 1) / 8;	/* Bytes */
    Output = malloc(size);
    if (Output == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "BitMapCopy: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "BitMapCopy: unable to allocate memory\n");
#endif
	abort();
    } /* if */

    (void) memcpy(Output, BitMap, size);
    return Output;
} /* BitMapCopy */


/*
 * BitMapCount - Return the count of set bits in the specified bitmap
 * Input: BitMap - The bit map to get count from
 * Output: Returns the count of set bits
 * NOTE: This routine adapted from Linux 2.4.9 <linux/bitops.h>.
 */
int BitMapCount(unsigned *BitMap) {
    int count, byte, size, word, res;
    unsigned scan;

    if (BitMap == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "BitMapCount: BitMap pointer is NULL\n");
#else
	syslog(LOG_ALERT, "BitMapCount: BitMap pointer is NULL\n");
#endif
	abort();
    } /* if */

    count = 0;
    size = (Node_Record_Count + (sizeof(unsigned)*8) - 1) / 8;	/* Bytes */
    size /= sizeof(unsigned);			/* Count of unsigned's */
    for (word=0; word<size; word++) {
	if (sizeof(unsigned) == 4) {
	    res = (BitMap[word] & 0x55555555) + ((BitMap[word] >> 1)  & 0x55555555);
	    res = (res  & 0x33333333) + ((res  >> 2)  & 0x33333333);
	    res = (res  & 0x0F0F0F0F) + ((res  >> 4)  & 0x0F0F0F0F);
	    res = (res  & 0x00FF00FF) + ((res  >> 8)  & 0x00FF00FF);
	    res = (res  & 0x0000FFFF) + ((res  >> 16) & 0x0000FFFF);
	    count += res;
	} else if (sizeof(unsigned) == 8) {
	    res = (BitMap[word] & 0x5555555555555555) + ((BitMap[word] >> 1) & 0x5555555555555555);
	    res = (res & 0x3333333333333333) + ((res >> 2)  & 0x3333333333333333);
	    res = (res & 0x0F0F0F0F0F0F0F0F) + ((res >> 4)  & 0x0F0F0F0F0F0F0F0F);
	    res = (res & 0x00FF00FF00FF00FF) + ((res >> 8)  & 0x00FF00FF00FF00FF);
	    res = (res & 0x0000FFFF0000FFFF) + ((res >> 16) & 0x0000FFFF0000FFFF);
	    res = (res & 0x00000000FFFFFFFF) + 
	          ((res >> sizeof(unsigned)*4) & 0x00000000FFFFFFFF);
	    count += res;
	} else {
	    for (byte=0; byte<(sizeof(unsigned)*8); byte+=8) {
		scan = BitMap[word] >> ((sizeof(unsigned)*8)-8-byte);
		if (scan & 0x01) count++;
		if (scan & 0x02) count++;
		if (scan & 0x04) count++;
		if (scan & 0x08) count++;
		if (scan & 0x10) count++;
		if (scan & 0x20) count++;
		if (scan & 0x40) count++;
		if (scan & 0x80) count++;
	    } /* for (byte */
	} /* else */
    } /* for (word */
    return count;
} /* BitMapCount */


/*
 * BitMapFill - Fill the provided bitmap so that all bits between the highest and lowest
 * 	previously set bits are also set (i.e fill in the gaps to make it contiguous)
 * Input: BitMap - Pointer to the bit map to fill in
 * Output: BitMap - The filled in bitmap
 */
void BitMapFill(unsigned *BitMap) {
    int bit, size, word;
    int first, last, position, gap;
    unsigned mask;

    if (BitMap == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "BitMapFill: BitMap pointer is NULL\n");
#else
	syslog(LOG_ALERT, "BitMapFill: BitMap pointer is NULL\n");
#endif
	abort();
    } /* if */

    first = last = position = gap = -1;
    size = (Node_Record_Count + (sizeof(unsigned)*8) - 1) / 8;	/* Bytes */
    size /= sizeof(unsigned);			/* Count of unsigned's */
    for (word=0; word<size; word++) {
	for (bit=0; bit<(sizeof(unsigned)*8); bit++) {
	    position++;
	    mask = (0x1 << ((sizeof(unsigned)*8)-1-bit));
	    if (BitMap[word] & mask) {
		if (first == -1) first=position;
		if ((last != (position-1)) && (last != -1)) gap=1;
		last = position;
	    } /* else */
	} /* for (bit */
    } /* for (word */

    if (gap == -1) return;

    position = -1;
    for (word=0; word<size; word++) {
	for (bit=0; bit<(sizeof(unsigned)*8); bit++) {
	    position++;
	    if (position <= first) continue;
	    if (position >= last)  continue;
	    mask = (0x1 << ((sizeof(unsigned)*8)-1-bit));
	    BitMap[word] |= mask;
	} /* for (bit */
    } /* for (word */
} /* BitMapFill */


/* 
 * BitMapIsSuper - Report if one bitmap's contents are a superset of another
 * Input: BitMap1 and BitMap2 - The bitmaps to compare
 * Output: Return 1 if if all bits in BitMap1 are also in BitMap2, 0 otherwise 
 */
int BitMapIsSuper(unsigned *BitMap1, unsigned *BitMap2) {
    int i, size;

    if ((BitMap1 == NULL) || (BitMap2 == NULL)) {
#if DEBUG_SYSTEM
	fprintf(stderr, "BitMapOR: BitMap pointer is NULL\n");
#else
	syslog(LOG_ALERT, "BitMapOR: BitMap pointer is NULL\n");
#endif
	abort();
    } /* if */

    size = (Node_Record_Count + (sizeof(unsigned)*8) - 1) / (sizeof(unsigned)*8);
    for (i=0; i<size; i++) {
	if (BitMap1[i] != (BitMap1[i] & BitMap2[i])) return 0;
    } /* for (i */
    return 1;
} /* BitMapIsSuper */


/*
 * BitMapOR - OR two bitmaps together
 * Input: BitMap1 and BitMap2 - The bitmaps to OR
 * Output: BitMap1 is set to the value of BitMap1 | BitMap2
 */
void BitMapOR(unsigned *BitMap1, unsigned *BitMap2) {
    int i, size;

    if ((BitMap1 == NULL) || (BitMap2 == NULL)) {
#if DEBUG_SYSTEM
	fprintf(stderr, "BitMapOR: BitMap pointer is NULL\n");
#else
	syslog(LOG_ALERT, "BitMapOR: BitMap pointer is NULL\n");
#endif
	abort();
    } /* if */

    size = (Node_Record_Count + (sizeof(unsigned)*8) - 1) / (sizeof(unsigned)*8);
    for (i=0; i<size; i++) {
	BitMap1[i] |= BitMap2[i];
    } /* for (i */
} /* BitMapOR */


/*
 * BitMapPrint - Convert the specified bitmap into a printable hexadecimal string
 * Input: BitMap - The bit map to print
 * Output: Returns a string
 * NOTE: The returned string must be freed by the calling program
 */
char *BitMapPrint(unsigned *BitMap) {
    int i, j, k, size, nibbles;
    char *Output, temp_str[2];

    if (BitMap == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "BitMapPrint: BitMap pointer is NULL\n");
#else
	syslog(LOG_ALERT, "BitMapPrint: BitMap pointer is NULL\n");
#endif
	abort();
    } /* if */

    size = (Node_Record_Count + (sizeof(unsigned)*8) - 1) / (sizeof(unsigned)*8);
    nibbles = (Node_Record_Count + 3) / 4;
    Output = (char *)malloc(nibbles+3);
    if (Output == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "BitMapPrint: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "BitMapPrint: unable to allocate memory\n");
#endif
	abort();
    } /* if */

    strcpy(Output, "0x");
    k = 0;
    for (i=0; i<size; i++) {				/* Each unsigned */
	for (j=((sizeof(unsigned)*8)-4); j>=0; j-=4) {	/* Each nibble */
	    sprintf(temp_str, "%x", ((BitMap[i]>>j)&0xf));
	    strcat(Output, temp_str);
	    k++;
	    if (k == nibbles) return Output;
	} /* for (j */
    } /* for (i */
    return Output;
} /* BitMapPrint */


/*
 * BitMapSet - Set the specified bit in the specified bitmap
 * Input: BitMap - The bit map to manipulate
 *        Position - Postition to set
 * Output: BitMap - Updated value
 */
void BitMapSet(unsigned *BitMap, int Position) {
    int val, bit;
    unsigned mask;

    if (BitMap == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "BitMapSet: BitMap pointer is NULL\n");
#else
	syslog(LOG_ALERT, "BitMapSet: BitMap pointer is NULL\n");
#endif
	abort();
    } /* if */

    val  = Position / (sizeof(unsigned)*8);
    bit  = Position % (sizeof(unsigned)*8);
    mask = (0x1 << ((sizeof(unsigned)*8)-1-bit));

    BitMap[val] |= mask;
} /* BitMapSet */


/*
 * BitMapValue - Return the value of specified bit in the specified bitmap
 * Input: BitMap - The bit map to get value from
 *        Position - Postition to get
 * Output: Normally returns the value 0 or 1
 */
int BitMapValue(unsigned *BitMap, int Position) {
    int val, bit;
    unsigned mask;

    if (BitMap == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "BitMapValue: BitMap pointer is NULL\n");
#else
	syslog(LOG_ALERT, "BitMapValue: BitMap pointer is NULL\n");
#endif
	abort();
    } /* if */

    val  = Position / (sizeof(unsigned)*8);
    bit  = Position % (sizeof(unsigned)*8);
    mask = (0x1 << ((sizeof(unsigned)*8)-1-bit));

    mask &= BitMap[val];
    if (mask == 0)
	return 0;
    else
	return 1;
} /* BitMapValue */


/*
 * Load_Integer - Parse a string for a keyword, value pair  
 * Input: *destination - Location into which result is stored
 *        keyword - String to search for
 *        In_Line - String to search for keyword
 * Output: *destination - set to value, No change if value not found, 
 *             Set to 1 if keyword found without value, 
 *             Set to -1 if keyword followed by "UNLIMITED"
 *         In_Line - The keyword and value (if present) are overwritten by spaces
 *         return value - 0 if no error, otherwise an error code
 * NOTE: In_Line is overwritten, DO NOT USE A CONSTANT
 */
int Load_Integer(int *destination, char *keyword, char *In_Line) {
    char Scratch[BUF_SIZE];	/* Scratch area for parsing the input line */
    char *str_ptr1, *str_ptr2, *str_ptr3;
    int i, str_len1, str_len2;

    str_ptr1 = (char *)strstr(In_Line, keyword);
    if (str_ptr1 != NULL) {
	str_len1 = strlen(keyword);
	strcpy(Scratch, str_ptr1+str_len1);
	if ((Scratch[0] == (char)NULL) || 
	    (isspace((int)Scratch[0]))) { /* Keyword with no value set */
	    *destination = 1;
	    str_len2 = 0;
	} else {
	    str_ptr2 = (char *)strtok_r(Scratch, SEPCHARS, &str_ptr3);
	    str_len2 = strlen(str_ptr2);
	    if (strcmp(str_ptr2, "UNLIMITED") == 0)
		*destination = -1;
	    else if ((str_ptr2[0] >= '0') && (str_ptr2[0] <= '9')) {
		*destination = (int) strtol(Scratch, (char **)NULL, 10);
	   }  else {
#if DEBUG_SYSTEM
		fprintf(stderr, "Load_Integer: bad value for keyword %s\n", keyword);
#else
		syslog(LOG_ERR, "Load_Integer: bad value for keyword %s\n", keyword);
#endif
		return EINVAL;	
	    } /* else */
	} /* else */

	for (i=0; i<(str_len1+str_len2); i++) {
	    str_ptr1[i] = ' ';
	} /* for */
    } /* if */
    return 0;
} /* Load_Integer */


/*
 * Load_String - Parse a string for a keyword, value pair  
 * Input: *destination - Location into which result is stored
 *        keyword - String to search for
 *        In_Line - String to search for keyword
 * Output: *destination - set to value, No change if value not found, 
 *	     if *destination had previous value, that memory location is automatically freed
 *         In_Line - The keyword and value (if present) are overwritten by spaces
 *         return value - 0 if no error, otherwise an error code
 * NOTE: destination must be free when no longer required
 * NOTE: if destination is non-NULL at function call time, it will be freed 
 * NOTE: In_Line is overwritten, DO NOT USE A CONSTANT
 */
int Load_String(char **destination, char *keyword, char *In_Line) {
    char Scratch[BUF_SIZE];	/* Scratch area for parsing the input line */
    char *str_ptr1, *str_ptr2, *str_ptr3;
    int i, str_len1, str_len2;

    str_ptr1 = (char *)strstr(In_Line, keyword);
    if (str_ptr1 != NULL) {
	str_len1 = strlen(keyword);
	strcpy(Scratch, str_ptr1+str_len1);
	if ((Scratch[0] == (char)NULL) || 
	    (isspace((int)Scratch[0]))) { /* Keyword with no value set */
#if DEBUG_SYSTEM
	    fprintf(stderr, "Load_String: keyword %s lacks value\n", keyword);
#else
	    syslog(LOG_ERR, "Load_String: keyword %s lacks value\n", keyword);
#endif
	    return EINVAL;
	} /* if */
	str_ptr2 = (char *)strtok_r(Scratch, SEPCHARS, &str_ptr3);
	str_len2 = strlen(str_ptr2);
	if (destination[0] != NULL) free(destination[0]);
	destination[0] = (char *)malloc(str_len2+1);
	if (destination[0] == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Load_String: unable to allocate memory\n");
#else
	    syslog(LOG_ALERT, "Load_String: unable to allocate memory\n");
#endif
	    abort();
	} /* if */
	strcpy(destination[0], str_ptr2);
	for (i=0; i<(str_len1+str_len2); i++) {
	    str_ptr1[i] = ' ';
	} /* for */
    } /* if */
    return 0;
} /* Load_String */


/* 
 * Read_Array - Read the specified value from the specified buffer
 * Input: Buffer - Pointer to read buffer, must be allocated by alloc()
 *        Buffer_Offset - Byte offset in Buffer, read location
 *        Buffer_Size - Byte size of Buffer
 *        Tag - Unique identification for information
 *        Value - Pointer to value to be loaded with POINTER TO ARRAY, NOT THE VALUE
 *        Size - Pointer to storeage for size of Value in bytes (NULL if not desired)
 * Output: Buffer_Offset - Incremented by  size of size plus the Value size itself
 *         Value - Set to the buffer contents or location
 *         Size - Set to the byte size of Value (if pointer non NULL)
 *         Returns 0 if no error or EFAULT on end of buffer, EINVAL on bad tag 
 */
int Read_Array(char *Buffer, int *Buffer_Offset, int Buffer_Size, 
		char *Tag, void **Value, int* Size) {
    char Read_Tag[TAG_SIZE], Err_Msg[128];
    int size;

    if ((*Buffer_Offset + TAG_SIZE + sizeof(int)) > Buffer_Size) {
	return EFAULT;
    } /* if */

    memcpy(Read_Tag, &Buffer[*Buffer_Offset], TAG_SIZE);
    (*Buffer_Offset) += TAG_SIZE;
    if (strncmp(Read_Tag, Tag, TAG_SIZE) != 0) {
	sprintf(Err_Msg, "Write_Value: Bad tag in buffer, expected %%.%ds, read %%.%ds\n", 
		TAG_SIZE, TAG_SIZE);
#if DEBUG_SYSTEM
	fprintf(stderr, Err_Msg, Tag, Read_Tag);
#else
	syslog(LOG_ERR, Err_Msg, Tag, Read_Tag);
#endif
	return EINVAL;
    } /* if */

    memcpy(&size, &Buffer[*Buffer_Offset], sizeof(size));
    (*Buffer_Offset) += sizeof(size);
    if ((*Buffer_Offset + size) > Buffer_Size) {
	return EFAULT;
    } /* if */

    if (size)
	Value[0] = &Buffer[*Buffer_Offset];
    else
	Value[0] = NULL;
    (*Buffer_Offset) += size;
    if (Size != NULL) (*Size) = size;
    return 0;
} /* Read_Array */


/* 
 * Read_Tag - Read the specified buffer up to and including the specified tag
 *            Used to reach end of a record type
 * Input: Buffer - Pointer to read buffer, must be allocated by alloc()
 *        Buffer_Offset - Byte offset in Buffer, read location
 *        Buffer_Size - Byte size of Buffer
 *        Tag - Unique identification for information
 * Output: Buffer_Offset - Incremented by  size of size plus the Value size itself
 *         Returns 0 if no error or EFAULT on end of buffer, EINVAL on bad tag 
 */
int Read_Tag(char *Buffer, int *Buffer_Offset, int Buffer_Size, char *Tag) {
    char Read_Tag[TAG_SIZE], Err_Msg[128];
    int size;

    while ((*Buffer_Offset + TAG_SIZE + sizeof(int)) <= Buffer_Size) {
	memcpy(Read_Tag, &Buffer[*Buffer_Offset], TAG_SIZE);
	(*Buffer_Offset) += TAG_SIZE;

	memcpy(&size, &Buffer[*Buffer_Offset], sizeof(size));
	(*Buffer_Offset) += (sizeof(size) + size);

	if (strncmp(Read_Tag, Tag, TAG_SIZE) == 0) return 0;
    } /* while */
    return EFAULT;
} /* Read_Tag */


/* 
 * Read_Value - Read the specified value from the specified buffer
 * Input: Buffer - Pointer to read buffer, must be allocated by alloc()
 *        Buffer_Offset - Byte offset in Buffer, read location
 *        Buffer_Size - Byte size of Buffer
 *        Tag - Unique identification for information
 *        Value - Pointer to value to be loaded, If pointer to pointer
 * Output: Buffer_Offset - Incremented by  size of size plus the Value size itself
 *         Value - Set to the buffer contents or location
 *         Returns 0 if no error or EFAULT on end of buffer, EINVAL on bad tag 
 */
int Read_Value(char *Buffer, int *Buffer_Offset, int Buffer_Size, 
		char *Tag, void *Value) {
    char Read_Tag[TAG_SIZE], Err_Msg[128];
    int size;

    if ((*Buffer_Offset + TAG_SIZE + sizeof(int)) > Buffer_Size) {
	return EFAULT;
    } /* if */

    memcpy(Read_Tag, &Buffer[*Buffer_Offset], TAG_SIZE);
    (*Buffer_Offset) += TAG_SIZE;
    if (strncmp(Read_Tag, Tag, TAG_SIZE) != 0) {
	sprintf(Err_Msg, "Write_Value: Bad tag in buffer, expected %%.%ds, read %%.%ds\n", 
		TAG_SIZE, TAG_SIZE);
#if DEBUG_SYSTEM
	fprintf(stderr, Err_Msg, Tag, Read_Tag);
#else
	syslog(LOG_ERR, Err_Msg, Tag, Read_Tag);
#endif
	return EINVAL;
    } /* if */

    memcpy(&size, &Buffer[*Buffer_Offset], sizeof(size));
    (*Buffer_Offset) += sizeof(size);
    if ((*Buffer_Offset + size) > Buffer_Size) {
	return EFAULT;
    } /* if */

    memcpy(Value, &Buffer[*Buffer_Offset], size);
    (*Buffer_Offset) += size;
    return 0;
} /* Read_Value */


/* 
 * Report_Leftover - Report any un-parsed (non-whitespace) characters on the
 * configuration input line.
 * Input: In_Line - What is left of the configuration input line.
 *        Line_Num - Line number of the configuration file.
 * Output: NONE
 */
void Report_Leftover(char *In_Line, int Line_Num) {
    int Bad_Index, i;

    Bad_Index = -1;
    for (i=0; i<strlen(In_Line); i++) {
	if (isspace((int)In_Line[i]) || (In_Line[i] == '\n')) continue;
	Bad_Index=i;
	break;
    } /* if */

    if (Bad_Index == -1) return;
#if DEBUG_SYSTEM
    fprintf(stderr, "Report_Leftover: Ignored input on line %d of configuration: %s\n", 
	Line_Num, &In_Line[Bad_Index]);
#else
    syslog(LOG_ERR, "Report_Leftover: Ignored input on line %d of configuration: %s\n", 
	Line_Num, &In_Line[Bad_Index]);
#endif
    return;
} /* Report_Leftover */


/* 
 * Write_Value - Write the specified value and its size to the specified buffer, 
 *               enlarging the buffer as needed
 * Input: Buffer - Pointer to write buffer, must be allocated by alloc()
 *        Buffer_Offset - Byte offset in Buffer, write location
 *        Buffer_Size - Byte size of Buffer
 *        Tag - Unique identification for information type
 *        Value - Pointer to value to be writen
 *        Value_Size - Byte size of Value (may be int, long, etc.)
 * Output: Buffer - Value is written here, buffer may be relocated by realloc()
 *         Buffer_Offset - Incremented by Value_Size
 *         Returns 0 if no error or errno otherwise 
 */
int Write_Value(char **Buffer, int *Buffer_Offset, int *Buffer_Size, 
		char *Tag, void *Value, int Value_Size) {
    if ((*Buffer_Offset + TAG_SIZE + sizeof(int) + Value_Size) >= *Buffer_Size) {
	(*Buffer_Size) += Value_Size + 8096;
	Buffer[0] = realloc(Buffer[0], *Buffer_Size);
	if (Buffer[0] == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Write_Value: unable to allocate memory\n");
#else
	    syslog(LOG_ALERT, "Write_Value: unable to allocate memory\n");
#endif
	    abort();
	} /* if */
    } /* if */

    memcpy(Buffer[0]+(*Buffer_Offset), Tag, TAG_SIZE);
    (*Buffer_Offset) += TAG_SIZE;

    memcpy(Buffer[0]+(*Buffer_Offset), &Value_Size, sizeof(Value_Size));
    (*Buffer_Offset) += sizeof(Value_Size);
    if (Value_Size == 0) return 0;

    memcpy(Buffer[0]+(*Buffer_Offset), Value, Value_Size);
    (*Buffer_Offset) += Value_Size;
    return 0;
} /* Write_Value */

