/*
 *  rpmvercmp.c
 * 
 *  Copyright (c) 2002-2004 by Judd Vinet <jvinet@zeroflux.org>
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

#include <stdio.h>
#include <ctype.h>
#include <string.h>

/* this function was taken from rpm 4.0.4 and rewritten */

int rpmvercmp(const char *a, const char *b)
{
	char *str1, *str2;
	char *one, *two;
	char *rel1 = NULL, *rel2 = NULL;
	char oldch1, oldch2;
	int is1num, is2num;
	int rc;

	if(!strcmp(a,b)) {
		return(0);
	}

	str1 = strdup(a);
	str2 = strdup(b);

	/* lose the release number */
	for(one = str1; *one && *one != '-'; one++);
	if(one) {
		*one = '\0';
		rel1 = ++one;
	}
	for(two = str2; *two && *two != '-'; two++);
	if(two) {
		*two = '\0';
		rel2 = ++two;
	}

	one = str1;
	two = str2;

	while(*one || *two) {
		while(*one && !isalnum(*one)) one++;
		while(*two && !isalnum(*two)) two++;

		str1 = one;
		str2 = two;

		/* find the next segment for each string */
		if(isdigit(*str1)) {
			is1num = 1;
			while(*str1 && isdigit(*str1)) str1++;
		} else {
			is1num = 0;
			while(*str1 && isalpha(*str1)) str1++;
		}
		if(isdigit(*str2)) {
			is2num = 1;
			while(*str2 && isdigit(*str2)) str2++;
		} else {
			is2num = 0;
			while(*str2 && isalpha(*str2)) str2++;
		}

		oldch1 = *str1;
		*str1 = '\0';
		oldch2 = *str2;
		*str2 = '\0';

		/* see if we ran out of segments on one string */
		if(one == str1 && two != str2) {
			return(is2num ? -1 : 1);
		}
		if(one != str1 && two == str2) {
			return(is1num ? 1 : -1);
		}

		/* see if we have a type mismatch (ie, one is alpha and one is digits) */
		if(is1num && !is2num) return(1);
		if(!is1num && is2num) return(-1);

		if(is1num) while(*one == '0') one++;
		if(is2num) while(*two == '0') two++;

		rc = strverscmp(one, two);
		if(rc) return(rc);

		*str1 = oldch1;
		*str2 = oldch2;
		one = str1;
		two = str2;
	}

	if((!*one) && (!*two)) {
		/* compare release numbers */
		if(rel1 && rel2) return(rpmvercmp(rel1, rel2));
		return(0);
	}

	return(*one ? 1 : -1);
}

/* vim: set ts=2 sw=2 noet: */
