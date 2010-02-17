/*
 * opts.h defines api for dln to use option parsing functions
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

#ifndef DLN_OPTS_H
#define DLN_OPTS_H

#include <unistd.h>

typedef struct {
	int verbose_flag;
	char* output_file;
	int safe_mode;
	int window_abs;
	double window_rel;
} dlnopt_t;


extern dlnopt_t dopt;

void dopt_init ();
int dopt_proc (int argc, char *argv[], const char *short_options, const struct option *long_options);
void dopt_finalize ();

#endif /* DLN_OPTS_H */
