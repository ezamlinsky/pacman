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
#include <stdarg.h>
#include <stdlib.h>
#include <signal.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
/* pacman */
#include "list.h"
#include "md5.h"
#include "package.h"
#include "db.h"
#include "util.h"
#include "pacsync.h"
#include "pacman.h"

extern tartype_t gztype;

/* other prototypes */
int rpmvercmp(const char *a, const char *b);
char* MDFile(char *);

/*
 * GLOBALS
 *
 */

/* pacman options */
char*          pmo_root       = NULL;
unsigned short pmo_op         = PM_MAIN;
unsigned short pmo_verbose    = 0;
unsigned short pmo_version    = 0;
unsigned short pmo_help       = 0;
unsigned short pmo_force      = 0;
unsigned short pmo_nodeps     = 0;
unsigned short pmo_upgrade    = 0;
unsigned short pmo_nosave     = 0;
unsigned short pmo_vertest    = 0;
unsigned short pmo_q_isfile   = 0;
unsigned short pmo_q_info     = 0;
unsigned short pmo_q_list     = 0;
unsigned short pmo_q_owns     = 0;
unsigned short pmo_s_upgrade  = 0;
unsigned short pmo_s_sync     = 0;
unsigned short pmo_s_search   = 0;
unsigned short pmo_s_clean    = 0;
PMList        *pmo_noupgrade  = NULL;


/* list of sync_t structs for sync locations */
PMList *pmc_syncs = NULL;
/* list of installed packages */
PMList *pm_packages = NULL;
/* list of targets specified on command line */
PMList *pm_targets  = NULL;

char *lckfile = "/tmp/pacman.lck";

int main(int argc, char *argv[])
{
	int ret = 0;
	char *ptr    = NULL;
	char *dbpath = NULL;
	char path[PATH_MAX];	
	pacdb_t *db_local = NULL;
	PMList *lp;

	/* default root */
	MALLOC(pmo_root, PATH_MAX);
	strcpy(pmo_root, "/");

	if(argc < 2) {
		usage(PM_MAIN, (char*)basename(argv[0]));
		return(0);
	}

	ret = parseargs(PM_ADD, argc, argv);
	if(ret) {
		return(ret);
	}

	/* check for permission */
	if(pmo_op != PM_MAIN && pmo_op != PM_QUERY && pmo_op != PM_DEPTEST) {
		if(pmo_op == PM_SYNC && pmo_s_search) {
			/* special case:  PM_SYNC can be used w/ pmo_s_search by any user */
		} else {
			uid_t uid = geteuid();
			if(uid != 0) {
				fprintf(stderr, "error: you cannot perform this operation unless you are root.\n");
				return(1);
			}
		}
	}

	vprint("Installation Root: %s\n", pmo_root);
	if(pm_targets) {
		vprint("Targets:\n");
		for(lp = pm_targets; lp; lp = lp->next) {
			vprint("  %s\n", lp->data);
		}
	}

	/* lock */
	if(lckmk(lckfile, 1, 1) == -1) {
		fprintf(stderr, "error: unable to lock pacman database.\n");		
		fprintf(stderr, "       if you're sure pacman is not already running, you\n");
		fprintf(stderr, "       can remove %s\n", lckfile);
		return(127);
	}

	/* set signal handlers */
	signal(SIGINT, cleanup);
	signal(SIGTERM, cleanup);

	/* parse the system-wide config file */
	snprintf(path, PATH_MAX, "/%s", PACCONF);
	if(parseconfig(path)) {
		cleanup(1);
	}

	/* check for db existence */
	/* add a trailing '/' if there isn't one */
	if(pmo_root[strlen(pmo_root)-1] != '/') {
		MALLOC(ptr, strlen(pmo_root)+1);
		strcpy(ptr, pmo_root);
		strcat(ptr, "/");
		FREE(pmo_root);
		pmo_root = ptr;
	}
	/* db location */
	MALLOC(dbpath, PATH_MAX);
	snprintf(dbpath, PATH_MAX-1, "%s%s", pmo_root, PKGDIR);
	vprint("Top-level DB Path:  %s\n", dbpath);

	db_local = db_open(dbpath, "local");
	if(db_local == NULL) {
		/* couldn't open the db directory - try creating it */
		char path[PATH_MAX];

		snprintf(path, PATH_MAX, "%s/local", dbpath);
		vprint("initializing database...\n", path);
	  ret = makepath(path);

		if(ret) {
			fprintf(stderr, "error: could not create database.\n");
			cleanup(1);
		}
		if((db_local = db_open(dbpath, "local")) == NULL) {
			fprintf(stderr, "error: could not open database.\n");
			cleanup(1);
		}
	}
	/* load pm_packages cache */
	pm_packages = db_loadpkgs(db_local, pm_packages);

	/* start the requested operation */
	switch(pmo_op) {
		case PM_ADD:     ret = pacman_add(db_local, pm_targets);     break;
		case PM_REMOVE:  ret = pacman_remove(db_local, pm_targets);  break;
		case PM_UPGRADE: ret = pacman_upgrade(db_local, pm_targets); break;
		case PM_QUERY:   ret = pacman_query(db_local, pm_targets);   break;
		case PM_SYNC:    ret = pacman_sync(db_local, pm_targets);    break;
		case PM_DEPTEST: ret = pacman_deptest(db_local, pm_targets); break;
		case PM_MAIN:    ret = 0; break;
		default: fprintf(stderr, "error: no operation specified (use -h for help)\n\n");
						 ret = 1;
	}
	db_close(db_local);
	FREE(pmo_root);
	FREE(dbpath);
	cleanup(ret);
	/* not reached */
	return(0);
}

int pacman_deptest(pacdb_t *db, PMList *targets)
{
	PMList *list = NULL;
	PMList *lp, *deps;
	pkginfo_t *dummy;

	if(pmo_vertest) {
		if(targets && targets->data && targets->next && targets->next->data) {
			int ret = rpmvercmp(targets->data, targets->next->data);
			printf("%d\n", ret);
			return(ret);
		}
		return(0);
	}

	dummy = newpkg();
	sprintf(dummy->name, "_dummy_");
	sprintf(dummy->version, "1.0-1");
	for(lp = targets; lp; lp = lp->next) {
		if(lp->data == NULL) continue;
		dummy->depends = list_add(dummy->depends, lp->data);
	}
	list = list_add(list, dummy);
	deps = checkdeps(db, PM_ADD, list);
	freepkg(dummy);
	list->data = NULL;
	list_free(list);

	if(deps) {
		for(lp = deps; lp; lp = lp->next) {
			depmissing_t *miss = (depmissing_t*)lp->data;
			if(miss->type == CONFLICT) {
				printf("conflict: %s\n", miss->depend.name);
			} else if(miss->type == DEPEND || miss->type == REQUIRED) {
				printf("requires: %s", miss->depend.name);
				switch(miss->depend.mod) {
					case DEP_EQ:  printf("=%s",  miss->depend.version); break;
					case DEP_GE:  printf(">=%s", miss->depend.version); break;
					case DEP_LE:  printf("<=%s", miss->depend.version); break;
				}
				printf("\n");
			}
			FREE(miss);
			lp->data = NULL;
		}
		FREE(deps);
		return(127);
	}
	return(0);
}

int pacman_sync(pacdb_t *db, PMList *targets)
{
	char dbpath[PATH_MAX];
	int allgood = 1, confirm = 0;
	int cols;
	PMList *i, *j, *k;
	PMList *final = NULL;
	PMList *trail = NULL;
	PMList *databases = NULL;

	if(!list_count(pmc_syncs)) {
		fprintf(stderr, "error: no usable package repositories configured.\n");
		return(1);
	}

	if(pmo_s_clean) {
		mode_t oldmask;

		printf("removing packages from cache... ");
		if(rmrf("/var/cache/pacman/pkg")) {
			fprintf(stderr, "error: could not remove cache directory: %s\n", strerror(errno));
			return(1);
		}

		oldmask = umask(0000);
	  if(mkdir("/var/cache/pacman", 0755) && mkdir("/var/cache/pacman/pkg", 0755)) {				
			fprintf(stderr, "error: could not create new cache directory: %s\n", strerror(errno));
			return(1);
		}
		umask(oldmask);

		printf("done.\n");
		return(0);
	}
	
	if(pmo_s_sync && !pmo_s_search) {
		/* grab a fresh package list */
		printf(":: Synchronizing package databases... \n");
		sync_synctree();
	}

	/* open the database(s) */
	for(i = pmc_syncs; i; i = i->next) {
		pacdb_t *db_sync = NULL;
		dbsync_t *dbs = NULL;
		sync_t *sync = (sync_t*)i->data;

		snprintf(dbpath, PATH_MAX, "%s%s", pmo_root, PKGDIR);
		db_sync = db_open(dbpath, sync->treename);
		if(db_sync == NULL) {
			fprintf(stderr, "error: could not open sync database: %s\n", sync->treename);
			fprintf(stderr, "       have you used --refresh yet?\n");
			return(1);
		}
		MALLOC(dbs, sizeof(dbsync_t));
		dbs->sync = sync;
		dbs->db = db_sync;
		/* cache packages */
		dbs->pkgcache = NULL;
		dbs->pkgcache = db_loadpkgs(db_sync, dbs->pkgcache);
		databases = list_add(databases, dbs);
	}

	final = list_new();
	trail = list_new();

	if(pmo_s_search) {
		/* search sync databases */
		for(i = targets; i; i = i->next) {
			char *targ = strdup(i->data);
			strtoupper(targ);
			for(j = databases; j; j = j->next) {
				dbsync_t *dbs = (dbsync_t*)j->data;
				for(k = dbs->pkgcache; k; k = k->next) {
					pkginfo_t *pkg = (pkginfo_t*)k->data;
					char *haystack;
					/* check name */
					haystack = strdup(pkg->name);
					strtoupper(haystack);
					if(strstr(haystack, targ)) {
						printf("%s %s\n", pkg->name, pkg->version);
					} else {
						/* check description */
						FREE(haystack);
						haystack = strdup(pkg->desc);
						strtoupper(haystack);
						if(strstr(haystack, targ)) {
							printf("%s %s\n", pkg->name, pkg->version);
						}
					}
					FREE(haystack);
				}
			}
			FREE(targ);
		}
	} else if(pmo_s_upgrade) {
		int newer = 0;
		for(i = pm_packages; i && allgood; i = i->next) {
			int cmp, found = 0;
			pkginfo_t *local = (pkginfo_t*)i->data;
			syncpkg_t *sync = NULL;
			MALLOC(sync, sizeof(syncpkg_t));

			for(j = databases; !found && j; j = j->next) {
				dbsync_t *dbs = (dbsync_t*)j->data;
				for(k = dbs->pkgcache; !found && k; k = k->next) {
					pkginfo_t *pkg = (pkginfo_t*)k->data;
					if(!strcmp(local->name, pkg->name)) {
						found = 1;
						sync->pkg = pkg;
						sync->dbs = dbs;
					}
				}
			}
			if(!found) {
				/*fprintf(stderr, "%s: not found in sync db.  skipping.", local->name);*/
				continue;
			}
			/* compare versions and see if we need to upgrade */
			cmp = rpmvercmp(local->version, sync->pkg->version);
			if(cmp > 0) {
				/* local version is newer */
				fprintf(stderr, ":: %s-%s: local version is newer\n",
					local->name, local->version);
				newer = 1;
				continue;
			} else if(cmp == 0) {
				/* versions are identical */
				continue;
			} else {
				/* re-fetch the package record with dependency info */
				sync->pkg = db_scan(sync->dbs->db, sync->pkg->name, INFRQ_DESC | INFRQ_DEPENDS);
				/* add to the targets list */
				if(!list_isin(final, sync)) {
					allgood = !resolvedeps(db, databases, sync, final, trail);
					/* check again, as resolvedeps could have added our target for us */
					if(!list_isin(final, sync)) {
						final = list_add(final, sync);
					}
				}
			}
		}
		if(newer) {
			fprintf(stderr, ":: Above packages will be skipped.  To manually upgrade use 'pacman -S <pkg>'\n");
		}
	} else {
		/* process targets */
		for(i = targets; i && allgood; i = i->next) {
			if(i->data) {
				int cmp, found = 0;
				pkginfo_t *local;
				syncpkg_t *sync = NULL;
				MALLOC(sync, sizeof(syncpkg_t));

				local = db_scan(db, (char*)i->data, INFRQ_DESC);
				//sync = db_scan(db_sync, (char*)i->data, INFRQ_DESC | INFRQ_DEPENDS);
				for(j = databases; !found && j; j = j->next) {
					dbsync_t *dbs = (dbsync_t*)j->data;
					for(k = dbs->pkgcache; !found && k; k = k->next) {
						pkginfo_t *pkg = (pkginfo_t*)k->data;
						if(!strcmp((char*)i->data, pkg->name)) {
							found = 1;
							sync->dbs = dbs;
							/* re-fetch the package record with dependency info */
							sync->pkg = db_scan(sync->dbs->db, pkg->name, INFRQ_DESC | INFRQ_DEPENDS);
							if(sync->pkg == NULL) {
								found = 0;
							}
						}
					}
				}
				if(!found) {
					fprintf(stderr, "%s: not found in sync db\n", (char*)i->data);
					continue;
				}
				if(local) {
					/* this is an upgrade, compare versions and determine if it is necessary */
					cmp = rpmvercmp(local->version, sync->pkg->version);
					if(cmp > 0) {
						/* local version is newer - get confirmation first */
						if(!yesno(":: %s-%s: local version is newer.  Upgrade anyway? [Y/n] ", local->name, local->version)) {
							continue;
						}
					} else if(cmp == 0) {
						/* versions are identical */
						if(!yesno(":: %s-%s: is up to date.  Upgrade anyway? [Y/n] ", local->name, local->version)) {
							continue;
						}
					}
				}
				/* add to targets list */
				found = 0;
				for(j = final; j; j = j->next) {
					syncpkg_t *tmp = (syncpkg_t*)j->data;
					if(tmp && !strcmp(tmp->pkg->name, sync->pkg->name)) {
						found = 1;
					}
				}
				if(!found) {
					allgood = !resolvedeps(db, databases, sync, final, trail);
					/* check again, as resolvedeps could have added our target for us */
					found = 0;
					for(j = final; j; j = j->next) {
						syncpkg_t *tmp = (syncpkg_t*)j->data;
						if(tmp && !strcmp(tmp->pkg->name, sync->pkg->name)) {
							found = 1;
						}
					}
					if(!found) {
						final = list_add(final, sync);
					}
				}
			}
		}
	}

	if(allgood) {
		/* check for inter-conflicts and whatnot */
		PMList *deps = NULL;
		PMList *list = NULL;

		for(i = final; i; i = i->next) {
			syncpkg_t *s = (syncpkg_t*)i->data;
			if(s) {
				list = list_add(list, s->pkg);
			}
		}
		deps = checkdeps(db, PM_UPGRADE, list);
		if(deps) {
			fprintf(stderr, "error: unresolvable conflicts/dependencies:\n");
			for(i = deps; i; i = i->next) {
				depmissing_t *miss = (depmissing_t*)i->data;
				if(miss->type == CONFLICT) {
					fprintf(stderr, "  %s: conflicts with %s\n", miss->target, miss->depend.name);
				} else if(miss->type == DEPEND || miss->type == REQUIRED) {
					fprintf(stderr, "  %s: requires %s", miss->target, miss->depend.name);
					switch(miss->depend.mod) {
						case DEP_EQ:  fprintf(stderr, "=%s",  miss->depend.version); break;
						case DEP_GE:  fprintf(stderr, ">=%s", miss->depend.version); break;
						case DEP_LE:  fprintf(stderr, "<=%s", miss->depend.version); break;
					}
					if(miss->type == DEPEND) {
						fprintf(stderr, " but it is not in the sync db\n");
					} else {
						fprintf(stderr, "\n");
					}
				}
				FREE(miss);
				i->data = NULL;
			}
			list_free(deps);
			/* abort mission */
			allgood = 0;
		}
		/* cleanup */
		for(i = list; i; i = i->next) {
			i->data = NULL;
		}
		list_free(list);
		list = NULL;

		/* list targets */
		if(final && final->data) {
			fprintf(stderr, "\nTargets: ");
		}
		cols = 9;
		for(i = final; allgood && i; i = i->next) {
			syncpkg_t *s = (syncpkg_t*)i->data;
			if(s && s->pkg) {
				char t[PATH_MAX];
				int len;
				snprintf(t, PATH_MAX, "%s-%s ", s->pkg->name, s->pkg->version);
				len = strlen(t);
				if(len+cols > 78) {
					cols = 9;
					fprintf(stderr, "\n%9s", " ");
				}
				fprintf(stderr, "%s", t);
				cols += len;
			}
		}
		printf("\n");

		/* get confirmation */
		confirm = 0;
		if(allgood && final && final->data) {
			confirm = yesno("\nDo you want to install/upgrade these packages? [Y/n] ");
		}
	}

	if(allgood && confirm && final && final->data) {
		char ldir[PATH_MAX];
		int varcache = 1;
		int done = 0;
		int count = 0;
		sync_t *current = NULL;
		PMList *processed = NULL;
		PMList *files = NULL;

		/* group sync records by repository and download */
		while(!done) {
			if(current) {
				processed = list_add(processed, current);
				current = NULL;
			}
			for(i = final; i; i = i->next) {
				syncpkg_t *sync = (syncpkg_t*)i->data;
				if(current == NULL) {
					/* we're starting on a new repository */
					if(!list_isin(processed, sync->dbs->sync)) {
						current = sync->dbs->sync;
					}
				}
				if(current && !strcmp(current->treename, sync->dbs->sync->treename)) {
					struct stat buf;
					char path[PATH_MAX];

					snprintf(path, PATH_MAX, "%svar/cache/pacman/pkg/%s-%s.pkg.tar.gz",
						pmo_root, sync->pkg->name, sync->pkg->version);
					if(stat(path, &buf)) {
						/* file is not in the cache dir, so add it to the list */
						snprintf(path, PATH_MAX, "%s-%s.pkg.tar.gz", sync->pkg->name, sync->pkg->version);					
						files = list_add(files, strdup(path));
					} else {
						count++;
					}
				}
			}
			snprintf(ldir, PATH_MAX, "%svar/cache/pacman/pkg", pmo_root);

			if(files) {
				struct stat buf;

				printf("\n:: Retrieving packages from %s...\n", current->treename);
				fflush(stdout);
				if(stat(ldir, &buf)) {
					mode_t oldmask;
					char parent[PATH_MAX];

					/* no cache directory.... try creating it */
					snprintf(parent, PATH_MAX, "%svar/cache/pacman", pmo_root);
					fprintf(stderr, "warning: no %s cache exists.  creating...\n", ldir);
					oldmask = umask(0000);
					mkdir(parent, 0755);
					if(mkdir(ldir, 0755)) {				
						/* couldn't mkdir the cache directory, so fall back to /tmp and unlink
						 * the package afterwards.
						 */
						fprintf(stderr, "warning: couldn't create package cache, using /tmp instead\n");
						snprintf(ldir, PATH_MAX, "/tmp");
						varcache = 0;
					}
					umask(oldmask);
				}
				if(downloadfiles(current->servers, ldir, files)) {
					fprintf(stderr, "error: failed to retrieve some files from %s.\n", current->treename);
					allgood = 0;
				}
				count += list_count(files);
				list_free(files);
				files = NULL;
			}
			if(count == list_count(final)) {
				done = 1;
			}
		}
		printf("\n");

		/* double-check */
		if(files) {
			list_free(files);
			files = NULL;
		}
		/* install targets */
		for(i = final; allgood && i; i = i->next) {
			char *str;
			syncpkg_t *sync = (syncpkg_t*)i->data;
			if(sync->pkg) {
				MALLOC(str, PATH_MAX);
				snprintf(str, PATH_MAX, "%s/%s-%s.pkg.tar.gz", ldir, sync->pkg->name, sync->pkg->version);
				files = list_add(files, str);
			}
		}
		if(allgood) {
			pacman_upgrade(db, files);
		}

		if(!varcache) {
			/* delete packages */
			for(i = files; i; i = i->next) {
				unlink(i->data);
			}
		}
	}

	/* cleanup */
	for(i = final; i; i = i->next) {
		syncpkg_t *sync = (syncpkg_t*)i->data;
		if(sync) freepkg(sync->pkg);
		free(sync);
		i->data = NULL;
	}
	for(i = trail; i; i = i->next) {
		/* this list used the same pointers as final, so they're already freed */
		i->data = NULL;
	}
	for(i = databases; i; i = i->next) {
		dbsync_t *dbs = (dbsync_t*)i->data;
		db_close(dbs->db);
		list_free(dbs->pkgcache);
		free(dbs);
		i->data = NULL;
	}
	list_free(databases);
	list_free(final);
	list_free(trail);
	return(!allgood);
}

int pacman_add(pacdb_t *db, PMList *targets)
{
	int i, ret = 0, errors = 0;
	TAR *tar = NULL;
	char expath[PATH_MAX];
	char pm_install[PATH_MAX];
	pkginfo_t *info = NULL;
	struct stat buf;
	PMList *targ, *file, *lp, *j;
	PMList *alltargs = NULL;
	PMList *filenames = NULL;
	unsigned short real_pmo_upgrade;

	if(targets == NULL) {
		return(0);
	}

	vprint("Loading all target data...\n");
	for(targ = targets; targ; targ = targ->next) {
		/* Populate the package struct */
		vprint("  %s\n", (char*)targ->data);
		info = load_pkg((char*)targ->data, 0);
		if(info == NULL) {
			return(1);
		}
		alltargs = list_add(alltargs, info);
		filenames = list_add(filenames, strdup(targ->data));
	}

	if(!pmo_nodeps) {
		vprint("Checking dependencies...\n");
		lp = checkdeps(db, (pmo_upgrade ? PM_UPGRADE : PM_ADD), alltargs);
		if(lp) {
			fprintf(stderr, "error: unsatisfied dependencies:\n");
			for(j = lp; j; j = j->next) {
				depmissing_t* miss = (depmissing_t*)j->data;
				printf("  %s: ", miss->target);
				if(miss->type == DEPEND || miss->type == REQUIRED) {
					printf("requires %s", miss->depend.name);
					switch(miss->depend.mod) {
						case DEP_EQ: printf("=%s", miss->depend.version);  break;
						case DEP_GE: printf(">=%s", miss->depend.version); break;
						case DEP_LE: printf("<=%s", miss->depend.version); break;
					}
					printf("\n");
				} else if(miss->type == CONFLICT) {
					printf("conflicts with %s\n", miss->depend.name);
				}
			}
			list_free(lp);
			return(1);
		}
		list_free(lp);
	}

	if(!pmo_force) {
		printf("checking for conflicts... ");
		fflush(stdout);
		lp = db_find_conflicts(db, alltargs);
		if(lp) {
			printf("\nerror: the following file conflicts were found:\n");
			for(j = lp; j; j = j->next) {
				printf("  %s\n", (char*)j->data);
			}
			printf("\n");
			list_free(lp);
			return(1);
		}
		printf("done.\n");
		list_free(lp);
	}

	/* this can get modified in the next for loop, so we reset it on each iteration */
	real_pmo_upgrade = pmo_upgrade;

	for(targ = alltargs, file = filenames; targ && file; targ = targ->next, file = file->next) {
		pkginfo_t* oldpkg = NULL;
		info = (pkginfo_t*)targ->data;
		errors = 0;

		pmo_upgrade = real_pmo_upgrade;
		/* check for an already installed package */
		if(!pmo_upgrade && is_pkgin(info, pm_packages)) {
			fprintf(stderr, "error: %s is already installed. (try --upgrade)\n", info->name);
			continue;
		}

		/* see if this is an upgrade.  if so, remove the old package first */
		if(pmo_upgrade) {
			if(is_pkgin(info, pm_packages)) {
				PMList* tmp = list_new();
				int retcode;

				printf("upgrading %s... ", info->name);
				/* we'll need the full record for backup checks later */
				oldpkg = db_scan(db, info->name, INFRQ_ALL);

				list_add(tmp, strdup(info->name));
				vprint("removing old package first...\n");
				retcode = pacman_remove(db, tmp);
				list_free(tmp);
				/* reload package cache */
				pm_packages = db_loadpkgs(db, pm_packages);
				if(retcode == 1) {
					fprintf(stderr, "\nupgrade aborted.\n");
					return(1);
				}
			} else {
				/* no previous package version is installed, so this is actually just an
				 * install
				 */
				pmo_upgrade = 0;
			}
		}
		if(!pmo_upgrade) {
			printf("installing %s... ", info->name);
		}
		fflush(stdout);

		/* open the .tar.gz package */
		if(tar_open(&tar, (char*)file->data, &gztype, O_RDONLY, 0, TAR_GNU) == -1) {
		  perror("could not open package");
			return(1);
		}
		vprint("extracting files...\n");
		for(i = 0; !th_read(tar); i++) {
			int nb = 0;
			int notouch = 0;
			char *md5_orig = NULL;
			char pathname[PATH_MAX];
			strncpy(pathname, th_get_pathname(tar), PATH_MAX);

			if(!strcmp(pathname, ".PKGINFO")) {
				tar_skip_regfile(tar);
				continue;
			}

			if(!strcmp(pathname, "._install")) {
				/* the install script goes inside the db */
				snprintf(expath, PATH_MAX, "%s%s/%s/%s-%s/install", pmo_root,
									PKGDIR, db->treename, info->name, info->version);
			} else {
				/* build the new pathname relative to pmo_root */
				snprintf(expath, PATH_MAX, "%s%s", pmo_root, pathname);
			}

			if(!stat(expath, &buf) && !S_ISDIR(buf.st_mode)) {
				/* file already exists */
				if(is_in(pathname, pmo_noupgrade)) {
					notouch = 1;
				} else {
					if(!pmo_upgrade) {
						nb = is_in(pathname, info->backup);
					} else {
						/* op == PM_UPGRADE */
						md5_orig = needbackup(pathname, oldpkg->backup);
						if(md5_orig) {
							nb = 1;
						}
					}
				}
			}

			if(nb) {
				char *temp;
				char *md5_local, *md5_pkg;

				md5_local = MDFile(expath);
				/* extract the package's version to a temporary file and md5 it */
				temp = strdup("/tmp/pacman_XXXXXX");
				mkstemp(temp);
			  if(tar_extract_file(tar, temp)) {
					fprintf(stderr, "could not extract %s: %s\n", pathname, strerror(errno));
					errors++;
					continue;
				}
				md5_pkg = MDFile(temp);
				/* append the new md5 hash to it's respective entry in info->backup
				 * (it will be the new orginal)
				 */
				for(lp = info->backup; lp; lp = lp->next) {
					char *fn;

					if(!lp->data) continue;
					if(!strcmp((char*)lp->data, pathname)) {
						/* 32 for the hash, 1 for the terminating NULL, and 1 for the tab delimiter */
						MALLOC(fn, strlen(lp->data)+34);
						sprintf(fn, "%s\t%s", (char*)lp->data, md5_pkg);
						free(lp->data);
						lp->data = fn;
					}
				}

				vprint("checking md5 hashes for %s\n", expath);
				vprint("  current:  %s\n", md5_local);
				vprint("  new:      %s\n", md5_pkg);
				if(md5_orig) {
					vprint("  original: %s\n", md5_orig);
				}

				if(!pmo_upgrade) {
					/* PM_ADD */

					/* if a file already exists with a different md5 hash,
					 * then we rename it to a .pacorig extension and continue */
					if(strcmp(md5_local, md5_pkg)) {
						char newpath[PATH_MAX];
						snprintf(newpath, PATH_MAX, "%s.pacorig", expath);
						if(rename(expath, newpath)) {
							fprintf(stderr, "error: could not rename %s: %s\n", expath, strerror(errno));
						}
						if(copyfile(temp, expath)) {
							fprintf(stderr, "error: could not copy %s to %s: %s\n", temp, expath, strerror(errno));
							errors++;
						} else {
							fprintf(stderr, "warning: %s saved as %s\n", expath, newpath);
						}
					}
				} else if(md5_orig) {					
					/* PM_UPGRADE */
					int installnew = 0;

					/* the fun part */
					if(!strcmp(md5_orig, md5_local)) {
						if(!strcmp(md5_local, md5_pkg)) {
							vprint("  action: installing new file\n");
							installnew = 1;
						} else {
							vprint("  action: installing new file\n");
							installnew = 1;
						}
					} else if(!strcmp(md5_orig, md5_pkg)) {
						vprint("  action: leaving existing file in place\n");
					} else if(!strcmp(md5_local, md5_pkg)) {
						vprint("  action: installing new file\n");
						installnew = 1;
					} else {
						char newpath[PATH_MAX];
						vprint("  action: saving current file and installing new one\n");
						installnew = 1;
						snprintf(newpath, PATH_MAX, "%s.pacsave", expath);
						if(rename(expath, newpath)) {
							fprintf(stderr, "error: could not rename %s: %s\n", expath, strerror(errno));
						} else {
							fprintf(stderr, "warning: %s saved as %s\n", expath, newpath);
						}
					}

					if(installnew) {
						/*vprint("  %s\n", expath);*/
						if(copyfile(temp, expath)) {
							fprintf(stderr, "error: could not copy %s to %s: %s\n", temp, expath, strerror(errno));
							errors++;
						}
					}
				}

				FREE(md5_local);
				FREE(md5_pkg);
				FREE(md5_orig);
				unlink(temp);
				FREE(temp);
			} else {
				if(!notouch) {
					/*vprint("  %s\n", expath);*/
				} else {
					vprint("%s is in NoUpgrade - skipping\n", pathname);
					strncat(expath, ".pacnew", PATH_MAX);
					fprintf(stderr, "warning: extracting %s%s as %s\n", pmo_root, pathname, expath);
					/*tar_skip_regfile(tar);*/
				}
			  if(tar_extract_file(tar, expath)) {
					fprintf(stderr, "could not extract %s: %s\n", pathname, strerror(errno));
					errors++;
				}
				/* calculate an md5 hash if this is in info->backup */
				for(lp = info->backup; lp; lp = lp->next) {
					char *fn, *md5;
					char path[PATH_MAX];

					if(!lp->data) continue;
					if(!strcmp((char*)lp->data, pathname)) {
						snprintf(path, PATH_MAX, "%s%s", pmo_root, (char*)lp->data);
						md5 = MDFile(path);
						/* 32 for the hash, 1 for the terminating NULL, and 1 for the tab delimiter */
						MALLOC(fn, strlen(lp->data)+34);
						sprintf(fn, "%s\t%s", (char*)lp->data, md5);
						free(lp->data);
						lp->data = fn;
					}
				}
			}
		}
		tar_close(tar);
		if(errors) {
			ret = 1;
			fprintf(stderr, "error installing %s - skipping db update for this package\n", info->name);
		} else {
			time_t t = time(NULL);

			/* if this is an upgrade then propagate the old package's requiredby list over to
			 * the new package */
			if(pmo_upgrade && oldpkg) {
				list_free(info->requiredby);
				info->requiredby = NULL;
				for(lp = oldpkg->requiredby; lp; lp = lp->next) {
					char *s = strdup(lp->data);
					info->requiredby = list_add(info->requiredby, s);
				}
			}

			vprint("Updating database...");
			/* make an install date (in UTC) */
			strncpy(info->installdate, asctime(gmtime(&t)), sizeof(info->installdate));
			if(db_write(db, info)) {
				fprintf(stderr, "error updating database for %s!\n", info->name);
				return(1);
			}
			vprint("done.\n");

			/* update dependency packages' REQUIREDBY fields */
			for(lp = info->depends; lp; lp = lp->next) {
				pkginfo_t *depinfo = NULL;
				depend_t depend;

				if(splitdep(lp->data, &depend)) {
					continue;
				}
				depinfo = db_scan(db, depend.name, INFRQ_ALL);
				if(depinfo == NULL) {
					continue;
				}
				depinfo->requiredby = list_add(depinfo->requiredby, strdup(info->name));
				db_write(db, depinfo);
				freepkg(depinfo);
			}
			printf("done.\n"); fflush(stdout);

			/* run the post-install script if it exists  */
			snprintf(pm_install, PATH_MAX, "%s%s/%s/%s-%s/install", pmo_root, PKGDIR, db->treename, info->name, info->version);
			if(!stat(pm_install, &buf)) {
				char cmdline[PATH_MAX+1];
				snprintf(pm_install, PATH_MAX, "%s/%s/%s-%s/install", PKGDIR, db->treename, info->name, info->version);
				vprint("Executing post-install script...\n");
				snprintf(cmdline, PATH_MAX, "chroot %s /bin/sh %s post_%s %s %s", pmo_root, pm_install,
					(pmo_upgrade ? "upgrade" : "install"), info->version, (pmo_upgrade ? oldpkg->version : ""));
				system(cmdline);
			}
		}

		freepkg(oldpkg);
	}	

	/* clean up */
	for(lp = alltargs; lp; lp = lp->next) {
		freepkg((pkginfo_t*)lp->data);
		lp->data = NULL;
	}
	list_free(alltargs);
	list_free(filenames);

	/* run ldconfig if it exists */
	snprintf(expath, PATH_MAX, "%setc/ld.so.conf", pmo_root);
	if(!stat(expath, &buf)) {
		snprintf(expath, PATH_MAX, "%ssbin/ldconfig", pmo_root);
		if(!stat(expath, &buf)) {
			char cmd[PATH_MAX];
			snprintf(cmd, PATH_MAX, "%s -r %s", expath, pmo_root);
			vprint("running \"%s\"\n", cmd);
			system(cmd);
		}
	}
	
	return(ret);
}

int pacman_remove(pacdb_t *db, PMList *targets)
{
	char line[PATH_MAX+1];
	char pm_install[PATH_MAX+1];
	pkginfo_t *info = NULL;
	pkginfo_t *depinfo = NULL;
	struct stat buf;
	char *newpath = NULL;
	depend_t depend;
	PMList *alltargs = NULL;
	PMList *targ, *lp, *j;

	if(targets == NULL) {
		return(0);
	}

	/* load package info from all targets */
	for(lp = targets; lp; lp = lp->next) {
		info = db_scan(db, (char*)lp->data, INFRQ_ALL);
		if(info == NULL) {
			fprintf(stderr, "error: could not find %s in database\n", (char*)lp->data);
			return(1);
		}
		alltargs = list_add(alltargs, info);
	}
	if(!pmo_nodeps && !pmo_upgrade) {
		vprint("Checking dependencies...\n");
		lp = checkdeps(db, PM_REMOVE, alltargs);
		if(lp) {
			fprintf(stderr, "error: this will break the following dependencies:\n");
			for(j = lp; j; j = j->next) {
				depmissing_t* miss = (depmissing_t*)j->data;
				printf("  %s: is required by %s\n", miss->target, miss->depend.name);
			}
			list_free(lp);
			return(1);
		}
		list_free(lp);
	}

	for(targ = alltargs; targ; targ = targ->next) {
		info = (pkginfo_t*)targ->data;

		if(!pmo_upgrade) {
			printf("removing %s... ", info->name);
			fflush(stdout);
			/* run the pre-remove script if it exists  */
			snprintf(pm_install, PATH_MAX, "%s%s/%s/%s-%s/install", pmo_root, PKGDIR, db->treename, info->name, info->version);
			if(!stat(pm_install, &buf)) {
				vprint("Executing pre-remove script...\n");
				snprintf(pm_install, PATH_MAX, "%s%s/%s/%s-%s/install", pmo_root, PKGDIR, db->treename, info->name, info->version);
				snprintf(line, PATH_MAX, "chroot %s /bin/sh %s pre_remove %s", pmo_root, pm_install, info->version);

				system(line);
			}
		}

		/* iterate through the list backwards, unlinking files */
		for(lp = list_last(info->files); lp; lp = lp->prev) {
			int nb = 0;
			if(needbackup((char*)lp->data, info->backup)) {
				nb = 1;
			}
			if(!nb && pmo_upgrade) {
				/* check pmo_noupgrade */
				if(is_in((char*)lp->data, pmo_noupgrade)) {
					nb = 1;
				}
			}
			snprintf(line, PATH_MAX, "%s%s", pmo_root, (char*)lp->data);
			if(lstat(line, &buf)) {
				vprint("file %s does not exist\n", line);
				continue;
			}
			if(S_ISDIR(buf.st_mode)) {
				/*vprint("  removing directory %s\n", line);*/
				if(rmdir(line)) {
					/* this is okay, other packages are probably using it. */
				}
			} else {
				/* if the file is flagged, back it up to .pacsave */
				if(nb) {
					if(pmo_upgrade) {
						/* we're upgrading so just leave the file as is.  pacman_add() will handle it */
					} else {
						if(!pmo_nosave) {
							newpath = (char*)realloc(newpath, strlen(line)+strlen(".pacsave")+1);
							sprintf(newpath, "%s.pacsave", line);
							rename(line, newpath);
							fprintf(stderr, "warning: %s saved as %s\n", line, newpath);
						} else {
							/*vprint("  unlinking %s\n", line);*/
							if(unlink(line)) {
								perror("cannot remove file");
							}
						}
					}
				} else {
					/*vprint("  unlinking %s\n", line);*/
					if(unlink(line)) {
						perror("cannot remove file");
					}
				}
			}
		}

		/* remove the package from the database */
		snprintf(line, PATH_MAX, "%s%s/%s/%s-%s", pmo_root, PKGDIR, db->treename,
			info->name, info->version);

		/* DESC */
		snprintf(pm_install, PATH_MAX, "%s/desc", line);
		unlink(pm_install);
		/* FILES */
		snprintf(pm_install, PATH_MAX, "%s/files", line);
		unlink(pm_install);
		/* DEPENDS */
		snprintf(pm_install, PATH_MAX, "%s/depends", line);
		unlink(pm_install);
		/* INSTALL */
		snprintf(pm_install, PATH_MAX, "%s/install", line);
		unlink(pm_install);
		/* directory */
		rmdir(line);

		/* update dependency packages' REQUIREDBY fields */
		for(lp = info->depends; lp; lp = lp->next) {
			PMList *last, *j;

			if(splitdep((char*)lp->data, &depend)) {
				continue;
			}
			depinfo = db_scan(db, depend.name, INFRQ_ALL);
			if(depinfo == NULL) {
				continue;
			}
			/* splice out this entry from requiredby */
			last = list_last(depinfo->requiredby);			
			for(j = depinfo->requiredby; j; j = j->next) {
				if(!strcmp((char*)j->data, info->name)) {
					if(j == depinfo->requiredby) {
						depinfo->requiredby = j->next;
					}
					if(j->prev)	j->prev->next = j->next;
					if(j->next)	j->next->prev = j->prev;
					/* free the spliced node */
					j->prev = j->next = NULL;
					list_free(j);
					break;
				}
			}
			db_write(db, depinfo);
			freepkg(depinfo);
		}
		if(!pmo_upgrade) {
			printf("done.\n");
		}
	}

	return(0);
}

int pacman_query(pacdb_t *db, PMList *targets)
{
	char *package = NULL;
	char path[PATH_MAX+1];
	pkginfo_t *info = NULL;
	PMList *targ, *lp, *q;
	int done = 0;

	for(targ = targets; !done; targ = (targ ? targ->next : NULL)) {
		if(targets == NULL) {
			done = 1;
		} else {
			if(targ->next == NULL) {
				done = 1;
			}
			package = (char*)targ->data;
		}
		/* output info for a .tar.gz package */
		if(pmo_q_isfile) {
			if(package == NULL) {
				fprintf(stderr, "error: no package file was specified (-p)\n");
				return(1);
			}
			info = load_pkg(package, pmo_q_info);
			if(info == NULL) {
				fprintf(stderr, "error: %s is not a package\n", package);
				return(1);
			}
			if(pmo_q_list) {
				for(lp = info->files; lp; lp = lp->next) {
					if(strcmp(lp->data, ".PKGINFO")) {
						printf("%s\n", (char*)lp->data);
					}
				}
			} else {
				if(!pmo_q_info) {
					printf("%s %s\n", info->name, info->version);
				}
			}
			continue;
		}

		/* determine the owner of a file */
		if(pmo_q_owns) {
			char rpath[PATH_MAX];
			if(package == NULL) {
				fprintf(stderr, "error: no file was specified for --owns\n");
				return(1);
			}
			if(realpath(package, rpath)) {
				int gotcha = 0;
				rewinddir(db->dir);
				while((info = db_scan(db, NULL, INFRQ_DESC | INFRQ_FILES)) != NULL && !gotcha) {
					for(lp = info->files; lp; lp = lp->next) {
						sprintf(path, "%s%s", pmo_root, (char*)lp->data);
						if(!strcmp(path, rpath)) {
							printf("%s %s\n", info->name, info->version);
							gotcha = 1;
						}
					}
				}
				if(!gotcha) {
					fprintf(stderr, "No package owns %s\n", package);
				}
				return(2);
			} else {
				fprintf(stderr, "error: %s is not a file.\n", package);
				return(1);
			}
		}

		/* find packages in the db */
		if(package == NULL) {
			/* no target */
			for(lp = pm_packages; lp; lp = lp->next) {
				pkginfo_t *tmpp = (pkginfo_t*)lp->data;
				if(pmo_q_list) {
					info = db_scan(db, tmpp->name, INFRQ_DESC | INFRQ_FILES);
					if(info == NULL) {
						/* something weird happened */
						return(1);
					}
					for(q = info->files; q; q = q->next) {
						printf("%s%s\n", pmo_root, (char*)q->data);
					}
				} else {
					printf("%s %s\n", tmpp->name, tmpp->version);
				}
			}
		} else {
			/* find a target */
			if(pmo_q_list) {
				info = db_scan(db, package, INFRQ_DESC | INFRQ_FILES);
				if(info == NULL) {
					fprintf(stderr, "Package \"%s\" was not found.\n", package);
					return(2);
				}
				for(lp = info->files; lp; lp = lp->next) {
					printf("%s%s\n", pmo_root, (char*)lp->data);
				}
			} else if(pmo_q_info) {
				int cols;

				info = db_scan(db, package, INFRQ_DESC | INFRQ_DEPENDS);
				if(info == NULL) {
					fprintf(stderr, "Package \"%s\" was not found.\n", package);
					return(2);
				}

				printf("Name          : %s\n", info->name);
				printf("Version       : %s\n", info->version);
				printf("Packager      : %s\n", info->packager);
				printf("Size          : %ld\n", info->size);
				printf("Build Date    : %s %s\n", info->builddate, strlen(info->builddate) ? "UTC" : "");
				printf("Install Date  : %s %s\n", info->installdate, strlen(info->installdate) ? "UTC" : "");
				printf("Install Script: %s\n", (info->scriptlet ? "yes" : "no"));
				printf("Depends On    : ");
				if(info->depends) {
					for(lp = info->depends, cols = 16; lp; lp = lp->next) {
						int s = strlen((char*)lp->data)+1;
						if(s+cols > 79) {
							cols = 16;
							printf("\n%16s%s ", " ", (char*)lp->data);
						} else {
							printf("%s ", (char*)lp->data);
						}
						cols += s;
					}
					printf("\n");
				} else {
					printf("None\n");
				}
				printf("Required By   : ");
				if(info->requiredby) {
					for(lp = info->requiredby, cols = 16; lp; lp = lp->next) {
						int s = strlen((char*)lp->data)+1;
						if(s+cols > 79) {
							cols = 16;
							printf("\n%16s%s ", " ", (char*)lp->data);
						} else {
							printf("%s ", (char*)lp->data);
						}
						cols += s;
					}
					printf("\n");
				} else {
					printf("None\n");
				}
				printf("Conflicts With: ");
				if(info->conflicts) {
					for(lp = info->conflicts, cols = 16; lp; lp = lp->next) {
						int s = strlen((char*)lp->data)+1;
						if(s+cols > 79) {
							cols = 16;
							printf("\n%16s%s ", " ", (char*)lp->data);
						} else {
							printf("%s ", (char*)lp->data);
						}
						cols += s;
					}
					printf("\n");
				} else {
					printf("None\n");
				}
				printf("Description   : %s\n", info->desc);
				printf("\n");
			} else {
				info = db_scan(db, package, INFRQ_DESC);
				if(info == NULL) {
					fprintf(stderr, "Package \"%s\" was not found.\n", package);
					return(2);
				}
				printf("%s %s\n", info->name, info->version);
			}
		}
	}

	if(info) {
		freepkg(info);
	}

	return(0);
}

int pacman_upgrade(pacdb_t *db, PMList *targets)
{
	/* this is basically just a remove,add process. pacman_add() will */
  /* handle it */
  pmo_upgrade = 1;
	return(pacman_add(db, targets));
}

/* populates *list with packages that need to be installed to satisfy all
 * dependencies (recursive) for *syncpkg->pkg
 *
 * make sure *list and *trail are already initialized
 */
int resolvedeps(pacdb_t *local, PMList *databases, syncpkg_t *syncpkg, PMList *list, PMList *trail)
{
	PMList *i, *j, *k;
	PMList *targ = NULL;
	PMList *deps = NULL;

	targ = list_new();
	targ = list_add(targ, syncpkg->pkg);
	deps = checkdeps(local, PM_ADD, targ);
	targ->data = NULL;
	list_free(targ);
	for(i = deps; i; i = i->next) {
		int found = 0;
		syncpkg_t *sync = NULL;
		depmissing_t *miss = (depmissing_t*)i->data;
		MALLOC(sync, sizeof(syncpkg_t));

		/* find the package in one of the repositories */
		for(j = databases; !found && j; j = j->next) {
			dbsync_t *dbs = (dbsync_t*)j->data;
			for(k = dbs->pkgcache; !found && k; k = k->next) {
				pkginfo_t *pkg = (pkginfo_t*)k->data;
				if(!strcmp(miss->depend.name, pkg->name)) {
					found = 1;
					/* re-fetch the package record with dependency info */
					sync->pkg = db_scan(dbs->db, pkg->name, INFRQ_DESC | INFRQ_DEPENDS);
					sync->dbs = dbs;
				}
			}
		}
		if(!found) {
			fprintf(stderr, "error: cannot resolve dependencies for \"%s\":\n", miss->target);
			fprintf(stderr, "       \"%s\" is not in the package set\n", miss->depend.name);
			return(1);
		}
		found = 0;
		for(j = list; j; j = j->next) {
			syncpkg_t *tmp = (syncpkg_t*)j->data;
			if(tmp && !strcmp(tmp->pkg->name, sync->pkg->name)) {
				found = 1;
			}
		}
		if(found) {
			/* this dep is already in the target list */
			continue;
		}
		if(miss->type == CONFLICT) {
			fprintf(stderr, "error: %s conflicts with %s\n", miss->target, miss->depend.name);
			return(1);
		} else if(miss->type == DEPEND) {
			/*printf("resolving %s\n", sync->pkg->name); fflush(stdout);*/
			found = 0;
			for(j = trail; j; j = j->next) {
				syncpkg_t *tmp = (syncpkg_t*)j->data;
				if(tmp && !strcmp(tmp->pkg->name, sync->pkg->name)) {
					found = 1;
				}
			}
			if(!found) {
				list_add(trail, sync);
				if(resolvedeps(local, databases, sync, list, trail)) {
					return(1);
				}
				vprint("adding %s-%s\n", sync->pkg->name, sync->pkg->version);
				list_add(list, sync);
			} else {
				/* cycle detected -- skip it */
				/*printf("cycle detected\n"); fflush(stdout);*/
			}
		}
	}
	return(0);
}

/*
 * returns a PMList* of missing_t pointers.
 *
 * conflicts are always name only, but dependencies can include versions
 * with depmod operators.
 *
 */
PMList* checkdeps(pacdb_t *db, unsigned short op, PMList *targets)
{
	pkginfo_t *info = NULL;
	depend_t depend;
	PMList *i, *j, *k;
	int cmp;
	int found = 0;
	PMList *baddeps = NULL;
	depmissing_t *miss = NULL;

	if(op == PM_UPGRADE) {
		/* PM_UPGRADE handles the backwards dependencies, ie, the packages
		 * listed in the requiredby field.
		 */
		for(i = targets; i; i = i->next) {
			pkginfo_t *tp, *oldpkg;
			if(i->data == NULL) {
				continue;
			}
			tp = (pkginfo_t*)i->data;

			if((oldpkg = db_scan(db, tp->name, INFRQ_DESC | INFRQ_DEPENDS)) == NULL) {
				continue;
			}
			for(j = oldpkg->requiredby; j; j = j->next) {
				char *ver;
				pkginfo_t *p;
				found = 0;
				if((p = db_scan(db, j->data, INFRQ_DESC | INFRQ_DEPENDS)) == NULL) {
					continue;
				}
				for(k = p->depends; k && !found; k = k->next) {
					if(splitdep(k->data, &depend)) {
						continue;
					}
					if(!strcmp(depend.name, oldpkg->name)) {
						found = 1;
					}
				}
				if(found == 0) {
					continue;
				}
				found = 0;
				if(depend.mod == DEP_ANY) {
					found = 1;
				} else {
					/* note that we use the version from the NEW package in the check */
					ver = strdup(tp->version);
					if(!index(depend.version,'-')) {
						char *ptr;
						for(ptr = ver; *ptr != '-'; ptr++);
						*ptr = '\0';
					}
					cmp = rpmvercmp(ver, depend.version);
					switch(depend.mod) {
						case DEP_EQ: found = (cmp == 0); break;
						case DEP_GE: found = (cmp >= 0); break;
						case DEP_LE: found = (cmp <= 0); break;
					}
					FREE(ver);
				}
				if(!found) {
					MALLOC(miss, sizeof(depmissing_t));
					miss->type = REQUIRED;
					miss->depend.mod = depend.mod;
					strncpy(miss->target, p->name, 256);
					strncpy(miss->depend.name, depend.name, 256);
					strncpy(miss->depend.version, depend.version, 64);
					baddeps = list_add(baddeps, miss);
				}
			}
			freepkg(oldpkg);
		}
	}
	if(op == PM_ADD || op == PM_UPGRADE) {
		for(i = targets; i; i = i->next) {
			pkginfo_t *tp;
			if(i->data == NULL) {
				continue;
			}
			tp = (pkginfo_t*)i->data;

			/* CONFLICTS */
			for(j = tp->conflicts; j; j = j->next) {
				/* check targets against database */
				for(k = pm_packages; k; k = k->next) {
					pkginfo_t *dp = (pkginfo_t*)k->data;
					if(!strcmp(j->data, dp->name)) {
						MALLOC(miss, sizeof(depmissing_t));
						miss->type = CONFLICT;
						miss->depend.mod = DEP_ANY;
						miss->depend.version[0] = '\0';
						strncpy(miss->target, tp->name, 256);
						strncpy(miss->depend.name, dp->name, 256);
						baddeps = list_add(baddeps, miss);
					}
				}
				/* check targets against targets */
				for(k = targets; k; k = k->next) {
					pkginfo_t *a = (pkginfo_t*)k->data;
					if(!strcmp(a->name, (char*)j->data)) {
						MALLOC(miss, sizeof(depmissing_t));
						miss->type = CONFLICT;
						miss->depend.mod = DEP_ANY;
						miss->depend.version[0] = '\0';
						strncpy(miss->target, tp->name, 256);
						strncpy(miss->depend.name, a->name, 256);
						baddeps = list_add(baddeps, miss);
					}
				}
			}
			/* check database against targets */
			rewinddir(db->dir);
			while((info = db_scan(db, NULL, INFRQ_DESC | INFRQ_DEPENDS)) != NULL) {
				for(j = info->conflicts; j; j = j->next) {
					if(!strcmp((char*)j->data, tp->name)) {
						MALLOC(miss, sizeof(depmissing_t));
						miss->type = CONFLICT;
						miss->depend.mod = DEP_ANY;
						miss->depend.version[0] = '\0';
						strncpy(miss->target, tp->name, 256);
						strncpy(miss->depend.name, (char*)j->data, 256);
						baddeps = list_add(baddeps, miss);
					}
				}
			}

			/* DEPENDENCIES */
			/* cycle thru this targets dependency list */
			for(j = tp->depends; j; j = j->next) {
				/* split into name/version pairs */				
				if(splitdep((char*)j->data, &depend)) {
					fprintf(stderr, "warning: invalid dependency in %s\n", (char*)tp->name);
					continue;
				}
				found = 0;
				/* check database */
				for(k = pm_packages; k && !found; k = k->next) {
					pkginfo_t *p = (pkginfo_t*)k->data;
					if(!strcmp(p->name, depend.name)) {
						if(depend.mod == DEP_ANY) {
							/* accept any version */
							found = 1;
						} else {
							char *ver = strdup(p->version);
							/* check for a release in depend.version.  if it's
							 * missing remove it from p->version as well.
							 */
							if(!index(depend.version,'-')) {
								char *ptr;
								for(ptr = ver; *ptr != '-'; ptr++);
								*ptr = '\0';
							}
							cmp = rpmvercmp(ver, depend.version);
							switch(depend.mod) {
								case DEP_EQ: found = (cmp == 0); break;
								case DEP_GE: found = (cmp >= 0); break;
								case DEP_LE: found = (cmp <= 0); break;
							}
							FREE(ver);
						}
					}
				}
				/* check other targets */
				for(k = targets; k && !found; k = k->next) {
					pkginfo_t *p = (pkginfo_t*)k->data;
					if(!strcmp(p->name, depend.name)) {
						if(depend.mod == DEP_ANY) {
							/* accept any version */
							found = 1;
						} else {
							char *ver = strdup(p->version);
							/* check for a release in depend.version.  if it's
							 * missing remove it from p->version as well.
							 */
							if(!index(depend.version,'-')) {
								char *ptr;
								for(ptr = ver; *ptr != '-'; ptr++);
								*ptr = '\0';
							}
							cmp = rpmvercmp(ver, depend.version);
							switch(depend.mod) {
								case DEP_EQ: found = (cmp == 0); break;
								case DEP_GE: found = (cmp >= 0); break;
								case DEP_LE: found = (cmp <= 0); break;
							}
							FREE(ver);
						}
					}
					/* TODO: switch positions in targets if one package should precede
					 * the other (wrt deps)
					 */
				}
				if(!found) {
					MALLOC(miss, sizeof(depmissing_t));
					miss->type = DEPEND;
					miss->depend.mod = depend.mod;
					strncpy(miss->target, tp->name, 256);
					strncpy(miss->depend.name, depend.name, 256);
					strncpy(miss->depend.version, depend.version, 64);
					baddeps = list_add(baddeps, miss);
				}
			}
		}
	} else if(op == PM_REMOVE) {
		/* check requiredby fields */
		for(i = targets; i; i = i->next) {
			pkginfo_t* tp;
			if(i->data == NULL) {
				continue;
			}
			tp = (pkginfo_t*)i->data;
			for(j = tp->requiredby; j; j = j->next) {
				if(!is_in((char*)j->data, targets)) {
					MALLOC(miss, sizeof(depmissing_t));
					miss->type = REQUIRED;
					miss->depend.mod = DEP_ANY;
					miss->depend.version[0] = '\0';
					strncpy(miss->target, tp->name, 256);
					strncpy(miss->depend.name, (char*)j->data, 256);
					baddeps = list_add(baddeps, miss);
				}
			}
		}
	}
	
	return(baddeps);
}

int splitdep(char *depstr, depend_t *depend)
{
	char *str = NULL;
	char *ptr = NULL;

	str = strdup(depstr);

	if((ptr = strstr(str, ">="))) {
		depend->mod = DEP_GE;
	} else if((ptr = strstr(str, "<="))) {
		depend->mod = DEP_LE;
	} else if((ptr = strstr(str, "="))) {
		depend->mod = DEP_EQ;
	} else {
		/* no version specified - accept any */
		depend->mod = DEP_ANY;
		strcpy(depend->name, str);
		strcpy(depend->version, "");
	}

	if(ptr == NULL) {
		return(0);
	}
	*ptr = '\0';
	strcpy(depend->name, str);
	ptr++;
	if(depend->mod != DEP_EQ) {
		ptr++;
	}
	strcpy(depend->version, ptr);
	FREE(str);
	return(0);
}

int lckmk(char *file, int retries, unsigned int sleep_secs)
{
	int fd, count = 0;

	while((fd = open(file, O_WRONLY | O_CREAT | O_EXCL, 0000)) == -1 && errno == EACCES) { 
		if(++count < retries) {
			sleep(sleep_secs);
		}	else {
			return(-1);
		}
	}
	return(fd > 0 ? 0 : -1);
}

int lckrm(char *file)
{
	return(unlink(file));
}

void cleanup(int signum)
{
	if(lckrm(lckfile)) {
		fprintf(stderr, "warning: could not remove lock file %s\n", lckfile);
	}
	exit(signum);
}

/* vim: set ts=2 sw=2 noet: */
