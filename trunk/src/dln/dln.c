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

#include <stdlib.h>
#include <getopt.h>
#include <stdio.h>
#include <limits.h> /* PATH_MAX */
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "opts.h"
#include "delta.h"
#include "../sql.h"

#define DLN_TMP ".dlnbak"

static struct option long_options[] = {
        {"help",      no_argument,       0, 'h'},
        {"version",   no_argument,       0, 'V'},
        {"verbose",   no_argument,       0, 'v'},
        {"silent",    no_argument,       0, 's'},
	{"safe",      no_argument,       0, 'S'},
        {"output",    required_argument, 0, 'o'},
	{"windowabs", required_argument, 0, 'a'},
	{"windowrel", required_argument, 0, 'r'},
        {0,           0,                 0,   0}
};

static const char* short_options = "hVvsSo:a:r:";

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



int main (int argc, char* argv[])
{
	/*
	 * If dopt.output_file not specified
	 *  Move InFile to InFile.dlnbak
	 *  Set outfilename to InFile
	 * Else
	 *  Set outfilename to dopt.output_file (check to see if it's the same as InFile and error otherwise (maybe force flag overrides this in the future))
	 * Add to database
	 * Encode to outfilename
	 * If infiletmp
	 *  Remove infiletmp
	 */
	
	FILE* SrcFile;
	FILE* InFile;
	FILE* OutFile;

	char* srcfilename;
	char* infilename;
	char* outfilename;
	char* infiletmp = NULL;

	int res;
	char *parent = NULL;
	struct stat statbuf;

	dopt_init();
	dopt_proc(argc, argv, short_options, long_options);
	dopt_finalize();

	if ( optind != argc - 2) { /* 2 required options - SrcFile and InFile */
		fprintf(stderr, "Usage %s [options] SrcFile InFile\n", argv[0]);
		exit(1);
	}

	res = sql_open();
	if (res) {
		sql_close();
		exit(1);
	}
	
	res = sql_init_db();
	
	srcfilename = make_absolute(argv[optind++]);
	infilename = make_absolute(argv[optind++]);

	sql_get_parent(srcfilename, &parent);
	if (parent) {  /* Don't allow links of links */
		fprintf(stderr, "Links of links are not allowed");
		exit(1);
	}
	
	SrcFile = fopen(srcfilename, "rb");
	if (!SrcFile) {
		fprintf(stderr, "Error opening SrcFile %d", -errno);
		return -errno;
	}
	
	if (!dopt.output_file) {
		infiletmp = malloc((strlen(infilename) + 8)*sizeof(char));
		strcpy(infiletmp, infilename);
		strcat(infiletmp, DLN_TMP);
		res = rename(infilename, infiletmp);
		if (res == -1) {
			perror("Could not rename");
			return -errno;
		}
		
		outfilename = infilename;
		
		InFile = fopen(infiletmp, "rb");
		
		if (!InFile) {
			fprintf(stderr, "Error opening InFile %d\n", -errno);
			return -errno;
		}

		if (stat(infiletmp, &statbuf) == -1) {
			perror("Could not stat");
			return -errno;
		}

		OutFile = fopen(outfilename, "w+b");
		if (!OutFile) {
			fprintf(stderr, "Error opening OutFile %d\n", -errno);
			return -errno;
		}
		
		sql_add(srcfilename, outfilename, statbuf.st_size);

		res = xdelta_encode(outfilename, InFile, SrcFile, OutFile);		

		if (res) {
			res = rename(infiletmp, infilename);
			if (res == -1) {
				perror("Could not rename");
				return -errno;
			}
			exit(1);
		}

		if (!dopt.safe_mode) {
			res = unlink(infiletmp);
			if (res) {
				perror("Could not unlink");
				return -errno;
			}
		}

	}
	else {
		outfilename = dopt.output_file;
		InFile = fopen(infilename, "rb");
		
		if (!InFile) {
			fprintf(stderr, "Error opening InFile %d\n", -errno);
			return -errno;
		}

		if (stat(infilename, &statbuf) == -1) {
			perror("Could not stat");
			exit(1);
		}

		OutFile = fopen(outfilename, "w+b");
		if (!OutFile) {
			fprintf(stderr, "Error opening OutFile %d\n", -errno);
			return -errno;
		}
		
		sql_add(srcfilename, outfilename, statbuf.st_size);

		res = xdelta_encode(outfilename, InFile, SrcFile, OutFile);
	}

	sql_close();
	return res;
}
