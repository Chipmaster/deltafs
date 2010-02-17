/*
 * opts.c implements option parsing for defs as defined in opts.h
 * Copyright (C)      Radek Podgorny <radek@podgorny.cz>
 * Copyright (C)      Bernd Schubert <bernd-schubert@gmx.de>
 * Copyright (C) 2009 Patrick Stetter <chipmaster32@gmail.com>
 * Copyright (C) 2009 Corey McClymonds <galeru@gmail.com>
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
#include <stdio.h>
#include <dirent.h> /* PATH_MAX */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "opts.h"
#include "version.h"


dopt_t dopt;

void dopt_init() 
{
	memset(&dopt, 0, sizeof(dopt_t)); /* initialize with zeros */
	dopt.window_rel = 0;
}

void dopt_finalize()
{
	if (!dopt.window_rel && !dopt.window_abs) {
		dopt.window_rel = .01; /* use relative window of 1/100 if no option specified */
		dopt.buffer = 200000;
	}
	else if (dopt.window_rel && dopt.window_abs) {
		fprintf(stderr, "You may not specify windowrel and windowabs\n");
		exit(1); /* still very early stage, we can abort here */
	}
	else if (dopt.window_abs) {
		dopt.buffer = dopt.window_abs; /* window_abs set */
	}
	else {
		dopt.buffer = 200000;
	}
}


/*
 * Add a trailing slash at the end of a branch. So functions using this
 * path don't have to care about this slash themselves.
 */
char *add_trailing_slash(char *path) 
{
	int len = strlen(path);
	if (path[len - 1] == '/') {
		return path; /* no need to add a slash, already there */
	}
  
	path = realloc(path, len + 2); /* +1 for '/' and +1 for '\0' */
	if (!path) {
		fprintf(stderr, "%s: realloc() failed, aborting\n", __func__);
		exit(1); /* still very early stage, we can abort here */
	}
  
	strcat(path, "/");
	return path;
}


/*
 * Take a relative path as argument and return the absolute path by using the 
 * current working directory. The return string is malloc'ed with this function.
 */
char *make_absolute(char *relpath)
{
	/* Already an absolute path */
	if (*relpath == '/') return relpath;
  
	char cwd[PATH_MAX];
	if (!getcwd(cwd, PATH_MAX)) {
		perror("Unable to get current working directory");
		return NULL;
	}
  
	size_t cwdlen = strlen(cwd);
	if (!cwdlen) {
		fprintf(stderr, "Zero-sized length of CWD!\n");
		return NULL;
	}
  
	/*
	 * 3 due to: +1 for '/' between cwd and relpath
	 * +1 for trailing '/'
	 */
	int abslen = cwdlen + strlen(relpath) + 2;
	if (abslen > PATH_MAX) {
		fprintf(stderr, "Absolute path too long!\n");
		return NULL;
	}
  
	char *abspath = malloc(abslen);
	if (abspath == NULL) {
		fprintf(stderr, "%s: malloc failed\n", __func__);
		exit(1); /* still at early stage, we can abort */
	}
	strcpy(abspath, cwd);
	strcat(abspath, "/");
	strcat(abspath, relpath);
	return abspath;
}


/*
 * Options without any -X prefix, so this defines our directory
 */
static int parse_directory(const char *arg)
{
	/* Don't touch the last argument as it's the mountpoint */
	struct stat buf;
  
	if (dopt.directory_set) {
		return 1;
	}
  
	char *fixed = strdup(arg);
	fixed = make_absolute(fixed);
	fixed = add_trailing_slash(fixed);
  
	if (stat(fixed, &buf) == -1) {  /* directory must exist */
		fprintf(stderr, "Must specify a valid directory\n");
		exit(1);
	}

	dopt.directory = fixed;
	dopt.directory_set = 1;
	return 0;
}


/*
 * fuse passes arguments with the argument prefix, e.g.
 * "-o window=16384" will give us "window=16384"
 * and we need to cut off the "window=" part
 */
int get_arg(const char *arg)
{
	char *str = index(arg, '=');
	
	if (!str) {
		fprintf(stderr, "parameter not properly specified, aborting!\n");
		exit(1); /* still early phase, we can abort */
	}

	str++; /* jump over the '=' */
	str = strdup(str);
	
	return atoi(str);
}


/*
 * fuse passes arguments with the argument prefix, e.g.
 * "-o window=16384" will give us "window=16384"
 * and we need to cut off the "window=" part
 */
double get_argf(const char *arg)
{
	char *str = index(arg, '=');
	
	if (!str) {
		fprintf(stderr, "parameter not properly specified, aborting!\n");
		exit(1); /* still early phase, we can abort */
	}

	str++; /* jump over the '=' */
	str = strdup(str);
	
	return atof(str);
}

static void print_help(const char *progname){
	printf(
		"DeltaFS "VERSION"\n"
		"by Patrick Stetter <chipmaster32@gmail.com>\n"
		"Corey McClymonds <galeru@gmail.com>\n"
		"\n"
		"Usage %s [options] directory mountpoint\n"
		"\n"
		"general options:\n"
		"    -h   --help            print help\n"
		"    -V   --version         print version\n"
		"\n"
		"DeltaFS options:\n"
		"    -o windowabs=size         delta window absolute size\n"
		"    -o windowrel=size         delta window relative size size\n"
		"\n",
		progname);
}


int defs_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs)
{
	(void)data;
  
	int res = 0;
	double dres = 0;
	
	switch (key) {
	case FUSE_OPT_KEY_NONOPT:
		return parse_directory(arg);
	case KEY_HELP:
		print_help(outargs->argv[0]);
		fuse_opt_add_arg(outargs, "-ho");
		return 0;
	case KEY_VERSION:
		printf("DeltaFS "VERSION"\n");
		return 1;
	case KEY_WINDOW_ABS:
		res = get_arg(arg);
		if (res >= (1U<<14) && res <= (1U<<23)) {
			dopt.window_abs = res;
		}
		return 0;
	case KEY_WINDOW_REL:
		dres = get_argf(arg);
		if (dres > 0 && dres <= 1) {
			dopt.window_rel = dres;
		}
		return 0;
	default:
		return 1;
	}
}
