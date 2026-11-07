#include<stdio.h>
#include<stdlib.h>
#include<sys/stat.h>
#include"db.h"

#define DEFAULT_PATH "data/site.db"

// CSERVER_DB permet aux tests d'utiliser une base jetable
static const char *db_path(void){
	const char *p = getenv("CSERVER_DB");
	return p != NULL ? p : DEFAULT_PATH;
}

sqlite3 *db_open(void){
	sqlite3 *db;
	if (sqlite3_open(db_path(), &db) != SQLITE_OK){
		fprintf(stderr, "sqlite: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		return NULL;
	}

	// si un autre processus est en train d'écrire, attendre jusqu'à 2 s
	// au lieu d'échouer tout de suite avec SQLITE_BUSY
	sqlite3_busy_timeout(db, 2000);
	return db;
}

int db_init(void){
	if (getenv("CSERVER_DB") == NULL){
		mkdir("data", 0755);   // sans effet si le dossier existe déjà
	}

	sqlite3 *db = db_open();
	if (db == NULL){return -1;}

	const char *schema =
		// WAL : les lectures ne sont pas bloquées pendant une écriture
		"PRAGMA journal_mode = WAL;"
		"CREATE TABLE IF NOT EXISTS counters ("
		"  name  TEXT PRIMARY KEY,"
		"  value INTEGER NOT NULL"
		");";

	char *err = NULL;
	if (sqlite3_exec(db, schema, NULL, NULL, &err) != SQLITE_OK){
		fprintf(stderr, "sqlite: %s\n", err);
		sqlite3_free(err);
		sqlite3_close(db);
		return -1;
	}

	sqlite3_close(db);
	return 0;
}

long db_counter_incr(sqlite3 *db, const char *name){
	// une seule requête : crée le compteur à 1, ou l'incrémente s'il existe
	const char *sql =
		"INSERT INTO counters (name, value) VALUES (?, 1) "
		"ON CONFLICT (name) DO UPDATE SET value = value + 1 "
		"RETURNING value;";

	sqlite3_stmt *stmt;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK){
		fprintf(stderr, "sqlite: %s\n", sqlite3_errmsg(db));
		return -1;
	}

	// le "?" est rempli par SQLite lui-même : aucune injection SQL possible
	sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);

	long value = -1;
	int rc;
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW){
		value = sqlite3_column_int64(stmt, 0);
	}
	if (rc != SQLITE_DONE){
		fprintf(stderr, "sqlite: %s\n", sqlite3_errmsg(db));
		value = -1;
	}

	sqlite3_finalize(stmt);
	return value;
}
