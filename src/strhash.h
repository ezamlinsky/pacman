/* evtgen string hash headers.
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
** strhash.h --
*/

#ifndef STRHASH_H
#define STRHASH_H

#define DEFAULT_STRHASH_FUNCTION strhash_pjw

typedef struct strhash_elmt_s strhash_elmt_t;
struct strhash_elmt_s
{
  char *key;
  char *data;
  strhash_elmt_t *next;
};

typedef unsigned long (*strhashfunc_t)(char *key);

typedef struct strhash_s strhash_t;
struct strhash_s
{
  strhash_elmt_t **htable;
  size_t size;
  int elmts;
  strhashfunc_t hash;
};

void strhash_add_list(strhash_t *hash, PMList* list);
strhash_t *new_strhash(size_t hsize);
void free_strhash(strhash_t *hash);
void clear_strhash(strhash_t *hash);
void strhash_add(strhash_t *hash, char *key, char *data);
int strhash_isin(strhash_t *hash, char* key);
char* strhash_get(strhash_t *hash, char* key);
int strhash_collide_count(strhash_t *hash);

unsigned long strhash_pjw(char *key);

#endif /* STRHASH_H */
