#include "stdafx.h"
#include "qvs.h"
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>

QVS_Container::QVS_Container(char *str_encoded)
{
    str_list = NULL;
    cur_string = NULL;
    cur_number = NULL;
    decode_string(str_encoded);
}

QVS_Container::~QVS_Container()
{
    StringNode *p;

    p = str_list;
    while (p)
    {
	str_list = str_list->next;
	delete p;
	p = str_list;
    }
}

int QVS_Container::encode_string(char *str)
{
    StringNode *p_iter;
    char pre[100], post[100];
    int number, extent;

    if (str_list == NULL)
    {
	str_list = new StringNode(str);
	return 0;
    }

    pre_number_post(str, pre, &number, &extent, post);

    p_iter = str_list;
    while (p_iter)
    {
	if ((strcmp(pre, p_iter->pre) == 0) &&
	    (extent == p_iter->number_extent) &&
	    (strcmp(post, p_iter->post) == 0))
	{
	    NumberNode *n = new NumberNode(number);
	    // insert sorted
	    if (p_iter->n_list)
	    {
		NumberNode *n_trailer, *n_iter;
		n_trailer = n_iter = p_iter->n_list;
		while (n_iter)
		{
		    if (n_iter->number > n->number)
		    {
			if (n_trailer != n_iter)
			    n_trailer->next = n;
			else
			    p_iter->n_list = n;
			n->next = n_iter;
			n_iter = NULL;
		    }
		    else if (n_iter->number < n->number)
		    {
			if (n_trailer != n_iter)
			    n_trailer = n_trailer->next;
			n_iter = n_iter->next;
			if (n_iter == NULL)
			{
			    n_trailer->next = n;
			    n->next = NULL;
			}
		    }
		    else
		    {
			delete n;
			n_iter = NULL;
		    }
		}
	    }
	    else
	    {
		p_iter->n_list = n;
	    }
	    return 0;
	}

	p_iter = p_iter->next;
    }

    p_iter = new StringNode(str);
    p_iter->next = str_list;
    str_list = p_iter;

    return 0;
}

int pre_number_post(char *str, char *pre, int *n, int *extent, char *post)
{
    if (str == NULL)
    {
	*pre = '\0';
	*post = '\0';
	*n = -1;
	*extent = 0;
	return 0;
    }

    while (!isdigit(*str) && *str)
    {
	*pre = *str;
	pre++;
	str++;
    }
    *pre = '\0';

    if (*str == '\0')
    {
	*post = '\0';
	*n = -1;
	*extent = 0;
	return 0;
    }

    *n = atoi(str);
    
    *extent = 0;
    while (isdigit(*str))
    {
	(*extent)++;
	str++;
    }

    if (*str == '\0')
    {
	*post = '\0';
	return 0;
    }
    strcpy(post, str);
    return 0;
}

bool snprintf_update(char *&pszStr, int &length, char *pszFormat, ...)
{
    va_list list;
    int n;

    va_start(list, pszFormat);
    n = _vsnprintf(pszStr, length, pszFormat, list);
    va_end(list);

    if (n < 0)
    {
	pszStr[length-1] = '\0';
	length = 0;
	return false;
    }

    pszStr = &pszStr[n];
    length = length - n;

    return true;
}

int QVS_Container::output_encoded_string(char *str, int length)
{
    StringNode *p_iter;
    char format[100];

    if (length < 1)
	return 0;
    *str = '\0';
    p_iter = str_list;
    while (p_iter)
    {
	if (p_iter->number_extent > 0)
	{
	    if (p_iter->n_list && !p_iter->n_list->next)
	    {
		// there is only one host in this range so print it normally rather than in encoded form.
		_snprintf(format, 100, "%%s%%0%dd%%s", str_list->number_extent);
		snprintf_update(str, length, format, p_iter->pre, p_iter->n_list->number, p_iter->post);
	    }
	    else
	    {
		if (!snprintf_update(str, length, "%s%%0%dd%s(", p_iter->pre, p_iter->number_extent, p_iter->post))
		    return -1;
		NumberNode *n = p_iter->n_list;
		while (n)
		{
		    if ((n->next) && 
			(n->next->number == n->number + 1) && 
			(n->next->next) && 
			(n->next->next->number == n->number + 2))
		    {
			// print range
			if (!snprintf_update(str, length, "%d..", n->number))
			    return -1;
			while (n->next && (n->next->number == n->number + 1))
			    n = n->next;
			if (!snprintf_update(str, length, "%d", n->number))
			    return -1;
		    }
		    else
		    {
			// print number
			if (!snprintf_update(str, length, "%d", n->number))
			    return -1;
		    }
		    if (n->next)
		    {
			if (!snprintf_update(str, length, ","))
			    return -1;
		    }
		    n = n->next;
		}
		if (!snprintf_update(str, length, ")"))
		    return -1;
	    }
	}
	else
	{
	    if (!snprintf_update(str, length, "%s", p_iter->pre))
		return -1;
	}
	p_iter = p_iter->next;
	if (p_iter)
	{
	    if (!snprintf_update(str, length, " "))
		return -1;
	}
    }

    return 0;
}

int QVS_Container::decode_string(char *str)
{
    char *string, *token;
    char decoded_string[MAX_QVS_STRING_LEN];
    char *percent, *openbrace, *closebrace;

    string = new char[strlen(str)+1];
    strcpy(string, str);

    token = strtok(string, " \t\n");
    while (token)
    {
	// figure out strings
	openbrace = closebrace = NULL;
	percent = strchr(token, '%');
	if (percent)
	    openbrace = strchr(percent, '(');
	if (openbrace)
	    closebrace = strchr(openbrace, ')');
	if (closebrace > openbrace)
	{
	    char format[100], *pchar, *pchar2;
	    int first, last, n;
	    pchar = token;
	    pchar2 = format;
	    while (pchar != openbrace)
	    {
		*pchar2 = *pchar;
		pchar++;
		pchar2++;
	    }
	    *pchar2 = '\0';
	    pchar++;

	    while (*pchar != ')')
	    {
		if (!isdigit(*pchar))
		{
		    printf("Error: missing number inside (\n");
		    delete string;
		    return -1;
		}
		first = atoi(pchar);
		while (isdigit(*pchar))
		    pchar++;
		if (*pchar == '.')
		{
		    pchar++;
		    if (*pchar == '.')
		    {
			pchar++;
			last = atoi(pchar);
			for (n = first; n<=last; n++)
			{
			    _snprintf(decoded_string, MAX_QVS_STRING_LEN, format, n);
			    encode_string(decoded_string);
			}
			while (isdigit(*pchar))
			    pchar++;
			if (*pchar == ',')
			    pchar++;
		    }
		    else
		    {
			printf("Error: single dot in range\n");
			delete string;
			return -1;
		    }
		}
		else
		{
		    _snprintf(decoded_string, MAX_QVS_STRING_LEN, format, first);
		    encode_string(decoded_string);
		    if (*pchar == ',')
			pchar++;
		}
	    }
	}
	else
	{
	    strncpy(decoded_string, token, MAX_QVS_STRING_LEN);
	    encode_string(decoded_string);
	}
	token = strtok(NULL, " \t\n");
    }

    delete string;
    return 0;
}

int QVS_Container::first(char *str, int length)
{
    char format[100];
    cur_string = str_list;
    if (str_list)
    {
	if (str_list->number_extent && str_list->n_list)
	{
	    _snprintf(format, 100, "%%s%%0%dd%%s", str_list->number_extent);
	    _snprintf(str, length, format, str_list->pre, str_list->n_list->number, str_list->post);
	}
	else
	{
	    _snprintf(str, length, "%s", str_list->pre);
	}
	if (str_list->n_list)
	    cur_number = str_list->n_list->next;
	else
	    cur_number = NULL;
	return 1;
    }
    return 0;
}

int QVS_Container::next(char *str, int length)
{
    char format[100];
    if (cur_string)
    {
	if (cur_number)
	{
	    _snprintf(format, 100, "%%s%%0%dd%%s", cur_string->number_extent);
	    _snprintf(str, length, format, cur_string->pre, cur_number->number, cur_string->post);
	    cur_number = cur_number->next;
	    return 1;
	}
	else
	{
	    cur_string = cur_string->next;
	    if (cur_string)
	    {
		cur_number = cur_string->n_list;
		if (cur_string->number_extent && cur_number)
		{
		    _snprintf(format, 100, "%%s%%0%dd%%s", cur_string->number_extent);
		    _snprintf(str, length, format, cur_string->pre, cur_number->number, cur_string->post);
		    cur_number = cur_number->next;
		    return 1;
		}
		else
		{
		    _snprintf(str, length, "%s", cur_string->pre);
		    cur_number = NULL;
		    return 1;
		}
	    }
	}
    }
    return 0;
}
