/*
 *  db.c
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
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include "package.h"
#include "util.h"
#include "db.h"

/* Open a database and return a pacdb_t handle */
pacdb_t* db_open(char *root, char *pkgdir, char *treename)
{
	pacdb_t *db = NULL;

	MALLOC(db, sizeof(pacdb_t));
	MALLOC(db->path, strlen(root)+strlen(pkgdir)+strlen(treename)+2);
	sprintf(db->path, "%s%s/%s", root, pkgdir, treename);
	db->dir = opendir(db->path);
	if(db->dir == NULL) {
		return(NULL);
	}
	strncpy(db->treename, treename, 128);
	
	return(db);
}

void db_close(pacdb_t* db)
{
	if(db) {
		if(db->dir) {
			closedir(db->dir);
		}
		FREE(db->path);
	}
	FREE(db);

	return;
}

/* frees pkgcache if necessary and returns a new package
 * cache from db 
 */
PMList* db_loadpkgs(pacdb_t *db)
{
	pkginfo_t *info;
	pkginfo_t **arr = NULL;
	unsigned int arrct = 0;
	int i;
	PMList *cache = NULL;

	rewinddir(db->dir);
	while((info = db_scan(db, NULL, INFRQ_DESC)) != NULL) {
		/* add to the collective */
		/* we load all package names into a linear array first, so qsort can handle it */
		if(arr == NULL) {
			arr = (pkginfo_t**)malloc(sizeof(pkginfo_t*));
			arrct++;
		} else {
			arr = (pkginfo_t**)realloc(arr, (++arrct)*sizeof(pkginfo_t*));
		}
		if(arr == NULL) {
			fprintf(stderr, "error: out of memory\n");
			exit(1);
		}
		arr[arrct-1] = info;
	}

	/* sort the package list */
	qsort(arr, (size_t)arrct, sizeof(pkginfo_t*), pkgcmp);

	/* now load them into the proper PMList */
	for(i = 0; i < arrct; i++) {
		cache = list_add(cache, arr[i]);
	}

	FREE(arr);

	return(cache);
}

pkginfo_t* db_scan(pacdb_t *db, char *target, unsigned int inforeq)
{
	struct dirent *ent = NULL;
	char name[256];
	char *ptr = NULL;
	int found = 0;

	if(target != NULL) {
		/* search for a specific package (by name only) */
		rewinddir(db->dir);
		/* read the . and .. first */
		ent = readdir(db->dir);
		ent = readdir(db->dir);

		ent = readdir(db->dir);
		while(!found && ent != NULL) {
			strncpy(name, ent->d_name, 255);
			/* truncate the string at the second-to-last hyphen, */
			/* which will give us the package name */
			if((ptr = rindex(name, '-'))) {
				*ptr = '\0';
			}
			if((ptr = rindex(name, '-'))) {
				*ptr = '\0';
			}
			if(!strcmp(name, target)) {
				found = 1;
				continue;
			}
			ent = readdir(db->dir);
		}
		if(!found) {
			return(NULL);
		}
	} else {
		/* normal iteration */
		ent = readdir(db->dir);
		if(ent && !strcmp(ent->d_name, ".")) {
			ent = readdir(db->dir);
		}
		if(ent && !strcmp(ent->d_name, "..")) {
			ent = readdir(db->dir);
		}
		if(ent == NULL) {
			return(NULL);
		}
	}
	return(db_read(db, ent, inforeq));
}

pkginfo_t* db_read(pacdb_t *db, struct dirent *ent, unsigned int inforeq)
{
	FILE *fp = NULL;
	struct stat buf;
	pkginfo_t *info = NULL;
	char path[PATH_MAX];
	char line[512];

	if(ent == NULL) {
		return(NULL);
	}

	/* we always load DESC */
	inforeq |= INFRQ_DESC;

	snprintf(path, PATH_MAX, "%s/%s", db->path, ent->d_name);
	if(stat(path, &buf)) {
		/* directory doesn't exist or can't be opened */
		return(NULL);
	}

	info = newpkg();

	/* DESC */
	if(inforeq & INFRQ_DESC) {
		snprintf(path, PATH_MAX, "%s/%s/desc", db->path, ent->d_name);
		fp = fopen(path, "r");
		if(fp == NULL) {
			fprintf(stderr, "error: %s: %s\n", path, strerror(errno));
			FREEPKG(info);
			return(NULL);
		}
		while(!feof(fp)) {
			if(fgets(line, 256, fp) == NULL) {
				break;
			}
			trim(line);
			if(!strcmp(line, "%NAME%")) {
				if(fgets(info->name, sizeof(info->name), fp) == NULL) {
					FREEPKG(info);
					return(NULL);
				}
				trim(info->name);
			} else if(!strcmp(line, "%VERSION%")) {
				if(fgets(info->version, sizeof(info->version), fp) == NULL) {
					FREEPKG(info);
					return(NULL);
				}
				trim(info->version);
			} else if(!strcmp(line, "%DESC%")) {
				if(fgets(info->desc, sizeof(info->desc), fp) == NULL) {
					FREEPKG(info);
					return(NULL);
				}
				trim(info->desc);
			} else if(!strcmp(line, "%GROUPS%")) {
				while(fgets(line, 512, fp) && strlen(trim(line))) {
					char *s = strdup(line);
					info->groups = list_add(info->groups, s);
				}
			} else if(!strcmp(line, "%URL%")) {
				if(fgets(info->url, sizeof(info->url), fp) == NULL) {
					FREEPKG(info);
					return(NULL);
				}
				trim(info->url);
			} else if(!strcmp(line, "%BUILDDATE%")) {
				if(fgets(info->builddate, sizeof(info->builddate), fp) == NULL) {
					FREEPKG(info);
					return(NULL);
				}
				trim(info->builddate);
			} else if(!strcmp(line, "%INSTALLDATE%")) {
				if(fgets(info->installdate, sizeof(info->installdate), fp) == NULL) {
					FREEPKG(info);
					return(NULL);
				}
				trim(info->installdate);
			} else if(!strcmp(line, "%PACKAGER%")) {
				if(fgets(info->packager, sizeof(info->packager), fp) == NULL) {
					FREEPKG(info);
					return(NULL);
				}
				trim(info->packager);
			} else if(!strcmp(line, "%SIZE%")) {
				char tmp[32];
				if(fgets(tmp, sizeof(tmp), fp) == NULL) {
					FREEPKG(info);
					return(NULL);
				}
				trim(tmp);
				info->size = atol(tmp);
			} else if(!strcmp(line, "%REPLACES%")) {
				/* the REPLACES tag is special -- it only appears in sync repositories,
				 * not the local one.
				 */
				while(fgets(line, 512, fp) && strlen(trim(line))) {
					char *s = strdup(line);
					info->replaces = list_add(info->replaces, s);
				}
			} else if(!strcmp(line, "%MD5SUM%")) {
				/* MD5SUM tag only appears in sync repositories,
				 * not the local one.
				 */
				if(fgets(info->md5sum, sizeof(info->md5sum), fp) == NULL) {
					FREEPKG(info);
					return(NULL);
				}
			}
		}
		fclose(fp);
	}

	/* FILES */
	if(inforeq & INFRQ_FILES) {
		snprintf(path, PATH_MAX, "%s/%s/files", db->path, ent->d_name);
		fp = fopen(path, "r");
		if(fp == NULL) {
			fprintf(stderr, "error: %s: %s\n", path, strerror(errno));
			FREEPKG(info);
			return(NULL);
		}
		while(fgets(line, 256, fp)) {
			trim(line);
			if(!strcmp(line, "%FILES%")) {
				while(fgets(line, 512, fp) && strlen(trim(line))) {
					char *s = strdup(line);
					info->files = list_add(info->files, s);
				}
			}
			if(!strcmp(line, "%BACKUP%")) {
				while(fgets(line, 512, fp) && strlen(trim(line))) {
					char *s = strdup(line);
					info->backup = list_add(info->backup, s);
				}
			}
		}
		fclose(fp);
	}

	/* DEPENDS */
	if(inforeq & INFRQ_DEPENDS) {
		snprintf(path, PATH_MAX, "%s/%s/depends", db->path, ent->d_name);
		fp = fopen(path, "r");
		if(fp == NULL) {
			fprintf(stderr, "db_read: error: %s: %s\n", path, strerror(errno));
			FREEPKG(info);
			return(NULL);
		}
		while(!feof(fp)) {
			fgets(line, 255, fp);
			trim(line);
			if(!strcmp(line, "%DEPENDS%")) {
				while(fgets(line, 512, fp) && strlen(trim(line))) {
					char *s = strdup(line);
					info->depends = list_add(info->depends, s);
				}
			}
			if(!strcmp(line, "%REQUIREDBY%")) {
				while(fgets(line, 512, fp) && strlen(trim(line))) {
					char *s = strdup(line);
					info->requiredby = list_add(info->requiredby, s);
				}
			}
			if(!strcmp(line, "%CONFLICTS%")) {
				while(fgets(line, 512, fp) && strlen(trim(line))) {
					char *s = strdup(line);
					info->conflicts = list_add(info->conflicts, s);
				}
			}
			if(!strcmp(line, "%PROVIDES%")) {
				while(fgets(line, 512, fp) && strlen(trim(line))) {
					char *s = strdup(line);
					info->provides = list_add(info->provides, s);
				}
			}
		}
		fclose(fp);
	}

	/* INSTALL */
	snprintf(path, PATH_MAX, "%s/%s/install", db->path, ent->d_name);
	if(!stat(path, &buf)) {
		info->scriptlet = 1;
	}

	return(info);
}

int db_write(pacdb_t *db, pkginfo_t *info)
{
	char topdir[PATH_MAX];
	FILE *fp = NULL;
	char path[PATH_MAX];
	mode_t oldmask;
	PMList *lp = NULL;

	if(info == NULL) {
		return(1);
	}

	snprintf(topdir, PATH_MAX, "%s/%s-%s", db->path,
		info->name, info->version);
	oldmask = umask(0000);
	mkdir(topdir, 0755);
	umask(oldmask);

	/* DESC */
	snprintf(path, PATH_MAX, "%s/desc", topdir);
	fp = fopen(path, "w");
	if(fp == NULL) {
		perror("db_write");
		return(1);
	}
	fputs("%NAME%\n", fp);
	fprintf(fp, "%s\n\n", info->name);
	fputs("%VERSION%\n", fp);
	fprintf(fp, "%s\n\n", info->version);
	fputs("%DESC%\n", fp);
	fprintf(fp, "%s\n\n", info->desc);
	fputs("%GROUPS%\n", fp);
	for(lp = info->groups; lp; lp = lp->next) {
		fprintf(fp, "%s\n", (char*)lp->data);
	}
	fprintf(fp, "\n");
	fputs("%URL%\n", fp);
	fprintf(fp, "%s\n\n", info->url);
	fputs("%BUILDDATE%\n", fp);
	fprintf(fp, "%s\n\n", info->builddate);
	fputs("%INSTALLDATE%\n", fp);
	fprintf(fp, "%s\n\n", info->installdate);
	fputs("%PACKAGER%\n", fp);
	fprintf(fp, "%s\n\n", info->packager);
	fputs("%SIZE%\n", fp);
	fprintf(fp, "%ld\n\n", info->size);
	fclose(fp);

	/* FILES */
	snprintf(path, PATH_MAX, "%s/files", topdir);
	fp = fopen(path, "w");
	if(fp == NULL) {
		perror("db_write");
		return(1);
	}
	fputs("%FILES%\n", fp);
	for(lp = info->files; lp; lp = lp->next) {
		fprintf(fp, "%s\n", (char*)lp->data);
	}
	fprintf(fp, "\n");
	fputs("%BACKUP%\n", fp);
	for(lp = info->backup; lp; lp = lp->next) {
		fprintf(fp, "%s\n", (char*)lp->data);
	}
	fprintf(fp, "\n");
	fclose(fp);

	/* DEPENDS */
	snprintf(path, PATH_MAX, "%s/depends", topdir);
	fp = fopen(path, "w");
	if(fp == NULL) {
		perror("db_write");
		return(1);
	}
	fputs("%DEPENDS%\n", fp);
	for(lp = info->depends; lp; lp = lp->next) {
		fprintf(fp, "%s\n", (char*)lp->data);
	}
	fprintf(fp, "\n");
	fputs("%REQUIREDBY%\n", fp);
	for(lp = info->requiredby; lp; lp = lp->next) {
		fprintf(fp, "%s\n", (char*)lp->data);
	}
	fprintf(fp, "\n");
	fputs("%CONFLICTS%\n", fp);
	for(lp = info->conflicts; lp; lp = lp->next) {
		fprintf(fp, "%s\n", (char*)lp->data);
	}
	fprintf(fp, "\n");
	fputs("%PROVIDES%\n", fp);
	for(lp = info->provides; lp; lp = lp->next) {
		fprintf(fp, "%s\n", (char*)lp->data);
	}
	fprintf(fp, "\n");
	fclose(fp);

	/* INSTALL */
	/* nothing needed here (script is automatically extracted) */

	return(0);
}


PMList* db_find_conflicts(pacdb_t *db, PMList *targets, char *root)
{
	PMList *i, *j, *k;
	char *filestr = NULL;
	char path[PATH_MAX+1];
	char *str = NULL;
	struct stat buf;
	PMList *conflicts = NULL;

	/* CHECK 1: check every db package against every target package */
	/* XXX: I've disabled the database-against-targets check for now, as the
	 *      many many strcmp() calls slow it down heavily and most of the
	 *      checking is redundant to the targets-against-filesystem check.
	 *      This will be re-enabled if I can improve performance significantly.
	 *
	pkginfo_t *info = NULL;
	char *dbstr   = NULL;
	rewinddir(db->dir);
	while((info = db_scan(db, NULL, INFRQ_DESC | INFRQ_FILES)) != NULL) {
		for(i = info->files; i; i = i->next) {
			if(i->data == NULL) continue;
			dbstr = (char*)i->data;
			for(j = targets; j; j = j->next) {
				pkginfo_t *targ = (pkginfo_t*)j->data;
				if(strcmp(info->name, targ->name)) {
					for(k = targ->files; k; k = k->next) {
						filestr = (char*)k->data;
						if(!strcmp(dbstr, filestr)) {
							if(rindex(k->data, '/') == filestr+strlen(filestr)-1) {
								continue;
							}
							MALLOC(str, 512);
							snprintf(str, 512, "%s: exists in \"%s\" (target) and \"%s\" (installed)", dbstr,
								targ->name, info->name);
							conflicts = list_add(conflicts, str);
						}
					}
				}
			}
		}
	}*/

	/* CHECK 2: check every target against every target */
	for(i = targets; i; i = i->next) {
		pkginfo_t *p1 = (pkginfo_t*)i->data;
		for(j = i; j; j = j->next) {
			pkginfo_t *p2 = (pkginfo_t*)j->data;
			if(strcmp(p1->name, p2->name)) {
				for(k = p1->files; k; k = k->next) {
					filestr = k->data;
					if(!strcmp(filestr, "._install") || !strcmp(filestr, ".INSTALL")) {
						continue;
					}
					if(rindex(filestr, '/') == filestr+strlen(filestr)-1) {
						/* this filename has a trailing '/', so it's a directory -- skip it. */
						continue;
					}
					if(is_in(filestr, p2->files)) {
						MALLOC(str, 512);
						snprintf(str, 512, "%s: exists in \"%s\" (target) and \"%s\" (target)",
							filestr, p1->name, p2->name);
						conflicts = list_add(conflicts, str);
					}
				}
			}
		}
	}

	/* CHECK 3: check every target against the filesystem */
	for(i = targets; i; i = i->next) {
		pkginfo_t *p = (pkginfo_t*)i->data;
		pkginfo_t *dbpkg = NULL;
		for(j = p->files; j; j = j->next) {
			filestr = (char*)j->data;
			snprintf(path, PATH_MAX, "%s%s", root, filestr);
			if(!stat(path, &buf) && !S_ISDIR(buf.st_mode)) {
				int ok = 0;
				if(dbpkg == NULL) {
					dbpkg = db_scan(db, p->name, INFRQ_DESC | INFRQ_FILES);
				}
				if(dbpkg && is_in(j->data, dbpkg->files)) {
					ok = 1;
				}
				if(!ok) {
					MALLOC(str, 512);
					snprintf(str, 512, "%s: exists in filesystem", path);
					conflicts = list_add(conflicts, str);
				}
			}
		}
		FREEPKG(dbpkg);
	}

	return(conflicts);
}

/* return a PMList of packages in "db" that provide "package" */
PMList *whatprovides(pacdb_t *db, char* package)
{
	PMList *pkgs, *i = NULL;
	pkginfo_t *info;

	rewinddir(db->dir);
	while((info = db_scan(db, NULL, INFRQ_DESC | INFRQ_DEPENDS)) != NULL) {
		if(is_in(package, info->provides)) {
			i = list_add(i, strdup(info->name));
		}
		FREEPKG(info);
	}
	pkgs = list_sort(i);
	FREELIST(i);

	return(pkgs);
}

/*
 * return a list of all groups present in *db
 * 
 */
PMList *find_groups(pacdb_t *db)
{
	PMList *groups, *i = NULL;
	PMList *lp = NULL;
	pkginfo_t *info;

	rewinddir(db->dir);
	while((info = db_scan(db, NULL, INFRQ_DESC)) != NULL) {
		for(lp = info->groups; lp; lp = lp->next) {
			if(!is_in((char*)lp->data, i)) {
				i = list_add(i, strdup((char*)lp->data));
			}
		}
		FREEPKG(info);
	}
	groups = list_sort(i);
	FREELIST(i);

	return(groups);
}

/*
 * return a list of all members of the specified group
 *
 */
PMList *pkg_ingroup(pacdb_t *db, char *group)
{
	PMList *pkg, *i = NULL;
	PMList *lp = NULL;
	pkginfo_t *info;

	rewinddir(db->dir);
	while((info = db_scan(db, NULL, INFRQ_DESC)) != NULL) {
		for(lp = info->groups; lp; lp = lp->next) {
			if(!strcmp((char*)lp->data, group)) {
				i = list_add(i, strdup(info->name));
			}
		}
		FREEPKG(info);
	}
	pkg = list_sort(i);
	FREELIST(i);

	return(pkg);
}

/* vim: set ts=2 sw=2 noet: */
