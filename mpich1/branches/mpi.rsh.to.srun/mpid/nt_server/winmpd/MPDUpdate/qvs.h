/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef QVS_H
#define QVS_H

#include <stdio.h>
#include <string.h>

#define MAX_QVS_STRING_LEN 1024

int pre_number_post(char *str, char *pre, int *n, int *extent, char *post);

struct QVS_Container
{
    QVS_Container() { str_list = NULL; cur_string = NULL; cur_number = NULL; };
    QVS_Container(char *str_encoded);
    ~QVS_Container();

    // encoder
    int encode_string(char *str);
    int output_encoded_string(char *str, int length);

    // decoder
    int decode_string(char *str);
    int first(char *str, int length);
    int next(char *str, int length);

private:
    struct NumberNode
    {
	NumberNode() { number = -1; next = NULL; };
	NumberNode(int n) { number = n; next = NULL; }; 
	int number;
	NumberNode *next;
    };
    struct StringNode
    {
	StringNode() { next = NULL; n_list = NULL; number_extent = 0; pre[0] = '\0'; post[0] = '\0'; };
	StringNode(char *string) { next = NULL; n_list = new NumberNode; pre_number_post(string, pre, &n_list->number, &number_extent, post); };
	~StringNode() { if (n_list != NULL) { NumberNode *p = n_list; while (n_list) { p = n_list; n_list = n_list->next; delete p; } } };

	int number_extent;
	NumberNode *n_list;
	char pre[100], post[100];
	StringNode *next;
    };

    StringNode *str_list;
    StringNode *cur_string;
    NumberNode *cur_number;
};

#endif
