#include <stdio.h>
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include "lists.h"

xpand_list_Strings *Strings_CreateList(initialLen)
int initialLen;
{
  xpand_list_Strings *tempPtr;

  if (initialLen < 1) {
    initialLen = 10;
  }
  tempPtr = (xpand_list_Strings *) malloc(sizeof(xpand_list_Strings));
  if (tempPtr) {
    tempPtr->list = (char **) malloc(sizeof(char *) * initialLen);
    if (!tempPtr->list) {
      return 0;
    }
    tempPtr->nused = 0;
    tempPtr->size = initialLen;
  } else {
    fprintf( stderr, "Could not allocate memory for expanding list\n");
  }
  return tempPtr;
}


int Strings_AddItem(listPtr, newItem)
xpand_list_Strings *listPtr;
char *newItem;
{
  if (listPtr->nused == listPtr->size) {
    if (listPtr->size < 1)
      listPtr->size = 1;
    listPtr->size *= 2;
    listPtr->list = (char **) realloc(listPtr->list,
				      sizeof(char *) * listPtr->size);
    if (!listPtr->list) {
      return 1;
    }
  }
  listPtr->list[(listPtr->nused)++] = newItem;
  return 0;
}


int Strings_ShrinkToFit(listPtr)
xpand_list_Strings *listPtr;
{
  listPtr->size = listPtr->nused;
  if (!listPtr->size)
    listPtr->size = 1;
  listPtr->list = (char **) realloc(listPtr->list,
				    sizeof(char *) * listPtr->size);
  if (!listPtr->list) {
    return 1;
  }
  return 0;
}


xpand_list_String *String_CreateList(initialLen)
int initialLen;
{
  xpand_list_String *tempPtr;

  if (initialLen < 1) {
    initialLen = 10;
  }
  tempPtr = (xpand_list_String *) malloc(sizeof(xpand_list_String));
  if (tempPtr) {
    tempPtr->list = (char *) malloc(sizeof(char) * initialLen);
    if (!tempPtr->list) {
      return 0;
    }
    tempPtr->nused = 0;
    tempPtr->size = initialLen;
  } else {
    fprintf( stderr, "Could not allocate memory for expanding list\n");
  }
  return tempPtr;
}


int String_AddItem(listPtr, newItem)
xpand_list_String *listPtr;
char newItem;
{
  if (listPtr->nused == listPtr->size) {
    if (listPtr->size < 1)
      listPtr->size = 1;
    listPtr->size *= 2;
    listPtr->list = (char *) realloc(listPtr->list,
				      sizeof(char) * listPtr->size);
    if (!listPtr->list) {
      return 1;
    }
  }
  listPtr->list[(listPtr->nused)++] = newItem;
  return 0;
}


int String_ShrinkToFit(listPtr)
xpand_list_String *listPtr;
{
  listPtr->size = listPtr->nused;
  if (!listPtr->size)
    listPtr->size = 1;
  listPtr->list = (char *) realloc(listPtr->list,
				    sizeof(char) * listPtr->size);
  if (!listPtr->list) {
    return 1;
  }
  return 0;
}


xpand_list_Int *Int_CreateList(initialLen)
int initialLen;
{
  xpand_list_Int *tempPtr;

  if (initialLen < 1) {
    initialLen = 10;
  }
  tempPtr = (xpand_list_Int *) malloc(sizeof(xpand_list_Int));
  if (tempPtr) {
    tempPtr->list = (int *) malloc(sizeof(int) * initialLen);
    if (!tempPtr->list) {
      return 0;
    }
    tempPtr->nused = 0;
    tempPtr->size = initialLen;
  } else {
    fprintf( stderr, "Could not allocate memory for expanding list\n");
  }
  return tempPtr;
}


int Int_AddItem(listPtr, newItem)
xpand_list_Int *listPtr;
int newItem;
{
  if (listPtr->nused == listPtr->size) {
    if (listPtr->size < 1)
      listPtr->size = 1;
    listPtr->size *= 2;
    listPtr->list = (int *) realloc(listPtr->list,
				      sizeof(int) * listPtr->size);
    if (!listPtr->list) {
      return 1;
    }
  }
  listPtr->list[(listPtr->nused)++] = newItem;
  return 0;
}


int Int_ShrinkToFit(listPtr)
xpand_list_Int *listPtr;
{
  listPtr->size = listPtr->nused;
  if (!listPtr->size)
    listPtr->size = 1;
  listPtr->list = (int *) realloc(listPtr->list,
				    sizeof(int) * listPtr->size);
  if (!listPtr->list) {
    return 1;
  }
  return 0;
}


