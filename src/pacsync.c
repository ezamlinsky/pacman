/*
 *  pacsync.c
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
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/time.h>
#include <ftplib.h>
/* pacman */
#include "list.h"
#include "package.h"
#include "db.h"
#include "util.h"
#include "pacsync.h"
#include "pacman.h"

/* progress bar */
static int log_progress(netbuf *ctl, int xfered, void *arg);
static char sync_fnm[25];
static int offset;
static struct timeval t0, t;
	static float rate;
	static int xfered1;
	static unsigned char eta_h, eta_m, eta_s;

/* pacman options */
extern char *pmo_root;
extern char *pmo_dbpath;
extern char *pmo_proxyhost;

extern unsigned short pmo_proxyport;
extern unsigned short pmo_nopassiveftp;

/* sync servers */
extern PMList *pmc_syncs;
extern int maxcols;

int sync_synctree()
{
	char ldir[PATH_MAX] = "";
	char path[PATH_MAX];
	mode_t oldmask;
	PMList *files = NULL;
	PMList *i;
	int success = 0;

	for(i = pmc_syncs; i; i = i->next) {
		sync_t *sync = (sync_t*)i->data;		
		snprintf(ldir, PATH_MAX, "%s%s", pmo_root, pmo_dbpath);

		/* build a one-element list */
		snprintf(path, PATH_MAX, "%s.db.tar.gz", sync->treename);
		files = list_add(files, strdup(path));

		success = 1;
		if(downloadfiles(sync->servers, ldir, files)) {
			fprintf(stderr, "failed to synchronize %s\n", sync->treename);
			success = 0;
		}
		FREELIST(files);
		snprintf(path, PATH_MAX, "%s/%s.db.tar.gz", ldir, sync->treename);

		if(success) {
			snprintf(ldir, PATH_MAX, "%s%s/%s", pmo_root, pmo_dbpath, sync->treename);
			/* remove the old dir */
			vprint("removing %s (if it exists)\n", ldir);
			rmrf(ldir);

			/* make the new dir */
			oldmask = umask(0000);
			mkdir(ldir, 0755);
			umask(oldmask);

			/* uncompress the sync database */
			vprint("Unpacking %s...\n", path);
			if(unpack(path, ldir)) {
				return(1);
			}
		}
		/* remove the .tar.gz */
		unlink(path);
	}

	return(!success);
}

int downloadfiles(PMList *servers, char *localpath, PMList *files)
{
	int fsz;
	netbuf *control = NULL;
	PMList *lp;
	int done = 0;
	PMList *complete = NULL;
	PMList *i;

	if(files == NULL) {
		return(0);
	}

	for(i = servers; i && !done; i = i->next) {
		server_t *server = (server_t*)i->data;

		if(!strcmp(server->protocol, "ftp") && !pmo_proxyhost) {
			FtpInit();
			vprint("Connecting to %s:21\n", server->server);
			if(!FtpConnect(server->server, &control)) {
				fprintf(stderr, "error: cannot connect to %s\n", server->server);
				continue;
			}
			if(!FtpLogin("anonymous", "arch@guest", control)) {
				fprintf(stderr, "error: anonymous login failed\n");
				FtpQuit(control);
				continue;
			}	
			if(!FtpChdir(server->path, control)) {
				fprintf(stderr, "error: could not cwd to %s: %s\n", server->path,
					FtpLastResponse(control));
				continue;
			}
			if(!pmo_nopassiveftp) {
				if(!FtpOptions(FTPLIB_CONNMODE, FTPLIB_PASSIVE, control)) {
					fprintf(stderr, "warning: failed to set passive mode\n");
				}
			} else {
				vprint("FTP passive mode not set\n");
			}
		/*} else if(!strcmp(server->protocol, "http") || (pmo_proxyhost && !strcmp(server->protocol, "ftp"))) {*/
		} else if(!strcmp(server->protocol, "http") || pmo_proxyhost) {
			char *host;
			unsigned port;
			host = (pmo_proxyhost) ? pmo_proxyhost : server->server;
			port = (pmo_proxyhost) ? pmo_proxyport : 80;
			vprint("Connecting to %s:%u\n", host, port);
			if(!HttpConnect(host, port, &control)) {
				fprintf(stderr, "error: cannot connect to %s\n", host);
				continue;
			}
		}

		/* set up our progress bar's callback (and idle timeout) */
		if(strcmp(server->protocol, "file")) {
			FtpOptions(FTPLIB_CALLBACK, (long)log_progress, control);
			FtpOptions(FTPLIB_IDLETIME, (long)1000, control);
			FtpOptions(FTPLIB_CALLBACKARG, (long)&fsz, control);
			FtpOptions(FTPLIB_CALLBACKBYTES, (10*1024), control);
		}

		/* get each file in the list */
		for(lp = files; lp; lp = lp->next) {
			char output[PATH_MAX];
			int j, filedone = 0;
			char *fn = (char*)lp->data;
			char *ptr;
			struct stat st;

			if(is_in(fn, complete)) {
				continue;
			}

			snprintf(output, PATH_MAX, "%s/%s.part", localpath, fn);
			strncpy(sync_fnm, fn, 24);
			/* drop filename extension */
			ptr = strstr(fn, ".db.tar.gz");
			if(ptr && (ptr-fn) < 24) {
				sync_fnm[ptr-fn] = '\0';
			}
			ptr = strstr(fn, ".pkg.tar.gz");
			if(ptr && (ptr-fn) < 24) {
				sync_fnm[ptr-fn] = '\0';
			}
			for(j = strlen(sync_fnm); j < 24; j++) {
				sync_fnm[j] = ' ';
			}
			sync_fnm[24] = '\0';
			offset = 0;

			/* ETA setup */
			gettimeofday(&t0, NULL);
			t = t0;
			rate = 0;
			xfered1 = 0;
			eta_h = 0;
			eta_m = 0;
			eta_s = 0;

			if(!strcmp(server->protocol, "ftp") && !pmo_proxyhost) {
				if(!FtpSize(fn, &fsz, FTPLIB_IMAGE, control)) {
					fprintf(stderr, "warning: failed to get filesize for %s\n", fn);
				}
				if(!stat(output, &st)) {
					offset = (int)st.st_size;
					if(!FtpRestart(offset, control)) {
						fprintf(stderr, "warning: failed to resume download -- restarting\n");
						/* can't resume: */
						/* unlink the file in order to restart download from scratch */
						unlink(output);
					}
				}
				if(!FtpGet(output, fn, FTPLIB_IMAGE, control)) {
					fprintf(stderr, "\nfailed downloading %s from %s: %s\n",
						fn, server->server, FtpLastResponse(control));
					/* we leave the partially downloaded file in place so it can be resumed later */
				} else {
					filedone = 1;
				}
			/*} else if(!strcmp(server->protocol, "http") || (pmo_proxyhost && !strcmp(server->protocol, "ftp"))) {*/
			} else if(!strcmp(server->protocol, "http") || pmo_proxyhost) {
				char src[PATH_MAX];
				if(!stat(output, &st)) {
					offset = (int)st.st_size;
				}
				if(!pmo_proxyhost) {
					snprintf(src, PATH_MAX, "%s%s", server->path, fn);
				} else {
					snprintf(src, PATH_MAX, "%s://%s%s%s", server->protocol, server->server, server->path, fn);
				}
				if(!HttpGet(server->server, output, src, &fsz, control, offset)) {
					fprintf(stderr, "\nfailed downloading %s from %s: %s\n",
						fn, server->server, FtpLastResponse(control));
					/* we leave the partially downloaded file in place so it can be resumed later */
				} else {
					filedone = 1;
				}
			} else if(!strcmp(server->protocol, "file")) {
				char src[PATH_MAX];
				snprintf(src, PATH_MAX, "%s%s", server->path, fn);
				vprint("copying %s to %s/%s\n", src, localpath, fn);
				/* local repository, just copy the file */
				if(copyfile(src, output)) {
					fprintf(stderr, "failed copying %s\n", src);
				} else {
					filedone = 1;
				}
			}

			if(filedone) {
				char completefile[PATH_MAX];
				if(!strcmp(server->protocol, "file")) {
					char out[56];
					printf(" %s [", sync_fnm);
					strncpy(out, server->path, 33);
					printf("%s", out);
					for(j = strlen(out); j < maxcols-64; j++) {
						printf(" ");
					}
					fputs("] 100% |   LOCAL |", stdout);
				} else {
					log_progress(control, fsz-offset, &fsz);
				}
				complete = list_add(complete, fn);
				/* rename "output.part" file to "output" file */
				snprintf(completefile, PATH_MAX, "%s/%s", localpath, fn);
				rename(output, completefile);
			}
			printf("\n");
			fflush(stdout);
		}
		if(list_count(complete) == list_count(files)) {
			done = 1;
		}

		if(!strcmp(server->protocol, "ftp")) {
			FtpQuit(control);
		} else if(!strcmp(server->protocol, "http")) {
			HttpQuit(control);
		}
	}

	return(!done);
}

static int log_progress(netbuf *ctl, int xfered, void *arg)
{
	int fsz = *(int*)arg;
	int pct = ((float)(xfered+offset) / fsz) * 100;
	int i, cur;
	struct timeval t1;
	float timediff;

	gettimeofday(&t1, NULL);
	if(xfered+offset == fsz) {
		t = t0;
	}
	timediff = t1.tv_sec-t.tv_sec + (float)(t1.tv_usec-t.tv_usec) / 1000000;

	if(xfered+offset == fsz) {
		/* average download rate */
		rate = xfered / (timediff * 1024);
		/* total download time */
		eta_s = (int)timediff;
		eta_h = eta_s / 3600;
		eta_s -= eta_h * 3600;
		eta_m = eta_s / 60;
		eta_s -= eta_m * 60;
	} else if(timediff > 1) {
		/* we avoid computing the rate & ETA on too small periods of time, so that
		   results are more significant */
		rate = (xfered-xfered1) / (timediff * 1024);
		xfered1 = xfered;
		gettimeofday(&t, NULL);
		eta_s = (fsz-(xfered+offset)) / (rate * 1024);
		eta_h = eta_s / 3600;
		eta_s -= eta_h * 3600;
		eta_m = eta_s / 60;
		eta_s -= eta_m * 60;
	}

	printf(" %s [", sync_fnm);
	cur = (int)((maxcols-64)*pct/100);
	for(i = 0; i < maxcols-64; i++) {
		(i < cur) ? printf("#") : printf(" ");
	}
	if(rate > 1000) {
		printf("] %3d%%| %6dK| %6.0fK/s| %02d:%02d:%02d\r", pct, ((xfered+offset) / 1024), rate, eta_h, eta_m, eta_s);
	} else {
		printf("] %3d%%| %6dK| %6.1fK/s| %02d:%02d:%02d\r", pct, ((xfered+offset) / 1024), rate, eta_h, eta_m, eta_s);
	}
	fflush(stdout);
	return(1);
}

/* Test for existance of a package in a PMList* of syncpkg_t*
 * If found, return a pointer to the respective syncpkg_t*
 */
syncpkg_t* find_pkginsync(char *needle, PMList *haystack)
{
	PMList *i;
	syncpkg_t *sync;
	int found = 0;

	for(i = haystack; i && !found; i = i->next) {
		sync = (syncpkg_t*)i->data;
		if(sync && !strcmp(sync->pkg->name, needle)) {
			found = 1;
		}
	}
	if(!found) {
		sync = NULL;
	}

	return sync;
}

/* vim: set ts=2 sw=2 noet: */
