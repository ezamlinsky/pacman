/*
 *  diskspace.c
 *
 *  Copyright (c) 2010-2011 Pacman Development Team <pacman-dev@archlinux.org>
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
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#if defined(HAVE_MNTENT_H)
#include <mntent.h>
#endif
#if defined(HAVE_SYS_STATVFS_H)
#include <sys/statvfs.h>
#endif
#if defined(HAVE_SYS_PARAM_H)
#include <sys/param.h>
#endif
#if defined(HAVE_SYS_MOUNT_H)
#include <sys/mount.h>
#endif
#if defined(HAVE_SYS_UCRED_H)
#include <sys/ucred.h>
#endif
#if defined(HAVE_SYS_TYPES_H)
#include <sys/types.h>
#endif

/* libarchive */
#include <archive.h>
#include <archive_entry.h>

/* libalpm */
#include "diskspace.h"
#include "alpm_list.h"
#include "util.h"
#include "log.h"
#include "trans.h"
#include "handle.h"

static int mount_point_cmp(const void *p1, const void *p2)
{
	const alpm_mountpoint_t *mp1 = p1;
	const alpm_mountpoint_t *mp2 = p2;
	return(strcmp(mp1->mount_dir, mp2->mount_dir));
}

static alpm_list_t *mount_point_list(void)
{
	alpm_list_t *mount_points = NULL;
	alpm_mountpoint_t *mp;

#if defined HAVE_GETMNTENT
	struct mntent *mnt;
	FILE *fp;
	FSSTATSTYPE fsp;

	fp = setmntent(MOUNTED, "r");

	if (fp == NULL) {
		return(NULL);
	}

	while((mnt = getmntent(fp))) {
		if(statvfs(mnt->mnt_dir, &fsp) != 0) {
			_alpm_log(PM_LOG_WARNING,
					_("could not get filesystem information for %s\n"), mnt->mnt_dir);
			continue;
		}

		MALLOC(mp, sizeof(alpm_mountpoint_t), RET_ERR(PM_ERR_MEMORY, NULL));
		mp->mount_dir = strdup(mnt->mnt_dir);
		memcpy(&(mp->fsp), &fsp, sizeof(FSSTATSTYPE));

		mp->blocks_needed = 0l;
		mp->max_blocks_needed = 0l;
		mp->used = 0;

		mount_points = alpm_list_add(mount_points, mp);
	}

	endmntent(fp);
#elif defined HAVE_GETMNTINFO
	int entries;
	FSSTATSTYPE *fsp;

	entries = getmntinfo(&fsp, MNT_NOWAIT);

	if (entries < 0) {
		return NULL;
	}

	for(; entries-- > 0; fsp++) {
		MALLOC(mp, sizeof(alpm_mountpoint_t), RET_ERR(PM_ERR_MEMORY, NULL));
		mp->mount_dir = strdup(fsp->f_mntonname);
		memcpy(&(mp->fsp), fsp, sizeof(FSSTATSTYPE));

		mp->blocks_needed = 0l;
		mp->max_blocks_needed = 0l;
		mp->used = 0;

		mount_points = alpm_list_add(mount_points, mp);
	}
#endif

	mount_points = alpm_list_msort(mount_points, alpm_list_count(mount_points),
			mount_point_cmp);
	return(mount_points);
}

static alpm_list_t *match_mount_point(const alpm_list_t *mount_points,
		const char *file)
{
	char real_path[PATH_MAX];
	snprintf(real_path, PATH_MAX, "%s%s", handle->root, file);

	alpm_list_t *mp = alpm_list_last(mount_points);
	do {
		alpm_mountpoint_t *data = mp->data;

		if(strncmp(data->mount_dir, real_path, strlen(data->mount_dir)) == 0) {
			return(mp);
		}

		mp = mp->prev;
	} while (mp != alpm_list_last(mount_points));

	/* should not get here... */
	return(NULL);
}

static int calculate_removed_size(const alpm_list_t *mount_points,
		pmpkg_t *pkg)
{
	alpm_list_t *file;

	alpm_list_t *files = alpm_pkg_get_files(pkg);
	for(file = files; file; file = file->next) {
		alpm_list_t *mp;
		alpm_mountpoint_t *data;
		struct stat st;
		char path[PATH_MAX];
		const char *filename = file->data;

		/* skip directories to be consistent with libarchive that reports them
		 * to be zero size and to prevent multiple counting across packages */
		if(*(filename + strlen(filename) - 1) == '/') {
			continue;
		}

		mp = match_mount_point(mount_points, filename);
		if(mp == NULL) {
			_alpm_log(PM_LOG_WARNING,
					_("could not determine mount point for file %s"), filename);
			continue;
		}

		snprintf(path, PATH_MAX, "%s%s", handle->root, filename);
		_alpm_lstat(path, &st);

		/* skip symlinks to be consistent with libarchive that reports them to
		 * be zero size */
		if(S_ISLNK(st.st_mode)) {
			continue;
		}

		data = mp->data;
		/* the addition of (divisor - 1) performs ceil() with integer division */
		data->blocks_needed -=
			(st.st_size + data->fsp.f_bsize - 1l) / data->fsp.f_bsize;
	}

	return(0);
}

static int calculate_installed_size(const alpm_list_t *mount_points,
		pmpkg_t *pkg)
{
	int ret=0;
	struct archive *archive;
	struct archive_entry *entry;
	const char *file;

	if ((archive = archive_read_new()) == NULL) {
		pm_errno = PM_ERR_LIBARCHIVE;
		ret = -1;
		goto cleanup;
	}

	archive_read_support_compression_all(archive);
	archive_read_support_format_all(archive);

	if(archive_read_open_filename(archive, pkg->origin_data.file,
				ARCHIVE_DEFAULT_BYTES_PER_BLOCK) != ARCHIVE_OK) {
		pm_errno = PM_ERR_PKG_OPEN;
		ret = -1;
		goto cleanup;
	}

	while(archive_read_next_header(archive, &entry) == ARCHIVE_OK) {
		alpm_list_t *mp;
		alpm_mountpoint_t *data;

		file = archive_entry_pathname(entry);

		/* approximate space requirements for db entries */
		if(file[0] == '.') {
			file = alpm_option_get_dbpath();
		}

		mp = match_mount_point(mount_points, file);
		if(mp == NULL) {
			_alpm_log(PM_LOG_WARNING,
					_("could not determine mount point for file %s"), file);
			continue;
		}

		data = mp->data;
		/* the addition of (divisor - 1) performs ceil() with integer division */
		data->blocks_needed +=
			(archive_entry_size(entry) + data->fsp.f_bsize - 1l) / data->fsp.f_bsize;
		data->used = 1;

		if(archive_read_data_skip(archive)) {
			_alpm_log(PM_LOG_ERROR, _("error while reading package %s: %s\n"),
					pkg->name, archive_error_string(archive));
			pm_errno = PM_ERR_LIBARCHIVE;
			break;
		}
	}

	archive_read_finish(archive);

cleanup:
	return(ret);
}

int _alpm_check_diskspace(pmtrans_t *trans, pmdb_t *db_local)
{
	alpm_list_t *mount_points, *i;
	size_t replaces = 0, current = 0, numtargs;
	int abort = 0;
	alpm_list_t *targ;

	numtargs = alpm_list_count(trans->add);
	mount_points = mount_point_list();
	if(mount_points == NULL) {
		_alpm_log(PM_LOG_ERROR, _("could not determine filesystem mount points"));
		return(-1);
	}

	replaces = alpm_list_count(trans->remove);
	if(replaces) {
		numtargs += replaces;
		for(targ = trans->remove; targ; targ = targ->next, current++) {
			pmpkg_t *local_pkg;
			int percent = (current * 100) / numtargs;
			PROGRESS(trans, PM_TRANS_PROGRESS_DISKSPACE_START, "", percent,
					numtargs, current);

			local_pkg = targ->data;
			calculate_removed_size(mount_points, local_pkg);
		}
	}

	for(targ = trans->add; targ; targ = targ->next, current++) {
		pmpkg_t *pkg, *local_pkg;
		int percent = (current * 100) / numtargs;
		PROGRESS(trans, PM_TRANS_PROGRESS_DISKSPACE_START, "", percent,
				numtargs, current);

		pkg = targ->data;
		/* is this package already installed? */
		local_pkg = _alpm_db_get_pkgfromcache(db_local, pkg->name);
		if(local_pkg) {
			calculate_removed_size(mount_points, local_pkg);
		}
		calculate_installed_size(mount_points, pkg);

		for(i = mount_points; i; i = alpm_list_next(i)) {
			alpm_mountpoint_t *data = i->data;
			if(data->blocks_needed > data->max_blocks_needed) {
				data->max_blocks_needed = data->blocks_needed;
			}
		}
	}

	PROGRESS(trans, PM_TRANS_PROGRESS_DISKSPACE_START, "", 100,
			numtargs, current);

	for(i = mount_points; i; i = alpm_list_next(i)) {
		alpm_mountpoint_t *data = i->data;
		if(data->used == 1) {
			/* cushion is roughly min(5% capacity, 20MiB) */
			long fivepc = ((long)data->fsp.f_blocks / 20) + 1;
			long twentymb = (20 * 1024 * 1024 / (long)data->fsp.f_bsize) + 1;
			long cushion = fivepc < twentymb ? fivepc : twentymb;

			_alpm_log(PM_LOG_DEBUG, "partition %s, needed %ld, cushion %ld, free %ld\n",
					data->mount_dir, data->max_blocks_needed, cushion,
					(unsigned long)data->fsp.f_bfree);
			if(data->max_blocks_needed + cushion >= 0 &&
			   (unsigned long)(data->max_blocks_needed + cushion) > data->fsp.f_bfree) {
				_alpm_log(PM_LOG_ERROR, _("Partition %s too full: %ld blocks needed, %ld blocks free)\n"),
						data->mount_dir, data->max_blocks_needed + cushion,
						(unsigned long)data->fsp.f_bfree);
				abort = 1;
			}
		}
	}

	for(i = mount_points; i; i = alpm_list_next(i)) {
		alpm_mountpoint_t *data = i->data;
		FREE(data->mount_dir);
	}
	FREELIST(mount_points);

	if(abort) {
		RET_ERR(PM_ERR_DISK_SPACE, -1);
	}

	return(0);
}

/* vim: set ts=2 sw=2 noet: */