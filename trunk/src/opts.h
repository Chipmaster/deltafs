/*
 * opts.h defines api for defs to use option parsing functions
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

#ifndef OPTS_H
#define OPTS_H

#include <fuse.h>

typedef struct {
	int directory_set;
	char *directory;
	int window_abs;
	double window_rel;
	int buffer;
} dopt_t;


enum {
	KEY_HELP,
	KEY_VERSION,
	KEY_WINDOW_ABS,
	KEY_WINDOW_REL
};


extern dopt_t dopt;

void dopt_init();
int defs_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs);
char *make_absolute(char *relpath);
char *add_trailing_slash(char *path);
void dopt_finalize();

#endif /* OPTS_H */
