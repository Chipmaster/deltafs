/*
 * sql.h defines api for defs to use sql functions
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

#ifndef SQL_H
#define SQL_H

#include <sqlite3.h>


/*
 * Open the database
 */
int sql_open();

/*
 * Close the database
 */
int sql_close();

/*
 * Initialize the database
 */
int sql_init_db();

/*
 * Updates the size of a child
 */

int sql_update_size(const char* child, const int);

/*
 * adds an entry linking parent to child
 */
int sql_add(const char* parent, const char* child, const int);

/*
 * returns the count of children in childc and a vector of child paths of any given parent
 */
int sql_get_children(const char* parent, int* childc, char*** childv);

/*
 * returns the parent of a given child
 */
int sql_get_parent(const char* child, char** parent);

/*
 * removes the entry linking the parent to this child
 */
int sql_remove_child(const char* child);

/*
 * returns the size of a given child
 */
int sql_get_size(const char* child);

#endif /* SQL_H */
