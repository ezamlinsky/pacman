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

/* pacman options */
extern char *pmo_root;
/* configuration options */
extern char pmc_syncserver[512];
extern char pmc_syncpath[512];
extern char pmc_syncname[512];

int sync_synctree()
{
	char ldir[PATH_MAX] = "";
	char path[PATH_MAX];
	mode_t oldmask;
	char *str;
	PMList *files = NULL;

	snprintf(ldir, PATH_MAX, "%s%s/%s", pmo_root, PKGDIR, pmc_syncname);

	/* remove the old dir */
	vprint("Removing %s (if it exists)\n", ldir);
	rmrf(ldir);

	/* make the new dir */
	oldmask = umask(0000);
  mkdir(ldir, 0755);
	umask(oldmask);

	/* build out list of one */
	snprintf(path, PATH_MAX, "%s.db.tar.gz", pmc_syncname);
	str = strdup(path);
	files = list_add(files, str);
	if(downloadfiles(pmc_syncserver, pmc_syncpath, ldir, files)) {
		list_free(files);
		return(1);
	}

	/* uncompress the sync database */
	snprintf(path, PATH_MAX, "%s/%s", ldir, (char*)files->data);
	list_free(files);
	vprint("Unpacking %s...\n", path);
	if(unpack(path, ldir)) {
		return(1);
	}

	/* remove the .tar.gz */
	unlink(path);

	return(0);
}

int downloadfiles(char *server, char *remotepath, char *localpath, PMList *files)
{
	int fsz;
	netbuf *control = NULL;
	PMList *lp;
	int ret = 0;

	if(files == NULL) {
		return(0);
	}

	FtpInit();
	if(!FtpConnect(server, &control)) {
		fprintf(stderr, "error: cannot connect to %s\n", server);
		return(1);
	}
	if(!FtpLogin("anonymous", "arch@guest", control)) {
		fprintf(stderr, "error: anonymous login failed\n");
		FtpQuit(control);
		return(1);
	}

	
	if(!FtpChdir(remotepath, control)) {
		fprintf(stderr, "error: could not cwd to %s: %s\n", remotepath,
			FtpLastResponse(control));
		return(1);
	}

	/* get each file in the list */
	for(lp = files; lp; lp = lp->next) {
		char output[PATH_MAX];
		int j;

		snprintf(output, PATH_MAX, "%s/%s", localpath, (char*)lp->data);

		/* passive mode */
		/* TODO: make passive ftp an option */
		if(!FtpOptions(FTPLIB_CONNMODE, FTPLIB_PASSIVE, control)) {
			fprintf(stderr, "warning: failed to set passive mode\n");
		}

		if(!FtpSize((char*)lp->data, &fsz, FTPLIB_IMAGE, control)) {
			fprintf(stderr, "warning: failed to get filesize for %s\n", (char*)lp->data);
		}

		/* set up our progress bar's callback */
		FtpOptions(FTPLIB_CALLBACK, (long)log_progress, control);
		FtpOptions(FTPLIB_IDLETIME, (long)1000, control);
		FtpOptions(FTPLIB_CALLBACKARG, (long)&fsz, control);
		FtpOptions(FTPLIB_CALLBACKBYTES, (10*1024), control);
		
		strncpy(sync_fnm, lp->data, 24);
		for(j = strlen(sync_fnm); j < 24; j++) {
			sync_fnm[j] = ' ';
		}
		sync_fnm[24] = '\0';
		if(!FtpGet(output, lp->data, FTPLIB_IMAGE, control)) {
			fprintf(stderr, "\nerror: could not download %s: %s\n", (char*)lp->data,
				FtpLastResponse(control));
			/* unlink the file */
			unlink(output);
			ret = 1;
		} else {
			log_progress(control, fsz, &fsz);
		}
		printf("\n");
		fflush(stdout);
	}
	
	FtpQuit(control);
	return(ret);
}

static int log_progress(netbuf *ctl, int xfered, void *arg)
{
    int fsz = *(int*)arg;
    int pct = (unsigned int)(xfered * 100) / fsz;
		int i;
		
		printf("%s [", sync_fnm);
		for(i = 0; i < (int)(pct/3); i++) {
			printf("#");
		}
		for(i = (int)(pct/3); i < (int)(100/3); i++) {
			printf(" ");
		}
		printf("] %3d%% | %6dK\r", pct, (xfered/1024));
    fflush(stdout);
    return(1);
}

/* vim: set ts=2 sw=2 noet: */
