/*
 *  pacman
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
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "list.h"
#include "package.h"
#include "db.h"
#include "util.h"
#include "pacman.h"

extern tartype_t gztype;

pkginfo_t* load_pkg(char *pkgfile, unsigned short output)
{
	char *expath;
	char *descfile;
	int i;
	TAR *tar;
	pkginfo_t *info = NULL;
	PMList *backup = NULL;
	PMList *lp;

	info = newpkg();
	descfile = strdup("/tmp/pacman_XXXXXX");

	if(tar_open(&tar, pkgfile, &gztype, O_RDONLY, 0, TAR_GNU) == -1) {
	  perror("could not open package");
		return(NULL);
	}
	vprint("load_pkg: loading filelist from package...\n");
	for(i = 0; !th_read(tar); i++) {
		if(!strcmp(th_get_pathname(tar), ".PKGINFO")) {
			/* extract this file into /tmp. it has info for us */
			vprint("load_pkg: found package description file.\n");
			mkstemp(descfile);
			tar_extract_file(tar, descfile);
			/* parse the info file */
			parse_descfile(descfile, info, &backup, output);
			if(!strlen(info->name)) {
				fprintf(stderr, "error: missing package name in description file.\n");
				return(NULL);
			}
			if(!strlen(info->version)) {
				fprintf(stderr, "error: missing package version in description file.\n");
				return(NULL);
			}
			for(lp = backup; lp; lp = lp->next) {
				if(lp->data) {
					info->backup = list_add(info->backup, lp->data);
				}
			}
			continue;
		}
		if(!strcmp(th_get_pathname(tar), "._install")) {
			info->scriptlet = 1;
		} else {
			expath = strdup(th_get_pathname(tar));
			/* add the path to the list */
			info->files = list_add(info->files, expath);
		}

		if(TH_ISREG(tar) && tar_skip_regfile(tar)) {
			perror("bad package file");
			return(NULL);
		}
		expath = NULL;
	}
	tar_close(tar);
	FREE(descfile);

	if(!strlen(info->name) || !strlen(info->version)) {
		fprintf(stderr, "Error: Missing .PKGINFO file in %s\n", pkgfile);
		return(NULL);
	}

	return(info);
}

/* Parses the package description file for the current package
 *
 * Returns: 0 on success, 1 on error
 *
 */
int parse_descfile(char *descfile, pkginfo_t *info, PMList **backup, int output)
{
	FILE* fp = NULL;
	char line[PATH_MAX+1];
	char* ptr = NULL;
	char* key = NULL;
	int linenum = 0;
	PMList* bak = NULL;

	if((fp = fopen(descfile, "r")) == NULL) {
		perror(descfile);
		return(1);
	}

	while(!feof(fp)) {
		fgets(line, PATH_MAX, fp);
		if(output) {
			printf("%s", line);
		}
		linenum++;
		trim(line);
		if(line[0] == '#') {
			continue;
		}
		if(strlen(line) == 0) {
			continue;
		}
		ptr = line;
		key = strsep(&ptr, "=");
		if(key == NULL || ptr == NULL) {
			fprintf(stderr, "Syntax error in description file line %d\n", linenum);
		} else {
			trim(key);
			key = strtoupper(key);
			trim(ptr);
			if(!strcmp(key, "PKGNAME")) {
				strncpy(info->name, ptr, sizeof(info->name));
			} else if(!strcmp(key, "PKGVER")) {
				strncpy(info->version, ptr, sizeof(info->version));
			} else if(!strcmp(key, "PKGDESC")) {
				strncpy(info->desc, ptr, sizeof(info->desc));
			} else if(!strcmp(key, "BUILDDATE")) {
				strncpy(info->builddate, ptr, sizeof(info->builddate));
			} else if(!strcmp(key, "INSTALLDATE")) {
				strncpy(info->installdate, ptr, sizeof(info->installdate));
			} else if(!strcmp(key, "PACKAGER")) {
				strncpy(info->packager, ptr, sizeof(info->packager));
			} else if(!strcmp(key, "SIZE")) {
				char tmp[32];
				strncpy(tmp, ptr, sizeof(tmp));
				info->size = atol(tmp);
			} else if(!strcmp(key, "DEPEND")) {				
				char *s = strdup(ptr);
				info->depends = list_add(info->depends, s);
			} else if(!strcmp(key, "CONFLICT")) {
				char *s = strdup(ptr);
				info->conflicts = list_add(info->conflicts, s);
			} else if(!strcmp(key, "BACKUP")) {
				char *s = strdup(ptr);
				bak = list_add(bak, s);
			} else {
				fprintf(stderr, "Syntax error in description file line %d\n", linenum);
			}
		}
		line[0] = '\0';
	}
	fclose(fp);
	unlink(descfile);

	*backup = bak;
	return(0);
}

pkginfo_t* newpkg()
{
	pkginfo_t* pkg = NULL;
	MALLOC(pkg, sizeof(pkginfo_t));

	pkg->name[0]        = '\0';
	pkg->version[0]     = '\0';
	pkg->desc[0]        = '\0';
	pkg->builddate[0]   = '\0';
	pkg->installdate[0] = '\0';
	pkg->packager[0]    = '\0';
	pkg->size           = 0;
	pkg->scriptlet      = 0;
	pkg->requiredby     = NULL;
	pkg->conflicts      = NULL;
	pkg->files          = NULL;
	pkg->backup         = NULL;
	pkg->depends        = NULL;

	return(pkg);
}

void freepkg(pkginfo_t *pkg)
{
	if(pkg == NULL) {
		return;
	}

	list_free(pkg->files);
	list_free(pkg->backup);
	list_free(pkg->depends);
	list_free(pkg->conflicts);
	list_free(pkg->requiredby);
	FREE(pkg);
	return;
}

/* Helper function for sorting packages
 */
int pkgcmp(const void *p1, const void *p2)
{
	pkginfo_t **pkg1 = (pkginfo_t**)p1;
	pkginfo_t **pkg2 = (pkginfo_t**)p2;

	return(strcmp(pkg1[0]->name, pkg2[0]->name));
}

/* vim: set ts=2 sw=2 noet: */
