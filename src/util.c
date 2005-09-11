/*
 *  util.c
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
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>
#include <zlib.h>
#include <libtar.h>
#include <regex.h>
#include "util.h"

extern unsigned short pmo_verbose;

/* Check verbosity option and, if set, print the
 * string to stdout
 */
void vprint(char *fmt, ...)
{
	va_list args;
	if(pmo_verbose) {
		va_start(args, fmt);
		vprintf(fmt, args);
		va_end(args);
		fflush(stdout);
	}
}

/* borrowed and modified from Per Liden's pkgutils (http://crux.nu) */
long gzopen_frontend(char *pathname, int oflags, int mode)
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

	return (long)gzf;
}

int unpack(char *archive, const char *prefix, const char *fn)
{
	TAR *tar = NULL;
	char expath[PATH_MAX];
	tartype_t gztype = {
		(openfunc_t) gzopen_frontend,
		(closefunc_t)gzclose,
		(readfunc_t) gzread,
		(writefunc_t)gzwrite
	};

	/* open the .tar.gz package */
	if(tar_open(&tar, archive, &gztype, O_RDONLY, 0, TAR_GNU) == -1) {
	  perror(archive);
		return(1);
	}
	while(!th_read(tar)) {
		if(fn && strcmp(fn, th_get_pathname(tar))) {
			if(TH_ISREG(tar) && tar_skip_regfile(tar)) {
				char errorstr[255];
				snprintf(errorstr, 255, "bad tar archive: %s", archive);
				perror(errorstr);
				return(1);
			}
			continue;
		}
		snprintf(expath, PATH_MAX, "%s/%s", prefix, th_get_pathname(tar));
	  if(tar_extract_file(tar, expath)) {
			fprintf(stderr, "could not extract %s: %s\n", th_get_pathname(tar), strerror(errno));
		}
		if(fn) break;
	}
	tar_close(tar);

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

/* does the same thing as 'mkdir -p' */
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
					umask(oldmask);
					return(1);
				}
			}
		}
	}
	free(orig);
	umask(oldmask);
	return(0);
}

/* does the same thing as 'rm -rf' */
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
		if(errno == ENOENT) {
			return(0);
		} else if(errno == EPERM) {
			/* fallthrough */
		} else if(errno == EISDIR) {
			/* fallthrough */
		} else if(errno == ENOTDIR) {
			return(1);
		} else {
			/* not a directory */
			return(1);
    }

		if((dirp = opendir(path)) == (DIR *)-1) {
			return(1);
		}
		for(dp = readdir(dirp); dp != NULL; dp = readdir(dirp)) {
			if(dp->d_ino) {
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

/* Convert a relative path to an absolute path
 *
 * This function was taken from the pathconvert library and massaged
 * to match our coding style.  The pathconvert version is
 * Copyright (c) 1997 Shigio Yamaguchi.
 */
char *rel2abs(const char *path, char *result, const size_t size)
{
	const char *pp, *bp;
	/* endp points the last position which is safe in the result buffer. */
	const char *endp = result + size - 1;
	char *rp;
	int length;
	char base[PATH_MAX+1];

	getcwd(base, PATH_MAX);

	if(*path == '/') {
		if(strlen(path) >= size) {
			goto erange;
		}
		strcpy(result, path);
		goto finish;
	} else if(*base != '/' || !size) {
		errno = EINVAL;
		return (NULL);
	} else if(size == 1) {
		goto erange;
	}

	length = strlen(base);

	if(!strcmp(path, ".") || !strcmp(path, "./")) {
		if(length >= size) {
			goto erange;
		}
		strcpy(result, base);
		/* rp points the last char. */
		rp = result + length - 1;
		/* remove the last '/'. */
		if(*rp == '/') {
			if(length > 1) {
				*rp = 0;
			}
		} else {
			rp++;
		}
		/* rp point NULL char */
		if(*++path == '/') {
			/* Append '/' to the tail of path name. */
			*rp++ = '/';
			if(rp > endp) {
				goto erange;
			}
			*rp = 0;
		}
		goto finish;
	}
	bp = base + length;
	if(*(bp - 1) == '/') {
		--bp;
	}
	/* up to root. */
	for(pp = path; *pp && *pp == '.'; ) {
		if(!strncmp(pp, "../", 3)) {
			pp += 3;
			while(bp > base && *--bp != '/');
		} else if(!strncmp(pp, "./", 2)) {
			pp += 2;
		} else if(!strncmp(pp, "..\0", 3)) {
			pp += 2;
			while(bp > base && *--bp != '/');
		} else {
			break;
		}	
	}
	/* down to leaf. */
	length = bp - base;
	if(length >= size) {
		goto erange;
	}
	strncpy(result, base, length);
	rp = result + length;
	if(*pp || *(pp - 1) == '/' || length == 0) {
		*rp++ = '/';
	}
	if(rp + strlen(pp) > endp) {
		goto erange;
	}
	strcpy(rp, pp);
finish:
	return result;
erange:
	errno = ERANGE;
	return (NULL);
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

	if(*str == '\0')
		/* string is empty, so we're done. */
		return(str);

	while(isspace(*pch)) {
		pch++;
	}
	if(pch != str) {
		memmove(str, pch, (strlen(pch) + 1));
	}

	/* check if there wasn't anything but whitespace in the string. */
	if(*str == '\0') {
		return(str);
	}
	
	pch = (char*)(str + (strlen(str) - 1));
	while(isspace(*pch)) {
		pch--;
	}
	*++pch = '\0';

	return(str);
}

/* A cheap grep for text files, returns 1 if a substring
 * was found in the text file fn, 0 if it wasn't
 */
int grep(const char *fn, const char *needle)
{
	FILE *fp;

	if((fp = fopen(fn, "r")) == NULL) {
		return(0);
	}
	while(!feof(fp)) {
		char line[1024];
		fgets(line, 1024, fp);
		if(feof(fp)) continue;
		if(strstr(line, needle)) {
			fclose(fp);
			return(1);
		}
	}	
	fclose(fp);
	return(0);
}

int reg_match(char *string, char *pattern)
{
	int result;
	regex_t reg;

	regcomp(&reg, pattern, REG_EXTENDED | REG_NOSUB);
	result = regexec(&reg, string, 0, 0, 0);
	regfree(&reg);
	return(!(result));
}

/* vim: set ts=2 sw=2 noet: */
