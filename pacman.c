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

#include "pacman.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libtar.h>
#include <zlib.h>

/* borrowed and modifed from Per Liden's pkgutils (http://crux.nu) */
static int gzopen_frontend(char *pathname, int oflags, int mode)
{
	char* gzoflags;
	int fd;
	gzFile gzf;
   
	switch (oflags & O_ACCMODE) {
		case O_WRONLY:
			gzoflags = "w";
			break;
		case O_RDONLY:
			gzoflags = "r";
			break;
		case O_RDWR:
		default:
			errno = EINVAL;
			return -1;
	}
	
	if((fd = open(pathname, oflags, mode)) == -1) {
		return -1;
	}
	if((oflags & O_CREAT) && fchmod(fd, mode)) {
		return -1;
	}
	if(!(gzf = gzdopen(fd, gzoflags))) {
		errno = ENOMEM;
		return -1;
	}

	return (int)gzf;
}

static tartype_t gztype = {
	(openfunc_t) gzopen_frontend,
	(closefunc_t)gzclose,
	(readfunc_t) gzread,
	(writefunc_t)gzwrite
};

/*
 * GLOBALS
 *
 */
FILE* dbfp    = NULL;
char* dbpath  = PKGDB;
char* pkgname = NULL;
char* pkgver  = NULL;

char*          pmo_root     = NULL;
unsigned short pmo_verbose  = 0;
unsigned short pmo_force    = 0;
unsigned short pmo_upgrade  = 0;
unsigned short pmo_nosave   = 0;
unsigned short pmo_nofunc   = 0;
unsigned short pmo_q_isfile = 0;
unsigned short pmo_q_info   = 0;
unsigned short pmo_q_list   = 0;
char*          pmo_q_owns   = NULL;

/* list of installed packages */
pkginfo_t**  pm_packages = NULL;
unsigned int pm_pkgcount = 0;

/* list of targets specified on command line */
fileset_t    pm_targets  = NULL;
unsigned int pm_targct   = 0;

/* path to post-install script, if any */
char* pm_install = NULL;

int main(int argc, char* argv[])
{
	pm_opfunc_t op_func;
	int i, ret = 0;

	/* default root */
	pmo_root = (char*)malloc(2);	
	strcpy(pmo_root, "/");

	if(argc < 2) {
		usage(PM_MAIN, (char*)basename(argv[0]));
		return(0);
	}

	/* determine the requested operation and pass off to */
	/* the handler function */
	if(!strcmp(argv[1], "-A") || !strcmp(argv[1], "--add")) {
		op_func = pacman_add;
		ret = parseargs(PM_ADD, argc, argv);
	} else if(!strcmp(argv[1], "-R") || !strcmp(argv[1], "--remove")) {
		op_func = pacman_remove;
		ret = parseargs(PM_REMOVE, argc, argv);
	} else if(!strcmp(argv[1], "-Q") || !strcmp(argv[1], "--query")) {
		op_func = pacman_query;
		ret = parseargs(PM_QUERY, argc, argv);
	} else if(!strcmp(argv[1], "-U") || !strcmp(argv[1], "--upgrade")) {
		op_func = pacman_upgrade;
		ret = parseargs(PM_UPGRADE, argc, argv);
	} else if(!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
		usage(PM_MAIN, (char*)basename(argv[0]));
		return(1);
	} else if(!strcmp(argv[1], "-V") || !strcmp(argv[1], "--version")) {
		version();
		return(1);
	} else {
		fprintf(stderr, "error: invalid operation\n\n");
		usage(PM_MAIN, (char*)basename(argv[0]));
		return(1);
	}

	if(ret || (pm_targct < 1 && op_func != pacman_query)) {
		return(1);
	}
	vprint("Installation Root: %s\n", pmo_root);
	if(pm_targct) {
		vprint("Targets:\n");
		for(i = 0; i < pm_targct; i++) {
			vprint("  %s\n", pm_targets[i]);
		}
	}

	/* check for db existence */
	if(pmo_root != NULL) {
		/* trim the trailing '/' if there is one */
		if((int)rindex(pmo_root, '/') == ((int)pmo_root)+strlen(pmo_root)-1) {
			pmo_root[strlen(pmo_root)-1] = '\0';
		}
		free(dbpath);
		dbpath = (char*)malloc(strlen(pmo_root) + strlen(PKGDB) + 1);
		strcpy(dbpath, pmo_root);
		dbpath = (char*)strcat(dbpath, PKGDB);
	}
	vprint("Using package DB:  %s\n", dbpath);

	ret = db_open(dbpath);
	if(ret == 1) {
		fprintf(stderr, "error:  Could not open package database file!\n");
		fprintf(stderr, "        Check to see that %s exists.\n", dbpath);
		fprintf(stderr, "        If not, you can create it by \"touch\"-ing it.\n");
		return(1);
	}
	if(ret == 2) {
		fprintf(stderr, "error:  Database is corrupt!  You may need to use the backup database.\n");
		return(1);
	}

	/* start the requested operation */
	if(!pmo_nofunc) {
		for(i = 0; i < pm_targct; i++) {
			if(op_func(pm_targets[i])) {
				ret = 1;
				/*
				if(op_func == pacman_add) {
					printf("Failed to add %s\n", pkgname != NULL ? pkgname : pm_targets[i]);
				} else if(op_func == pacman_remove) {
					printf("Failed to remove %s\n", pkgname != NULL ? pkgname : pm_targets[i]);
				} else if(op_func == pacman_upgrade) {
					printf("Failed to upgrade %s\n", pkgname != NULL ? pkgname : pm_targets[i]);
				}
				*/
			}
		}
		if(op_func == pacman_query && pm_targct == 0) {
			ret = op_func(NULL);
		}
	}
	
	if(op_func == pacman_remove) {
		/* the remove function no longer updates the db itself. we do it here
		 * instead, in an effort to save some expensive file rewrites. note that we
		 * can't do this for pacman_add() yet, as he's gotta call db_find_conflicts
		 * for each package.
		 */
		ret = db_update(NULL, 0);
	}

	fclose(dbfp);
	return(ret);
}

int pacman_add(char* pkgfile)
{
	int i, ret = 0, errors = 0;
	TAR* tar;
	char* errmsg = NULL;
	char* expath = NULL;
	/*char* newpath = NULL;*/
	fileset_t files = NULL;
	unsigned int filecount = 0, nb = 0;
	struct stat buf;

	/* Populate the file list */
	filecount = load_pkg(pkgfile, &files, 0);

	if(filecount == 0) {
		return(1);
	}

	/* Now check for conflicts in the db */
	vprint("Looking for DB conflicts...\n");
	if((i = db_find_conflicts(files, filecount)) == 1) {
		if(pmo_force) {
			fprintf(stderr, "\nInstalling package anyway...\n");
			fprintf(stderr, "  You might have duplicate entries in your package\n");
			fprintf(stderr, "  database now.  You may want to edit it and remove\n");
			fprintf(stderr, "  one of the copies.\n\n");
		} else {
			fprintf(stderr, "Aborting...\n");
			fprintf(stderr, "   (use -f to override)\n\n");		
			return(1);
		}
	} else if(i == 2) {
		return(1);
	} else {
		vprint("No DB conflicts found\n");
	}

	/* see if this is an upgrade.  if so, remove the old package first */
	if(pmo_upgrade) {
		vprint("Removing old package first...\n");
		/* save the old nosave option and disable it for the package remove */
		i = pmo_nosave;
		pmo_nosave = 0;
		ret = pacman_remove(pkgname);
		/* restore nosave */
		pmo_nosave = i;
		if(ret == 1) {
			fprintf(stderr, "\nUpgrade aborted.\n");
			return(1);
		} else if(ret == 2) {
			fprintf(stderr, "%s is not installed.  skipping...\n", pkgname);
			return(0);
		}
		if(db_update(NULL, 0)) {
			fprintf(stderr, "\nError updating database. Upgrade aborted.\n");
			return(1);
		}
	}

	/* open the .tar.gz package */
	if(tar_open(&tar, pkgfile, &gztype, O_RDONLY, 0, TAR_GNU) == -1) {
	  perror("could not open package");
		return(1);
	}
	vprint("Extracting files...\n");
	for(i = 0; !th_read(tar); i++) {
		if(!strcmp(th_get_pathname(tar), ".PKGINFO")) {
			tar_skip_regfile(tar);
			continue;
		}
		/* build the new pathname relative to pmo_root */
		expath = (char*)malloc(strlen(th_get_pathname(tar))+strlen(pmo_root)+2);
		strcpy(expath, pmo_root);
		strcat(expath, "/");
		strcat(expath, th_get_pathname(tar));
		
		/* see if this file should be backed up */
		nb = needbackup(expath, files, filecount);

		vprint("  %s\n", expath);
		if(!pmo_nosave && nb && !stat(expath, &buf)) {
			/* backup this file */
			/*newpath = (char*)realloc(newpath, strlen(expath)+6);
			strcpy(newpath, expath);
			strcat(newpath, ".save");
			rename(expath, newpath);
			fprintf(stderr, "%s renamed to %s\n", expath, newpath);*/

			/* keep the old file and write the new one to a .pacnew extension */
			fprintf(stderr, "%s already exists, extracting to %s.pacnew\n", expath, expath);
			expath = (char*)realloc(expath, strlen(expath)+strlen(".pacnew")+1);
			strcat(expath, ".pacnew");
		}
	  if(tar_extract_file(tar, expath)) {
			errmsg = strerror(errno);
			fprintf(stderr, "could not extract %s: %s\n", th_get_pathname(tar), errmsg);
			errors = 1;
		}
		free(expath);
	}
	tar_close(tar);
	vprint("Done.\n");
	if(errors) {
		fprintf(stderr, "There were errors.  No database update was performed.\n");
		return(1);
	} else {
		vprint("Updating database...\n");
		if(db_update(files, filecount)) {
			fprintf(stderr, "error: Could not update database!  The database may not\n");
			fprintf(stderr, "       be in a sane state!\n");
			return(1);
		}
		vprint("Done.\n");
	}

	/* run the script in pm_install  */
	if(pm_install != NULL) {
		vprint("Executing post-install script...\n");
		expath = (char*)malloc(256);
		snprintf(expath, 255, "/bin/bash %s", pm_install);
		system(expath);
		free(expath);
		unlink(pm_install);
		free(pm_install);
	}
	
	return(0);
}

int pacman_remove(char* pkgfile)
{
	int found = 0, done = 0;
	int i;
	char line[PATH_MAX+1];
	fileset_t files = NULL;
	unsigned int filecount = 0, nb = 0;
	struct stat buf;
	char* newpath = NULL;
	char* file = NULL;

	if(pkgfile == NULL) {
		return(0);
	}

	/* find the package's filelist in the db */
	rewind(dbfp);
	while(!found && !feof(dbfp)) {
		fgets(line, 255, dbfp);
		strcpy(line, trim(line));
		if(!strcmp(line, pkgfile)) {
			/* read the version */
			fgets(line, 63, dbfp);
			found = 1;
		}
	}
	if(!found) {
		if(pmo_upgrade) {
			/* special handling */
			return(2);
		} else {
			fprintf(stderr, "Cannot remove %s: Package was not found.\n", pkgfile);
			return(1);
		}
	}

	while(!done) {
		fgets(line, PATH_MAX, dbfp);
		strcpy(line, trim(line));
		if(strlen(line)) {
			/* add the path to the list */
			files = (fileset_t)realloc(files, (++filecount) * sizeof(char*));
			files[filecount-1] = (char*)malloc(strlen(line)+1);
			strcpy(files[filecount-1], line);
		} else {
			done = 1;
		}
	}
	/* iterate through the list backwards, unlinking files */
	for(i = filecount-1; i >= 0; i--) {
		file = (char*)realloc(file, strlen(files[i])+strlen(pmo_root)+1);
		strcpy(file, pmo_root);
		if(files[i][0] == '*') {
			nb = 1;
			strcat(file, (char*)(files[i]+1));
		} else {
			nb = 0;
			strcat(file, files[i]);
		}
		if(lstat(file, &buf)) {
			vprint("file %s does not exist\n", file);
			continue;
		}
		if(S_ISDIR(buf.st_mode)) {
			vprint("  removing directory %s\n", file);
			if(rmdir(file)) {
				/* this is okay, other packages are probably using it. */
				/* perror("cannot remove directory"); */
			}
		} else {
			/* if the file is flagged, back it up to .pacsave */
			if(nb) {
				if(!pmo_upgrade && !pmo_nosave) {
					newpath = (char*)realloc(newpath, strlen(file)+strlen(".pacsave")+1);
					strcpy(newpath, file);
					strcat(newpath, ".pacsave");
					rename(file, newpath);
					fprintf(stderr, "%s renamed to %s\n", file, newpath);
				} else if(!pmo_upgrade && pmo_nosave) {
					vprint("  unlinking %s\n", file);
					if(unlink(file)) {
						perror("cannot remove file");
					}
				} else {
					/* skip */
				}
			} else {
				vprint("  unlinking %s\n", file);
				if(unlink(file)) {
					perror("cannot remove file");
				}
			}
		}
	}

	/* now splice this name out of the packages list */
	found = 0;
	for(i = 0; i < pm_pkgcount-1; i++) {
		if(found) {
			pm_packages[i] = pm_packages[i+1];
		} else {
			if(!strcmp(pm_packages[i]->name, pkgfile)) {
				found = 1;
				if(i < pm_pkgcount-1) {
					pm_packages[i] = pm_packages[i+1];
				}
			} 
		}
	}
	/* drop the last item */
	pm_packages = (pkginfo_t**)realloc(pm_packages, (--pm_pkgcount)*sizeof(pkginfo_t*));

	/* the db will be commited back up in main() */
	return(0);
}

int pacman_query(char* pkgfile)
{
	char *str = NULL;
	char name[256];
	char ver[64];
	char line[PATH_MAX+1];
	int found = 0;
	int done  = 0;
	int i;
	unsigned int filecount = 0;
	fileset_t files = NULL;

	/* output info for a .tar.gz package */
	if(pmo_q_isfile) {
		filecount = load_pkg(pkgfile, &files, pmo_q_info);
		if(pmo_q_list) {
			for(i = 0; i < filecount; i++) {
				if(strcmp(files[i], ".PKGINFO")) {
				  if(files[i][0] == '*') {
						printf("%s\n", (char*)(files[i]+1));
					} else {
						printf("%s\n", files[i]);
					}
				}
			}
		} else {
			printf("%s %s\n", pkgname, pkgver);
		}

		return(0);
	}

	/* determine the owner of a file */
	if(pmo_q_owns != NULL) {
		rewind(dbfp);
		while(!found && !feof(dbfp)) {
			fgets(name, 255, dbfp);
			strcpy(name, trim(name));
			fgets(ver, 63, dbfp);
			strcpy(ver, trim(ver));
			strcpy(line, " ");
			while(strlen(line) && !feof(dbfp)) {
				fgets(line, PATH_MAX, dbfp);
				strcpy(line, trim(line));
				str = line;
				if(line[0] == '*') {
					str++;
				}
				str += strlen(pmo_root);
				if(!strcmp(str, pmo_q_owns)) {
					printf("%s %s\n", name, ver);
					return(0);
				}
			}
		}
		fprintf(stderr, "No package owns this file.\n");
		return(2);
	}

	/* find packages in the db */
	rewind(dbfp);
	while(!done) {
		found = 0;
		while(!found && !feof(dbfp)) {
			fgets(name, 255, dbfp);
			strcpy(name, trim(name));
			if(pkgfile == NULL || (pkgfile != NULL && !strcmp(name, pkgfile))) {
				/* read the version */
				fgets(ver, 63, dbfp);
				strcpy(ver, trim(ver));
				found = 1;
				if(pkgfile != NULL) {
					done = 1;
				}
			}
		}
		if(feof(dbfp)) {
			if(pkgfile != NULL && !found) {
				fprintf(stderr, "Package \"%s\" was not found in database.\n", pkgfile);
				return(2);
			}
			break;
		}
		found = 0;
		while(!found) {
			fgets(line, PATH_MAX, dbfp);
			strcpy(line, trim(line));
			if(strlen(line)) {
				if(pmo_q_list) {
					if(line[0] == '*') {
						printf("%s%s\n", pmo_root, (char*)(line+1));
					} else {
						printf("%s%s\n", pmo_root, line);
					}
				}
			} else {
				found = 1;
			}
		}
		if(!pmo_q_list) {
			printf("%s %s\n", name, ver);
		}
		if(feof(dbfp)) {
			done = 1;
		}
	}
	
	return(0);
}

int pacman_upgrade(char* pkgfile)
{
	/* this is basically just a remove,add process. pacman_add() will */
  /* handle it */
  pmo_upgrade = 1;
	return pacman_add(pkgfile);
}

/*
 * Populate the package file list
 *     pkgfile: filename of package
 *     listptr: this will be set to the new fileset_t
 *
 * Returns: the number of filenames read in the package, or 0 on error
 *
 */
int load_pkg(char* pkgfile, fileset_t* listptr, unsigned short output)
{
	char* expath;
	char* descfile;
	int i;
	TAR* tar;
	unsigned int filecount = 0;
	fileset_t files = NULL;
	fileset_t backup = NULL;
	unsigned int bakct = 0;

	descfile = (char*)malloc(strlen("/tmp/pacman_XXXXXX")+1);
	strcpy(descfile, "/tmp/pacman_XXXXXX");	

	if(tar_open(&tar, pkgfile, &gztype, O_RDONLY, 0, TAR_GNU) == -1) {
	  perror("could not open package");
		return(0);
	}
	vprint("Loading filelist from package...\n");
	for(i = 0; !th_read(tar); i++) {
		if(!strcmp(th_get_pathname(tar), ".PKGINFO")) {
			/* extract this file into /tmp. it has info for us */
			vprint("Found package description file.\n");
			mkstemp(descfile);
			tar_extract_file(tar, descfile);
			parse_descfile(descfile, output, &backup, &bakct);
			continue;
		}
		/* build the new pathname relative to pmo_root */
		if(is_in(th_get_pathname(tar), backup, bakct)) {
			expath = (char*)malloc(strlen(th_get_pathname(tar))+strlen(pmo_root)+3);
			// prepend the backup symbol
			strcpy(expath, "*");
		} else {
			expath = (char*)malloc(strlen(th_get_pathname(tar))+strlen(pmo_root)+2);
			expath[0] = '\0';
		}
		strcat(expath, pmo_root);
		strcat(expath, "/");
		strcat(expath, th_get_pathname(tar));
		/* add the path to the list */
		files = (fileset_t)realloc(files, (++filecount) * sizeof(char*));
		files[filecount-1] = expath;

		if(TH_ISREG(tar) && tar_skip_regfile(tar)) {
			perror("bad package file");
			return(1);
		}
		expath = NULL;
	}
	tar_close(tar);

	free(descfile);

	if(pkgname == NULL || pkgver == NULL) {
		fprintf(stderr, "Error: Missing .PKGINFO file in %s\n", pkgfile);
		return(0);
	}

	(*listptr) = files;
	return(filecount);
}

/* Open the database file
 *     path: the full pathname of the file
 */
int db_open(char* path)
{
	char line[PATH_MAX+1];
	int i;
	pkginfo_t* info;

	/* if pm_packages already contains data, free it first */
	if(pm_pkgcount) {
		for(i = 0; i < pm_pkgcount; i++) {
			free(pm_packages[i]);
		}
		free(pm_packages);
		pm_packages = NULL;
		pm_pkgcount = 0;
	}
	
	dbfp = fopen(path, "r");
	if(dbfp == NULL) {
		return(1);
	}
	while(!feof(dbfp)) {
		info = (pkginfo_t*)malloc(sizeof(pkginfo_t));
		fgets(line, sizeof(info->name)-1, dbfp);
		if(feof(dbfp)) {
			break;
		}
		strcpy(info->name, trim(line));
		fgets(line, sizeof(info->version)-1, dbfp);
		strcpy(info->version, trim(line));
		/* add to the collective */
		pm_packages = (pkginfo_t**)realloc(pm_packages, (++pm_pkgcount) * sizeof(pkginfo_t*));
		pm_packages[pm_pkgcount-1] = info;
		for(;;) {
			fgets(line, PATH_MAX, dbfp);
			if(feof(dbfp)) {
				return(2);
			}
			if(strlen(trim(line)) == 0) {
				break;
			}
		}
	}

	return(0);
}

/* Copy the old database file to a backup and build the
 * new copy in its place.
 *     files:     list of files in the new package, can be null
 *     filecount: number of entries in files
 *
 * Returns: 0 on success
 * 
 */
int db_update(fileset_t files, unsigned int filecount)
{
	FILE* olddb = NULL;
	char* newpath = NULL;
	char* str = NULL;
	char name[256];
	char ver[64];
	char line[PATH_MAX+1];
	int  i = 0, found = 0, done = 0;

	/* build the backup pathname */
	newpath = (char*)malloc(strlen(dbpath)+5);
	strcpy(newpath, dbpath);
	strcat(newpath, ".bak");
	
	/* rename the existing db */
	fclose(dbfp);
	rename(dbpath, newpath);

	olddb = fopen(newpath, "r");
	free(newpath);
	dbfp = fopen(dbpath, "w");
	if(olddb == NULL || dbfp == NULL) {
		return(1);
	}

	rewind(olddb);
	while(!feof(olddb)) {
		if(!fgets(name, 255, olddb)) {
			break;
		}
		strcpy(name, trim(name));
		fgets(ver, 63, olddb);
		found = 0;
		for(i = 0; i < pm_pkgcount && !found; i++) {
			if(!strcmp(pm_packages[i]->name, name)) {
				/* it's there... copy the entries over */
				found = 1;
				fputs(name, dbfp);
				fputc('\n', dbfp);
				fputs(ver, dbfp);
				for(done = 0; !done;) {
					fgets(line, PATH_MAX, olddb);
					if(found) {
						fputs(line, dbfp);
					}
					if(strlen(trim(line)) == 0 || feof(olddb)) {
						done = 1;
					}
				}
			}
		}
		if(!found) {
			/* skip through filelist for this package */
			for(done = 0; !done;) {
				fgets(line, PATH_MAX, olddb);
				if(strlen(trim(line)) == 0 || feof(olddb)) {
					done = 1;
				}
			}
		}
	}
	fclose(olddb);

	/* write the current info  */
	if(files != NULL) {
		fputs(pkgname, dbfp);
		fputc('\n', dbfp);
		fputs(pkgver, dbfp);
	  fputc('\n', dbfp);
		for(i = 0; i < filecount; i++) {
			str = files[i];
			str += strlen(pmo_root);
			if(files[i][0] == '*') {
				fputc('*', dbfp);
				str++;
			}
			fputs(str, dbfp);
			fputc('\n', dbfp);
		}
		fputs("\n", dbfp);
	}
	fclose(dbfp);
	db_open(dbpath);

	return(0);
}

/* Check the database for conflicts
 *     files:     list of files in the new package
 *     filecount: number of entries in files
 *
 * Returns: 0 if no conflicts were found, 1 otherwise
 *
 */
int db_find_conflicts(fileset_t files, unsigned int filecount)
{
	int i;
	char line[PATH_MAX+1];
	char name[256];
	char* dbstr   = NULL;
	char* filestr = NULL;
	struct stat buf;
	int conflicts = 0;

	/* CHECK 1: checking db conflicts */
	rewind(dbfp);
	while(!feof(dbfp)) {
		fgets(name, 255, dbfp);
		strcpy(name, trim(name));
		if(!pmo_upgrade && !strcmp(name, pkgname)) {
			fprintf(stderr, "error: This package is already installed.\n");
			fprintf(stderr, "       Maybe you should be using --upgrade.\n");
			return(2);
		}
		fgets(line, 64, dbfp);
		while(!feof(dbfp)) {
			fgets(line, PATH_MAX, dbfp);
			strcpy(line, trim(line));
			dbstr = line;
			if(dbstr[0] == '*') {
			  dbstr++;
			}
			if(!strlen(dbstr)) {
				break;
			}
			if(index(dbstr, '/') == dbstr && (!pmo_upgrade || strcmp(name,pkgname))) {
				/* we're looking at a file in the db that belongs to a different package */
				for(i = 0; i < filecount; i++) {
					filestr = files[i];
					if(filestr[0] == '*') {
						filestr++;
					}
					if(!strcmp(dbstr, filestr)) {
						if(rindex(files[i], '/') == filestr+strlen(filestr)-1) {
							/* this filename has a trailing '/', so it's a directory -- skip it. */
							continue;
						}
						fprintf(stderr, "conflict: %s already exists in package \"%s\"\n", dbstr, name);
						conflicts = 1;
					}
				}
			}
		}
	}

	/* CHECK 2: checking filesystem conflicts */
	/* TODO: run filesystem checks for upgrades */
	for(i = 0; i < filecount && !pmo_upgrade; i++) {
		filestr = files[i];
		if(filestr[0] == '*') {
			filestr++;
		}
		if(!stat(filestr, &buf) && !S_ISDIR(buf.st_mode)) {
			fprintf(stderr, "conflict: %s already exists in filesystem\n", filestr);
			conflicts = 1;
		}
	}

	return(conflicts);
}

/* Parses the package description file for the current package
 *     descfile: the full pathname of the description file
 *
 * Returns: 0 on success, 1 on error
 *
 */
int parse_descfile(char* descfile, unsigned short output, fileset_t *bakptr,
								unsigned int* bakct)
{
	FILE* fp = NULL;
	FILE* inst = NULL;
	char line[PATH_MAX+1];
	char* ptr = NULL;
	char* key = NULL;
	int linenum = 0;
	fileset_t backup = NULL;
	unsigned int count = 0;

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
		strcpy(line, trim(line));
		if(index(line, '#') == (char*)line) {
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
			key = trim(key);
			key = strtoupper(key);
			ptr = trim(ptr);
			if(!strcmp(key, "PKGNAME")) {
				pkgname = (char*)malloc(strlen(ptr)+1);
				strcpy(pkgname, ptr);
			} else if(!strcmp(key, "PKGVER")) {
				pkgver = (char*)malloc(strlen(ptr)+1);
				strcpy(pkgver, ptr);
			} else if(!strcmp(key, "PKGDESC")) {
				/* Not used yet */
			} else if(!strcmp(key, "INSTALL")) {
				if(inst == NULL) {
					pm_install = (char*)malloc(strlen("/tmp/pacman_XXXXXX")+1);
					strcpy(pm_install, "/tmp/pacman_XXXXXX");	
					mkstemp(pm_install);
					inst = fopen(pm_install, "w");
					if(inst == NULL) {
						perror("fopen");
						return(1);
					}
				}
				fputs(ptr, inst);
				fputc('\n', inst);
			} else if(!strcmp(key, "BACKUP")) {
				backup = (fileset_t)realloc(backup, (++count) * sizeof(char*));
				backup[count-1] = (char*)malloc(strlen(ptr)+1);
				strcpy(backup[count-1], ptr);
			} else {
				fprintf(stderr, "Syntax error in description file line %d\n", linenum);
			}
		}
		line[0] = '\0';
	}
	fclose(fp);
	unlink(descfile);

	if(inst != NULL) {
		fputs("exit 0\n", inst);
		fclose(inst);
	}

	if(count > 0) {
		(*bakptr) = backup;
		(*bakct)  = count;
	}

	return(0);
}

/* Parse command-line arguments for each operation
 *     op:   the operation code requested
 *     argc: argc
 *     argv: argv
 *     
 * Returns: 0 on success, 1 on error
 */
int parseargs(int op, int argc, char** argv)
{
	int i;

	for(i = 2; i < argc; i++) {
		if(strlen(argv[i]) > PATH_MAX) {
			fprintf(stderr, "error: argument %d is too long.\n", i);
			return(1);
		}
		if(!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			pmo_nofunc = 1;
			usage(op, (char*)basename(argv[0]));
			return(1);
		} else if(!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose")) {
			pmo_verbose = 1;
		} else if(!strcmp(argv[i], "-f") || !strcmp(argv[i], "--force")) {
			pmo_force = 1;
		} else if(!strcmp(argv[i], "-r") || !strcmp(argv[i], "--root")) {
			i++;
			if(i >= argc) {
				fprintf(stderr, "error: missing argument for %s\n", argv[i-1]);
				return(1);
			}
			free(pmo_root);
			pmo_root = (char*)malloc(PATH_MAX);
			if(realpath(argv[i], pmo_root) == NULL) {
				perror("bad root path");
				return(1);
			}
		} else if(!strcmp(argv[i], "-n") || !strcmp(argv[i], "--nosave")) {
			pmo_nosave = 1;
		} else if(!strcmp(argv[i], "-o") || !strcmp(argv[i], "--owns")) {
			/* PM_QUERY only */
			i++;
			if(i >= argc) {
				fprintf(stderr, "error: missing argument for %s\n", argv[i-1]);
				return(1);
			}
			if(strlen(argv[i]) > PATH_MAX) {
				fprintf(stderr, "error: argument %d is too long.\n", i);
				return(1);
			}
			free(pmo_q_owns);
			pmo_q_owns = (char*)malloc(PATH_MAX);
			if(realpath(argv[i], pmo_q_owns) == NULL) {
				perror("bad path specified for --owns");
				pmo_nofunc = 1;
				return(1);
			}
		} else if(!strcmp(argv[i], "-l") || !strcmp(argv[i], "--list")) {
			/* PM_QUERY only */
			pmo_q_list = 1;
		} else if(!strcmp(argv[i], "-p") || !strcmp(argv[i], "--file")) {
			/* PM_QUERY only */
			pmo_q_isfile = 1;
		} else if(!strcmp(argv[i], "-i") || !strcmp(argv[i], "--info")) {
			/* PM_QUERY only */
			pmo_q_info = 1;
		} else {
			/* add the target to our pseudo linked list */
			pm_targets = (fileset_t)realloc(pm_targets, (++pm_targct) * sizeof(char*));
			pm_targets[pm_targct-1] = argv[i];
		}
	}

	if(op != PM_QUERY && pm_targct < 1) {
		fprintf(stderr, "error: no package specified\n\n");
		usage(op, (char*)basename(argv[0]));
		return(1);
	}
	if(op == PM_QUERY && pmo_q_isfile && pm_targct < 1) {
		fprintf(stderr, "error: no package file specified\n\n");
		return(1);
	}
	return(0);
}

/* Display usage/syntax for the specified operation.
 *     op:     the operation code requested
 *     myname: basename(argv[0])
 */
void usage(int op, char* myname)
{
	if(op == PM_MAIN) {
	  printf("usage:  %s {-h --help}\n", myname);
	  printf("        %s {-V --version}\n", myname);
	  printf("        %s {-A --add}     [options] <file>\n", myname);
	  printf("        %s {-R --remove}  [options] <package>\n", myname);
	  printf("        %s {-U --upgrade} [options] <file>\n", myname);
	  printf("        %s {-Q --query}   [options] [package]\n", myname);
	} else if(op == PM_ADD) {
	  printf("usage:  %s {-A --add} [options] <file>\n", myname);
		printf("options:\n");
		printf("  -f, --force        force install, overwrite conflicting files\n");
		printf("  -n, --nosave       overwrite configuration files as well\n");
		printf("  -v, --verbose      be verbose\n");
		printf("  -r, --root <path>  set an alternate installation root\n");
	} else if(op == PM_REMOVE) {
		printf("usage:  %s {-R --remove} [options] <package>\n", myname);
		printf("options:\n");
		printf("  -n, --nosave       do not save configuration files as .pacsave\n");
		printf("  -v, --verbose      be verbose\n");
		printf("  -r, --root <path>  set an alternate installation root\n");
	} else if(op == PM_UPGRADE) {
		printf("usage:  %s {-U --upgrade} [options] <file>\n", myname);
		printf("options:\n");
		printf("  -f, --force        force install, overwrite conflicting files\n");
		printf("  -n, --nosave       upgrade configuration files as well (old one is deleted)\n");
		printf("  -v, --verbose      be verbose\n");
		printf("  -r, --root <path>  set an alternate installation root\n");
	} else if(op == PM_QUERY) {
		printf("usage:  %s {-Q --query} [options] [package]\n", myname);
		printf("options:\n");
		printf("  -o, --owns <file>  query the package that owns <file>\n");
		printf("  -l, --list         list the contents of the queried package\n");
		printf("  -i, --info         view the package info file (only used with -p)\n");
		printf("  -p, --file         if used, then [package] will be the path\n");
		printf("                     to a .tar.gz package file to query\n");
		printf("  -v, --verbose      be verbose\n");
		printf("  -r, --root <path>  set an alternate installation root\n");
	}
}

/* Version
 */
void version(void)
{
  printf("\n");
	printf(" .--.                    Pacman v%s\n", VERSION);
	printf("/ _.-' .-.  .-.  .-.     Copyright (C) 2002 Judd Vinet <jvinet@zeroflux.org>\n");
	printf("\\  '-. '-'  '-'  '-'    \n");
	printf(" '--'                    This program may be freely redistributed under\n");
	printf("                         the terms of the GNU GPL\n\n");
}

/* Check verbosity option and, if set, print the
 * string to stdout
 */
int vprint(char* fmt, ...)
{
	va_list args;
	if(pmo_verbose) {
		va_start(args, fmt);
		vprintf(fmt, args);
		va_end(args);
		fflush(stdout);
	}
	return(0);
}

/* See if a file should be backed up or not
 */
int needbackup(char* file, fileset_t files, unsigned int filect)
{
	int i;

	for(i = 0; i < filect; i++) {
		if(files[i][0] == '*' && !strcmp((char*)(files[i]+1), file)) {
			return(1);
		}
	}
	return(0);
}

/* Test for existence of a string in a fileset
 */
int is_in(char* needle, fileset_t haystack, unsigned int hayct)
{
	int i;

	for(i = 0; i < hayct; i++) {
		if(!strcmp(haystack[i], needle)) {
			return(1);
		}
	}
	return(0);
}

/* Convert a string to uppercase
 */
char* strtoupper(char* str)
{
	char* ptr = str;

	while(*ptr) {
		(*ptr) = toupper(*ptr);
		ptr++;
	}
	return str;
}

/* Trim whitespace and newlines from a string
 */
char* trim(char* str)
{
	char* start = str;
	char* end   = str + strlen(str);
	int mid = 0;
	char ch;

	while(mid < 2) {
		if(!mid) {
			ch = *start;
			if(ch == 10 || ch == 13 || ch == 9 || ch == 32) {
				start++;
			} else {
				mid = 1;
			}
		} else {
			end--;
			ch = *end;
			if(ch == 10 || ch == 13 || ch == 9 || ch == 32) {
				*end = '\0';
			} else {
				mid = 2;
			}
		}
	}
	return(start);
}

/* vim: set ts=2 noet: */
