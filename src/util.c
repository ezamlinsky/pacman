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
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <getopt.h>
#include <ctype.h>
#include "list.h"
#include "package.h"
#include "db.h"
#include "util.h"
#include "pacsync.h"
#include "pacman.h"

extern char*          pmo_root;
extern unsigned short pmo_op;
extern unsigned short pmo_version;
extern unsigned short pmo_verbose;
extern unsigned short pmo_help;
extern unsigned short pmo_force;
extern unsigned short pmo_nodeps;
extern unsigned short pmo_upgrade;
extern unsigned short pmo_freshen;
extern unsigned short pmo_nosave;
extern unsigned short pmo_d_vertest;
extern unsigned short pmo_d_resolve;
extern unsigned short pmo_q_isfile;
extern unsigned short pmo_q_info;
extern unsigned short pmo_q_list;
extern unsigned short pmo_q_owns;
extern unsigned short pmo_s_sync;
extern unsigned short pmo_s_search;
extern unsigned short pmo_s_clean;
extern unsigned short pmo_s_upgrade;
extern unsigned short pmo_s_downloadonly;
extern PMList        *pmo_noupgrade;
extern PMList        *pmo_ignorepkg;

extern PMList *pmc_syncs;
extern PMList *pm_targets;

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

tartype_t gztype = {
	(openfunc_t) gzopen_frontend,
	(closefunc_t)gzclose,
	(readfunc_t) gzread,
	(writefunc_t)gzwrite
};

int unpack(char *archive, char *prefix)
{
	TAR *tar = NULL;
	char expath[PATH_MAX];

	/* open the .tar.gz package */
	if(tar_open(&tar, archive, &gztype, O_RDONLY, 0, TAR_GNU) == -1) {
	  perror(archive);
		return(1);
	}
	while(!th_read(tar)) {
		snprintf(expath, PATH_MAX, "%s/%s", prefix, th_get_pathname(tar));
	  if(tar_extract_file(tar, expath)) {
			fprintf(stderr, "could not extract %s: %s\n", th_get_pathname(tar), strerror(errno));
		}
	}
	tar_close(tar);

	return(0);
}

/* Parse command-line arguments for each operation
 *     op:   the operation code requested
 *     argc: argc
 *     argv: argv
 *     
 * Returns: 0 on success, 1 on error
 */
int parseargs(int op, int argc, char **argv)
{
	int opt;
	int option_index = 0;
	static struct option opts[] =
	{
		{"add",        no_argument,       0, 'A'},
		{"remove",     no_argument,       0, 'R'},
		{"upgrade",    no_argument,       0, 'U'},
		{"freshen",    no_argument,       0, 'F'},
		{"query",      no_argument,       0, 'Q'},
		{"sync",       no_argument,       0, 'S'},
		{"deptest",    no_argument,       0, 'T'},
		{"vertest",    no_argument,       0, 'Y'},
		{"resolve",    no_argument,       0, 'D'},
		{"root",       required_argument, 0, 'r'},
		{"verbose",    no_argument,       0, 'v'},
		{"version",    no_argument,       0, 'V'},
		{"help",       no_argument,       0, 'h'},
		{"search",     no_argument,       0, 's'},
		{"clean",      no_argument,       0, 'c'},
		{"force",      no_argument,       0, 'f'},
		{"nodeps",     no_argument,       0, 'd'},
		{"nosave",     no_argument,       0, 'n'},
		{"owns",       no_argument,       0, 'o'},
		{"list",       no_argument,       0, 'l'},
		{"file",       no_argument,       0, 'p'},
		{"info",       no_argument,       0, 'i'},
		{"sysupgrade", no_argument,       0, 'u'},
		{"downloadonly", no_argument,     0, 'w'},
		{"refresh",    no_argument,       0, 'y'},
		{0, 0, 0, 0}
	};

	while((opt = getopt_long(argc, argv, "ARUFQSTDYr:vhscVfnoldpiuwy", opts, &option_index))) {
		if(opt < 0) {
			break;
		}
		switch(opt) {
			case 0:   break;
			case 'A': pmo_op = (pmo_op != PM_MAIN ? 0 : PM_ADD);     break;
			case 'R': pmo_op = (pmo_op != PM_MAIN ? 0 : PM_REMOVE);  break;
			case 'U': pmo_op = (pmo_op != PM_MAIN ? 0 : PM_UPGRADE); break;
			case 'F': pmo_op = (pmo_op != PM_MAIN ? 0 : PM_UPGRADE); pmo_freshen = 1; break;
			case 'Q': pmo_op = (pmo_op != PM_MAIN ? 0 : PM_QUERY);   break;
			case 'S': pmo_op = (pmo_op != PM_MAIN ? 0 : PM_SYNC);    break;
			case 'T': pmo_op = (pmo_op != PM_MAIN ? 0 : PM_DEPTEST); break;
			case 'Y': pmo_op = (pmo_op != PM_MAIN ? 0 : PM_DEPTEST); pmo_d_vertest = 1; break;
			case 'D': pmo_op = (pmo_op != PM_MAIN ? 0 : PM_DEPTEST); pmo_d_resolve = 1; break;
			case 'h': pmo_help = 1; break;
			case 'V': pmo_version = 1; break;
			case 'v': pmo_verbose = 1; break;
			case 'f': pmo_force = 1; break;
			case 'd': pmo_nodeps = 1; break;
			case 'n': pmo_nosave = 1; break;
			case 'l': pmo_q_list = 1; break;
			case 'p': pmo_q_isfile = 1; break;
			case 'i': pmo_q_info = 1; break;
			case 'o': pmo_q_owns = 1; break;
			case 'u': pmo_s_upgrade = 1; break;
			case 'w': pmo_s_downloadonly = 1; break;
			case 'y': pmo_s_sync = 1; break;
			case 's': pmo_s_search = 1; break;
			case 'c': pmo_s_clean = 1; break;
			case 'r': if(realpath(optarg, pmo_root) == NULL) {
									perror("bad root path");
									return(1);
								} break;
			case '?': return(1);
			default:  return(1);
		}
	}

	if(pmo_op == 0) {
		fprintf(stderr, "error: only one operation may be used at a time\n\n");
		return(1);
	}

	if(pmo_help) {
		usage(pmo_op, (char*)basename(argv[0]));
		return(2);
	}
	if(pmo_version) {
		version();
		return(2);
	}

	while(optind < argc) {
		/* add the target to our target array */
		char *s = strdup(argv[optind]);
		pm_targets = list_add(pm_targets, s);
		optind++;
	}

	return(0);
}

#define min(X, Y)  ((X) < (Y) ? (X) : (Y))
int parseconfig(char *configfile)
{
	FILE *fp = NULL;
	char line[PATH_MAX+1];
	char *ptr = NULL;
	char *key = NULL;
	int linenum = 0;
	char section[256] = "";
	sync_t *sync = NULL;

	if((fp = fopen(configfile, "r")) == NULL) {
		perror(configfile);
		return(1);
	}

	while(fgets(line, PATH_MAX, fp)) {
		linenum++;
		trim(line);
		if(strlen(line) == 0) {
			continue;
		}
		if(line[0] == '#') {
			continue;
		}
		if(line[0] == '[' && line[strlen(line)-1] == ']') {
			/* new config section */
			ptr = line;
			ptr++;
			strncpy(section, ptr, min(255, strlen(ptr)-1));
			section[min(255, strlen(ptr)-1)] = '\0';
			vprint("config: new section '%s'\n", section);
			if(!strlen(section)) {
				fprintf(stderr, "config: line %d: bad section name\n", linenum);
				return(1);
			}
			if(!strcmp(section, "local")) {
				fprintf(stderr, "config: line %d: %s is reserved and cannot be used as a package tree\n",
					linenum, section);
				return(1);
			}
			if(strcmp(section, "options")) {
				/* start a new sync record */
				MALLOC(sync, sizeof(sync_t));
				sync->treename = strdup(section);
				sync->servers = NULL;
				pmc_syncs = list_add(pmc_syncs, sync);
			}
		} else {
			/* directive */
			if(!strlen(section)) {
				fprintf(stderr, "config: line %d: all directives must belong to a section\n", linenum);
				return(1);
			}
			ptr = line;
			key = strsep(&ptr, "=");
			if(key == NULL || ptr == NULL) {
				fprintf(stderr, "config: line %d: syntax error\n", linenum);
				return(1);
			} else {
				trim(key);
				key = strtoupper(key);
				trim(ptr);
				if(!strcmp(section, "options")) {
					if(!strcmp(key, "NOUPGRADE")) {
						char *p = ptr;
						char *q;
						while((q = strchr(p, ' '))) {
							*q = '\0';
							pmo_noupgrade = list_add(pmo_noupgrade, strdup(p));
							vprint("config: noupgrade: %s\n", p);
							p = q;
							p++;
						}
						pmo_noupgrade = list_add(pmo_noupgrade, strdup(p));
						vprint("config: noupgrade: %s\n", p);
					} else if(!strcmp(key, "IGNOREPKG")) {
						char *p = ptr;
						char *q;
						while((q = strchr(p, ' '))) {
							*q = '\0';
							pmo_ignorepkg = list_add(pmo_ignorepkg, strdup(p));
							vprint("config: ignorepkg: %s\n", p);
							p = q;
							p++;
						}
						pmo_ignorepkg = list_add(pmo_ignorepkg, strdup(p));
						vprint("config: ignorepkg: %s\n", p);
					} else {
						fprintf(stderr, "config: line %d: syntax error\n", linenum);
						return(1);
					}
				} else {
					if(!strcmp(key, "SERVER")) {
						/* parse our special url */
						server_t *server;
						char *p;

						MALLOC(server, sizeof(server_t));
						server->server = server->path = NULL;
						server->islocal = 0;

						p = strstr(ptr, "://");
						if(p == NULL) {
							fprintf(stderr, "config: line %d: bad server location\n", linenum);
							return(1);
						}
						*p = '\0';
						p++; p++; p++;
						if(p == NULL || *p == '\0') {
							fprintf(stderr, "config: line %d: bad server location\n", linenum);
							return(1);
						}
						server->islocal = !strcmp(ptr, "local");
						if(!server->islocal) {
							char *slash;
							/* no http support yet */
							if(strcmp(ptr, "ftp")) {
								fprintf(stderr, "config: line %d: protocol %s is not supported\n", linenum, ptr);
								return(1);
							}
							/* split the url into domain and path */
							slash = strchr(p, '/');
							if(slash == NULL) {
								/* no path included, default to / */
								server->path = strdup("/");
							} else {
								/* add a trailing slash if we need to */
								if(slash[strlen(slash)-1] == '/') {
									server->path = strdup(slash);
								} else {
									MALLOC(server->path, strlen(slash)+2);
									sprintf(server->path, "%s/", slash);
								}
								*slash = '\0';
							}
							server->server = strdup(p);
						} else {
							/* add a trailing slash if we need to */
							if(p[strlen(p)-1] == '/') {
								server->path = strdup(p);
							} else {
								MALLOC(server->path, strlen(p)+2);
								sprintf(server->path, "%s/", p);
							}
						}
						/* add to the list */
						vprint("config: %s: server: %s %s\n", section, server->server, server->path);
						sync->servers = list_add(sync->servers, server);
					} else {
						fprintf(stderr, "config: line %d: syntax error\n", linenum);
						return(1);
					}
				}
				line[0] = '\0';
			}
		}
	}
	fclose(fp);

	return(0);
}

int copyfile(char *src, char *dest)
{
	FILE *in, *out;
	size_t len;
	char buf[4097];

	in = fopen(src, "r");
	if(in == NULL) {
		return(1);
	}
	out = fopen(dest, "w");
	if(out == NULL) {
		return(1);
	}

	while((len = fread(buf, 1, 4096, in))) {
		fwrite(buf, 1, len, out);
	}

	fclose(in);
	fclose(out);
	return(0);
}

int makepath(char *path)
{
	char *orig, *str, *ptr;
	char full[PATH_MAX] = "";
	mode_t oldmask;

	oldmask = umask(0000);

	orig = strdup(path);
	str = orig;
	while((ptr = strsep(&str, "/"))) {
		if(strlen(ptr)) {
			struct stat buf;

			strcat(full, "/");
			strcat(full, ptr);
			if(stat(full, &buf)) {
			  if(mkdir(full, 0755)) {
					free(orig);
					return(1);
				}
			}
		}
	}
	free(orig);
	umask(oldmask);
	return(0);
}

int rmrf(char *path)
{
	int errflag = 0;
	struct dirent *dp;
	DIR *dirp;
	char name[PATH_MAX];
	extern int errno;

	if(!unlink(path)) {
		return(0);
	} else {
		if (errno == ENOENT) {
			return(0);
		} else if (errno == EPERM) {
			/* fallthrough */
		} else if (errno == EISDIR) {
			/* fallthrough */
		} else if (errno == ENOTDIR) {
			return(1);
		} else {
			/* not a directory */
			return(1);
    }

		if((dirp = opendir(path)) == (DIR *)-1) {
			return(1);
		}
		for(dp = readdir(dirp); dp != NULL; dp = readdir(dirp)) {
			if (dp->d_ino) {
				sprintf(name, "%s/%s", path, dp->d_name);
				if(strcmp(dp->d_name, "..") && strcmp(dp->d_name, ".")) {
					errflag += rmrf(name);
				}
			}
		}
		closedir(dirp);
		if(rmdir(path)) {
			errflag++;
		}
		return(errflag);
	}
	return(0);
}

/* Display usage/syntax for the specified operation.
 *     op:     the operation code requested
 *     myname: basename(argv[0])
 */
void usage(int op, char *myname)
{
	if(op == PM_MAIN) {
	  printf("usage:  %s {-h --help}\n", myname);
	  printf("        %s {-V --version}\n", myname);
	  printf("        %s {-A --add}     [options] <file>\n", myname);
	  printf("        %s {-R --remove}  [options] <package>\n", myname);
	  printf("        %s {-U --upgrade} [options] <file>\n", myname);
	  printf("        %s {-F --freshen} [options] <file>\n", myname);
	  printf("        %s {-Q --query}   [options] [package]\n", myname);
	  printf("        %s {-S --sync}    [options] [package]\n", myname);
		printf("\nuse '%s --help' with other options for more syntax\n\n", myname);
	} else {
		if(op == PM_ADD) {
		  printf("usage:  %s {-A --add} [options] <file>\n", myname);
			printf("options:\n");
			printf("  -f, --force         force install, overwrite conflicting files\n");
			printf("  -d, --nodeps        skip dependency checks\n");
		} else if(op == PM_REMOVE) {
			printf("usage:  %s {-R --remove} [options] <package>\n", myname);
			printf("options:\n");
			printf("  -d, --nodeps        skip dependency checks\n");
			printf("  -n, --nosave        remove configuration files as well\n");
		} else if(op == PM_UPGRADE) {
			if(pmo_freshen) {
				printf("usage:  %s {-F --freshen} [options] <file>\n", myname);
			} else {
				printf("usage:  %s {-U --upgrade} [options] <file>\n", myname);
			}
			printf("options:\n");
			printf("  -f, --force         force install, overwrite conflicting files\n");
			printf("  -d, --nodeps        skip dependency checks\n");
		} else if(op == PM_QUERY) {
			printf("usage:  %s {-Q --query} [options] [package]\n", myname);
			printf("options:\n");
			printf("  -o, --owns <file>   query the package that owns <file>\n");
			printf("  -l, --list          list the contents of the queried package\n");
			printf("  -i, --info          view package information\n");
			printf("  -p, --file          pacman will query the package file [package] instead of\n");
			printf("                      looking in the database\n");
		} else if(op == PM_SYNC) {
			printf("usage:  %s {-S --sync} [options] [package]\n", myname);
			printf("options:\n");
			printf("  -s, --search        search sync database for matching strings\n");
			printf("  -f, --force         force install, overwrite conflicting files\n");
			printf("  -d, --nodeps        skip dependency checks\n");
			printf("  -y, --refresh       download a fresh package sync database from the server\n");
			printf("  -u, --sysupgrade    upgrade all packages that are out of date\n");
			printf("  -w, --downloadonly  download packages, but do not install/upgrade anything\n");
			printf("  -c, --clean         remove packages from cache directory to free up diskspace\n");
		}
		printf("  -v, --verbose       be verbose\n");
		printf("  -r, --root <path>   set an alternate installation root\n");
	}
}

/* Version
 */
void version(void)
{
  printf("\n");
	printf(" .--.                    Pacman v%s\n", PACVER);
	printf("/ _.-' .-.  .-.  .-.     Copyright (C) 2002 Judd Vinet <jvinet@zeroflux.org>\n");
	printf("\\  '-. '-'  '-'  '-'    \n");
	printf(" '--'                    This program may be freely redistributed under\n");
	printf("                         the terms of the GNU GPL\n\n");
}

/* Check verbosity option and, if set, print the
 * string to stdout
 */
int vprint(char *fmt, ...)
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

int yesno(char *fmt, ...)
{
	char response[32];
	va_list args;

	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
	fflush(stdout);
	if(fgets(response, 32, stdin)) {
		trim(response);
		if(!strcasecmp(response, "Y") || !strcasecmp(response, "YES") || !strlen(response)) {
			return(1);
		}
	}
	return(0);
}
 
/* output a string, but wrap words properly with a specified indentation
 */
void indentprint(char *str, int indent)
{
	char *p = str;
	char *cenv = NULL;
	int cols = 80;
	int cidx = indent;

	cenv = getenv("COLUMNS");
	if(cenv) {
		cols = atoi(cenv);
	}

	while(*p) {
		if(*p == ' ') {
			char *next = NULL;
			int len;
			p++;
			if(p == NULL || *p == ' ') continue;
			next = strchr(p, ' ');
			if(next == NULL) {
				next = p + strlen(p);
			}
			len = next - p;
			if(len > (cols-cidx-1)) {
				/* newline */
				int i;
				printf("\n");
				for(i = 0; i < indent; i++) {
					printf(" ");
				}
				cidx = indent;
			} else {
				printf(" ");
				cidx++;
			}
		}
		printf("%c", *p);
		p++;
		cidx++;
	}
}

/* Test for existence of a string in a PMList
 */
int is_in(char *needle, PMList *haystack)
{
	PMList *lp;

	for(lp = haystack; lp; lp = lp->next) {
		if(lp->data && !strcmp(lp->data, needle)) {
			return(1);
		}
	}
	return(0);
}


/* Look for a filename in a pkginfo_t.backup list.  If we find it,
 * then we return the md5 hash (parsed from the same line)
 */
char* needbackup(char* file, PMList *backup)
{
	PMList *lp;

	/* run through the backup list and parse out the md5 hash for our file */
	for(lp = backup; lp; lp = lp->next) {
		char* str = strdup(lp->data);
		char* ptr;
		
		/* tab delimiter */
		ptr = index(str, '\t');
		if(ptr == NULL) {
			FREE(str);
			continue;
		}
		*ptr = '\0';
		ptr++;
		/* now str points to the filename and ptr points to the md5 hash */
		if(!strcmp(file, str)) {
			char *md5 = strdup(ptr);
			FREE(str);
			return(md5);
		}
		FREE(str);
	}
	return(NULL);
}

/* Test for existence of a package in a PMList*
 * of pkginfo_t*
 *
 * returns:  0 for no match
 *           1 for identical match
 *          -1 for name-only match (version mismatch)
 */
int is_pkgin(pkginfo_t *needle, PMList *haystack)
{
	PMList *lp;
	pkginfo_t *info;

	for(lp = haystack; lp; lp = lp->next) {
		info = (pkginfo_t*)lp->data;
		if(info && !strcmp(info->name, needle->name)) {
			if(!strcmp(info->version, needle->version)) {
				return(1);
			}
			return(-1);
		}
	}
	return(0);
}

/* Convert a string to uppercase
 */
char* strtoupper(char *str)
{
	char *ptr = str;

	while(*ptr) {
		(*ptr) = toupper(*ptr);
		ptr++;
	}
	return str;
}

/* Trim whitespace and newlines from a string
 */
char* trim(char *str)
{
	char *pch = str;
	while(isspace(*pch)) {
		pch++;
	}
	if(pch != str) {
		memmove(str, pch, (strlen(pch) + 1));
	}
	
	pch = (char*)(str + (strlen(str) - 1));
	while(isspace(*pch)) {
		pch--;
	}
	*++pch = '\0';

	return str;
}

/* vim: set ts=2 sw=2 noet: */
