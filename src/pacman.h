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
#ifndef _PAC_PACMAN_H
#define _PAC_PACMAN_H

#ifndef PACVER
#define PACVER    "2.4.1"
#endif

#ifndef PKGDIR
#define PKGDIR		"var/lib/pacman"
#endif

#ifndef PACCONF
#define PACCONF   "etc/pacman.conf"
#endif

/* Operations */
#define PM_MAIN			1
#define PM_ADD			2
#define PM_REMOVE		3
#define	PM_UPGRADE	4
#define PM_QUERY		5
#define PM_SYNC     6
#define PM_DEPTEST  7

int pacman_add(pacdb_t *db, PMList *targets);
int pacman_remove(pacdb_t *db, PMList *targets);
int pacman_upgrade(pacdb_t *db, PMList *targets);
int pacman_query(pacdb_t *db, PMList *targets);
int pacman_sync(pacdb_t *db, PMList *targets);
int pacman_deptest(pacdb_t *db, PMList *targets);

PMList* checkdeps(pacdb_t *db, unsigned short op, PMList *targets);
int resolvedeps(pacdb_t *local, PMList *databases, syncpkg_t *sync, PMList *list, PMList *trail);
int splitdep(char *depstr, depend_t *depend);

int lckmk(char *file, int retries, unsigned int sleep_secs);
int lckrm(char *lckfile);
void cleanup(int signum);

#endif /* PACMAN_H */

/* vim: set ts=2 sw=2 noet: */
