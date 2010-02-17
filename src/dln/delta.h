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

#ifndef DLN_DELTA_H
#define DLN_DELTA_H

#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>

/*
 * Encodes the differences from an original Source File, an In File with changes
 * to an OutFile that will become a delta file with a defs header
 */
int xdelta_encode(const char* OutFileName, FILE* InFile, FILE* SrcFile, FILE* OutFile);


#endif /* DLN_DELTA_H */
