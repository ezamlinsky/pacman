/*
 *  package.h
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
#ifndef _PAC_PACKAGE_H
#define _PAC_PACKAGE_H

#include "list.h"

#define FREEPKG(p) { freepkg(p); p = NULL; }

#define FREELISTPKGS(p) {\
	PMList *i;\
	for(i = p; i; i = i->next) {\
		FREEPKG(i->data);\
	}\
	FREELIST(p);\
}

/* reasons -- ie, why the package was installed */
#define REASON_EXPLICIT  0  /* explicitly requested by the user              */
#define REASON_DEPEND    1  /* installed as a dependency for another package */

/* mods for depend_t.mod */
#define DEP_ANY 0
#define DEP_EQ	1
#define DEP_GE	2
#define DEP_LE	3

/* Package Structures */
typedef char** fileset_t;
typedef struct __pkginfo_t {
	char name[256];
	char version[64];
	char desc[512];
	char url[256];
	char license[128];
	char builddate[32];
	char installdate[32];
	char packager[64];
	char md5sum[33];
	char arch[32];
	unsigned long size;
	unsigned short scriptlet;
	unsigned short force;
	unsigned short reason;
	PMList *replaces;
	PMList *groups;
	PMList *files;
	PMList *backup;
	PMList *depends;
	PMList *requiredby;
	PMList *conflicts;
	PMList *provides;
	/* if the package has an associated filename on the local system
	 * (eg, filename.pkg.tar.gz) then it will be stored here, otherwise NULL
	 */
	char *filename;
} pkginfo_t;

typedef struct __depend_t {
	unsigned short mod;
	char name[256];
	char version[64];
} depend_t;

typedef struct __depmissing_t {
	enum {DEPEND, REQUIRED, CONFLICT} type;
	char target[256];
	depend_t depend;
} depmissing_t;

pkginfo_t* load_pkg(char *pkgfile);
int parse_descfile(char *descfile, pkginfo_t *info, PMList **backup, int output);
pkginfo_t* newpkg();
void freepkg(pkginfo_t *pkg);
int pkgcmp(const void *p1, const void *p2);
int is_pkgin(pkginfo_t *needle, PMList *haystack);
void dump_pkg_full(pkginfo_t *info);
void dump_pkg_sync(pkginfo_t *info, char *treename);
int split_pkgname(char *pkgfile, char *name, char *version);

#endif
/* vim: set ts=2 sw=2 noet: */
