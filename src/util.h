/*
 *  util.h
 * 
 *  Copyright (c) 2002-2005 by Judd Vinet <jvinet@zeroflux.org>
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
#ifndef _PAC_UTIL_H
#define _PAC_UTIL_H

#define MALLOC(p, b) { if((b) > 0) { \
                       p = malloc(b); if (!(p)) { \
                       fprintf(stderr, "malloc failure: could not allocate %d bytes\n", b); \
                       exit(1); }} else p = NULL; }

#define FREE(p)      { if (p) { free(p); (p)= NULL; }}

void vprint(char *fmt, ...);
long gzopen_frontend(char *pathname, int oflags, int mode);
int unpack(char *archive, const char *prefix, const char *fn);
int copyfile(char *src, char *dest);
int makepath(char *path);
int rmrf(char *path);
void indentprint(char *str, int indent);
char* trim(char *str);
char* strtoupper(char *str);
int grep(const char *fn, const char *needle);

#endif
/* vim: set ts=2 sw=2 noet: */
