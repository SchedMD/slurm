#include <sys/types.h>
#include <time.h>
#include <sys/time.h>        /*I <sys/time.h>  I*/
long SYhhmmtoSec( s )
char *s;
{
struct tm TM;
sscanf( s, "%d:%d", &TM.tm_hour, &TM.tm_min );
return 60 * (TM.tm_min + 60 * TM.tm_hour);
}
void SYGetDayTime( tp )
struct timeval *tp;
{
gettimeofday( tp, (struct timezone *)0 );
}
