/*
 *  pacman
 * 
 *  Copyright (c) 2002 by Judd Vinet <jvinet@zeroflux.org>
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
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "list.h"

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
	return(list);
}

void list_free(PMList *list)
{
	if(list == NULL) {
		return;
	}
	if(list->data != NULL) {
		free(list->data);
		list->data = NULL;
	}
	if(list->next != NULL) {
		list_free(list->next);
	}
	free(list);
	return;
}

PMList* list_add(PMList *list, void *data)
{
	PMList *ptr, *lp;

	ptr = list;
	if(ptr == NULL) {
		ptr = list_new();
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
		lp = lp->next;
	}
	lp->data = data;
	return(ptr);
}

int list_count(PMList *list)
{
	int i;
	PMList *lp;

	for(lp = list, i = 0; lp; lp = lp->next, i++);
	return(i);
}

/* List one is extended and returned
 * List two is freed (but not its data)
 */
PMList* list_merge(PMList *one, PMList *two)
{
	PMList *lp;

	for(lp = two; lp; lp = lp->next) {
		if(lp->data) {
			list_add(one, lp->data);
			lp->data = NULL;
		}
	}
	list_free(two);

	return(one);
}

PMList* list_last(PMList *list)
{
	PMList *ptr;

	for(ptr = list; ptr && ptr->next; ptr = ptr->next);
	return(ptr);
}

/* vim: set ts=2 sw=2 noet: */
