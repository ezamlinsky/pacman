#ifndef _PAC_LIST_H
#define _PAC_LIST_H

/* your average linked list */
typedef struct __pmlist_t {
	void*   data;
	struct __pmlist_t* prev;
	struct __pmlist_t* next;
} PMList;

PMList* list_new();
void list_free(PMList* list);
PMList* list_add(PMList* list, void* data);
int list_count(PMList* list);
int list_isin(PMList *haystack, void *needle);
PMList* list_merge(PMList *one, PMList *two);
PMList* list_last(PMList* list);

#endif

/* vim: set ts=2 sw=2 noet: */
