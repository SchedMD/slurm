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

#if DEBUG_MODULE
int	Node_Record_Count = 0;		/* Count of records in the Node Record Table */

/* main is used here for module testing purposes only */
main(int argc, char * argv[]) {
    char In_Line[128];
    char *Out_Line;
    int  Error_Code, Int_Found, i, size;
    char *String_Found;
    unsigned *Map1, *Map2, *Map3;

    printf("Testing string manipulation functions...\n");
    strcpy(In_Line, "Test1=UNLIMITED Test2=1234 Test3 LeftOver Test4=My_String");

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
    if (strcmp(String_Found,"My_String") != 0) 
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
    printf("BitMapPrint #1 shows %s\n", Out_Line);
    free(Out_Line);
    Map2 = BitMapCopy(Map1);
    Out_Line = BitMapPrint(Map2);
    printf("BitMapPrint #2 shows %s\n", Out_Line);
    free(Out_Line);
    Map3 = BitMapCopy(Map1);
    BitMapClear(Map2, 23);
    BitMapOR(Map3, Map2);
    if (BitMapValue(Map3, 23) != 1) printf("ERROR: BitMap error 1\n");
    if (BitMapValue(Map3, 71) != 1) printf("ERROR: BitMap error 2\n");
    if (BitMapValue(Map3, 93) != 0) printf("ERROR: BitMap error 3\n");
    BitMapAND(Map3, Map2);
    if (BitMapValue(Map3, 23) != 0) printf("ERROR: BitMap error 4\n");
    if (BitMapValue(Map3, 71) != 1) printf("ERROR: BitMap error 5\n");
    if (BitMapValue(Map3, 93) != 0) printf("ERROR: BitMap error 6\n");
    Out_Line = BitMapPrint(Map3);
    printf("BitMapPrint #3 shows %s\n", Out_Line);
    free(Out_Line);

    memset(Map1, 0, size);
    for (i=0; i<10; i++) {
	BitMapSet(Map1, (i+35));
	if (i>0) BitMapSet(Map1, (i+65));
    } /* for */
    Out_Line = BitMapPrint(Map1);
    printf("BitMapPrint #4 shows %s\n", Out_Line);
    size = BitMapCount(Map1);
    if (size != 19) printf("ERROR: BitMapCount error, %d\n", size);
    size = BitMapConsecutive(Map1, &i);
    if (size != 10) printf("ERROR: BitMapConsecutive error 1, %d\n", size);
    if (i != 35)    printf("ERROR: BitMapConsecutive error 2, %d\n", i);
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
	return;
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
	return;
    } /* if */

    val  = Position / (sizeof(unsigned)*8);
    bit  = Position % (sizeof(unsigned)*8);
    mask = ~(0x1 << ((sizeof(unsigned)*8)-1-bit));

    BitMap[val] &= mask;
} /* BitMapClear */


/*
 * BitMapConsecutive - Return the count of consecutive set bits in the specified bitmap
 * Input: BitMap - The bit map to get count from
 *        Position - Location into which the node index of the first entry is stored
 * Output: Position - Location of the first node index in the sequence
 *         Returns the count of set bits
 */
int BitMapConsecutive(unsigned *BitMap, int *Position) {
    int bit, size, word;
    int count, biggest, biggest_location, start;
    unsigned mask;

    if (BitMap == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "BitMapConsecutive: BitMap pointer is NULL\n");
#else
	syslog(LOG_ALERT, "BitMapConsecutive: BitMap pointer is NULL\n");
#endif
	return 0;
    } /* if */

    biggest = biggest_location = count = 0;
    size = (Node_Record_Count + (sizeof(unsigned)*8) - 1) / 8;	/* Bytes */
    size /= sizeof(unsigned);			/* Count of unsigned's */
    for (word=0; word<size; word++) {
	for (bit=0; bit<(sizeof(unsigned)*8); bit++) {
	    mask = (0x1 << ((sizeof(unsigned)*8)-1-bit));
	    if (BitMap[word] & mask) {
		if (count++ == 0) start = (word * sizeof(unsigned) * 8 + bit);
	    } else {
		if (count > biggest) {
		    biggest = count;
		    biggest_location = start;
		} /* if */
		count = 0;
	    } /* else */
	} /* for (bit */
    } /* for (word */
    if (count > biggest) {
	biggest = count;
	biggest_location = start;
    } /* if */
    *Position = biggest_location;
    return biggest;
} /* BitMapConsecutive */


/*
 * BitMapCopy - Create a copy of a bitmap
 * Input: BitMap - The bitmap create a copy of
 * Output: Returns pointer to copy of BitMap or NULL if error (no memory)
 * NOTE:  The returned value MUST BE FREED by the calling routine
 */
unsigned *BitMapCopy(unsigned *BitMap) {
    int i, size;
    unsigned *Output;

    if (BitMap == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "BitMapCopy: BitMap pointer is NULL\n");
#else
	syslog(LOG_ALERT, "BitMapCopy: BitMap pointer is NULL\n");
#endif
	return NULL;
    } /* if */

    size = (Node_Record_Count + (sizeof(unsigned)*8) - 1) / 8;	/* Bytes */
    Output = malloc(size);
    if (Output == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "BitMapCopy: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "BitMapCopy: unable to allocate memory\n");
#endif
	return NULL;
    } /* if */

    size /= sizeof(unsigned);			/* Count of unsigned's */
    for (i=0; i<size; i++) {
	Output[i] = BitMap[i];
    } /* for (i */
    return Output;
} /* BitMapCopy */


/*
 * BitMapCount - Return the count of set bits in the specified bitmap
 * Input: BitMap - The bit map to get count from
 * Output: Returns the count of set bits
 */
int BitMapCount(unsigned *BitMap) {
    int count, bit, size, word;
    unsigned mask;

    if (BitMap == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "BitMapCount: BitMap pointer is NULL\n");
#else
	syslog(LOG_ALERT, "BitMapCount: BitMap pointer is NULL\n");
#endif
	return 0;
    } /* if */

    count = 0;
    size = (Node_Record_Count + (sizeof(unsigned)*8) - 1) / 8;	/* Bytes */
    size /= sizeof(unsigned);			/* Count of unsigned's */
    for (word=0; word<size; word++) {
	for (bit=0; bit<(sizeof(unsigned)*8); bit++) {
	    mask = (0x1 << ((sizeof(unsigned)*8)-1-bit));
	    if (BitMap[word] & mask) {
		count++;
	    } /* if */
	} /* for (bit */
    } /* for (word */
    return count;
} /* BitMapCount */


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
	return;
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
	return NULL;
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
	return NULL;
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
	return;
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
 * Output: Normally returns the value 0 or 1, returns -1 if given bad BitMap ponter
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
	return -1;
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
	    return ENOMEM;
	} /* if */
	strcpy(destination[0], str_ptr2);
	for (i=0; i<(str_len1+str_len2); i++) {
	    str_ptr1[i] = ' ';
	} /* for */
    } /* if */
    return 0;
} /* Load_String */



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
	if (In_Line[i] == '\n') In_Line[i]=' ';
	if (isspace((int)In_Line[i])) continue;
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
