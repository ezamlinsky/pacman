#
#  pacman
#
#  Copyright (c) 2002 by Judd Vinet <jvinet@zeroflux.org>
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, 
#  USA.
#

# NOTE:  Since pacman is compiled statically, you will need the static
#        libraries for zlib and glibc.

DESTDIR =
BINDIR = /usr/bin
MANDIR = /usr/man
ETCDIR = /etc

VERSION = 1.2
LIBTAR_VERSION = 1.2.4

CXX = gcc
CXXFLAGS += -DNDEBUG -O2 -Wall -pedantic -fno-exceptions -fno-rtti \
            -D_GNU_SOURCE -DVERSION=\"$(VERSION)\" -Ilibtar-$(LIBTAR_VERSION)/lib
LDFLAGS += -static -Llibtar-$(LIBTAR_VERSION)/lib -ltar -lz

OBJECTS = pacman.o

all: libtar pacman man

pacman: $(OBJECTS)
	$(CXX) $(OBJECTS) -o $@ $(LDFLAGS)

pacman.o: pacman.c pacman.h
	$(CXX) $(CXXFLAGS) -c pacman.c

man: pacman.8 pacsync.8 makepkg.8

%: %.in
	sed -e "s/#VERSION#/$(VERSION)/" $< > $@

dist: distclean
	(cd ..; tar czvf pacman-$(VERSION).tar.gz pacman-$(VERSION))

libtar:
	(tar xzf libtar-$(LIBTAR_VERSION).tar.gz; \
	cd libtar-$(LIBTAR_VERSION); \
	LDFLAGS="" ./configure --disable-encap --disable-encap-install; \
	make;)

install: all
	install -D -m0755 pacman $(DESTDIR)$(BINDIR)/pacman
	install -D -m0644 pacman.8 $(DESTDIR)$(MANDIR)/man8/pacman.8
	install -D -m0644 pacsync.8 $(DESTDIR)$(MANDIR)/man8/pacsync.8
	install -D -m0644 makepkg.8 $(DESTDIR)$(MANDIR)/man8/makepkg.8
	install -D -m0755 makepkg $(DESTDIR)$(BINDIR)/makepkg
	install -D -m0755 makeworld $(DESTDIR)$(BINDIR)/makeworld
	install -D -m0755 pacsync $(DESTDIR)$(BINDIR)/pacsync
	@echo ""
	@echo "*** If this is a first-time install, you should copy makepkg.conf"
	@echo "*** and pacsync.conf to /etc now."
	@echo ""

clean:
	rm -f *~ *.o *.8

distclean: clean
	rm -f pacman
	rm -rf libtar-$(LIBTAR_VERSION)

# End of file
