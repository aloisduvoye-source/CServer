#include<stdio.h>
#include<string.h>
#include<sys/stat.h>
#include"internal.h"

// crée le dossier qui contient la base ("data/site.db" -> "data"), s'il manque
static void make_parent_dir(const char *path){
	const char *slash = strrchr(path, '/');
	if (slash == NULL || slash == path){return;}

	char dir[512];
	size_t n = slash - path;
	if (n >= sizeof(dir)){return;}
	memcpy(dir, path, n);
	dir[n] = '\0';
	mkdir(dir, 0755);   // sans effet si le dossier existe déjà
}

// prépare une requête ; affiche l'erreur et renvoie NULL si elle est invalide
sqlite3_stmt *cs_db_prepare(sqlite3 *db, const char *sql){
	sqlite3_stmt *stmt;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK){
		fprintf(stderr, "sqlite: %s\n", sqlite3_errmsg(db));
		return NULL;
	}
	return stmt;
}

// exécute une requête qui ne renvoie aucune ligne, puis la libère ; 0 si OK
int cs_db_run(sqlite3 *db, sqlite3_stmt *stmt){
	int rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE){
		fprintf(stderr, "sqlite: %s\n", sqlite3_errmsg(db));
	}
	sqlite3_finalize(stmt);
	return rc == SQLITE_DONE ? 0 : -1;
}

// exécute un texte SQL (plusieurs requêtes possibles, sans paramètre)
static int exec(sqlite3 *db, const char *sql){
	char *err = NULL;
	if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK){
		fprintf(stderr, "sqlite: %s\n", err);
		sqlite3_free(err);
		return -1;
	}
	return 0;
}

sqlite3 *db_open(void){
	sqlite3 *db;
	if (sqlite3_open(cs_config.db_path, &db) != SQLITE_OK){
		fprintf(stderr, "sqlite: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		return NULL;
	}

	// si un autre processus est en train d'écrire, attendre jusqu'à 2 s
	// au lieu d'échouer tout de suite avec SQLITE_BUSY
	sqlite3_busy_timeout(db, 2000);

	// à activer sur chaque connexion : sinon SQLite ignore les REFERENCES
	exec(db, "PRAGMA foreign_keys = ON;");
	return db;
}

/* ---------------------------------------------------------------- schéma */

static const char *SCHEMA =
	// WAL : les lectures ne sont pas bloquées pendant une écriture
	"PRAGMA journal_mode = WAL;"
	"CREATE TABLE IF NOT EXISTS messages ("
	"  id         INTEGER PRIMARY KEY,"
	"  name       TEXT NOT NULL,"
	"  message    TEXT NOT NULL,"
	"  created_at TEXT NOT NULL DEFAULT (datetime('now'))"   // en UTC
	");"
	"CREATE TABLE IF NOT EXISTS users ("
	"  id            INTEGER PRIMARY KEY,"
	"  name          TEXT NOT NULL UNIQUE COLLATE NOCASE,"   // "Jean" et "jean" : même nom
	"  password_hash TEXT NOT NULL,"
	"  created_at    TEXT NOT NULL DEFAULT (datetime('now'))"
	");"
	"CREATE TABLE IF NOT EXISTS sessions ("
	"  token      TEXT PRIMARY KEY,"
	"  user_id    INTEGER NOT NULL REFERENCES users(id),"
	"  expires_at INTEGER NOT NULL"                          // timestamp Unix
	");"
	"CREATE TABLE IF NOT EXISTS login_failures ("
	"  ip TEXT NOT NULL,"
	"  at INTEGER NOT NULL"
	");"
	"CREATE INDEX IF NOT EXISTS login_failures_ip ON login_failures (ip, at);";

static int user_version(sqlite3 *db){
	sqlite3_stmt *stmt = cs_db_prepare(db, "PRAGMA user_version;");
	if (stmt == NULL){return -1;}
	int version = (sqlite3_step(stmt) == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : -1;
	sqlite3_finalize(stmt);
	return version;
}

int db_init(void){
	make_parent_dir(cs_config.db_path);

	sqlite3 *db = db_open();
	if (db == NULL){return -1;}

	int ok = exec(db, SCHEMA) == 0;

	// migrations : `PRAGMA user_version` (0 au départ) retient lesquelles ont déjà
	// été appliquées, pour faire évoluer une base existante sans perdre ses données
	int version = ok ? user_version(db) : -1;
	if (version < 0){ok = 0;}

	if (ok && version < 1){
		// v1 : chaque message appartient à un utilisateur (NULL pour les anciens)
		ok = exec(db,
			"ALTER TABLE messages ADD COLUMN user_id INTEGER REFERENCES users(id);"
			"PRAGMA user_version = 1;") == 0;
	}

	sqlite3_close(db);
	return ok ? 0 : -1;
}

/* ---------------------------------------------------------------- comptes */

long db_user_create(sqlite3 *db, const char *name, const char *password_hash){
	sqlite3_stmt *stmt = cs_db_prepare(db, "INSERT INTO users (name, password_hash) VALUES (?, ?);");
	if (stmt == NULL){return -1;}
	sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, password_hash, -1, SQLITE_STATIC);

	int rc = sqlite3_step(stmt);
	long id = -1;
	if (rc == SQLITE_DONE){
		id = (long)sqlite3_last_insert_rowid(db);
	}
	else if (rc == SQLITE_CONSTRAINT){
		id = 0;   // contrainte UNIQUE : le nom est déjà pris
	}
	else {
		fprintf(stderr, "sqlite: %s\n", sqlite3_errmsg(db));
	}

	sqlite3_finalize(stmt);
	return id;
}

int db_user_find(sqlite3 *db, const char *name, cs_user *user, char *hash, size_t hash_size){
	sqlite3_stmt *stmt = cs_db_prepare(db, "SELECT id, name, password_hash FROM users WHERE name = ?;");
	if (stmt == NULL){return -1;}
	sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);

	int found = 0;
	int rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW){
		user->id = (long)sqlite3_column_int64(stmt, 0);
		snprintf(user->name, sizeof(user->name), "%s", (const char *)sqlite3_column_text(stmt, 1));
		snprintf(hash, hash_size, "%s", (const char *)sqlite3_column_text(stmt, 2));
		found = 1;
	}
	else if (rc != SQLITE_DONE){
		fprintf(stderr, "sqlite: %s\n", sqlite3_errmsg(db));
		found = -1;
	}

	sqlite3_finalize(stmt);
	return found;
}

int db_session_create(sqlite3 *db, const char *token, long user_id, long expires_at){
	// au passage, on fait le ménage dans les sessions expirées
	sqlite3_stmt *clean = cs_db_prepare(db,
		"DELETE FROM sessions WHERE expires_at <= CAST(strftime('%s', 'now') AS INTEGER);");
	if (clean != NULL){cs_db_run(db, clean);}

	sqlite3_stmt *stmt = cs_db_prepare(db, "INSERT INTO sessions (token, user_id, expires_at) VALUES (?, ?, ?);");
	if (stmt == NULL){return -1;}
	sqlite3_bind_text(stmt, 1, token, -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 2, user_id);
	sqlite3_bind_int64(stmt, 3, expires_at);
	return cs_db_run(db, stmt);
}

int db_session_user(sqlite3 *db, const char *token, long now, cs_user *user){
	sqlite3_stmt *stmt = cs_db_prepare(db,
		"SELECT users.id, users.name FROM sessions "
		"JOIN users ON users.id = sessions.user_id "
		"WHERE sessions.token = ? AND sessions.expires_at > ?;");
	if (stmt == NULL){return -1;}
	sqlite3_bind_text(stmt, 1, token, -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 2, now);

	int found = 0;
	int rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW){
		user->id = (long)sqlite3_column_int64(stmt, 0);
		snprintf(user->name, sizeof(user->name), "%s", (const char *)sqlite3_column_text(stmt, 1));
		found = 1;
	}
	else if (rc != SQLITE_DONE){
		fprintf(stderr, "sqlite: %s\n", sqlite3_errmsg(db));
		found = -1;
	}

	sqlite3_finalize(stmt);
	return found;
}

void db_session_delete(sqlite3 *db, const char *token){
	sqlite3_stmt *stmt = cs_db_prepare(db, "DELETE FROM sessions WHERE token = ?;");
	if (stmt == NULL){return;}
	sqlite3_bind_text(stmt, 1, token, -1, SQLITE_STATIC);
	cs_db_run(db, stmt);
}

int db_login_failures(sqlite3 *db, const char *ip, long since){
	sqlite3_stmt *stmt = cs_db_prepare(db, "SELECT COUNT(*) FROM login_failures WHERE ip = ? AND at > ?;");
	if (stmt == NULL){return -1;}
	sqlite3_bind_text(stmt, 1, ip, -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 2, since);

	int count = (sqlite3_step(stmt) == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : -1;
	sqlite3_finalize(stmt);
	return count;
}

void db_login_failure_add(sqlite3 *db, const char *ip, long now, long forget_before){
	sqlite3_stmt *stmt = cs_db_prepare(db, "INSERT INTO login_failures (ip, at) VALUES (?, ?);");
	if (stmt != NULL){
		sqlite3_bind_text(stmt, 1, ip, -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt, 2, now);
		cs_db_run(db, stmt);
	}

	sqlite3_stmt *clean = cs_db_prepare(db, "DELETE FROM login_failures WHERE at <= ?;");
	if (clean != NULL){
		sqlite3_bind_int64(clean, 1, forget_before);
		cs_db_run(db, clean);
	}
}
