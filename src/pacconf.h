/*
 *  pacconf.h
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
#ifndef _PAC_PACCONF_H
#define _PAC_PACCONF_H

#ifndef PACVER
#define PACVER    "2.9.5"
#endif

#ifndef PACDBDIR
#define PACDBDIR  "var/lib/pacman"
#endif

#ifndef PKGEXT
#define PKGEXT    ".pkg.tar.gz"
#endif

#ifndef PACCONF
#define PACCONF   "/etc/pacman.conf"
#endif

#ifndef CACHEDIR
#define CACHEDIR  "var/cache/pacman/pkg"
#endif

#endif /* PACCONF_H */

/* vim: set ts=2 sw=2 noet: */
