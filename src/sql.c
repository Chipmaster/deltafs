/*
 * sql.c implements sql functions for defs as defined in sql.h
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
#include <sqlite3.h>
#include <dirent.h>  /* PATH_MAX */
#include <stdlib.h>  /* malloc */
#include <unistd.h>  /* sleep */
#include <string.h>  /* strcpy, strlen*/

#include "sql.h"


#define DEFS_DB "/var/lib/defs/defs.db"
#define DEFS_TBL "MAP"


sqlite3 *db;

int sqlite_open(sqlite3 **database)
{
	return sqlite3_open(DEFS_DB, database);
}

int sqlite_close(sqlite3 *database)
{
	return sqlite3_close(database);
}

int sqlite_sanatize(const char* input, char* output)
{
	/* In SQL, we must translate single quotes (') into the escaped form ('') */
	int i, j;
	j = 0;
	for (i = 0; i < strlen(input); ++i, ++j) {
		output[j] = input[i];
		if (input[i] ==  '\'') {
			++j;
			output[j] = '\'';
		}
	}
	output[j] = '\0';
	return 0;
}

static int sqlite_callback(void *NotUsed, int argc, char **argv, char **azColName)
{
	int i;
	for (i=0; i<argc; i++) {
		printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
	}
	printf("\n");
	return 0;
}

int sqlite_init_db(sqlite3 *database)
{
	int rc;
	char cmd[50+2*PATH_MAX];
	char *zErrMsg = 0;
	snprintf(cmd, 50+2*PATH_MAX, "CREATE TABLE %s (Parent varchar(%d), Child varchar(%d), Size integer)", DEFS_TBL, PATH_MAX, PATH_MAX);
	rc = sqlite3_exec(database, cmd, sqlite_callback, 0, &zErrMsg);
	return 0;
}


int sqlite_update_size(sqlite3 *database, const char* child, int size)
{
	int rc;
	char cmd[50+2*PATH_MAX];
	char *zErrMsg = 0;

	char* child_s = malloc(2*PATH_MAX*sizeof(char));
  
	rc = sqlite_sanatize(child, child_s);

	snprintf(cmd, 50+2*PATH_MAX, "UPDATE %s SET Size='%d' WHERE Child='%s'", DEFS_TBL, size, child_s);
	rc = sqlite3_exec(database, cmd, sqlite_callback, 0, &zErrMsg);
	if (rc!=SQLITE_OK) {
		printf("SQL error: %s\n", zErrMsg);
		fflush(NULL);
		sqlite3_free(zErrMsg);
	}
  
	free(child_s);
	return 0;
}


/*
 * adds an entry linking parent to child
 */
int sqlite_add(sqlite3 *database, const char* parent, const char* child, const int size)
{
	int rc;
	char cmd[50+2*PATH_MAX];
	char *zErrMsg = 0;

	char* parent_s = malloc(2*PATH_MAX*sizeof(char));
	char* child_s = malloc(2*PATH_MAX*sizeof(char));

	rc = sqlite_sanatize(parent, parent_s);
	rc = sqlite_sanatize(child, child_s);
  
	snprintf(cmd, 50+2*PATH_MAX, "INSERT INTO %s VALUES (\'%s\',\'%s\', '%d')", DEFS_TBL, parent_s, child_s, size);
	rc = sqlite3_exec(database, cmd, sqlite_callback, 0, &zErrMsg);
	if (rc!=SQLITE_OK) {
		printf("SQL error: %s\n", zErrMsg);
		fflush(NULL);
		sqlite3_free(zErrMsg);
	}

	free(parent_s);
	free(child_s);
	return 0;
}

/*
 * removes the entry linking the parent to this child 
 */
int sqlite_remove_child(sqlite3 *database, const char* child)
{
	int rc;
	char cmd[50+2*PATH_MAX];
	char *zErrMsg = 0;

	char* child_s = malloc(2*PATH_MAX*sizeof(char));
  
	rc = sqlite_sanatize(child, child_s);

	snprintf(cmd, 50+2*PATH_MAX, "DELETE FROM %s WHERE Child='%s'", DEFS_TBL, child_s);
	rc = sqlite3_exec(database, cmd, sqlite_callback, 0, &zErrMsg);
	if (rc!=SQLITE_OK) {
		printf("SQL error: %s\n", zErrMsg);
		fflush(NULL);
		sqlite3_free(zErrMsg);
	}
  
	free(child_s);
	return 0;
}

int sqlite_get_size(sqlite3 *database, const char* child)
{
	int rc, retval;
	char cmd[50+2*PATH_MAX];
	sqlite3_stmt *stmt;

	char* child_s = malloc(2*PATH_MAX*sizeof(char));

	rc = sqlite_sanatize(child, child_s);

	snprintf(cmd, 50+2*PATH_MAX, "SELECT Size FROM %s WHERE Child='%s'", DEFS_TBL, child_s);
	rc = sqlite3_prepare(database, cmd, (50+2*PATH_MAX)*sizeof(char), &stmt, 0);
	if (rc!=SQLITE_OK) {
		fprintf(stderr, "sql error #%d: %s\n", rc, sqlite3_errmsg(database));
	} 
	else {
		while ((rc = sqlite3_step(stmt)) != SQLITE_DONE) {
			switch (rc) {
			case SQLITE_BUSY:
				fprintf(stderr, "busy, wait 1 seconds\n");
				sleep(1);
				break;
			case SQLITE_ERROR:
				fprintf(stderr, "step error: %s\n", sqlite3_errmsg(database));
				break;
			case SQLITE_ROW:
				retval = sqlite3_column_int(stmt,0);
				break;
			}
		}
	}
	sqlite3_finalize(stmt);
  
	free(child_s);
	return retval;
}

int sqlite_get_children(sqlite3 *database, const char* parent, int *childc, char** *childv)
{
	int rc;
	char cmd[50+2*PATH_MAX];
	sqlite3_stmt *stmt;
	char temp[PATH_MAX];

	char* parent_s = malloc(2*PATH_MAX*sizeof(char));

	*childc = 0;
	(*childv) = NULL;

	rc = sqlite_sanatize(parent, parent_s);

	snprintf(cmd, 50+2*PATH_MAX, "SELECT Child FROM %s WHERE Parent='%s'", DEFS_TBL, parent_s);
	rc = sqlite3_prepare(database, cmd, (50+2*PATH_MAX)*sizeof(char), &stmt, 0);
	if (rc!=SQLITE_OK) {
		printf("sql error #%d: %s\n", rc, sqlite3_errmsg(database)); 
		return -1;
	}
	else {
		while ((*childc < 255) && (rc != SQLITE_DONE)) {
			rc = sqlite3_step(stmt);
			switch(rc) {
			case SQLITE_BUSY:  /* in the future, make sure we don't wait forever if it's locked */
				printf("busy, wait 1 seconds\n");
				sleep(1);
				break;
			case SQLITE_ERROR:
				printf("step error: %s\n", sqlite3_errmsg(database));
				break;
			case SQLITE_ROW:
				sprintf(temp,"%s",sqlite3_column_text(stmt,0));
				(*childv) = realloc((*childv), (*childc+1)*sizeof(char*));
				(*childv)[*childc] = strdup(temp);
				*childc = *childc + 1;
				break;
			}
		}
	}

	sqlite3_finalize(stmt);
  
	free(parent_s);
	return 0;
}

/*
 * returns the parent of a given child
 */
int sqlite_get_parent(sqlite3 *database, const char* child, char** parent)
{
	int rc;
	char cmd[50+2*PATH_MAX];
	sqlite3_stmt *stmt;
	char temp[PATH_MAX];

	char* child_s = malloc(2*PATH_MAX*sizeof(char));

	rc = sqlite_sanatize(child, child_s);

	snprintf(cmd, 50+2*PATH_MAX, "SELECT Parent FROM %s WHERE Child='%s'", DEFS_TBL, child_s);
	rc = sqlite3_prepare(database, cmd, (50+2*PATH_MAX)*sizeof(char), &stmt, 0);
	if (rc!=SQLITE_OK) {
		fprintf(stderr, "sql error #%d: %s\n", rc, sqlite3_errmsg(database));
	} 
	else {
		while ((rc = sqlite3_step(stmt)) != SQLITE_DONE) {
			switch (rc) {
			case SQLITE_BUSY:
				fprintf(stderr, "busy, wait 1 seconds\n");
				sleep(1);
				break;
			case SQLITE_ERROR:
				fprintf(stderr, "step error: %s\n", sqlite3_errmsg(database));
				break;
			case SQLITE_ROW:
				*parent = malloc(PATH_MAX*sizeof(char));
				sprintf(temp, "%s", sqlite3_column_text(stmt,0));
				strcpy(*parent, temp);
				break;
			}
		}
	}
	sqlite3_finalize(stmt);
  
	free(child_s);
	return 0;
}


int sql_open()
{
	return sqlite_open(&db);
}

int sql_close()
{
	return sqlite_close(db);
}

int sql_init_db()
{
	return sqlite_init_db(db);
}

int sql_update_size(const char* child, const int size)
{
	return sqlite_update_size(db, child, size);
}

int sql_add(const char* parent, const char* child, const int size)
{
	return sqlite_add(db, parent, child, size);
}

int sql_get_size(const char* child)
{
	return sqlite_get_size(db, child);
}

int sql_get_children(const char* parent, int* childc, char*** childv)
{
	return sqlite_get_children(db, parent, childc, childv);
}

int sql_get_parent(const char* child, char** parent)
{
	return sqlite_get_parent(db, child, parent);
}

int sql_remove_child(const char* child)
{
	return sqlite_remove_child(db, child);
}
