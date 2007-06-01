#include <stdio.h>
#include "wrappergen.h"

void WriteWrappers( outf, wrapperFiles, nwrapperFiles, fn_list, n_fn )
FILE *outf;
char **wrapperFiles;
fn_def *fn_list;
int n_fn, nwrapperFiles;
{
  int fn_num, argNum;

  fprintf( outf, "%d\n", n_fn );
  for (fn_num=0; fn_num<n_fn; fn_num++) {
    fprintf( outf, "%s\n%s\n",
	     fn_list[fn_num].returnType,
	     fn_list[fn_num].name );

    fprintf( outf, "%d\n", fn_list[fn_num].nargs );

    for (argNum=0; argNum<fn_list[fn_num].nargs; argNum++) {
      fprintf( outf, "%s\n%s\n%s\n",
	       fn_list[fn_num].argTypePrefix[argNum],
	       fn_list[fn_num].argNames[argNum],
	       fn_list[fn_num].argTypeSuffix[argNum] );
    }
  }
}


PrintHelp( argv )
char **argv;
{
  printf( "write_proto - write prof_wrapper prototype file given doctext functions\n\n" );

  printf( "  write_proto <fn. list> <output file> <function declaration files...>\n\n" );

  exit( 0 );
}


