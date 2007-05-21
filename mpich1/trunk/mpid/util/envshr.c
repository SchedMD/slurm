/*
 * This file contains routines for sharing environment variables.
 * 
 * The design allows each node to get an enviroment variable, and then
 * distribute the values.  The assumption is that the likely cases are
 * (a) All have the same value (e.g., environment variable in .cshrc)
 * (b) Only one has the variable set (e.g., user set and called mpirun)
 *
 * These form a general replacement for getenv.
 *
 * The general usage is
 * void MPID_EnvInit( int maxenv )
 * void MPID_EnvAdd( char *name, char *defvalue )
 * void MPID_EnvCollect( )
 * char *MPID_EnvGetValue( char *name )
 *
 * Maybe this should give integer values, or, as an optimization, first 
 * check whether all values ARE integers.
 */
