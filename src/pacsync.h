/*
 *  pacsync.h
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
#ifndef _PAC_PACSYNC_H
#define _PAC_PACSYNC_H

/* Servers */
typedef struct __server_t {
	char* protocol;
	char* server;
	char* path;
} server_t;

/* Repositories */
typedef struct __sync_t {
	char* treename;
	char* lastupdate;
	PMList *servers;
} sync_t;

/* linking structs */
typedef struct __dbsync_t {
	pacdb_t *db;
	sync_t *sync;
	PMList *pkgcache;
} dbsync_t;

typedef struct __syncpkg_t {
	pkginfo_t *pkg;
	dbsync_t *dbs;
	PMList *replaces;
} syncpkg_t;

int sync_synctree();

int downloadfiles(PMList *servers, const char *localpath, PMList *files);
int downloadfiles_forreal(PMList *servers, const char *localpath,
		PMList *files, const char *mtime1, char *mtime2);

syncpkg_t* find_pkginsync(char *needle, PMList *haystack);
PMList* rm_pkginsync(char *needle, PMList *haystack);

#endif

/* vim: set ts=2 sw=2 noet: */
