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

#include "xdelta/xdelta3.h"
#include "xdelta/xdelta3.c"
#include "delta.h"
#include "sql.h"
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


int xdelta_read(const char *file, const char *parent, size_t bytes, off_t offset, char *buffer)
{
	/*
	 * xDelta Read Routine
	 * -------------------
	 *
	 * Decode necessary window(s)
	 * Write to buffer
	 * Return bytes read for success, otherwise -errno
	 */

	FILE* InFile;
	FILE* SrcFile;
  
	struct stat statbuf;
	xd3_stream stream;
	xd3_source source;
	xd3_config config;

	void* Input_Buf;
	int Input_Buf_Read;
	int BufSize;
	int r, ret;

	unsigned int target_offset;
	unsigned int window_offset;
	unsigned int current_offset;
	unsigned int loff, roff;
	unsigned int buffoff;

	target_offset = 0;
	InFile = fopen(file, "rb");
	if (!InFile) {
		return -errno;
	}
  
	printf("xdelta_read\n");
	fflush(NULL);

	SrcFile = fopen(parent, "rb");
	if (!SrcFile) {
		return -errno;
	}

  
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

	if(BufSize < XD3_ALLOCSIZE){
		BufSize = XD3_ALLOCSIZE;
	}

	memset (&stream, 0, sizeof(stream));
	memset (&source, 0, sizeof(source));

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
	buffoff = 0;

	do {
		Input_Buf_Read = fread(Input_Buf, 1, BufSize, InFile);
		if (Input_Buf_Read < BufSize) {
			xd3_set_flags(&stream, XD3_FLUSH | stream.flags);
		}
		xd3_avail_input(&stream, Input_Buf, Input_Buf_Read);
    
	process:
		ret = xd3_decode_input(&stream);
    
    
		switch (ret) {
		case XD3_INPUT:
			DEBUG2(printf("DEBUG: XD3_INPUT\n"));
			continue;

		case XD3_OUTPUT:
			DEBUG2(printf("DEBUG: XD3_OUTPUT\n"));
			window_offset+= stream.avail_out;
			current_offset = target_offset + window_offset;
			DEBUG2(printf("DEBUG: offset %u bytes %u current_offset %u stream.avail_out %u\n", (unsigned int) offset, (unsigned int) bytes, (unsigned int)current_offset, (unsigned int) stream.avail_out));
			if (offset + bytes < current_offset - stream.avail_out || offset > current_offset) {
				goto process;
			}
      
			if (offset < current_offset - stream.avail_out) {
				/* Start from beginning */
				loff = 0;
			} else {
				loff = offset - (current_offset - stream.avail_out);
			}
	
			if (offset + bytes > current_offset) {
				/* Go to end */
				roff = stream.avail_out;
			} else {
				roff = offset+bytes - (current_offset - stream.avail_out);
			}
      
			DEBUG2(printf("Writing to buffer %u with buffoff %u at %u, writing from stream.next_out at %u writing %d bytes\n", (unsigned int) buffer, buffoff, (unsigned int) buffer+buffoff, (unsigned int) stream.next_out+loff, roff-loff));
			memcpy(buffer+buffoff, stream.next_out+loff, roff-loff);
			buffoff+= roff-loff;
      
			xd3_consume_output(&stream);
			goto process;
		case XD3_GETSRCBLK:
			DEBUG2(printf("DEBUG: XD3_GETSRCBLK %qd\n", source.getblkno));
			if (SrcFile) {
				r = fseek(SrcFile, source.blksize * source.getblkno, SEEK_SET);
				if (r) {
					return -errno;
				}
				source.onblk = fread((void*)source.curblk, 1, source.blksize, SrcFile);
				source.curblkno = source.getblkno;
	
				DEBUG2(printf("Source.onblk %d Source.curblkno %d\n", (int) source.onblk, (int) source.curblkno));
			}
			goto process;
		case XD3_GOTHEADER:
			DEBUG2(printf("DEBUG: XD3_GOTHEADER\n"));
		case XD3_WINSTART:
			window_offset = 0;
			DEBUG2(printf("DEBUG: XD3_WINSTART\n"));
			DEBUG2(printf("DEBUG: Current Window, Total Out, Target Window Length: %u %u %u\n", 
				      (unsigned int) stream.current_window, (unsigned int) target_offset, stream.dec_tgtlen));
			if (target_offset < (offset+bytes) && (target_offset+stream.dec_tgtlen > offset)) {
				/* This is a window to decode */
				DEBUG2(printf("DEBUG: Decoding window %d of window_size %d due to offset being %d and bytes %d\n", 
					      (unsigned int) stream.current_window, stream.dec_tgtlen, (unsigned int) offset, bytes));
				xd3_set_flags(&stream, ~(XD3_SKIP_WINDOW) & stream.flags);
			} else {
				/* Do not decode window */
				DEBUG2(printf("DEBUG: Not decoding window %d of window_size %d due to offset being %d and bytes %d\n",
					      (unsigned int) stream.current_window, stream.dec_tgtlen, (unsigned int) offset, bytes));
				xd3_set_flags(&stream, XD3_SKIP_WINDOW | stream.flags);
			}
			fflush(NULL);
			goto process;
		case XD3_WINFINISH:
			DEBUG2(printf("DEBUG: XD3_WINFINISH\n"));
			target_offset+= stream.dec_tgtlen;
			goto process;
		default:
			DEBUG2(printf("DEBUG: INVALID %s %d\n", stream.msg, ret));
			return ret;
		} 
	} while(Input_Buf_Read == BufSize);
  
	free(Input_Buf);
	free((void*)source.curblk);
	xd3_close_stream(&stream);
	xd3_free_stream(&stream);

	fclose(InFile);
	fclose(SrcFile);
	return buffoff;
}


int xdelta_link(const char *f1, const char *f2)
{
	/*
	 * xDelta Link Routine
	 * -------------------
	 *
	 * Create firm link of f1 at f2
	 * 
	 * Routine handles all file I/O
	 * Return 0 for success, otherwise -errno
	 */
	FILE* InFile;
	FILE* SrcFile;
	FILE* OutFile;
	int r;

	InFile = fopen(f1, "rb");
	SrcFile = fopen(f1, "rb");
	OutFile = fopen(f2, "wb");

	r = xdelta_encode(f2, InFile, SrcFile, OutFile);
 
	fclose(InFile);
	fclose(SrcFile);
	fclose(OutFile);
	return r;
}


int xdelta_write(const char *file, const char *buf, size_t size, off_t offset, int childc, char **childv, char *parent)
{
	/*
	 * xDelta Write Routine
	 * --------------------
	 *
	 * Do a full decode saving to fd
	 * Write appropriate changes to fd
	 * Do an encode
	 * Return bytes written for success, otherwise -errno
	 */  
  
	int r;
	char* buffer;
	int res;
	off_t off;

	printf("File - %s\n", file);
	printf("Parent - %s\n", parent);
	fflush(NULL);

	if (parent) {
		/*
		 * Write to a child
		 *  Decode child
		 *  Write changes
		 *  Encode child
		 */
		sem_t *sem_child;
		char* sem_child_name = semaphore_hash(file);
		sem_child = sem_open(sem_child_name, O_CREAT, 0777, 1);
		free(sem_child_name);
		r = sem_wait(sem_child);
    
		FILE* OutFile;
		FILE* SrcFile;
		FILE* TmpFile;
    
		r = 0;
		TmpFile = tmpfile();
		if (!TmpFile) {
			printf("ERROR Opening TmpFile %d", -errno);
		}
    
		buffer = malloc(dopt.buffer);
		fseek(TmpFile, 0, SEEK_SET);
    
		off = 0;
		do {
			r = xdelta_read(file, parent, dopt.buffer, off, buffer);
			r = fwrite(buffer, 1, r, TmpFile);
			off+= r;
		} while (r == dopt.buffer);
    
		free(buffer);
		fflush(NULL); /* Important to flush the current output buffers before writing changes. */

		r = pwrite(fileno(TmpFile), buf, size, offset);

		printf("PWrite returned: %d\n", r);
		fflush(NULL); /* Important to flush output before encoding */
    
		OutFile = fopen(file, "wb");
		SrcFile = fopen(parent, "rb");

		fseek(TmpFile, 0, SEEK_SET);  /* Point to beginning of empty file */
		fseek(SrcFile, 0, SEEK_SET);
		res = xdelta_encode(file, TmpFile, SrcFile, OutFile);
    
		printf("xDelta Encode returned: %d\n", res);
		printf("WROTE %s of size %d at offset %d\n", buf, size, (int) offset);
		fflush(NULL);
    

		sem_post(sem_child);
		fclose(OutFile);
		fclose(TmpFile);
		fclose(SrcFile);
	}
	else {
		/*
		 * Create an array of temp files for each child
		 * Decode each child into a temp file
		 * Write changes to parent
		 * Encode each child
		 */
    
		FILE* TmpFile[childc];
		FILE* SrcFile;
		FILE* OutFile;
		sem_t *sem_child[childc];
		sem_t *sem_parent;
		char *sem_child_name;
		char *sem_parent_name;
		int i;
    
		sem_parent_name =  semaphore_hash(file);
		sem_parent = sem_open(sem_parent_name, O_CREAT, 0777, 1);
		free(sem_parent_name);
		r = sem_wait(sem_parent);
    
		/*
		 * Create an array of temp files for each child
		 * Decode each child into a temp file
		 */
		for (i = 0; i < childc; ++i) {
			printf("Create and decode\n");
			fflush(NULL);
      
			sem_child_name = semaphore_hash(childv[i]);
			sem_child[i] = sem_open(sem_child_name, O_CREAT, 0777, 1);
			free(sem_child_name);
			r = sem_wait(sem_child[i]);

			TmpFile[i] = tmpfile();

			buffer = malloc(dopt.buffer);
			fseek(TmpFile[i], 0, SEEK_SET);
    
			off = 0;
			do {
				r = xdelta_read(childv[i], file, dopt.buffer, off, buffer);
				fwrite(buffer, 1, r, TmpFile[i]);
				off+= r;
			} while (r == dopt.buffer);
			free(buffer);
		}
		/*
		 * Write changes to parent
		 */
		printf("Write Changes\n");
		fflush(NULL);  /* Important to flush output before writing changes */

		SrcFile = fopen(file, "r+b");
		r = pwrite(fileno(SrcFile), buf, size, offset);
		fflush(NULL); /* Important to write changes before closing */
		fclose(SrcFile);
		SrcFile = fopen(file, "rb");
		
		/*
		 * Encode each child
		 */
		for (i = 0; i < childc; ++i) {
			printf("Encode\n");
			fflush(NULL); /* Important to flush output before encoding */
			truncate(childv[i], 0);
			OutFile = fopen(childv[i], "w+b");

			fseek(TmpFile[i], 0, SEEK_SET);  /* Point to beginning of empty file */
			fseek(SrcFile, 0, SEEK_SET);
      
			res = xdelta_encode(childv[i], TmpFile[i], SrcFile, OutFile);
      
			printf("xDelta Encode returned: %d\n", res);
			printf("WROTE %s of size %d at offset %d\n", buf, size, (int) offset);
			fflush(NULL);
      
			sem_post(sem_child[i]);
			fclose(OutFile);
			fclose(TmpFile[i]);
		}
		fclose(SrcFile);
		sem_post(sem_parent);
	}  
	return r;
}

int xdelta_promote(const char *file, int childc, char **childv)
{
	/*
	 * First child is promoted to parent
	 *  Decode first child
	 *  Write back to child file
	 * Other children are made children of new parent
	 *  Decode other children
	 *  Encode with new parent
	 */

  
	FILE* TmpFile;
	FILE* OutFile;
	FILE* SrcFile;
 
	char* buffer;
	int r, i;
	off_t off, off_count;

	sem_t *sem_child[childc];
	char *sem_child_name;
  
	for (i = 0; i < childc; ++i) {
		sem_child_name = semaphore_hash(childv[i]);
		sem_child[i] = sem_open(sem_child_name, O_CREAT, 0777, 1);
		r = sem_wait(sem_child[i]);
		free(sem_child_name);
	}
  
	/* Decode first Child */
	TmpFile = tmpfile();
	if (!TmpFile) {
		printf("Error opening TmpFile %d", -errno);
		return -errno;
	}
  
	buffer = malloc(dopt.buffer);
	fseek(TmpFile, 0, SEEK_SET);

	off = 0;
	do {
		r = xdelta_read(childv[0], file, dopt.buffer, off, buffer);
		fwrite(buffer, 1, r, TmpFile);
		off+= r;
	} while (r == dopt.buffer);

	/* Write back to child file */
	off_count = 0;
	truncate(childv[0], 0);
	SrcFile = fopen(childv[0], "w+b");  
	do {
		r = pread(fileno(TmpFile), buffer, dopt.buffer, off_count);
		r = pwrite(fileno(SrcFile), buffer, r, off_count);
		off_count += r;
	} while (off_count < off);
	free(buffer);
	fclose(SrcFile);
	fclose(TmpFile);
  
	for (i = 1; i < childc; ++i) {
		/* Decode other children */
		TmpFile = tmpfile();
		if (!TmpFile) {
			printf("Error opening TmpFile %d", -errno);
			return -errno;
		}
    
		buffer = malloc(dopt.buffer);
		fseek(TmpFile, 0, SEEK_SET);
    
		off = 0;
		do {
			r = xdelta_read(childv[i], file, dopt.buffer, off, buffer);
			fwrite(buffer, 1, r, TmpFile);
			off+= r;
		} while (r == dopt.buffer);
		free(buffer);

		/* Encode with new parent */
		truncate(childv[i], 0);
		OutFile = fopen(childv[i], "w+b");
		SrcFile = fopen(childv[0], "rb");
    
		fseek(TmpFile, 0, SEEK_SET);  /* Point to beginning of empty file */
		fseek(SrcFile, 0, SEEK_SET);
 
		r = xdelta_encode(childv[i], TmpFile, SrcFile, OutFile);
    
		sem_post(sem_child[i]);
		fclose(OutFile);
		fclose(SrcFile);
		fclose(TmpFile);
	}
	return 0;
}


int xdelta_truncate(const char *file, off_t size, char *parent, int childc, char **childv)
{
	/*
	 * xDelta Truncate Routine
	 * -----------------------
	 *
	 * If it's a child
	 *  Do a full decode
	 *  Truncate
	 *  Do an encode
	 *  Return 0 on success, -1 on error setting errno
	 *
	 * If it's a parent
	 *  Decode all children
	 *  Truncate Parent
	 *  Encode all children
	 *  Return 0 on success, -1 on error setting errno
	 */

	char* buffer;
	off_t off;
	int res, r;


	if (parent) {
		/* If it's a child */

		/* Do a full decode */
		FILE* TmpFile;
		FILE* OutFile;
		FILE* SrcFile;
		sem_t *sem_child;
		char *sem_child_name;
    
		sem_child_name = semaphore_hash(file);
		sem_child = sem_open(sem_child_name, O_CREAT, 0777, 1);
		free(sem_child_name);
		r = sem_wait(sem_child);

		TmpFile = tmpfile();
		if (!TmpFile) {
			printf("Error opening TmpFile %d", -errno);
			return -errno;
		}
    
		buffer = malloc(dopt.buffer);
		fseek(TmpFile, 0, SEEK_SET);
    
		off = 0;
		do {
			r = xdelta_read(file, parent, dopt.buffer, off, buffer);
			fwrite(buffer, 1, r, TmpFile);
			off+= r;
		} while (r == dopt.buffer);
    
		free(buffer);

		/* Truncate */
		res = ftruncate(fileno(TmpFile), size);

		if (res) {
			return -errno;
		}
    
		/* Do an encode */
		truncate(file, 0);
		OutFile = fopen(file, "w+b");
		SrcFile = fopen(parent, "rb");
    
		fseek(TmpFile, 0, SEEK_SET);  /* Point to beginning of empty file */
		fseek(SrcFile, 0, SEEK_SET);
    
		res = xdelta_encode(file, TmpFile, SrcFile, OutFile);
    
		sem_post(sem_child);

		fclose(SrcFile);
		fclose(OutFile);
		fclose(TmpFile);
	} else {
		int i;
		FILE* TmpFile[childc];
		FILE* SrcFile;
		FILE* OutFile;
		sem_t *sem_child[childc];
		sem_t *sem_parent;
		char *sem_parent_name;
		char *sem_child_name;
    
		sem_parent_name = semaphore_hash(file);
		sem_parent = sem_open(file, O_CREAT, 0777, 1);
		free(sem_parent_name);
		r = sem_wait(sem_parent);

		for (i = 0; i < childc; ++i) {
			/* Decode children */
			sem_child_name = semaphore_hash(childv[i]);
			sem_child[i] = sem_open(sem_child_name, O_CREAT, 0777, 1);
			free(sem_child_name);
			r = sem_wait(sem_child[i]);

			TmpFile[i] = tmpfile();
			if (!TmpFile[i]) {
				printf("Error opening TmpFile %d", -errno);
				return -errno;
			}
      
			buffer = malloc(dopt.buffer);
			fseek(TmpFile[i], 0, SEEK_SET);
      
			off = 0;
			do {
				r = xdelta_read(childv[i], file, dopt.buffer, off, buffer);
				fwrite(buffer, 1, r, TmpFile[i]);
				off+= r;
			} while (r == dopt.buffer);
			free(buffer);
		}

		/* Truncate */
		res = truncate(file, size);
		if (res) {
			return -errno;
		}
		
		/* Encode children */
		for (i = 0; i < childc; ++i) {
			truncate(childv[i], 0);
      
			OutFile = fopen(childv[i], "w+b");
			SrcFile = fopen(parent, "rb");
      
			fseek(TmpFile[i], 0, SEEK_SET);  /* Point to beginning of empty file */
			fseek(SrcFile, 0, SEEK_SET);
      
			res = xdelta_encode(childv[i], TmpFile[i], SrcFile, OutFile);
      
			sem_post(sem_child[i]);
			fclose(SrcFile);
			fclose(OutFile);
			fclose(TmpFile[i]);
		}

	}
	return 0;
}
