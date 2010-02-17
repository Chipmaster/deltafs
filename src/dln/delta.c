/*
 * delta.c implements xdelta functions for defs as defined in delta.h
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

#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <fcntl.h> /* for O_* constants */
#include <limits.h> /* for NAME_MAX */

#include "../xdelta/xdelta3.h"
#include "../xdelta/xdelta3.c"
#include "delta.h"
#include "../sql.h"
#include "opts.h"

#ifndef DEBUG_MODE1
#define DEBUG_MODE1 0
#endif

#ifndef DEBUG_MODE2
#define DEBUG_MODE2 0
#endif

#if DEBUG_MODE1
#define DEBUG1(x) x
#else
#define DEBUG1(x)
#endif

#if DEBUG_MODE2
#define DEBUG2(x) x
#else
#define DEBUG2(x) 
#endif


char* semaphore_hash (const char* key)
{
	/* First character is a slash, followed by a hash of the path */
	int i;
	unsigned int tmp_hash = 0;
	char tmp[NAME_MAX-5];
	char *hash = malloc(NAME_MAX-4);
	for(i = 0; i < strlen(key); ++i) {
		tmp_hash += (unsigned int) key[i] * i;
	}
	strcpy(hash, "/");
	snprintf(tmp, NAME_MAX-5, "%u", tmp_hash);
	strcat(hash, tmp);
	return hash;
}


int xdelta_encode (const char* OutFileName, FILE* InFile, FILE* SrcFile, FILE* OutFile)
{
	int BufSize;
	struct stat statbuf;
	struct stat instatbuf;
	xd3_stream stream;
	xd3_config config;
	xd3_source source;
	void* Input_Buf;
	int Input_Buf_Read;
	int r, ret;

	if (dopt.window_abs) {
		BufSize = dopt.window_abs;
	}
	else {
		r = fstat(fileno(SrcFile), &statbuf);
		if (r) {
			return -errno;
		}
		BufSize = (int) (statbuf.st_size * dopt.window_rel);
	}

	if (BufSize < XD3_ALLOCSIZE) {
		BufSize = XD3_ALLOCSIZE;
	}

	printf("xdelta_encode\n");
	fflush(NULL);
  
	memset (&stream, 0, sizeof(stream));
	memset (&stream, 0, sizeof(source));

	xd3_init_config(&config, XD3_ADLER32);
	config.winsize = BufSize;
	xd3_config_stream(&stream, &config);


	if (SrcFile) {
		r = fstat(fileno(SrcFile), &statbuf);
		if (r) {
			return -errno;
		}
		source.size = statbuf.st_size;
		source.blksize = BufSize;
		source.curblk = malloc(source.blksize);
    
		/* Load 1st block of stream. */
		r = fseek(SrcFile, 0, SEEK_SET);
		if (r) {
			return -errno;
		}
		source.onblk = fread((void*)source.curblk, 1, source.blksize, SrcFile);
		source.curblkno = 0;
		/* Set the stream. */
		xd3_set_source(&stream, &source);
	}

	Input_Buf = malloc(BufSize);  
	fseek(InFile, 0, SEEK_SET);

	if (InFile) {
		r = fstat(fileno(InFile), &instatbuf);
		if (r) {
			return -errno;
		}
		sql_update_size(OutFileName, instatbuf.st_size);
	}

	fseek(OutFile, 0, SEEK_SET);
  
	do {
		Input_Buf_Read = fread(Input_Buf, 1, BufSize, InFile);
		if (Input_Buf_Read < BufSize) {
			xd3_set_flags(&stream, XD3_FLUSH | stream.flags);
		}
		xd3_avail_input(&stream, Input_Buf, Input_Buf_Read);
    
	process:
		ret = xd3_encode_input(&stream);


		switch (ret) {
		case XD3_INPUT:
			DEBUG1(printf("DEBUG: XD3_INPUT\n"));
			continue;

		case XD3_OUTPUT:
			DEBUG1(printf("DEBUG: XD3_OUTPUT\n"));
			r = fwrite(stream.next_out, 1, stream.avail_out, OutFile);
			DEBUG1(printf("Stream.avail_out  PARTY%u", (unsigned int) stream.avail_out));
			fflush(NULL);
			if (r != (int)stream.avail_out) {
				return -r;
			}
			xd3_consume_output(&stream);
			goto process;
      
		case XD3_GETSRCBLK:
			DEBUG1(printf("DEBUG: XD3_GETSRCBLK %qd\n", source.getblkno));
			if (SrcFile) {
				r = fseek(SrcFile, source.blksize * source.getblkno, SEEK_SET);
				if (r) {
					return -errno;
				}
				source.onblk = fread((void*)source.curblk, 1, source.blksize, SrcFile);
				source.curblkno = source.getblkno;
			}
			goto process;
    
		case XD3_GOTHEADER:
			DEBUG1(printf("DEBUG: XD3_GOTHEADER\n"));
			goto process;

		case XD3_WINSTART:
			DEBUG1(printf("DEBUG: XD3_WINSTART\n"));
			goto process;
    
		case XD3_WINFINISH:
			DEBUG1(printf("DEBUG: XD3_WINFINISH\n"));
			goto process;
    
		default:
			DEBUG1(printf("DEBUG: INVALID %s %d\n", stream.msg, ret));
			return -ret;
		}  
	} while(Input_Buf_Read == BufSize);
    
	free(Input_Buf);
	free((void*)source.curblk);
	xd3_close_stream(&stream);
	xd3_free_stream(&stream);

	return 0;
}
