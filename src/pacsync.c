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
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <ftplib.h>
/* pacman */
#include "list.h"
#include "package.h"
#include "db.h"
#include "util.h"
#include "pacsync.h"
#include "pacman.h"

static int log_progress(netbuf *ctl, int xfered, void *arg);
static char sync_fnm[25];
static int offset;

/* pacman options */
extern char *pmo_root;
extern unsigned char pmo_nopassiveftp;

/* sync servers */
extern PMList *pmc_syncs;

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
		snprintf(ldir, PATH_MAX, "%s%s", pmo_root, PKGDIR);

		/* build a one-element list */
		snprintf(path, PATH_MAX, "%s.db.tar.gz", sync->treename);
		files = list_add(files, strdup(path));

		success = 1;
		if(downloadfiles(sync->servers, ldir, files)) {
			fprintf(stderr, "failed to synchronize %s\n", sync->treename);
			success = 0;
		}
		/*printf("\n");*/
		list_free(files);
		files = NULL;
		snprintf(path, PATH_MAX, "%s/%s.db.tar.gz", ldir, sync->treename);

		if(success) {
			snprintf(ldir, PATH_MAX, "%s%s/%s", pmo_root, PKGDIR, sync->treename);
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

		if(!server->islocal) {
			FtpInit();
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
		}

		/* get each file in the list */
		for(lp = files; lp; lp = lp->next) {
			char output[PATH_MAX];
			int j;
			char *fn = (char*)lp->data;
			struct stat st;

			if(is_in(fn, complete)) {
				continue;
			}

			snprintf(output, PATH_MAX, "%s/%s.part", localpath, fn);
			strncpy(sync_fnm, lp->data, 24);
			for(j = strlen(sync_fnm); j < 24; j++) {
				sync_fnm[j] = ' ';
			}
			sync_fnm[24] = '\0';

			if(!server->islocal) {
				if(!pmo_nopassiveftp) {
					if(!FtpOptions(FTPLIB_CONNMODE, FTPLIB_PASSIVE, control)) {
						fprintf(stderr, "warning: failed to set passive mode\n");
					}
				} else {
					vprint("FTP passive mode not set\n");
				}
				if(!FtpSize(fn, &fsz, FTPLIB_IMAGE, control)) {
					fprintf(stderr, "warning: failed to get filesize for %s\n", fn);
				}
				offset = 0;
				if(!stat(output, &st)) {
					offset = (int)st.st_size;
				}
				if(offset) {
					if(!FtpRestart(offset, control)) {
						fprintf(stderr, "warning: failed to resume download -- restarting\n");
						/* can't resume: */
						/* unlink the file in order to restart download from scratch */
						unlink(output);
					}
				}
				/* set up our progress bar's callback */
				FtpOptions(FTPLIB_CALLBACK, (long)log_progress, control);
				FtpOptions(FTPLIB_IDLETIME, (long)1000, control);
				FtpOptions(FTPLIB_CALLBACKARG, (long)&fsz, control);
				FtpOptions(FTPLIB_CALLBACKBYTES, (10*1024), control);

				if(!FtpGet(output, lp->data, FTPLIB_IMAGE, control)) {
					fprintf(stderr, "\nfailed downloading %s from %s: %s\n",
						fn, server->server, FtpLastResponse(control));
					/* we leave the partially downloaded file in place so it can be resumed later */
				} else {
					char completefile[PATH_MAX];
					log_progress(control, fsz-offset, &fsz);
					complete = list_add(complete, fn);
					/* rename "output.part" file to "output" file */
					snprintf(completefile, PATH_MAX, "%s/%s", localpath, fn);
					rename(output, completefile);
				}
				printf("\n");
				fflush(stdout);
			} else {
				/* local repository, just copy the file */
				char src[PATH_MAX], dest[PATH_MAX];
				snprintf(src, PATH_MAX, "%s%s", server->path, fn);
				snprintf(dest, PATH_MAX, "%s/%s", localpath, fn);
				vprint("copying %s to %s\n", src, dest);
				if(copyfile(src, dest)) {
					fprintf(stderr, "failed copying %s\n", src);
				} else {
					char out[56];
					printf("%s [", sync_fnm);
					strncpy(out, server->path, 33);
					printf("%s", out);
					for(j = strlen(out); j < 33; j++) {
						printf(" ");
					}
					fputs("] 100% |   LOCAL\n", stdout);
					fflush(stdout);

					complete = list_add(complete, fn);
				}
			}
		}
		if(list_count(complete) == list_count(files)) {
			done = 1;
		}

		if(!server->islocal) {
			FtpQuit(control);
		}
	}

	return(!done);
}

static int log_progress(netbuf *ctl, int xfered, void *arg)
{
	int fsz = *(int*)arg;
	int pct = ((float)(xfered+offset) / fsz) * 100;
	int i;

	printf("%s [", sync_fnm);
	for(i = 0; i < (int)(pct/3); i++) {
		printf("#");
	}
	for(i = (int)(pct/3); i < (int)(100/3); i++) {
		printf(" ");
	}
	printf("] %3d%% | %6dK\r ", pct, ((xfered+offset)/1024));
	fflush(stdout);
	return(1);
}

/* vim: set ts=2 sw=2 noet: */
