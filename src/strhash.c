/* evtgen string hash functions.
   Copyright (C) 2003 Julien Olivain and LSV, CNRS UMR 8643 & ENS Cachan.

   This file is part of evtgen.

   evtgen is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   evtgen is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with evtgen; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/* Copyright (C) 2004 Tommi Rantala <tommi.rantala@cs.helsinki.fi>
 *
 * Modified for usage in Pacman.
 */

/*
** strhash.c -- string hash utility functions
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "util.h"
#include "list.h"
#include "strhash.h"

void
strhash_add_list(strhash_t *hash, PMList* list)
{
	for(; list; list = list->next)
		strhash_add(hash, list->data, NULL);
}

strhash_t *
new_strhash(size_t hsize)
{
  strhash_t *h;

  MALLOC(h, sizeof (strhash_t));
  h->size = hsize;
  h->elmts = 0;
  h->hash = DEFAULT_STRHASH_FUNCTION;
  MALLOC(h->htable, hsize * sizeof (strhash_elmt_t *));
  memset(h->htable, 0, hsize * sizeof(strhash_elmt_t *));

  return (h);
}

void
clear_strhash(strhash_t *hash)
{
  int i;
  strhash_elmt_t *tmp;
  strhash_elmt_t *tmp_next;

  for (i = 0; i < hash->size; i++)
    {
      tmp = hash->htable[i];
      while (tmp)
        {
          tmp_next = tmp->next;
          free(tmp);
          tmp = tmp_next;
        }
    }
  hash->elmts = 0;
  memset(hash->htable, 0, hash->size * sizeof (void *));
}


void
free_strhash(strhash_t *hash)
{
  int i;
  strhash_elmt_t *tmp;
  strhash_elmt_t *tmp_next;

  for (i = 0; i < hash->size; i++)
    {
      tmp = hash->htable[i];
      while (tmp)
        {
          tmp_next = tmp->next;
          free(tmp);
          tmp = tmp_next;
        }
    }

  free(hash->htable);
  free(hash);
}

void
strhash_add(strhash_t *hash, char *key, char *data)
{
  strhash_elmt_t *elmt;
  unsigned long hcode;

  MALLOC(elmt, sizeof (strhash_elmt_t));
  elmt->key = key;
  elmt->data = data;

  hcode = hash->hash(key) % hash->size;
  elmt->next = hash->htable[hcode];
  hash->htable[hcode] = elmt;
  hash->elmts++;
}

/* 1: Yes, the key exists in the hash table.
 * 0: No, its not here.
 */

int
strhash_isin(strhash_t *hash, char* key)
{
  strhash_elmt_t *elmt;

  elmt = hash->htable[hash->hash(key) % hash->size];
  for (; elmt; elmt = elmt->next)
    {
      if (!strcmp(key, elmt->key))
        return 1;
    }

  return 0;
}

char*
strhash_get(strhash_t *hash, char* key)
{
  strhash_elmt_t *elmt;

  elmt = hash->htable[hash->hash(key) % hash->size];
  for (; elmt; elmt = elmt->next)
    {
      if (!strcmp(key, elmt->key))
        return elmt->data;
    }

  return NULL;
}

/*
** fast hash function samples
*/

unsigned long
strhash_pjw(char *key)
{
  unsigned long h;
  unsigned long g;

  h = 0;
  while (*key)
    {
      h = (h << 4) + *key++;
      if ((g = h & 0xF0000000U) != 0)
        {
          h = h ^ (g >> 24);
          h = h ^ g;
        }
    }

  return (h);
}

int
strhash_collide_count(strhash_t *hash)
{
  int count;
  int i;

  count = 0;
  for (i = 0; i < hash->size; i++)
    {
      strhash_elmt_t *tmp;

      for (tmp = hash->htable[i]; tmp; tmp = tmp->next)
        if (tmp->next)
          count++;
    }

  return (count);
}
