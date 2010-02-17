/*
 * delta.h defines api for defs to use xdelta functions
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

#ifndef DELTA_H
#define DELTA_H

#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>

/*
 * Encodes the differences from an original Source File, an In File with changes
 * to an OutFile that will become a delta file with a defs header
 */
int xdelta_encode(const char* OutFileName, FILE* InFile, FILE* SrcFile, FILE* OutFile);


/*
 * xDelta Link Routine
 * -------------------
 *
 * Create firm link of f1 at f2
 * Routine handles all file I/O
 * Return 0 for success, otherwise -errno
 */
int xdelta_link(const char *f1, const char *f2);



/*
 * xDelta Read Routine
 * -------------------
 *
 * Decode necessary window(s)
 * Write to buffer
 * Returns bytes read for success, otherwise -errno
 */
int xdelta_read(const char *file, const char* parent, size_t bytes, off_t offset, char *buffer);



/*
 * xDelta Write Routine
 * --------------------
 *
 * Do a full decode
 * Write appropriate changes
 * Do an encode
 * Returns bytes written for success, otherwise -errno
 */
int xdelta_write(const char *file, const char *buf, size_t size, off_t offset, int childc, char **childv, char *parent);

/*
 * xDelta Promote Routine
 * ----------------------
 *
 * First child is promoted to parent
 *  Decode first child
 *  Write back to child file
 * Other children are made children of new parent
 *  Decode other children
 *  Encode with new parent
 */
int xdelta_promote(const char *file, int childc, char **childv);


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

int xdelta_truncate(const char *file, off_t size, char *parent, int childc, char **childv);



#endif /* DELTA_H */
