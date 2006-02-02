/*
 *  list.c
 * 
 *  Copyright (c) 2002-2006 by Judd Vinet <jvinet@zeroflux.org>
 * 
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, 
 *  USA.
 */

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "list.h"

/* Check PMList sanity
 *
 * 1: List seems to be OK.
 * 0: We're in deep ...
 */
int check_list(PMList* list)
{
	PMList* it = NULL;
	
	if(list == NULL) {
		return(1);
	}
	if(list->last == NULL) {
		return(0);
	}

	for(it = list; it && it->next; it = it->next);
	if(it != list->last) {
		return(0);
	}

	return(1);
}

PMList* list_new()
{
	PMList *list = NULL;
	
	list = (PMList*)malloc(sizeof(PMList));
	if(list == NULL) {
		return(NULL);
	}
	list->data = NULL;
	list->prev = NULL;
	list->next = NULL;
	list->last = list;
	return(list);
}

void list_free(PMList *list)
{
	PMList *ptr, *it = list;

	while(it) {
		ptr = it->next;
		free(it->data);
		free(it);
		it = ptr;
	}
	return;
}

PMList* list_add(PMList *list, void *data)
{
	PMList *ptr, *lp;

	ptr = list;
	if(ptr == NULL) {
		ptr = list_new();
		if (!ptr)
			return(NULL);
	}

	lp = list_last(ptr);
	if(lp == ptr && lp->data == NULL) {
		/* nada */
	} else {
		lp->next = list_new();
		if(lp->next == NULL) {
			return(NULL);
		}
		lp->next->prev = lp;
		lp->last = NULL;
		lp = lp->next;
	}

	lp->data = data;
	ptr->last = lp;

	return(ptr);
}

/* list: the beginning of the list
 * item: the item in the list to be removed
 *
 * returns:
 *     list with item removed
 */

PMList* list_remove(PMList* list, PMList* item)
{
	assert(check_list(list));

	if (list == NULL || item == NULL)
		return NULL;

	/* Remove first item in list. */
	if (item == list) {
		if (list->next == NULL) {            /* Only item in list. */
			list_free(item);
			return NULL;
		} else {
			list->next->prev = NULL;
			list->next->last = list->last;
			list = list->next;
			item->prev = item->next = NULL;
			list_free(item);
			return list;
		}
	}

	/* Remove last item in list. */
	if (list->last == item) {
		list->last = item->prev;
		item->prev->next = NULL;
		item->prev = item->next = NULL;
		list_free(item);
		return list;
	}

	/* Remove middle item in list. */
	assert(item->prev != NULL &&
	       item->next != NULL);

	item->prev->next = item->next;
	item->next->prev = item->prev;
	item->prev = item->next = NULL;
	list_free(item);

	assert(check_list(list));
	return list;
}

int list_count(PMList *list)
{
	int i;
	PMList *lp;

	for(lp = list, i = 0; lp; lp = lp->next, i++);
	return(i);
}

int list_isin(PMList *haystack, void *needle)
{
	PMList *lp;

	for(lp = haystack; lp; lp = lp->next) {
		if(lp->data == needle) {
			return(1);
		}
	}
	return(0);
}

/* Test for existence of a string in a PMList
 */
PMList* is_in(char *needle, PMList *haystack)
{
	PMList *lp;

	for(lp = haystack; lp; lp = lp->next) {
		if(lp->data && !strcmp(lp->data, needle)) {
			return(lp);
		}
	}
	return(NULL);
}

/* List one is extended and returned
 */
PMList* list_merge(PMList *one, PMList *two)
{
	PMList *lp, *ptr;

	if(two == NULL) {
		return one;
	}

	ptr = one;
	if(ptr == NULL) {
		ptr = list_new();
	}

	for(lp = two; lp; lp = lp->next) {
		if(lp->data) {
			ptr = list_add(ptr, lp->data);
			lp->data = NULL;
		}
	}

	return(ptr);
}

PMList* list_last(PMList *list)
{
	if (list == NULL)
		return NULL;

	assert(list->last != NULL);
	return list->last;
}

/* Helper function for sorting a list of strings
 */
int list_strcmp(const void *s1, const void *s2)
{
	char **str1 = (char **)s1;
	char **str2 = (char **)s2;

	return(strcmp(*str1, *str2));
}

/* Sort a list of strings.
 */
PMList *list_sort(PMList *list)
{
	char **arr = NULL;
	PMList *lp;
	unsigned int arrct;
	int i;

	if(list == NULL) {
		return(NULL);
	}

	arrct = list_count(list);
	arr = (char **)malloc(arrct*sizeof(char*));
	for(lp = list, i = 0; lp; lp = lp->next) {
		arr[i++] = (char *)lp->data;
	}

	qsort(arr, (size_t)arrct, sizeof(char *), list_strcmp);

	lp = NULL;
	for(i = 0; i < arrct; i++) {
		lp = list_add(lp, strdup(arr[i]));
	}

	if(arr) {
		free(arr);
		arr = NULL;
	}

	return(lp);
}

/* Filter out any duplicate strings in a list.
 *
 * Not the most efficient way, but simple to implement -- we assemble
 * a new list, using is_in() to check for dupes at each iteration.
 *
 */
PMList* list_remove_dupes(PMList *list)
{
	PMList *i, *newlist = NULL;

	for(i = list; i; i = i->next) {
		if(!is_in(i->data, newlist)) {
			newlist = list_add(newlist, strdup(i->data));
		}
	}
	return newlist;
}

/* Reverse the order of a list
 *
 * The caller is responsible for freeing the old list
 */
PMList* list_reverse(PMList *list)
{
	/* simple but functional -- we just build a new list, starting
	 * with the old list's tail
	 */
	PMList *newlist = NULL;
	PMList *lp;

	for(lp = list->last; lp; lp = lp->prev) {
		newlist = list_add(newlist, lp->data);
	}
	return(newlist);
}

void list_display(const char *title, PMList *list)
{
	PMList *lp;
	int cols, len, maxcols = 80;
	char *cenv = NULL;

	cenv = getenv("COLUMNS");
	if(cenv) {
		maxcols = atoi(cenv);
	}

	len = strlen(title);
	printf("%s ", title);

	if(list) {
		for(lp = list, cols = len; lp; lp = lp->next) {
			int s = strlen((char*)lp->data)+1;
			if(s+cols >= maxcols) {
				int i;
				cols = len;
				printf("\n");
				for (i = 0; i < len+1; i++) {
					printf(" ");
				}
			}
			printf("%s ", (char*)lp->data);
			cols += s;
		}
		printf("\n");
	} else {
		printf("None\n");
	}
}


/* Helper function for comparing string nodes.
 */
int strlist_cmp(const void *s1, const void *s2)
{
	char *str1 = (char *)s1;
	char *str2 = (char *)s2;

	return(strcmp(str1, str2));
}


/*  Add items to a list in sorted order.  Use the given
 *  comparision func to determine order.
 */
PMList* list_add_sorted(PMList *list, void *data, cmp_fn sortfunc)
{
	PMList *add;
	PMList *prev = NULL;
	PMList *iter = list;

	add = list_new();
	add->data = data;

	/*  Find insertion point.  */
	while(iter) {
		if(sortfunc(add->data, iter->data) <= 0) break;
		prev = iter;
		iter = iter->next;
	}

	/*  Insert node before insertion point.  */
	add->prev = prev;
	add->next = iter;

	if(iter != NULL) {
		iter->prev = add;		/*  Not at end.  */
	} else {
		if (list != NULL)
			list->last = add;   /* Added new to end, so update the link to last. */
	}

	if(prev != NULL) {
		prev->next = add;				/*  In middle.  */
	} else {
		if (list == NULL) {
			add->last = add;
		} else {
			add->last = list->last;
			list->last = NULL;
		}
		list = add;						/*  Start or empty, new list head.  */
	}

	return(list);
}

/* vim: set ts=2 sw=2 noet: */
