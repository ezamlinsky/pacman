/*
 *  db.c
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

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <libgen.h>
#include <unistd.h>
#include "package.h"
#include "strhash.h"
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
	strncpy(db->treename, treename, sizeof(db->treename)-1);
	
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

/* reads dbpath/.lastupdate and populates *ts with the contents.
 * *ts should be malloc'ed and should be at least 15 bytes.
 *
 * Returns 0 on success, 1 on error
 *
 */
int db_getlastupdate(const char *dbpath, char *ts)
{
	FILE *fp;
	char path[PATH_MAX];
	/* get the last update time, if it's there */
	snprintf(path, PATH_MAX, "%s/.lastupdate", dbpath);
	if((fp = fopen(path, "r")) == NULL) {
		return(1);
	} else {
		char line[256];
		if(fgets(line, sizeof(line), fp)) {
			strncpy(ts, line, 15); /* YYYYMMDDHHMMSS */
			ts[14] = '\0';
		} else {
			fclose(fp);
			return(1);
		}
	}
	fclose(fp);
	return(0);
}

/* writes the db->path/.lastupdate with the contents of *ts
 */
int db_setlastupdate(const char *dbpath, const char *ts)
{
	FILE *fp;
	char path[PATH_MAX];

	snprintf(path, PATH_MAX, "%s/.lastupdate", dbpath);
	if((fp = fopen(path, "w")) == NULL) {
		return(1);
	}
	if(fputs(ts, fp) <= 0) {
		fclose(fp);
		return(1);
	}
	fclose(fp);
	return(0);
}

/* frees pkgcache if necessary and returns a new package
 * cache from db 
 */
PMList* db_loadpkgs(pacdb_t *db)
{
	pkginfo_t *info;
	PMList *cache = NULL;

	rewinddir(db->dir);
	while((info = db_scan(db, NULL, INFRQ_DESC | INFRQ_DEPENDS)) != NULL) {
		/* add to the collective */
		cache = list_add_sorted(cache, info, pkgcmp);
	}

	return(cache);
}

pkginfo_t* db_scan(pacdb_t *db, char *target, unsigned int inforeq)
{
	struct dirent *ent = NULL;
	struct stat sbuf;
	char path[PATH_MAX];
	int path_len = 0;
	char *name = NULL;
	char *ptr = NULL;
	int found = 0;

	/* hash table for caching directory names */
	static strhash_t* htable = NULL;
	
	if (!htable)
		htable = new_strhash(951);

	snprintf(path, PATH_MAX, "%s/", db->path);
	path_len = strlen(path);

	if(target != NULL) {
		/* search for a specific package (by name only) */

		/* See if we have the path cached. */
		strcat(path, target);
		if (strhash_isin(htable, path)) {
			struct dirent* pkgdir;
			pkginfo_t* pkg;

			/* db_read() wants 'struct dirent' so lets give it one.
			 * Actually it only uses the d_name field. */

			MALLOC(pkgdir, sizeof(struct dirent));
			strcpy(pkgdir->d_name, strhash_get(htable, path));

			pkg = db_read(db, pkgdir, inforeq);
			FREE(pkgdir);
			return pkg;
		}
		path[path_len] = '\0';

		/* OK the entry was not in cache, so lets look for it manually. */
			
		rewinddir(db->dir);
		ent = readdir(db->dir);

		while(!found && ent != NULL) {
			if(!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) {
				ent = readdir(db->dir);
				continue;
			}

			/* stat the entry, make sure it's a directory */
			path[path_len] = '\0';
			strncat(path, ent->d_name, PATH_MAX - path_len);

			if(stat(path, &sbuf) || !S_ISDIR(sbuf.st_mode)) {
				ent = readdir(db->dir);
				continue;
			}

			name = path + path_len;

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
		int isdir = 0;
		while(!isdir) {
			ent = readdir(db->dir);
			if(ent == NULL) {
				return(NULL);
			}

			/* stat the entry, make sure it's a directory */
			path[path_len] = '\0';
			strncat(path, ent->d_name, PATH_MAX - path_len);

			if(!stat(path, &sbuf) && S_ISDIR(sbuf.st_mode)) {
				isdir = 1;
			}
			if(!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) {
				isdir = 0;
				continue;
			}

			name = path + path_len;

			if((ptr = rindex(name, '-'))) {
				*ptr = '\0';
			}
			if((ptr = rindex(name, '-'))) {
				*ptr = '\0';
			}

			/* Add entries like:
			 *
			 *         key:  /var/lib/pacman/extra/xrally
			 *         data: xrally-1.1.1-1
			 */

			if (!strhash_isin(htable, path)) {
				strhash_add(htable, strdup(path), strdup(ent->d_name));
			}
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
			} else if(!strcmp(line, "%LICENSE%")) {
				if(fgets(info->license, sizeof(info->license), fp) == NULL) {
					FREEPKG(info);
					return(NULL);
				}
				trim(info->license);
			} else if(!strcmp(line, "%ARCH%")) {
				if(fgets(info->arch, sizeof(info->arch), fp) == NULL) {
					FREEPKG(info);
					return(NULL);
				}
				trim(info->arch);
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
			} else if(!strcmp(line, "%REASON%")) {
				char tmp[32];
				if(fgets(tmp, sizeof(tmp), fp) == NULL) {
					FREEPKG(info);
					return(NULL);
				}
				trim(tmp);
				info->reason = (unsigned short)atoi(tmp);
			} else if(!strcmp(line, "%SIZE%")) {
				char tmp[32];
				if(fgets(tmp, sizeof(tmp), fp) == NULL) {
					FREEPKG(info);
					return(NULL);
				}
				trim(tmp);
				info->size = (unsigned long)atol(tmp);
			} else if(!strcmp(line, "%CSIZE%")) {
				/* NOTE: the CSIZE and SIZE fields both share the "size" field
				 *       in the pkginfo_t struct.  This can be done b/c CSIZE
				 *       is currently only used in sync databases, and SIZE is
				 *       only used in local databases.
				 */
				char tmp[32];
				if(fgets(tmp, sizeof(tmp), fp) == NULL) {
					FREEPKG(info);
					return(NULL);
				}
				trim(tmp);
				info->size = atol(tmp);
			} else if(!strcmp(line, "%REPLACES%")) {
				/* the REPLACES tag is special -- it only appears in sync repositories,
				 * not the local one.  */
				while(fgets(line, 512, fp) && strlen(trim(line))) {
					char *s = strdup(line);
					info->replaces = list_add(info->replaces, s);
				}
			} else if(!strcmp(line, "%MD5SUM%")) {
				/* MD5SUM tag only appears in sync repositories,
				 * not the local one.  */
				if(fgets(info->md5sum, sizeof(info->md5sum), fp) == NULL) {
					FREEPKG(info);
					return(NULL);
				}
			} else if(!strcmp(line, "%FORCE%")) {
				/* FORCE tag only appears in sync repositories,
				 * not the local one.  */
				info->force = 1;
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
	if(inforeq & INFRQ_ALL) {
		snprintf(path, PATH_MAX, "%s/%s/install", db->path, ent->d_name);
		if(!stat(path, &buf)) {
			info->scriptlet = 1;
		}
	}

	return(info);
}

int db_write(pacdb_t *db, pkginfo_t *info, unsigned int inforeq)
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
	/* make sure we have a sane umask */
	umask(0022);

	/* DESC */
	if(inforeq & INFRQ_DESC) {
		snprintf(path, PATH_MAX, "%s/desc", topdir);
		fp = fopen(path, "w");
		if(fp == NULL) {
			perror("db_write");
			umask(oldmask);
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
		fputs("%LICENSE%\n", fp);
		fprintf(fp, "%s\n\n", info->license);
		fputs("%ARCH%\n", fp);
		fprintf(fp, "%s\n\n", info->arch);
		fputs("%BUILDDATE%\n", fp);
		fprintf(fp, "%s\n\n", info->builddate);
		fputs("%INSTALLDATE%\n", fp);
		fprintf(fp, "%s\n\n", info->installdate);
		fputs("%PACKAGER%\n", fp);
		fprintf(fp, "%s\n\n", info->packager);
		fputs("%SIZE%\n", fp);
		fprintf(fp, "%ld\n\n", info->size);
		fputs("%REASON%\n", fp);
		fprintf(fp, "%d\n\n", info->reason);
		fclose(fp);
	}

	/* FILES */
	if(inforeq & INFRQ_FILES) {
		snprintf(path, PATH_MAX, "%s/files", topdir);
		fp = fopen(path, "w");
		if(fp == NULL) {
			perror("db_write");
			umask(oldmask);
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
	}

	/* DEPENDS */
	if(inforeq & INFRQ_DEPENDS) {
		snprintf(path, PATH_MAX, "%s/depends", topdir);
		fp = fopen(path, "w");
		if(fp == NULL) {
			perror("db_write");
			umask(oldmask);
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
 	}

	/* INSTALL */
	/* nothing needed here (script is automatically extracted) */

	umask(oldmask);
	return(0);
}

void db_search(pacdb_t *db, PMList *cache, const char *treename, PMList *needles)
{
	PMList *i, *j;
	if(needles == NULL || needles->data == NULL) {
		return;
	}

	for(i = needles; i; i = i->next) {
		char *targ = strdup(i->data);
		strtoupper(targ);
		for(j = cache; j; j = j->next) {
			pkginfo_t *pkg = (pkginfo_t*)j->data;
			char *haystack;
			int match = 0;
			/* check name */
			haystack = strdup(pkg->name);
			strtoupper(haystack);
			if(strstr(haystack, targ)) {
				match = 1;
			}
			FREE(haystack);

			/* check description */
			if(!match) {
				haystack = strdup(pkg->desc);
				strtoupper(haystack);
				if(strstr(haystack, targ)) {
					match = 1;
				}
				FREE(haystack);
			}

			/* check provides */
			if(!match) {
				PMList *m;
				pkginfo_t *info = db_scan(db, pkg->name, INFRQ_DESC | INFRQ_DEPENDS);
				if(info != NULL) {
					for(m = info->provides; m; m = m->next) {
						haystack = strdup(m->data);
						strtoupper(haystack);
						if(strstr(haystack, targ)) {
							match = 1;
						}
						FREE(haystack);
					}
					FREEPKG(info);
				}
			}

			if(match) {
				printf("%s/%s %s\n    ", treename, pkg->name, pkg->version);
				indentprint(pkg->desc, 4);
				printf("\n");
			}
		}
		FREE(targ);
	}
}


PMList* db_find_conflicts(pacdb_t *db, PMList *targets, char *root)
{
	PMList *i, *j, *k;
	char *filestr = NULL;
	char path[PATH_MAX+1];
	char *str = NULL;
	struct stat buf, buf2;
	PMList *conflicts = NULL;
	strhash_t** htables;
	int target_num = 0;
	int d = 0;
	int e = 0;

	/* Create and initialise an array of hash tables.
	 *
	 * htables [ 0 ... target_num ] : targets' files
	 * htables [ target_num       ] : used later
	 */

	target_num = list_count(targets);

	MALLOC(htables, (target_num+1) * sizeof(strhash_t*));

	for(d = 0, i = targets; i; i = i->next, d++) {
		htables[d] = new_strhash(151);

		strhash_add_list(htables[d], ((pkginfo_t*)i->data)->files);
	}

	htables[target_num] = new_strhash(151);

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
			dbstr = (char*)i->data;
			
			if(dbstr == NULL || rindex(dbstr, '/') == dbstr+strlen(dbstr)-1)
				continue;

			for(d = 0, j = targets; j; j = j->next, d++) {
				pkginfo_t *targ = (pkginfo_t*)j->data;
				if(strcmp(info->name, targ->name) && strhash_isin(htables[d], dbstr)) {
					MALLOC(str, 512);
					snprintf(str, 512, "%s: exists in \"%s\" (target) and \"%s\" (installed)", dbstr,
							targ->name, info->name);
					conflicts = list_add(conflicts, str);
				}
			}
		}
	}*/

	/* CHECK 2: check every target against every target */
	for(d = 0, i = targets; i; i = i->next, d++) {
		pkginfo_t *p1 = (pkginfo_t*)i->data;
		for(e = d, j = i; j; j = j->next, e++) {
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
					if(strhash_isin(htables[e], filestr)) {
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

					if(dbpkg)
						strhash_add_list(htables[target_num], dbpkg->files);
				}
				if(dbpkg && strhash_isin(htables[target_num], filestr)) {
					ok = 1;
				}
				/* Make sure that the supposedly-conflicting file is not actually just
				 * a symlink that points to a path that used to exist in the package.
				 */
				/* Check if any part of the conflicting file's path is a symlink */
				if(dbpkg && !ok) {
					MALLOC(str, PATH_MAX);
					for(k = dbpkg->files; k; k = k->next) {
						snprintf(str, PATH_MAX, "%s%s", root, (char*)k->data);
						stat(str, &buf2);
						if(buf.st_ino == buf2.st_ino && buf.st_dev == buf2.st_dev) {
							ok = 1;
						}
					}
					FREE(str);
				}
				/* Check if the conflicting file has been moved to another package/target */
				if(!ok) {
					/* Look at all the targets */
					for(k = targets; k && !ok; k = k->next) {
						pkginfo_t *p1 = (pkginfo_t*)k->data;
						/* As long as they're not the current package */
						if(strcmp(p1->name, p->name)) {
							pkginfo_t *dbpkg2 = NULL;
							dbpkg2 = db_scan(db, p1->name, INFRQ_DESC | INFRQ_FILES);
							/* If it used to exist in there, but doesn't anymore */
							if(dbpkg2 && !is_in(filestr, p1->files) && is_in(filestr, dbpkg2->files)) {
								ok = 1;
							}
							FREEPKG(dbpkg2);
						}
					}
				}
				if(!ok) {
					MALLOC(str, 512);
					snprintf(str, 512, "%s: %s: exists in filesystem", p->name, path);
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
