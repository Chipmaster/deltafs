/*
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
#include <stdlib.h>
#include "sql.h"

int main(int argc, char **argv)
{
	int rc;
	int childc;
	char **childv;
	char *parent;
	int i;

	if (argc != 4) {
		printf("Usage %s (add,get) parent child (remove) child", argv[0]);
		return -1;
	}
  
	rc = sql_open();
	if (rc) {
		sql_close();
		return -1;
	}
  
	rc = sql_init_db();
  
	printf("ADDING: %s, %s\n", argv[1], argv[2]);
	rc = sql_add(argv[1], argv[2], 100);

	printf("GET Children of Parent %s\n", argv[1]);
	rc = sql_get_children(argv[1], &childc, &childv);
	for (i = 0; i < childc; ++i) {
		printf("Child = %s\n", childv[i]);
		free(childv[i]);
	}
	free(childv);

	printf("GET Parent of Child %s\n", argv[2]);
	rc = sql_get_parent(argv[2], &parent);
	printf("Parent = %s\n", parent);
	free(parent);

	printf("Remove Child %s\n", argv[3]);
	rc = sql_remove_child(argv[3]);
  
	sql_close();
	return 0;
}
