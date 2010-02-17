/* 
 * opts.c implements option parsing for dln as defined in opts.h
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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>

#include "../version.h"
#include "opts.h"

dlnopt_t dopt;

void dopt_init ()
{
	memset(&dopt, 0, sizeof(dlnopt_t));  /* initialize with zeroes */
	dopt.window_rel = 0;
}

void dopt_finalize ()
{
	if (!dopt.window_rel && !dopt.window_abs) {
                dopt.window_rel = .01; /* use relative window of 1/100 if no option specified */
        }
        else if (dopt.window_rel && dopt.window_abs) {
                fprintf(stderr, "You may not specify windowrel and windowabs\n");
                exit(1); /* still very early stage, we can abort here */
        }

}

void print_help (const char *program_name)
{
	printf(
		"DLN (DeltaFS) "VERSION"\n"
		"by Patrick Stetter <chipmaster32@gmail.com>\n"
		"Corey McClymonds <galeru@gmail.com>\n"
		"\n"
		"Usage %s [options] SrcFile InFile\n"
		"\n"
		"general options:\n"
		"    -h   --help            print help\n"
		"    -V   --version         print version\n"
		"\n"
		"DLN options:\n"
		"    -v   --verbose         verbose mode\n"
		"    -s   --silent          silent mode\n"
		"    -S   --safe            safe mode\n"
		"    -o   --output          specify a different output file\n"
		"    -a   --windowabs       specify a delta window absolute size\n"
		"    -r   --windowrel       specify a delta window relative size\n",
		program_name);
}

int dopt_proc (int argc, char *argv[], const char *short_options, const struct option *long_options)
{
	int next_option;
	int res = 0;
	double dres = 0;


	do {
		next_option = getopt_long(argc, argv, short_options, long_options, NULL);
		
		switch (next_option) {
		case 'h':  /* -h or --help */
			print_help(argv[0]);
			exit(0);
			
		case 'V':  /* -V or --version */
			printf("DLN (DeltaFS) "VERSION"\n");
			exit(0);
			
		case 'v':  /* -v or verbose */
			dopt.verbose_flag = 1;
			break;
			
		case 's':  /* -s or --silent */
			dopt.verbose_flag = 2;
			break;

		case 'S':  /* -S or --safe */
			dopt.safe_mode = 1;
			break;

		case 'o':  /* -o or --output */
			dopt.output_file = strdup(optarg);
			break;
			
		case 'a':  /* -a or --windowabs */
			res = atoi(optarg);
			if (res >= (1U<<14) && res <= (1U<<23)) {
				dopt.window_abs = res;
			}
			break;
		       
		case 'r':  /* -r or --windowrel */
			dres = atof(optarg);
			if (dres > 0 && dres <= 1) {
				dopt.window_rel = dres;
			}
			break;

		case -1:
			break;

		default:
			abort();
		}
	}
	while (next_option != -1);
	return 0;
}
