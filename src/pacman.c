/*
 *  pacman.c
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
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <getopt.h>
#include <zlib.h>
#include <syslog.h>
#include <libtar.h>
/* pacman */
#include "rpmvercmp.h"
#include "md5.h"
#include "list.h"
#include "package.h"
#include "util.h"
#include "db.h"
#include "pacsync.h"
#include "pacman.h"

/*
 * GLOBALS
 *
 */

/* command line options */
char          *pmo_root       = NULL;
unsigned short pmo_op         = PM_MAIN;
unsigned short pmo_verbose    = 0;
unsigned short pmo_version    = 0;
unsigned short pmo_help       = 0;
unsigned short pmo_force      = 0;
unsigned short pmo_nodeps     = 0;
unsigned short pmo_upgrade    = 0;
unsigned short pmo_freshen    = 0;
unsigned short pmo_nosave     = 0;
unsigned short pmo_d_vertest  = 0;
unsigned short pmo_d_resolve  = 0;
unsigned short pmo_q_isfile   = 0;
unsigned short pmo_q_info     = 0;
unsigned short pmo_q_list     = 0;
unsigned short pmo_q_orphans  = 0;
unsigned short pmo_q_owns     = 0;
unsigned short pmo_r_cascade  = 0;
unsigned short pmo_s_upgrade  = 0;
unsigned short pmo_s_downloadonly = 0;
unsigned short pmo_s_sync     = 0;
unsigned short pmo_s_search   = 0;
unsigned short pmo_s_clean    = 0;
unsigned short pmo_group      = 0;
/* configuration file options */
char          *pmo_dbpath       = NULL;
PMList        *pmo_noupgrade    = NULL;
PMList        *pmo_ignorepkg    = NULL;
unsigned short pmo_usesyslog    = 0;
unsigned short pmo_nopassiveftp = 0;


/* list of sync_t structs for sync locations */
PMList *pmc_syncs = NULL;
/* list of installed packages */
PMList *pm_packages = NULL;
/* list of targets specified on command line */
PMList *pm_targets  = NULL;

char *lckfile  = "/tmp/pacman.lck";
char *workfile = NULL;
enum {READ_ONLY, READ_WRITE} pm_access;
int maxcols = 80;

int main(int argc, char *argv[])
{
	int ret = 0;
	char *ptr    = NULL;
	char path[PATH_MAX];	
	pacdb_t *db_local = NULL;
	char *cenv = NULL;

	cenv = getenv("COLUMNS");
	if(cenv) {
		maxcols = atoi(cenv);
	}

	/* default root */
	MALLOC(pmo_root, PATH_MAX);
	strcpy(pmo_root, "/");
	/* default dbpath */
	MALLOC(pmo_dbpath, PATH_MAX);
	strcpy(pmo_dbpath, PKGDIR);

	if(argc < 2) {
		usage(PM_MAIN, (char*)basename(argv[0]));
		return(0);
	}

	/* parse the command line */
	ret = parseargs(PM_ADD, argc, argv);
	if(ret) {
		return(ret);
	}

	/* check for permission */
	pm_access = READ_ONLY;
	if(pmo_op != PM_MAIN && pmo_op != PM_QUERY && pmo_op != PM_DEPTEST) {
		if(pmo_op == PM_SYNC && !pmo_s_sync && (pmo_s_search || pmo_group)) {
			/* special case:  PM_SYNC can be used w/ pmo_s_search by any user */
		} else {
			if(geteuid() != 0) {
				fprintf(stderr, "error: you cannot perform this operation unless you are root.\n");
				return(1);
			}
			pm_access = READ_WRITE;
			/* lock */
			if(lckmk(lckfile, 1, 1) == -1) {
				fprintf(stderr, "error: unable to lock pacman database.\n");		
				fprintf(stderr, "       if you're sure pacman is not already running, you\n");
				fprintf(stderr, "       can remove %s\n", lckfile);
				return(32);
			}
		}
	}

	/* set signal handlers */
	signal(SIGINT, cleanup);
	signal(SIGTERM, cleanup);

	/* parse the system-wide config file */
	snprintf(path, PATH_MAX, "/%s", PACCONF);
	if(parseconfig(path)) {
		cleanup(1);
	}

	if(pmo_usesyslog) {
		openlog("pacman", 0, LOG_USER);
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

	vprint("Installation Root: %s\n", pmo_root);
	/* db location */
	vprint("Top-level DB Path: %s%s\n", pmo_root, pmo_dbpath);
	if(pmo_verbose) {
		list_display("Targets: ", pm_targets);
	}

	db_local = db_open(pmo_root, pmo_dbpath, "local");
	if(db_local == NULL) {
		/* couldn't open the db directory - try creating it */
		char path[PATH_MAX];

		snprintf(path, PATH_MAX, "%s%s/local", pmo_root, pmo_dbpath);
		vprint("initializing database %s...\n", path);
		ret = makepath(path);

		if(ret) {
			fprintf(stderr, "error: could not create database.\n");
			cleanup(1);
		}
		if((db_local = db_open(pmo_root, pmo_dbpath, "local")) == NULL) {
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
	FREE(pmo_dbpath);
	cleanup(ret);
	/* not reached */
	return(0);
}

int pacman_deptest(pacdb_t *db, PMList *targets)
{
	PMList *list = NULL;
	PMList *lp, *deps;
	pkginfo_t *dummy;

	if(pmo_d_vertest) {
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
		/* return 126 = deps were missing, but successfully resolved
		 * return 127 = deps were missing, and failed to resolve; OR
		 *            = deps were missing, but no resolution was attempted; OR
		 *            = unresolvable conflicts were found
		 */
		int ret = 126;
		PMList *synctargs = NULL;
		for(lp = deps; lp; lp = lp->next) {
			depmissing_t *miss = (depmissing_t*)lp->data;
			if(miss->type == CONFLICT) {
				/* we can't auto-resolve conflicts */
				printf("conflict: %s\n", miss->depend.name);
				ret = 127;
			} else if(miss->type == DEPEND || miss->type == REQUIRED) {
				if(!pmo_d_resolve) {
					printf("requires: %s", miss->depend.name);
					switch(miss->depend.mod) {
						case DEP_EQ:  printf("=%s",  miss->depend.version); break;
						case DEP_GE:  printf(">=%s", miss->depend.version); break;
						case DEP_LE:  printf("<=%s", miss->depend.version); break;
					}
					printf("\n");
				}
				synctargs = list_add(synctargs, strdup(miss->depend.name));
			}
			FREE(miss);
			lp->data = NULL;
		}
		FREE(deps);
		/* attempt to resolve missing dependencies */
		/* TODO: handle version comparators (eg, glibc>=2.2.5) */
		if(ret == 126 && synctargs != NULL) {
			if(!pmo_d_resolve || pacman_sync(db, synctargs)) {
				/* error (or -D not used) */
				ret = 127;
			}
		}
		list_free(synctargs);
		synctargs = NULL;
		return(ret);
	}
	return(0);
}

int pacman_sync(pacdb_t *db, PMList *targets)
{
	int allgood = 1, confirm = 0;
	PMList *i, *j, *k;
	PMList *rmtargs = NULL; /* conflicting packages to remove */
	PMList *final = NULL;   /* packages to upgrade */
	PMList *trail = NULL;   /* a breadcrumb trail to avoid running in circles */
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
  	if(makepath("/var/cache/pacman/pkg")) {
			fprintf(stderr, "error: could not create new cache directory: %s\n", strerror(errno));
			return(1);
		}
		umask(oldmask);

		printf("done.\n");
		return(0);
	}
	
	if(pmo_s_sync) {
		/* grab a fresh package list */
		printf(":: Synchronizing package databases... \n");
		if(pmo_usesyslog) {
			syslog(LOG_INFO, "synchronizing package lists");
		}
		sync_synctree();
	}

	/* open the database(s) */
	for(i = pmc_syncs; i; i = i->next) {
		pacdb_t *db_sync = NULL;
		dbsync_t *dbs = NULL;
		sync_t *sync = (sync_t*)i->data;

		db_sync = db_open(pmo_root, pmo_dbpath, sync->treename);
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
		if(targets) {
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
							printf("%s/%s %s\n    ", dbs->sync->treename, pkg->name, pkg->version);
							indentprint(pkg->desc, 4);
							printf("\n");
						} else {
							/* check description */
							FREE(haystack);
							haystack = strdup(pkg->desc);
							strtoupper(haystack);
							if(strstr(haystack, targ)) {
								printf("%s/%s %s\n    ", dbs->sync->treename, pkg->name, pkg->version);
								indentprint(pkg->desc, 4);
								printf("\n");
							}
						}
						FREE(haystack);
					}
				}
				FREE(targ);
			}
		} else {
			for(j = databases; j; j = j->next) {
				dbsync_t *dbs = (dbsync_t*)j->data;
				for(k = dbs->pkgcache; k; k = k->next) {
					pkginfo_t *pkg = (pkginfo_t*)k->data;
					printf("%s/%s %s\n    ", dbs->sync->treename, pkg->name, pkg->version);
					indentprint(pkg->desc, 4);
					printf("\n");
				}
			}
		}
	} else if(pmo_group) {
		PMList *pm, *allgroups, *groups;
		i = NULL;
		/* fetch the list of existing groups */
		for(j = databases; j; j = j->next) {
			dbsync_t *dbs = (dbsync_t*)j->data;
			k = find_groups(dbs->db);
			for(pm = k; pm; pm = pm->next) {
				if(!is_in((char *)pm->data, i)) {
					i = list_add(i, strdup((char *)pm->data));
				}
			}
			list_free(k);
		}
		allgroups = list_sort(i);
		list_free(i);
		if(targets) {
			groups = NULL;
			for(j = targets; j; j = j->next) {
				if(is_in((char *)j->data, allgroups)) {
					groups = list_add(groups, (char *)j->data);
				}
			}
			list_free(allgroups);
		} else {
			groups = allgroups;
		}
		/* display the packages belonging to groups */
		for(pm = groups; pm; pm = pm->next) {
			PMList *pkg;
			printf("%s\n", (char *)pm->data);
			i = NULL;
			for(j = databases; j; j = j->next) {
				PMList *lp;
				dbsync_t *dbs = (dbsync_t*)j->data;
				k = pkg_ingroup(dbs->db, (char *)pm->data);
				for(lp = k; lp; lp = lp->next) {
					if(!is_in((char *)lp->data, i)) {
						i = list_add(i, strdup((char *)lp->data));
					}
				}
				list_free(k);
			}
			pkg = list_sort(i);
			list_free(i);
			list_display("    ", pkg);
			list_free(pkg);
		}
		list_free(groups);
	} else if(pmo_s_upgrade) {
		int newer = 0;
		int ignore = 0;
		if(pmo_usesyslog) {
			syslog(LOG_INFO, "starting full system upgrade");
		}
		/* check for "recommended" package replacements */
		for(i = databases; i && allgood; i = i->next) {
			dbsync_t *dbs = (dbsync_t*)i->data;
			for(j = dbs->pkgcache; j; j = j->next) {
				pkginfo_t *pkg = (pkginfo_t*)j->data;
				for(k = pkg->replaces; k; k = k->next) {
					PMList *m;
					for(m = pm_packages; m; m = m->next) {
						pkginfo_t *p = (pkginfo_t*)m->data;
						if(!strcmp(k->data, p->name)) {
							/* if confirmed, add this to the 'final' list, designating 'p' as
							 * the package to replace.
							 */
							if(yesno(":: replace %s with %s from \"%s\"? [Y/n] ", p->name, pkg->name, dbs->db->treename)) {
								syncpkg_t *sync = NULL;
								/* we save the dependency info so we can move p's requiredby stuff
								 * over to the replacing package
								 */
								pkginfo_t *q = db_scan(db, p->name, INFRQ_DESC | INFRQ_DEPENDS);

								/* check if pkg->name is already in the final list. */
								sync = find_pkginsync(pkg->name, final);
								if(sync) {
									/* found it -- just append to the replaces list */
									sync->replaces = list_add(sync->replaces, q);
								} else {
									/* none found -- enter pkg into the final sync list */
									MALLOC(sync, sizeof(syncpkg_t));
									sync->dbs = dbs;
									sync->replaces = NULL;
									sync->replaces = list_add(sync->replaces, q);
									sync->pkg = db_scan(sync->dbs->db, pkg->name, INFRQ_DESC | INFRQ_DEPENDS);
									/* add to the targets list */
									allgood = !resolvedeps(db, databases, sync, final, trail);
									/* check again, as resolvedeps could have added our target for us */
									if(find_pkginsync(sync->pkg->name, final) == NULL) {
										final = list_add(final, sync);
									}
								}
							}
							break;
						}
					}
				}
			}
		}
		/* match installed packages with the sync dbs and compare versions */
		for(i = pm_packages; i && allgood; i = i->next) {
			int cmp, found = 0;
			pkginfo_t *local = (pkginfo_t*)i->data;
			syncpkg_t *sync = NULL;
			MALLOC(sync, sizeof(syncpkg_t));
			sync->replaces = NULL;

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
				FREE(sync);
				continue;
			}
			/* compare versions and see if we need to upgrade */
			cmp = rpmvercmp(local->version, sync->pkg->version);
			if(cmp > 0) {
				/* local version is newer */
				fprintf(stderr, ":: %s-%s: local version is newer\n",
					local->name, local->version);
				newer = 1;
				FREE(sync);
				continue;
			} else if(cmp == 0) {
				/* versions are identical */
				FREE(sync);
				continue;
			} else if(is_in((char*)i->data, pmo_ignorepkg)) {
			  /* package should be ignored (IgnorePkg) */
				fprintf(stderr, ":: %s-%s: ignoring package upgrade (%s)\n",
					local->name, local->version, sync->pkg->version);
				ignore = 1;
				FREE(sync);
				continue;
			}

			/* re-fetch the package record with dependency info */
			sync->pkg = db_scan(sync->dbs->db, sync->pkg->name, INFRQ_DESC | INFRQ_DEPENDS);

			/* add to the targets list */
			found = (find_pkginsync(sync->pkg->name, final) != NULL);
			if(!found) {
				allgood = !resolvedeps(db, databases, sync, final, trail);
				/* check again, as resolvedeps could have added our target for us */
				found = (find_pkginsync(sync->pkg->name, final) != NULL);
				if(!found) {
					final = list_add(final, sync);
				}
			}
		}
		if((newer || ignore) && allgood) {
			fprintf(stderr, ":: Above packages will be skipped.  To manually upgrade use 'pacman -S <pkg>'\n");
		}
	} else {
		/* process targets */
		for(i = targets; i && allgood; i = i->next) {
			if(i->data) {
				int cmp, found = 0, group = 0;
				pkginfo_t *local;
				syncpkg_t *sync = NULL;
				MALLOC(sync, sizeof(syncpkg_t));
				sync->replaces = NULL;

				local = db_scan(db, (char*)i->data, INFRQ_DESC);
				for(j = databases; !found && j; j = j->next) {
					dbsync_t *dbs = (dbsync_t*)j->data;
					for(k = dbs->pkgcache; !found && k; k = k->next) {
						pkginfo_t *pkg = (pkginfo_t*)k->data;
						if(is_in((char*)i->data, pkg->groups)) {
							group = 1;
							if(!yesno(":: install %s from group %s? [Y/n] ", pkg->name, (char*)i->data)) {
								continue;
							}
							targets = list_add(targets, strdup(pkg->name));
						} else if(!strcmp((char*)i->data, pkg->name)) {
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
				if(!found || group) {
					if(!group) {
						fprintf(stderr, "%s: not found in sync db\n", (char*)i->data);
						allgood = 0;
					}
					freepkg(local);
					FREE(sync);
					continue;
				}
				if(local && !pmo_s_downloadonly) {
					/* this is an upgrade, compare versions and determine if it is necessary */
					cmp = rpmvercmp(local->version, sync->pkg->version);
					if(cmp > 0) {
						/* local version is newer - get confirmation first */
						if(!yesno(":: %s-%s: local version is newer.  Upgrade anyway? [Y/n] ", local->name, local->version)) {
							freepkg(local);
							freepkg(sync->pkg);
							FREE(sync);
							continue;
						}
					} else if(cmp == 0) {
						/* versions are identical */
						if(!yesno(":: %s-%s: is up to date.  Upgrade anyway? [Y/n] ", local->name, local->version)) {
							freepkg(local);
							freepkg(sync->pkg);
							FREE(sync);
							continue;
						}
					}
				}
				freepkg(local);

				found = (find_pkginsync(sync->pkg->name, final) != NULL);
				if(!found && !pmo_nodeps) {
					allgood = !resolvedeps(db, databases, sync, final, trail);
					/* check again, as resolvedeps could have added our target for us */
					found = (find_pkginsync(sync->pkg->name, final) != NULL);
				}
				if(!found) {
					final = list_add(final, sync);
				}
			}
		}
	}

	if(allgood) {
		/* check for inter-conflicts and whatnot */
		if(!pmo_nodeps && !pmo_s_downloadonly) {
			int errorout = 0;
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
				for(i = deps; i; i = i->next) {
					depmissing_t *miss = (depmissing_t*)i->data;
					if(miss->type == DEPEND || miss->type == REQUIRED) {
						if(!errorout) {
							fprintf(stderr, "error: unresolvable dependencies:\n");
							errorout = 1;
						}
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
				}
				if(!errorout) {
					int found;
					errorout = 0;
					/* no unresolvable deps, so look for conflicts */
					for(i = deps; i && !errorout; i = i->next) {
						depmissing_t *miss = (depmissing_t*)i->data;
						if(miss->type == CONFLICT) {
							/* check if the conflicting package is one that's about to be removed/replaced.
							 * if so, then just ignore it
							 */
							found = 0;
							for(j = final; j && !found; j = j->next) {
								syncpkg_t *sync = (syncpkg_t*)j->data;
								for(k = sync->replaces; k && !found; k = k->next) {
									pkginfo_t *p = (pkginfo_t*)k->data;
									if(!strcmp(p->name, miss->depend.name)) {
										found = 1;
									}
								}
							}
							/* if we didn't find it in any sync->replaces lists, then it's a conflict */
							if(!found && !is_in(miss->depend.name, rmtargs)) {
								pkginfo_t p1;
								/* build a "fake" pkginfo_t so we can search with is_pkgin() */
								snprintf(p1.name, sizeof(p1.name), miss->depend.name);
								sprintf(p1.version, "1.0-1");
								
								if(is_pkgin(&p1, pm_packages)) {
									if(yesno(":: %s conflicts with %s. Remove %s? [Y/n] ",
											miss->target, miss->depend.name, miss->depend.name)) {
										/* remove miss->depend.name */
										rmtargs = list_add(rmtargs, strdup(miss->depend.name));
									} else {
										/* abort */
										fprintf(stderr, "\nerror: package conflicts detected\n");
										errorout = 1;
									}
								} else {
									if(!is_in(miss->depend.name, rmtargs) & !is_in(miss->target, rmtargs)) {
										fprintf(stderr, "\nerror: %s conflicts with %s\n", miss->target,
											miss->depend.name); 
										errorout = 1;
									}
								}
							}
						}
					}
				}
				list_free(deps);
				if(errorout) {
					/* abort mission */
					allgood = 0;
				}
			}
			/* cleanup */
			for(i = list; i; i = i->next) {
				i->data = NULL;
			}
			list_free(list);
			list = NULL;
		}

		/* any packages in rmtargs need to be removed from final. */
		/* rather than ripping out nodes from final, we just copy over */
	  /* our "good" nodes to a new list and reassign. */
		k = NULL;
		for(i = final; i; i = i->next) {
			syncpkg_t *s = (syncpkg_t*)i->data;
			int keepit = 1;
			for(j = rmtargs; j && keepit; j = j->next) {
				if(!strcmp(j->data, s->pkg->name)) {
					FREE(i->data);
					keepit = 0;
				}
			}
			if(keepit) {
				k = list_add(k, s);
			}
			i->data = NULL;
		}
		list_free(final);
		final = k;

		/* list targets */
		if(final && final->data && allgood) {
			PMList *list = NULL;
			for(i = rmtargs; i; i = i->next) {
				list = list_add(list, strdup(i->data));
			}
			for(i = final; i; i = i->next) {
				syncpkg_t *s = (syncpkg_t*)i->data;
				for(j = s->replaces; j; j = j->next) {
					pkginfo_t *p = (pkginfo_t*)j->data;
					list = list_add(list, strdup(p->name));
				}
			}
			if(list) {
				printf("\nRemove:  ");
				indentprint(buildstring(list), 9);
				printf("\n");
				list_free(list);
				list = NULL;
			}
			for(i = final; i; i = i->next) {
				syncpkg_t *s = (syncpkg_t*)i->data;
				if(s && s->pkg) {
					char *str = NULL;
					MALLOC(str, strlen(s->pkg->name)+strlen(s->pkg->version)+2);
					sprintf(str, "%s-%s", s->pkg->name, s->pkg->version);
					list = list_add(list, str);
				}
			}
			printf("\nTargets: ");
			indentprint(buildstring(list), 9);
			printf("\n");
			list_free(list);
			list = NULL;
		}

		/* get confirmation */
		confirm = 0;
		if(allgood && final && final->data) {
			if(pmo_s_downloadonly) {
				confirm = yesno("\nProceed with download? [Y/n] ");
			} else {
				/* don't get any confirmation if we're called from makepkg */
				if(pmo_d_resolve) {
					confirm = 1;
				} else {
					confirm = yesno("\nProceed with upgrade? [Y/n] ");
				}
			}
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
						vprint(" %s-%s.pkg.tar.gz is already in the cache\n", sync->pkg->name, sync->pkg->version);
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
					logaction("warning: no %s cache exists.  creating...\n", ldir);
					oldmask = umask(0000);
					mkdir(parent, 0755);
					if(mkdir(ldir, 0755)) {				
						/* couldn't mkdir the cache directory, so fall back to /tmp and unlink
						 * the package afterwards.
						 */
						logaction("warning: couldn't create package cache, using /tmp instead\n");
						snprintf(ldir, PATH_MAX, "/tmp");
						varcache = 0;
					}
					umask(oldmask);
				}
				if(downloadfiles(current->servers, ldir, files)) {
					fprintf(stderr, "error: failed to retrieve some files from %s\n", current->treename);
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

		if(!pmo_s_downloadonly) {
			/* remove any conflicting packages (WITH dep checks) */
			if(rmtargs) {
				int retcode;
				retcode	= pacman_remove(db, rmtargs);
				list_free(rmtargs);
				rmtargs = NULL;
				if(retcode == 1) {
					fprintf(stderr, "\nupgrade aborted.\n");
					allgood = 0;
				}
				/* reload package cache */
				pm_packages = db_loadpkgs(db, pm_packages);
			}
			list_free(rmtargs);
			rmtargs = NULL;
			for(i = final; allgood && i; i = i->next) {
				char *str;
				syncpkg_t *sync = (syncpkg_t*)i->data;
				if(sync->pkg) {
					MALLOC(str, PATH_MAX);
					snprintf(str, PATH_MAX, "%s/%s-%s.pkg.tar.gz", ldir, sync->pkg->name, sync->pkg->version);
					files = list_add(files, str);
				}
				for(j = sync->replaces; j; j = j->next) {
					pkginfo_t *pkg = (pkginfo_t*)j->data;
					rmtargs = list_add(rmtargs, pkg->name);
				}
			}
			/* remove to-be-replaced packages */
			if(allgood && rmtargs) {
				int oldval = pmo_nodeps;
				/* we make pacman_remove() skip dependency checks by setting pmo_nodeps high */
				pmo_nodeps = 1;
				allgood = !pacman_remove(db, rmtargs);
				pmo_nodeps = oldval;
				if(!allgood) {
					fprintf(stderr, "package removal failed.  aborting...\n");
				}
			}
			/* install targets */
			if(allgood) {
				allgood = !pacman_upgrade(db, files);
			}
			/* propagate replaced packages' requiredby fields to their new owners */
			if(allgood) {
				for(i = final; i; i = i->next) {
					syncpkg_t *sync = (syncpkg_t*)i->data;
					if(sync->replaces) {
						pkginfo_t *new = db_scan(db, sync->pkg->name, INFRQ_ALL);						
						for(j = sync->replaces; j; j = j->next) {
							pkginfo_t *old = (pkginfo_t*)j->data;
							/* merge lists */
							for(k = old->requiredby; k; k = k->next) {
								if(!is_in(k->data, new->requiredby)) {
									/* replace old's name with new's name in the requiredby's dependency list */
									PMList *m;
									pkginfo_t *depender = db_scan(db, k->data, INFRQ_ALL);
									for(m = depender->depends; m; m = m->next) {
										if(!strcmp(m->data, old->name)) {
											FREE(m->data);
											m->data = strdup(new->name);
										}
									}
									db_write(db, depender);
									
									/* add the new requiredby */
									new->requiredby = list_add(new->requiredby, k->data);
								}
							}
						}
						db_write(db, new);
						freepkg(new);
					}
				}
			}
		}

		if(!varcache && !pmo_s_downloadonly) {
			/* delete packages */
			for(i = files; i; i = i->next) {
				unlink(i->data);
			}
		}
	}

	/* cleanup */
	for(i = final; i; i = i->next) {
		syncpkg_t *sync = (syncpkg_t*)i->data;
		if(sync) {
			freepkg(sync->pkg);
			for(j = sync->replaces; j; j = j->next) {
				freepkg(j->data);
				j->data = NULL;
			}
			list_free(sync->replaces);
		}
		FREE(sync);
		i->data = NULL;
	}
	for(i = trail; i; i = i->next) {
		/* this list used the same pointers as final, so they're already freed */
		i->data = NULL;
	}
	for(i = databases; i; i = i->next) {
		dbsync_t *dbs = (dbsync_t*)i->data;
		db_close(dbs->db);
		for(j = dbs->pkgcache; j; j = j->next) {
			if(j->data) {
				/* XXX: this freepkg() still locks up, but only when the "replaces"
				 *      code has been run
				freepkg(j->data);*/
				j->data = NULL;
			}
		}
		list_free(dbs->pkgcache);
		FREE(dbs);
		i->data = NULL;
	}
	list_free(databases);
	list_free(final);
	list_free(trail);
	list_free(rmtargs);
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
	PMList *targ, *file, *lp, *j, *k;
	PMList *alltargs = NULL;
	PMList *filenames = NULL;
	unsigned short real_pmo_upgrade;
	tartype_t gztype = {
		(openfunc_t) gzopen_frontend,
		(closefunc_t)gzclose,
		(readfunc_t) gzread,
		(writefunc_t)gzwrite
	};

	if(targets == NULL) {
		return(0);
	}

	printf("loading package data... ");
	fflush(stdout);
	for(targ = targets; targ; targ = targ->next) {
		/* Populate the package struct */
		vprint("reading %s\n", (char*)targ->data);
		info = load_pkg((char*)targ->data, 0);
		if(info == NULL) {
			return(1);
		}
		if(pmo_freshen) {
			/* only upgrade/install this package if it is already installed and at a lesser version */
			pkginfo_t *dummy = db_scan(db, info->name, INFRQ_DESC);
			if(dummy == NULL || rpmvercmp(dummy->version, info->version) >= 0) {
				freepkg(info);
				info = NULL;
				continue;
			}
			freepkg(dummy);
		}
		alltargs = list_add(alltargs, info);
		filenames = list_add(filenames, strdup(targ->data));
	}
	printf("done.\n");

	/* No need to check deps if pacman_add was called during a sync:
	 * it is already done in pacman_sync. */
	if(!pmo_nodeps && pmo_op != PM_SYNC) {
		int errorout = 0;
		vprint("checking dependencies...\n");
		lp = checkdeps(db, (pmo_upgrade ? PM_UPGRADE : PM_ADD), alltargs);
		if(lp) {
			/* look for unsatisfied dependencies */
			for(j = lp; j; j = j->next) {
				depmissing_t* miss = (depmissing_t*)j->data;
				if(miss->type == DEPEND || miss->type == REQUIRED) {
					if(!errorout) {
						fprintf(stderr, "error: unsatisfied dependencies:\n");
						errorout = 1;
					}
					printf("  %s: requires %s", miss->target, miss->depend.name);
					switch(miss->depend.mod) {
						case DEP_EQ: printf("=%s", miss->depend.version);  break;
						case DEP_GE: printf(">=%s", miss->depend.version); break;
						case DEP_LE: printf("<=%s", miss->depend.version); break;
					}
					printf("\n");
				}
			}
			if(!errorout) {
				PMList *rmtargs = NULL;
				errorout = 0;
				/* no unsatisfied deps, so look for conflicts */
				for(j = lp; j && !errorout; j = j->next) {
					depmissing_t* miss = (depmissing_t*)j->data;
					if(miss->type == CONFLICT && !is_in(miss->depend.name, rmtargs)) {
						pkginfo_t p1;
						/* build a "fake" pkginfo_t so we can search with is_pkgin() */
						snprintf(p1.name, sizeof(p1.name), miss->depend.name);
						sprintf(p1.version, "1.0-1");
								
						if(is_pkgin(&p1, pm_packages)) {
							if(yesno(":: %s conflicts with %s. Remove %s? [Y/n] ",
								miss->target, miss->depend.name, miss->depend.name)) {
								/* remove miss->depend.name */
								rmtargs = list_add(rmtargs, strdup(miss->depend.name));
							} else {
								/* abort */
								fprintf(stderr, "\nerror: package conflicts detected\n");
								errorout = 1;
							}
						} else {
							if(!is_in(miss->depend.name, rmtargs) & !is_in(miss->target, rmtargs)) {
								fprintf(stderr, "\nerror: %s conflicts with %s\n", miss->target,
									miss->depend.name); 
								errorout = 1;
							}
						}
					}
				}
				if(rmtargs && !errorout) {
					int retcode;
				 	int oldupg = pmo_upgrade;
					/* any packages in rmtargs need to be removed from alltargs. */
					/* rather than ripping out nodes from alltargs, we just copy over */
				  /* our "good" nodes to a new list and reassign. */
					k = NULL;
					for(lp = alltargs; lp; lp = lp->next) {
						pkginfo_t *p = (pkginfo_t*)lp->data;
						int keepit = 1;
						for(j = rmtargs; j && keepit; j = j->next) {
							if(!strcmp(j->data, p->name)) {
								/* gone! */
								FREE(lp->data);
								keepit = 0;
							}
						}
						if(keepit) {
							k = list_add(k, p);
						}
						lp->data = NULL;
					}
					list_free(alltargs);
					alltargs = k;
					/* make sure pacman_remove does it's own dependency check */
					pmo_upgrade = 0;
					retcode	= pacman_remove(db, rmtargs);
					list_free(rmtargs);
					if(retcode == 1) {
						fprintf(stderr, "\n%s aborted.\n", oldupg ? "upgrade" : "install");
						return(1);
					}
					/* reload package cache */
					pm_packages = db_loadpkgs(db, pm_packages);
					pmo_upgrade = oldupg;
				}
			}
			if(errorout) {
				list_free(lp);
				return(1);
			}
			list_free(lp);
		}
		
		/* re-order w.r.t. dependencies */		
		vprint("sorting by dependencies\n");
		lp = sortbydeps(alltargs);
		/* free the old alltargs */
		for(j = alltargs; j; j = j->next) {
			j->data = NULL;
		}
		list_free(alltargs);
		alltargs = lp;
	}

	if(!pmo_force) {
		printf("checking for file conflicts... ");
		fflush(stdout);
		lp = db_find_conflicts(db, alltargs, pmo_root);
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
				if(retcode == 1) {
					fprintf(stderr, "\nupgrade aborted.\n");
					return(1);
				}
				/* reload package cache */
				pm_packages = db_loadpkgs(db, pm_packages);
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

			if(!strcmp(pathname, ".PKGINFO") || !strcmp(pathname, ".FILELIST")) {
				tar_skip_regfile(tar);
				continue;
			}

			if(!strcmp(pathname, "._install") || !strcmp(pathname, ".INSTALL")) {
				/* the install script goes inside the db */
				snprintf(expath, PATH_MAX, "%s%s/%s/%s-%s/install", pmo_root,
									pmo_dbpath, db->treename, info->name, info->version);
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
					logaction("could not extract %s: %s\n", pathname, strerror(errno));
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
							logaction("error: could not rename %s: %s\n", expath, strerror(errno));
						}
						if(copyfile(temp, expath)) {
							logaction("error: could not copy %s to %s: %s\n", temp, expath, strerror(errno));
							errors++;
						} else {
							logaction("warning: %s saved as %s\n", expath, newpath);
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
							logaction("error: could not rename %s: %s\n", expath, strerror(errno));
						} else {
							logaction("warning: %s saved as %s\n", expath, newpath);
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
					logaction("warning: extracting %s%s as %s\n", pmo_root, pathname, expath);
					/*tar_skip_regfile(tar);*/
				}
			  if(tar_extract_file(tar, expath)) {
					logaction("could not extract %s: %s\n", pathname, strerror(errno));
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
			logaction("errors occurred while %s %s\n",
				(pmo_upgrade ? "upgrading" : "installing"), info->name);

/* XXX: this "else" is disabled so the db_write() ALWAYS occurs.  If it doesn't
 *      packages can get lost during an upgrade.
 */
		} /*else*/ {
			time_t t = time(NULL);

			/* Update the requiredby field by scaning the whole database 
			 * looking for packages depending on the package to add */
			for(lp = pm_packages; lp; lp = lp->next) {
				pkginfo_t *tmpp = NULL;
				PMList *tmppm = NULL;

				tmpp = db_scan(db, ((pkginfo_t*)lp->data)->name, INFRQ_DEPENDS);
				if(tmpp == NULL) {
					continue;
				}
				for(tmppm = tmpp->depends; tmppm; tmppm = tmppm->next) {
					depend_t depend;
					if(splitdep(tmppm->data, &depend)) {
						continue;
					}
					if(tmppm->data && !strcmp(depend.name, info->name)) {
						info->requiredby = list_add(info->requiredby, strdup(tmpp->name));
						continue;
					}
				}
			}

			vprint("Updating database...");
			/* make an install date (in UTC) */
			strncpy(info->installdate, asctime(gmtime(&t)), sizeof(info->installdate));
			if(db_write(db, info)) {
				logaction("error updating database for %s!\n", info->name);
				return(1);
			}
			vprint("done.\n");
			if(pmo_usesyslog) {
				if(pmo_upgrade) {
					syslog(LOG_INFO, "upgraded %s (%s -> %s)\n", info->name,
						oldpkg->version, info->version);
				} else {
					syslog(LOG_INFO, "installed %s (%s)\n", info->name, info->version);
				}
			}

			/* update dependency packages' REQUIREDBY fields */
			for(lp = info->depends; lp; lp = lp->next) {
				pkginfo_t *depinfo = NULL;
				depend_t depend;

				if(splitdep(lp->data, &depend)) {
					continue;
				}
				depinfo = db_scan(db, depend.name, INFRQ_ALL);
				if(depinfo == NULL) {
					/* look for a provides package */
					PMList *provides = whatprovides(db, depend.name);
					if(provides) {
						/* use the first one */
						depinfo = db_scan(db, provides->data, INFRQ_ALL);
						if(depinfo == NULL) {
							/* wtf */
							continue;
						}
					} else {
						continue;
					}
				}
				depinfo->requiredby = list_add(depinfo->requiredby, strdup(info->name));
				db_write(db, depinfo);
				freepkg(depinfo);
			}
			printf("done.\n"); fflush(stdout);

			/* run the post-install script if it exists  */
			snprintf(pm_install, PATH_MAX, "%s%s/%s/%s-%s/install", pmo_root, pmo_dbpath, db->treename, info->name, info->version);
			if(!stat(pm_install, &buf)) {
				char cmdline[PATH_MAX+1];
				snprintf(pm_install, PATH_MAX, "%s/%s/%s-%s/install", pmo_dbpath, db->treename, info->name, info->version);
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
			PMList *groups;
			/* if the target is a group, ask if its packages should be removed */
			groups = find_groups(db);
			if(is_in((char *)lp->data, groups)) {
				PMList *pkg;
				pkg = pkg_ingroup(db, (char *)lp->data);
				for(j = pkg; j; j = j->next) {
					if(yesno(":: Remove %s from group %s? [Y/n] ", (char *)j->data, (char *)lp->data)) {
						info = db_scan(db, (char *)j->data, INFRQ_ALL);
						alltargs = list_add(alltargs, info);
					}
				}
				list_free(pkg);
				list_free(groups);
				continue;
			}
			list_free(groups);
			fprintf(stderr, "error: could not find %s in database\n", (char*)lp->data);
			return(1);
		}
		alltargs = list_add(alltargs, info);
	}
	if(!pmo_nodeps && !pmo_upgrade) {
		vprint("checking dependencies...\n");
		lp = checkdeps(db, PM_REMOVE, alltargs);
		if(lp) {
			if(pmo_r_cascade) {
				while(lp) {
					for(j = lp; j; j = j->next) {
						depmissing_t* miss = (depmissing_t*)j->data;
						info = db_scan(db, miss->depend.name, INFRQ_ALL);
						if(!is_pkgin(info, alltargs)) {
							list_add(alltargs, info);
						}
					}
					list_free(lp);
					lp = checkdeps(db, PM_REMOVE, alltargs);
				}
				/* list targets */
				list_display("\nTargets: ", alltargs);
				/* get confirmation */
				if(yesno("\nDo you want to remove these packages? [Y/n] ") == 0) {
					list_free(alltargs);
					list_free(lp);
					return(1);
				}
			} else {
				fprintf(stderr, "error: this will break the following dependencies:\n");
				for(j = lp; j; j = j->next) {
					depmissing_t* miss = (depmissing_t*)j->data;
					printf("  %s: is required by %s\n", miss->target, miss->depend.name);
				}
				list_free(alltargs);
				list_free(lp);
				return(1);
			}
		}
		list_free(lp);
	}

	for(targ = alltargs; targ; targ = targ->next) {
		info = (pkginfo_t*)targ->data;

		if(!pmo_upgrade) {
			printf("removing %s... ", info->name);
			fflush(stdout);
			/* run the pre-remove script if it exists  */
			snprintf(pm_install, PATH_MAX, "%s%s/%s/%s-%s/install", pmo_root, pmo_dbpath, db->treename, info->name, info->version);
			if(!stat(pm_install, &buf)) {
				vprint("Executing pre-remove script...\n");
				snprintf(pm_install, PATH_MAX, "%s/%s/%s-%s/install", pmo_dbpath, db->treename, info->name, info->version);
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
							logaction("warning: %s saved as %s\n", line, newpath);
						} else {
							/*vprint("  unlinking %s\n", line);*/
							if(unlink(line)) {
								perror("cannot remove file");
							}
						}
					}
				} else {
					vprint("  unlinking %s\n", line);
					if(unlink(line)) {
						perror("cannot remove file");
					}
				}
			}
		}

		/* remove the package from the database */
		snprintf(line, PATH_MAX, "%s%s/%s/%s-%s", pmo_root, pmo_dbpath, db->treename,
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
				/* look for a provides package */
				PMList *provides = whatprovides(db, depend.name);
				if(provides) {
					/* TODO: should check _all_ packages listed in provides, not just
					 *       the first one.
					 */
					/* use the first one */
					depinfo = db_scan(db, provides->data, INFRQ_ALL);
					list_free(provides);
					if(depinfo == NULL) {
						/* wtf */
						continue;
					}
				} else {
					list_free(provides);
					continue;
				}
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
			if(pmo_usesyslog) {
				syslog(LOG_INFO, "removed %s (%s)\n", info->name, info->version);
			}
		}
	}

	list_free(alltargs);

	/* run ldconfig if it exists */
	snprintf(line, PATH_MAX, "%setc/ld.so.conf", pmo_root);
	if(!stat(line, &buf)) {
		snprintf(line, PATH_MAX, "%ssbin/ldconfig", pmo_root);
		if(!stat(line, &buf)) {
			char cmd[PATH_MAX];
			snprintf(cmd, PATH_MAX, "%s -r %s", line, pmo_root);
			vprint("running \"%s\"\n", cmd);
			system(cmd);
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

		/* looking for groups */
		if(pmo_group) {
			PMList *pkg, *groups;
			groups = find_groups(db);
			if(package == NULL) {
				for(lp = groups; lp; lp = lp->next) {
					pkg = pkg_ingroup(db, (char *)lp->data);
					for(q = pkg; q; q = q->next) {
						printf("%s %s\n", (char *)lp->data, (char *)q->data);
					}
					list_free(pkg);
				}
			} else {
				if(!is_in(package, groups)) {
					fprintf(stderr, "Group \"%s\" was not found.\n", package);
					return(2);
				}
				pkg = pkg_ingroup(db, package);
				for(q = pkg; q; q = q->next) {
					printf("%s %s\n", package, (char *)q->data);
				}
				list_free(pkg);
			}
			list_free(groups);
			continue;
		}

		/* output info for a .tar.gz package */
		if(pmo_q_isfile) {
			if(package == NULL) {
				fprintf(stderr, "error: no package file was specified for --file\n");
				return(1);
			}
			info = load_pkg(package, pmo_q_info);
			if(info == NULL) {
				fprintf(stderr, "error: %s is not a package\n", package);
				return(1);
			}
			if(pmo_q_info) {
				printf("\n");
			} else if(pmo_q_list) {
				for(lp = info->files; lp; lp = lp->next) {
					printf("%s %s\n", info->name, (char*)lp->data);
				}
			} else {
				printf("%s %s\n", info->name, info->version);
			}
			freepkg(info);
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
							printf("%s is owned by %s %s\n", package, info->name, info->version);
							gotcha = 1;
						}
					}
					freepkg(info);
				}
				if(!gotcha) {
					fprintf(stderr, "No package owns %s\n", package);
				}
				continue;
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
						printf("%s %s%s\n", info->name, pmo_root, (char*)q->data);
					}
					freepkg(info);
				} else if(pmo_q_orphans) {
					info = db_scan(db, tmpp->name, INFRQ_DESC | INFRQ_DEPENDS);
					if(info == NULL) {
						return(1);
					}
					if(info->requiredby == NULL) {
						printf("%s %s\n", tmpp->name, tmpp->version);
					}
				} else {
					printf("%s %s\n", tmpp->name, tmpp->version);
				}
			}
		} else {
			/* find a target */
			if(pmo_q_info) {
				info = db_scan(db, package, INFRQ_DESC | INFRQ_DEPENDS);
				if(info == NULL) {
					fprintf(stderr, "Package \"%s\" was not found.\n", package);
					return(2);
				}
				dump_pkg(info);
				printf("\n");
			} else if(pmo_q_list) {
				info = db_scan(db, package, INFRQ_DESC | INFRQ_FILES);
				if(info == NULL) {
					fprintf(stderr, "Package \"%s\" was not found.\n", package);
					return(2);
				}
				for(lp = info->files; lp; lp = lp->next) {
					printf("%s %s%s\n", info->name, pmo_root, (char*)lp->data);
				}
			} else if(pmo_q_orphans) {
				info = db_scan(db, package, INFRQ_DESC | INFRQ_DEPENDS);
				if(info == NULL) {
					fprintf(stderr, "Package \"%s\" was not found.\n", package);
					return(2);
				}
				if(info->requiredby == NULL) {
					printf("%s %s\n", info->name, info->version);
				}
			} else {
				info = db_scan(db, package, INFRQ_DESC);
				if(info == NULL) {
					fprintf(stderr, "Package \"%s\" was not found.\n", package);
					return(2);
				}
				printf("%s %s\n", info->name, info->version);
			}
			freepkg(info);
		}
	}

	return(0);
}

int pacman_upgrade(pacdb_t *db, PMList *targets)
{
	/* this is basically just a remove-then-add process. pacman_add() will */
	/* handle it */
	pmo_upgrade = 1;
	return(pacman_add(db, targets));
}

/* Re-order a list of target packages with respect to their dependencies.
 *
 * Example:
 *   A depends on C
 *   B depends on A
 *   Target order is A,B,C,D
 *
 *   Should be re-ordered to C,A,B,D
 * 
 * This function returns the new PMList* target list.
 *
 */ 
PMList* sortbydeps(PMList *targets)
{
	PMList *newtargs = NULL;
	PMList *i, *j, *k;
	int change = 1;
	int numscans = 0;
	int numtargs = 0;
	int clean = 0;

	/* count the number of targets */
	numtargs = list_count(targets);	

	while(change) {
		change = 0;
		if(numscans > numtargs) {
			vprint("warning: possible dependency cycle detected\n");
			change = 0;
			continue;
		}
		newtargs = NULL;
		numscans++;
		/* run thru targets, moving up packages as necessary */
		for(i = targets; i; i = i->next) {
			pkginfo_t *p = (pkginfo_t*)i->data;
			for(j = p->depends; j; j = j->next) {
				depend_t dep;
				int found = 0;
				pkginfo_t *q = NULL;

				splitdep(j->data, &dep);
				/* look for dep.name -- if it's farther down in the list, then
				 * move it up above p
				 */
				for(k = i->next; k && !found; k = k->next) {
					q = (pkginfo_t*)k->data;
					if(!strcmp(dep.name, q->name)) {
						found = 1;
					}
				}
				if(found) {
					if(!is_pkgin(q, newtargs)) {
						change = 1;
						newtargs = list_add(newtargs, q);
					}
				}
			}
			if(!is_pkgin(p, newtargs)) {
				newtargs = list_add(newtargs, p);
			}
		}
		if(clean && change) {
			/* free up targets -- it's local now */
			for(i = targets; i; i = i->next) {
				i->data = NULL;
			}
			list_free(targets);
		}
		targets = newtargs;
		clean = 1;
	}
	return(targets);
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
		depmissing_t *miss = (depmissing_t*)i->data;

		/* XXX: conflicts are now treated specially in the _add and _sync functions */

		/*if(miss->type == CONFLICT) {
			fprintf(stderr, "error: cannot resolve dependencies for \"%s\":\n", miss->target);
			fprintf(stderr, "       %s conflicts with %s\n", miss->target, miss->depend.name);
			return(1);
		} else*/
		if(miss->type == DEPEND) {
			syncpkg_t *sync = NULL;
			MALLOC(sync, sizeof(syncpkg_t));
			sync->replaces = NULL;

			/* find the package in one of the repositories */
			for(j = databases; !found && j; j = j->next) {
				PMList *provides;
				dbsync_t *dbs = (dbsync_t*)j->data;
				/* check literals */
				for(k = dbs->pkgcache; !found && k; k = k->next) {
					pkginfo_t *pkg = (pkginfo_t*)k->data;
					if(!strcmp(miss->depend.name, pkg->name)) {
						found = 1;
						/* re-fetch the package record with dependency info */
						sync->pkg = db_scan(dbs->db, pkg->name, INFRQ_DESC | INFRQ_DEPENDS);
						sync->dbs = dbs;
					}
				}
				/* check provides */
				if(!found) {
					provides = whatprovides(dbs->db, miss->depend.name);
					if(provides) {
						found = 1;
						/* re-fetch the package record with dependency info */
						sync->pkg = db_scan(dbs->db, provides->data, INFRQ_DESC | INFRQ_DEPENDS);
						sync->dbs = dbs;
					}
					list_free(provides);
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
			vprint("resolving %s\n", sync->pkg->name);
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
				vprint("dependency cycle detected: %s\n", sync->pkg->name);
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
					/* hmmm... package isn't installed.. */
					continue;
				}
				if(is_pkgin(p, targets)) {
					/* this package is also in the upgrade list, so don't worry about it */
					continue;
				}
				for(k = p->depends; k && !found; k = k->next) {
					/* find the dependency info in p->depends */
					if(splitdep(k->data, &depend)) {
						continue;
					}
					if(!strcmp(depend.name, oldpkg->name)) {
						found = 1;
					}
				}
				if(found == 0) {
					/* look for packages that list depend.name as a "provide" */
					PMList *provides = whatprovides(db, depend.name);
					if(provides == NULL) {
						/* not found */
						continue;
					}
					/* we found an installed package that provides depend.name */
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
					if(!list_isin(baddeps, miss)) {
						baddeps = list_add(baddeps, miss);
					}
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
						if(!list_isin(baddeps, miss)) {
							baddeps = list_add(baddeps, miss);
						}
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
						if(!list_isin(baddeps, miss)) {
							baddeps = list_add(baddeps, miss);
						}
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
						strncpy(miss->depend.name, info->name, 256);
						if(!list_isin(baddeps, miss)) {
							baddeps = list_add(baddeps, miss);
						}
					}
				}
			}

			/* PROVIDES -- check to see if another package already provides what
			 *             we offer
			 */
			/* XXX: disabled -- we allow multiple packages to provide the same thing.
			 *      list packages in conflicts if they really do conflict.
			for(j = tp->provides; j; j = j->next) {
				PMList *provs = whatprovides(db, j->data);
				for(k = provs; k; k = k->next) {
					if(!strcmp(tp->name, k->data)) {
						// this is the same package -- skip it
						continue;
					}
					// we treat this just like a conflict
					MALLOC(miss, sizeof(depmissing_t));
					miss->type = CONFLICT;
					miss->depend.mod = DEP_ANY;
					miss->depend.version[0] = '\0';
					strncpy(miss->target, tp->name, 256);
					strncpy(miss->depend.name, k->data, 256);
					if(!list_isin(baddeps, miss)) {
						baddeps = list_add(baddeps, miss);
					}
				}
			}*/

			/* DEPENDENCIES -- look for unsatisfied dependencies */
			for(j = tp->depends; j; j = j->next) {
				/* split into name/version pairs */				
				if(splitdep((char*)j->data, &depend)) {
					logaction("warning: invalid dependency in %s\n", (char*)tp->name);
					continue;
				}
				found = 0;
				/* check database for literal packages */
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
					/* see if the package names match OR if p provides depend.name */
					if(!strcmp(p->name, depend.name) || is_in(depend.name, p->provides)) {
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
				/* check database for provides matches */
				if(!found){
					k = whatprovides(db, depend.name);
					if(k) {
						/* grab the first one (there should only really be one, anyway) */
						pkginfo_t *p = db_scan(db, k->data, INFRQ_DESC);
						if(p == NULL) {
							/* wtf */
							fprintf(stderr, "data error: %s supposedly provides %s, but it was not found in db\n",
								(char*)k->data, depend.name);
							list_free(k);
							continue;
						}
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
					list_free(k);
				}
				/* else if still not found... */
				if(!found) {
					MALLOC(miss, sizeof(depmissing_t));
					miss->type = DEPEND;
					miss->depend.mod = depend.mod;
					strncpy(miss->target, tp->name, 256);
					strncpy(miss->depend.name, depend.name, 256);
					strncpy(miss->depend.version, depend.version, 64);
					if(!list_isin(baddeps, miss)) {
						baddeps = list_add(baddeps, miss);
					}
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
					if(!list_isin(baddeps, miss)) {
						baddeps = list_add(baddeps, miss);
					}
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
		strncpy(depend->name, str, sizeof(depend->name));
		strncpy(depend->version, "", sizeof(depend->version));
	}

	if(ptr == NULL) {
		return(0);
	}
	*ptr = '\0';
	strncpy(depend->name, str, sizeof(depend->name));
	ptr++;
	if(depend->mod != DEP_EQ) {
		ptr++;
	}
	strncpy(depend->version, ptr, sizeof(depend->version));
	FREE(str);
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
		{"dbpath",     required_argument, 0, 'b'},
		{"verbose",    no_argument,       0, 'v'},
		{"version",    no_argument,       0, 'V'},
		{"help",       no_argument,       0, 'h'},
		{"search",     no_argument,       0, 's'},
		{"clean",      no_argument,       0, 'c'},
		{"force",      no_argument,       0, 'f'},
		{"nodeps",     no_argument,       0, 'd'},
		{"orphans",    no_argument,       0, 'e'},
		{"nosave",     no_argument,       0, 'n'},
		{"owns",       no_argument,       0, 'o'},
		{"list",       no_argument,       0, 'l'},
		{"file",       no_argument,       0, 'p'},
		{"info",       no_argument,       0, 'i'},
		{"sysupgrade", no_argument,       0, 'u'},
		{"downloadonly", no_argument,     0, 'w'},
		{"refresh",    no_argument,       0, 'y'},
		{"cascade",    no_argument,       0, 'c'},
		{"groups",     no_argument,       0, 'g'},
		{0, 0, 0, 0}
	};

	while((opt = getopt_long(argc, argv, "ARUFQSTDYr:b:vhscVfnoldepiuwyg", opts, &option_index))) {
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
			case 'b': strcpy(pmo_dbpath, optarg); break;
			case 'c': pmo_s_clean = 1; pmo_r_cascade = 1; break;
			case 'd': pmo_nodeps = 1; break;
			case 'e': pmo_q_orphans = 1; break;
			case 'f': pmo_force = 1; break;
			case 'g': pmo_group = 1; break;
			case 'i': pmo_q_info = 1; break;
			case 'l': pmo_q_list = 1; break;
			case 'n': pmo_nosave = 1; break;
			case 'p': pmo_q_isfile = 1; break;
			case 'o': pmo_q_owns = 1; break;
			case 'r': if(realpath(optarg, pmo_root) == NULL) {
									perror("bad root path");
									return(1);
								} break;
			case 's': pmo_s_search = 1; break;
			case 'u': pmo_s_upgrade = 1; break;
			case 'v': pmo_verbose = 1; break;
			case 'w': pmo_s_downloadonly = 1; break;
			case 'y': pmo_s_sync = 1; break;
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
		if(strlen(line) == 0 || line[0] == '#') {
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
			if(key == NULL) {
				fprintf(stderr, "config: line %d: syntax error\n", linenum);
				return(1);
			}
			trim(key);
			key = strtoupper(key);
			if(ptr == NULL) {
				if(!strcmp(key, "NOPASSIVEFTP")) {
					pmo_nopassiveftp = 1;
					vprint("config: nopassiveftp\n");
				} else if(!strcmp(key, "USESYSLOG")) {
					pmo_usesyslog = 1;
					vprint("config: usesyslog\n");
				} else {
					fprintf(stderr, "config: line %d: syntax error\n", linenum);
					return(1);			
				}
			} else {
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
					} else if(!strcmp(key, "DBPATH")) {
						/* shave off the leading slash, if there is one */
						if(*ptr == '/') {
							ptr++;
						}
						strncpy(pmo_dbpath, ptr, PATH_MAX);
						vprint("config: dbpath: %s\n", pmo_dbpath);
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
						server->protocol = NULL;

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
						server->protocol = strdup(ptr);
						if(!strcmp(server->protocol, "ftp") || !strcmp(server->protocol, "http")) {
							char *slash;
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
						} else if(!strcmp(server->protocol, "file")){
							/* add a trailing slash if we need to */
							if(p[strlen(p)-1] == '/') {
								server->path = strdup(p);
							} else {
								MALLOC(server->path, strlen(p)+2);
								sprintf(server->path, "%s/", p);
							}
						} else {
							fprintf(stderr, "config: line %d: protocol %s is not supported\n", linenum, ptr);
							return(1);
						}
						/* add to the list */
						vprint("config: %s: server: %s %s %s\n", section, server->protocol, server->server, server->path);
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
			printf("  -d, --nodeps        skip dependency checks\n");
			printf("  -f, --force         force install, overwrite conflicting files\n");
		} else if(op == PM_REMOVE) {
			printf("usage:  %s {-R --remove} [options] <package>\n", myname);
			printf("options:\n");
			printf("  -c, --cascade       remove packages and all packages that depend on them\n");
			printf("  -d, --nodeps        skip dependency checks\n");
			printf("  -n, --nosave        remove configuration files as well\n");
		} else if(op == PM_UPGRADE) {
			if(pmo_freshen) {
				printf("usage:  %s {-F --freshen} [options] <file>\n", myname);
			} else {
				printf("usage:  %s {-U --upgrade} [options] <file>\n", myname);
			}
			printf("options:\n");
			printf("  -d, --nodeps        skip dependency checks\n");
			printf("  -f, --force         force install, overwrite conflicting files\n");
		} else if(op == PM_QUERY) {
			printf("usage:  %s {-Q --query} [options] [package]\n", myname);
			printf("options:\n");
			printf("  -i, --info          view package information\n");
			printf("  -g, --groups        view all members of a package group\n");
			printf("  -l, --list          list the contents of the queried package\n");
			printf("  -o, --owns <file>   query the package that owns <file>\n");
			printf("  -p, --file          pacman will query the package file [package] instead of\n");
			printf("                      looking in the database\n");
		} else if(op == PM_SYNC) {
			printf("usage:  %s {-S --sync} [options] [package]\n", myname);
			printf("options:\n");
			printf("  -c, --clean         remove packages from cache directory to free up diskspace\n");
			printf("  -d, --nodeps        skip dependency checks\n");
			printf("  -f, --force         force install, overwrite conflicting files\n");
			printf("  -g, --groups        view all members of a package group\n");
			printf("  -s, --search        search sync database for matching strings\n");
			printf("  -u, --sysupgrade    upgrade all packages that are out of date\n");
			printf("  -w, --downloadonly  download packages, but do not install/upgrade anything\n");
			printf("  -y, --refresh       download a fresh package sync database from the server\n");
		}
		printf("  -v, --verbose       be verbose\n");
		printf("  -r, --root <path>   set an alternate installation root\n");
		printf("  -b, --dbpath <path> set an alternate database location\n");
	}
}

/* Version
 */
void version(void)
{
	printf("\n");
	printf(" .--.                  Pacman v%s\n", PACVER);
	printf("/ _.-' .-.  .-.  .-.   Copyright (C) 2002-2003 Judd Vinet <jvinet@zeroflux.org>\n");
	printf("\\  '-. '-'  '-'  '-'  \n");
	printf(" '--'                  This program may be freely redistributed under\n");
	printf("                       the terms of the GNU General Public License\n\n");
}

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

/* Output a message to stderr, and (optionally) syslog */
void logaction(char *fmt, ...)
{
	char msg[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(msg, 1024, fmt, args);
	va_end(args);

	fprintf(stderr, "%s", msg);
	if(pmo_usesyslog) {
		syslog(LOG_WARNING, "%s", msg);
	}
}

/* Condense a list of strings into one long (space-delimited) string
 */
char* buildstring(PMList *strlist)
{
	char* str;
	int size = 1;
	PMList *lp;

	for(lp = strlist; lp; lp = lp->next) {
		size += strlen(lp->data) + 1;
	}
	MALLOC(str, size);
	str[0] = '\0';
	for(lp = strlist; lp; lp = lp->next) {
		strcat(str, lp->data);
		strcat(str, " ");
	}
	/* shave off the last space */
	str[strlen(str)-1] = '\0';

	return(str);
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
	if(pm_access == READ_WRITE && lckrm(lckfile)) {
		logaction("warning: could not remove lock file %s\n", lckfile);
	}
	if(workfile) {
		/* remove the current file being downloaded (as it's not complete) */
		unlink(workfile);
		FREE(workfile);
	}
	if(pmo_usesyslog) {
		closelog();
	}
	exit(signum);
}

/* vim: set ts=2 sw=2 noet: */
