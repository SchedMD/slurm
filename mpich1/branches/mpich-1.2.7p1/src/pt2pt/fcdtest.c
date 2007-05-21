/* fcdtest.c */
/* test to see whether TWO_WORD_FCD needs to be defined on a Cray */
/* if this program fails to compile, then define _TWO_WORD_FCD */

#include <fortran.h>
void main()
{
    void  *buf;
    _fcd temp;
    temp = _fcdtocp(buf);
}

