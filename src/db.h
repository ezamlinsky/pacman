/*
 *  db.h
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
#ifndef _PAC_DB_H
#define _PAC_DB_H

#include <dirent.h>

/* info requests for db_read */
#define INFRQ_DESC     0x01
#define INFRQ_DEPENDS  0x02
#define INFRQ_FILES    0x04
#define INFRQ_ALL      0xFF

typedef struct __pacdb_t {
	char *path;
	char treename[128];
	DIR* dir;
} pacdb_t;

pacdb_t* db_open(char *root, char *dbpath, char *treename);
void db_close(pacdb_t *db);
int db_getlastupdate(const char *dbpath, char *ts);
int db_setlastupdate(const char *dbpath, const char *ts);
PMList* db_loadpkgs(pacdb_t *db);
pkginfo_t* db_scan(pacdb_t *db, char *target, unsigned int inforeq);
pkginfo_t* db_read(pacdb_t *db, struct dirent *ent, unsigned int inforeq);
int db_write(pacdb_t *db, pkginfo_t *info, unsigned int inforeq);
void db_search(pacdb_t *db, PMList *cache, const char *treename, PMList *needles);
PMList* db_find_conflicts(pacdb_t *db, PMList* targets, char *root);
PMList *whatprovides(pacdb_t *db, char* package);
PMList *find_groups(pacdb_t *db);
PMList *pkg_ingroup(pacdb_t *db, char *group);

#endif
/* vim: set ts=2 sw=2 noet: */
