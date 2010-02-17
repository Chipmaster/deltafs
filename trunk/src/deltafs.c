/*
 * DeltaFS
 * Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
 * Copyright (C) 2009  Corey McClymonds <galeru@gmail.com>
 * Copyright (C) 2009  Patrick Stetter  <chipmaster32@gmail.com>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif /* linux */

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <limits.h> /* PATH_MAX */
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif /* HAVE_SETXATTR */

#include "delta.h"
#include "sql.h"
#include "opts.h"

static struct fuse_opt defs_opts[] = {
	FUSE_OPT_KEY("--help", KEY_HELP),
	FUSE_OPT_KEY("--version", KEY_VERSION),
	FUSE_OPT_KEY("-h", KEY_HELP),
	FUSE_OPT_KEY("-V", KEY_VERSION),
	FUSE_OPT_KEY("windowabs=%s", KEY_WINDOW_ABS),
	FUSE_OPT_KEY("windowrel=%s", KEY_WINDOW_REL),
	FUSE_OPT_END
};


char *defs_fix_path(const char *path)
{
	char *fixed_path = strdup(dopt.directory);
	fixed_path[strlen(fixed_path)-1] = '\0';   /* Remove trailing / */
	fixed_path = realloc(fixed_path, strlen(fixed_path) + strlen(path) + 1);
	strcat(fixed_path, path);
	return fixed_path;
}

static int defs_getattr(const char *path, struct stat *stbuf)
{
	int res;
	int fd;
	char *parent = NULL;
	char *fixed_path = defs_fix_path(path);

	sql_get_parent(fixed_path, &parent);

	res = lstat(fixed_path, stbuf);
	if (res == -1) {
		free(fixed_path);
		return -errno;
	}  
  
	fd = open(fixed_path, O_RDONLY);
	if (fd == -1) {
		free(fixed_path);
		return -errno;
	}
   
	if (parent) { /* it's a delta file */
		stbuf->st_size = sql_get_size(fixed_path);
		free(parent);
	}
  
	close(fd);
	free(fixed_path);
	return 0;
}

static int defs_access(const char *path, int mask)
{
	int res;
  
	char *fixed_path = defs_fix_path(path);

	res = access(fixed_path, mask);
	if (res == -1) {
		free(fixed_path);
		return -errno;
	}

	free(fixed_path);
	return 0;
}

static int defs_readlink(const char *path, char *buf, size_t size)
{
	int res;
  
	char *fixed_path = defs_fix_path(path);

	res = readlink(fixed_path, buf, size - 1);
	if (res == -1) {
		free(fixed_path);
		return -errno;
	}

	buf[res] = '\0';
	free(fixed_path);    
	return 0;
}


static int defs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			off_t offset, struct fuse_file_info *fi)
{
	DIR *dp;
	struct dirent *de;
  
	(void) offset;
	(void) fi;
  
	char *fixed_path = defs_fix_path(path);

	dp = opendir(fixed_path);
	if (dp == NULL) {
		free(fixed_path);
		return -errno;
	}
  
	while ((de = readdir(dp)) != NULL) {
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;
		if (filler(buf, de->d_name, &st, 0)) {
			break;
		}
	}
  
	closedir(dp);
	free(fixed_path);
	return 0;
}

static int defs_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res;

	char *fixed_path = defs_fix_path(path);
  
	/* On Linux this could just be 'mknod(path, mode, rdev)' but this
	   is more portable */
	if (S_ISREG(mode)) {
		res = open(fixed_path, O_CREAT | O_EXCL | O_WRONLY, mode);
		if (res >= 0) {
			res = close(res);
		}
	} 
	else if (S_ISFIFO(mode)) {
		res = mkfifo(fixed_path, mode);
	}
	else {
		res = mknod(fixed_path, mode, rdev);
	}
	if (res == -1) {
		free(fixed_path);
		return -errno;
	}
  
	free(fixed_path);
	return 0;
}

static int defs_mkdir(const char *path, mode_t mode)
{
	int res;
  
	char *fixed_path = defs_fix_path(path);

	res = mkdir(fixed_path, mode);
	if (res == -1) {
		free(fixed_path);
		return -errno;
	}

	free(fixed_path);
	return 0;
}

static int defs_unlink(const char *path)
{
	int res, r;
	char *parent = NULL;
	int childc;
	char **childv;
	int i;

	char *fixed_path = defs_fix_path(path);

	sql_get_parent(fixed_path, &parent);
	sql_get_children(fixed_path, &childc, &childv);

	if (parent) { /* child */
		sql_remove_child(fixed_path);
		free(parent);
	} else if (childc != 0) { /* parent with children */
		res = xdelta_promote(fixed_path, childc, childv);
    
		for (i = 1; i < childc; ++i) {
			r = sql_get_size(childv[i]);
			sql_remove_child(childv[i]);
			sql_add(childv[0], childv[i], r);
		}
	}
  
	for (i = 0; i < childc; ++i) {
		free(childv[i]);
	}
	free(childv);
	
	res = unlink(fixed_path);
	if (res == -1) {
		free(fixed_path);
		return -errno;
	}
  
	free(fixed_path);
	return 0;
}

static int defs_rmdir(const char *path)
{
	int res;

	char *fixed_path = defs_fix_path(path);
  
	res = rmdir(fixed_path);
	if (res == -1) {
		free(fixed_path);
		return -errno;
	}
  
	free(fixed_path);
	return 0;
}

static int defs_symlink(const char *from, const char *to)
{
	int res;

	char *fixed_from = defs_fix_path(from);
	char *fixed_to = defs_fix_path(to);

	res = symlink(fixed_from, fixed_to);
	if (res == -1) {
		free(fixed_from);
		free(fixed_to);
		return -errno;
	}

	free(fixed_from);
	free(fixed_to);
	return 0;
}

static int defs_rename(const char *from, const char *to)
{
	int res, r;
	char *parent = NULL;
	int childc;
	char **childv;
	int i;

	char *fixed_from = defs_fix_path(from);
	char *fixed_to = defs_fix_path(to);

	sql_get_parent(fixed_from, &parent);
	sql_get_children(fixed_from, &childc, &childv);

	/* Currently this only supports a one level hierarchy */
	if (parent) {  /* child */
		r = sql_get_size(fixed_from);
		sql_remove_child(fixed_from);
		sql_add(parent, fixed_to, r);
		free(parent);
	} else if (childc != 0) {  /* parent */
		for (i = 0; i < childc; ++i) {
			r = sql_get_size(childv[i]);
			sql_remove_child(childv[i]);
			sql_add(fixed_to, childv[i], r);
		}
	}

	for (i = 0; i < childc; ++i) {
		free(childv[i]);
	}
	free(childv);

	res = rename(fixed_from, fixed_to);
	if (res == -1) {
		free(fixed_from);
		free(fixed_to);
		return -errno;
	}
  
	free(fixed_from);
	free(fixed_to);
	return 0;
}

static int defs_link(const char *from, const char *to)
{
	int rc;
	char *fixed_from = defs_fix_path(from);
	char *fixed_to = defs_fix_path(to);
	struct stat statbuf;
	
	char *parent = NULL;

	sql_get_parent(fixed_from, &parent);

	if (parent) { /* Don't allow links of links */
		free(parent);
		return -1;
	}
	
	if (stat(fixed_from, &statbuf) == -1) {
		return -1;
	}

	sql_add(fixed_from, fixed_to, statbuf.st_size);
	rc = xdelta_link(fixed_from, fixed_to);

	free(fixed_from);
	free(fixed_to);
	return rc;
}

static int defs_chmod(const char *path, mode_t mode)
{
	int res;

	char *fixed_path = defs_fix_path(path);
  
	res = chmod(fixed_path, mode);
	if (res == -1) {
		free(fixed_path);
		return -errno;
	}
  
	free(fixed_path);
	return 0;
}

static int defs_chown(const char *path, uid_t uid, gid_t gid)
{
	int res;
  
	char *fixed_path = defs_fix_path(path);

	res = lchown(fixed_path, uid, gid);
	if (res == -1) {
		free(fixed_path);
		return -errno;
	}
  
	free(fixed_path);
	return 0;
}

static int defs_truncate(const char *path, off_t size)
{
	int res;
	char *parent = NULL;
	int childc;
	char **childv;
	int i;

	char *fixed_path = defs_fix_path(path);

	sql_get_parent(fixed_path, &parent);
	sql_get_children(fixed_path, &childc, &childv);
  
	if (parent || (childc != 0)) {
		res = xdelta_truncate(fixed_path, size, parent, childc, childv);
		free(parent);
	}
	else {
		res = truncate(fixed_path, size);
	}
	for (i = 0; i < childc; ++i) {
		free(childv[i]);
	}
	free(childv);
	if (res == -1) {
		free(fixed_path);
		return -errno;
	}

	free(fixed_path);
	return 0;
}

static int defs_utimens(const char *path, const struct timespec ts[2])
{
	int res;
	struct timeval tv[2];
  
	char *fixed_path = defs_fix_path(path);

	tv[0].tv_sec = ts[0].tv_sec;
	tv[0].tv_usec = ts[0].tv_nsec / 1000;
	tv[1].tv_sec = ts[1].tv_sec;
	tv[1].tv_usec = ts[1].tv_nsec / 1000;
  
	res = utimes(fixed_path, tv);
	if (res == -1) {
		free(fixed_path);
		return -errno;
	}
  
	free(fixed_path);
	return 0;
}

static int defs_open(const char *path, struct fuse_file_info *fi)
{
	int res;
  
	char *fixed_path = defs_fix_path(path);

	res = open(fixed_path, fi->flags);
	if (res == -1) {
		free(fixed_path);
		return -errno;
	}
  
	close(res);
	free(fixed_path);
	return 0;
}

static int defs_read(const char *path, char *buf, size_t size, off_t offset,
		     struct fuse_file_info *fi)
{
	int fd;
	int res;

	char *fixed_path = defs_fix_path(path);
	char* parent = NULL;
	sql_get_parent(fixed_path, &parent);

	(void) fi;
	fd = open(fixed_path, O_RDONLY);
	if (fd == -1) {
		free(fixed_path);
		return -errno;
	}

	if (parent) {
		res = xdelta_read(fixed_path, parent, size, offset, buf);
		free(parent);
	}
	else {
		res = pread(fd, buf, size, offset);
		if (res == -1) {
			res = -errno;
		}
	}

	close(fd);
	free(fixed_path);
	return res;
}

static int defs_write(const char *path, const char *buf, size_t size,
		      off_t offset, struct fuse_file_info *fi)
{
	int fd;
	int res;

	char *fixed_path = defs_fix_path(path);

	(void) fi;
	fd = open(fixed_path, O_RDWR);
	if (fd == -1) {
		free(fixed_path);
		return -errno;
	}
    
	int childc;
	char **childv;
	int i;
	char *parent = NULL;
  
	sql_get_parent(fixed_path, &parent);
	sql_get_children(fixed_path, &childc, &childv);
	printf("Has %d children\n", childc);
	for (i = 0; i < childc; ++i) {
		printf("Child %d - %s", i, childv[i]);
	}
	printf("Parent %s\n", parent);
	fflush(NULL);
  
	if (parent) { /* child */
		res = xdelta_write(fixed_path, buf, size, offset, childc, childv, parent);
		free(parent);
	}
	else if (childc != 0) { /* parent */
		res = xdelta_write(fixed_path, buf, size, offset, childc, childv, parent);
	}
	else { /* neither */
		res = pwrite(fd, buf, size, offset);
		if (res == -1) {
			res = -errno;
		}
	}

	for (i = 0; i < childc; ++i) {
		free(childv[i]);
	}
	free(childv);

	close(fd);
	free(fixed_path);
	return res;
}

static int defs_statfs(const char *path, struct statvfs *stbuf)
{
	int res;

	char *fixed_path = defs_fix_path(path);
  
	res = statvfs(fixed_path, stbuf);
	if (res == -1) {
		free(fixed_path);
		return -errno;
	}
  
	free(fixed_path);
	return 0;
}

static int defs_release(const char *path, struct fuse_file_info *fi)
{
	/* Just a stub. This method is optional and can safely be left
	   unimplemented */
  
	char *fixed_path = defs_fix_path(path);

	(void) fixed_path;
	(void) fi;
  
	free(fixed_path);
	return 0;
}

static int defs_fsync(const char *path, int isdatasync,
		      struct fuse_file_info *fi)
{
	/* Just a stub. This method is optional and can safely be left
	   unimplemented */
  
	char *fixed_path = defs_fix_path(path);

	(void) fixed_path;
	(void) isdatasync;
	(void) fi;

	free(fixed_path);
	return 0;
}

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int defs_setxattr(const char *path, const char *name, const char *value,
			 size_t size, int flags)
{
	char *fixed_path = defs_fix_path(path);

	int res = lsetxattr(fixed_path, name, value, size, flags);
	if (res == -1) {
		free(fixed_path);
		return -errno;
	}

	free(fixed_path);
	return 0;
}

static int defs_getxattr(const char *path, const char *name, char *value,
			 size_t size)
{
	char *fixed_path = defs_fix_path(path);

	int res = lgetxattr(fixed_path, name, value, size);
	if (res == -1) {
		free(fixed_path);
		return -errno;
	}
  
	free(fixed_path);
	return res;
}

static int defs_listxattr(const char *path, char *list, size_t size)
{
	char *fixed_path = defs_fix_path(path);

	int res = llistxattr(fixed_path, list, size);
	if (res == -1) {
		free(fixed_path);
		return -errno;
	}

	free(fixed_path);
	return res;
}

static int defs_removexattr(const char *path, const char *name)
{
	char *fixed_path = defs_fix_path(path);

	int res = lremovexattr(fixed_path, name);
	if (res == -1) {
		free(fixed_path);
		return -errno;
	}

	free(fixed_path);
	return 0;
}
#endif /* HAVE_SETXATTR */

static struct fuse_operations defs_oper = {
	.getattr	= defs_getattr,
	.access 	= defs_access,
	.readlink	= defs_readlink,
	.readdir	= defs_readdir,
	.mknod  	= defs_mknod,
	.mkdir  	= defs_mkdir,
	.symlink	= defs_symlink,
	.unlink 	= defs_unlink,
	.rmdir  	= defs_rmdir,
	.rename 	= defs_rename,
	.link		= defs_link,
	.chmod  	= defs_chmod,
	.chown  	= defs_chown,
	.truncate	= defs_truncate,
	.utimens	= defs_utimens,
	.open		= defs_open,
	.read		= defs_read,
	.write  	= defs_write,
	.statfs 	= defs_statfs,
	.release	= defs_release,
	.fsync  	= defs_fsync,
#ifdef HAVE_SETXATTR
	.setxattr	= defs_setxattr,
	.getxattr	= defs_getxattr,
	.listxattr	= defs_listxattr,
	.removexattr	= defs_removexattr,
#endif
};

int main(int argc, char *argv[])
{
	int rc;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	dopt_init();

	if (fuse_opt_parse(&args, NULL, defs_opts, defs_opt_proc) == -1) {
		return 1;
	}

	dopt_finalize();
	
	rc = sql_open();
	if (rc) {
		sql_close();
		return -1;
	}

	rc = sql_init_db();
  
	umask(0); /* change to fix permissions */
	rc = fuse_main(args.argc, args.argv, &defs_oper, NULL);
	sql_close();
	return rc;
}
