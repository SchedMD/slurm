#ifndef GETOPT_H
#define GETOPT_H

bool GetOpt(int &argc, char * *&argv, char * flag);
bool GetOpt(int &argc, char * *&argv, char * flag, int *n);
bool GetOpt(int &argc, char * *&argv, char * flag, long *n);
bool GetOpt(int &argc, char * *&argv, char * flag, double *d);
bool GetOpt(int &argc, char * *&argv, char * flag, char * str);

#endif
