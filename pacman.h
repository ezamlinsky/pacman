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
#ifndef PACMAN_H
#define PACMAN_H

#define VERSION   "1.1"

#define PKGEXT		".tar.gz"
#define PKGDB			"/var/lib/pacman/pacman.db"

/* Operations */
#define PM_MAIN			0
#define PM_ADD			1
#define PM_REMOVE		2
#define PM_QUERY		3
#define	PM_UPGRADE	4

typedef int (*pm_opfunc_t)(char*);
typedef char** fileset_t;
typedef struct __pkginfo_t {
	char version[32];
	char name[64];
} pkginfo_t;

int pacman_add(char* pkgfile);
int pacman_remove(char* pkgfile);
int pacman_query(char* pkgfile);
int pacman_upgrade(char* pkgfile);

int db_open(char* path);
int db_update(fileset_t files, unsigned int filecount);
int db_find_conflicts(fileset_t files, unsigned int filecount);
int load_pkg(char* pkgfile, fileset_t* listptr, unsigned short output);

char* parseargs(int op, int argc, char** argv);
int parse_descfile(char* descfile, unsigned short output, fileset_t* bakptr,
								unsigned int* bakct);

int vprint(char* fmt, ...);
void usage(int op, char* myname);
void version(void);
int is_in(char* needle, fileset_t haystack, unsigned int hayct);
int needbackup(char* file, fileset_t files, unsigned int filect);
char* trim(char* str);
char* strtoupper(char* str);
static int gzopen_frontend(char *pathname, int oflags, int mode);

#endif /* PACMAN_H */

/* vim: set ts=2 noet: */
