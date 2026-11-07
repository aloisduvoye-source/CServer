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

// prépare une requête ; affiche l'erreur et renvoie NULL si elle est invalide
static sqlite3_stmt *prepare(sqlite3 *db, const char *sql){
	sqlite3_stmt *stmt;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK){
		fprintf(stderr, "sqlite: %s\n", sqlite3_errmsg(db));
		return NULL;
	}
	return stmt;
}

// exécute une requête qui ne renvoie aucune ligne, puis la libère ; 0 si OK
static int run(sqlite3 *db, sqlite3_stmt *stmt){
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
	if (sqlite3_open(db_path(), &db) != SQLITE_OK){
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
	"CREATE TABLE IF NOT EXISTS counters ("
	"  name  TEXT PRIMARY KEY,"
	"  value INTEGER NOT NULL"
	");"
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
	sqlite3_stmt *stmt = prepare(db, "PRAGMA user_version;");
	if (stmt == NULL){return -1;}
	int version = (sqlite3_step(stmt) == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : -1;
	sqlite3_finalize(stmt);
	return version;
}

int db_init(void){
	if (getenv("CSERVER_DB") == NULL){
		mkdir("data", 0755);   // sans effet si le dossier existe déjà
	}

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

/* ---------------------------------------------------------------- compteur */

long db_counter_incr(sqlite3 *db, const char *name){
	// une seule requête : crée le compteur à 1, ou l'incrémente s'il existe
	sqlite3_stmt *stmt = prepare(db,
		"INSERT INTO counters (name, value) VALUES (?, 1) "
		"ON CONFLICT (name) DO UPDATE SET value = value + 1 "
		"RETURNING value;");
	if (stmt == NULL){return -1;}

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

/* ---------------------------------------------------------------- comptes */

long db_user_create(sqlite3 *db, const char *name, const char *password_hash){
	sqlite3_stmt *stmt = prepare(db, "INSERT INTO users (name, password_hash) VALUES (?, ?);");
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

int db_user_find(sqlite3 *db, const char *name, User *user, char *hash, size_t hash_size){
	sqlite3_stmt *stmt = prepare(db, "SELECT id, name, password_hash FROM users WHERE name = ?;");
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
	sqlite3_stmt *clean = prepare(db,
		"DELETE FROM sessions WHERE expires_at <= CAST(strftime('%s', 'now') AS INTEGER);");
	if (clean != NULL){run(db, clean);}

	sqlite3_stmt *stmt = prepare(db, "INSERT INTO sessions (token, user_id, expires_at) VALUES (?, ?, ?);");
	if (stmt == NULL){return -1;}
	sqlite3_bind_text(stmt, 1, token, -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 2, user_id);
	sqlite3_bind_int64(stmt, 3, expires_at);
	return run(db, stmt);
}

int db_session_user(sqlite3 *db, const char *token, long now, User *user){
	sqlite3_stmt *stmt = prepare(db,
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
	sqlite3_stmt *stmt = prepare(db, "DELETE FROM sessions WHERE token = ?;");
	if (stmt == NULL){return;}
	sqlite3_bind_text(stmt, 1, token, -1, SQLITE_STATIC);
	run(db, stmt);
}

int db_login_failures(sqlite3 *db, const char *ip, long since){
	sqlite3_stmt *stmt = prepare(db, "SELECT COUNT(*) FROM login_failures WHERE ip = ? AND at > ?;");
	if (stmt == NULL){return -1;}
	sqlite3_bind_text(stmt, 1, ip, -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 2, since);

	int count = (sqlite3_step(stmt) == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : -1;
	sqlite3_finalize(stmt);
	return count;
}

void db_login_failure_add(sqlite3 *db, const char *ip, long now, long forget_before){
	sqlite3_stmt *stmt = prepare(db, "INSERT INTO login_failures (ip, at) VALUES (?, ?);");
	if (stmt != NULL){
		sqlite3_bind_text(stmt, 1, ip, -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt, 2, now);
		run(db, stmt);
	}

	sqlite3_stmt *clean = prepare(db, "DELETE FROM login_failures WHERE at <= ?;");
	if (clean != NULL){
		sqlite3_bind_int64(clean, 1, forget_before);
		run(db, clean);
	}
}

/* ---------------------------------------------------------------- livre d'or */

// colonnes lues par les deux requêtes ci-dessous, dans cet ordre
#define MESSAGE_COLUMNS \
	"id, IFNULL(user_id, 0), name, message, strftime('%Y-%m-%d %H:%M', created_at, 'localtime')"

// exécute stmt et appelle fn pour chaque ligne ; renvoie le nombre de lignes, -1 si erreur
static int each_message(sqlite3 *db, sqlite3_stmt *stmt, MessageFn fn, void *ctx){
	int count = 0;
	int rc;
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW){
		if (fn != NULL){
			fn(ctx,
				(long)sqlite3_column_int64(stmt, 0),
				(long)sqlite3_column_int64(stmt, 1),
				(const char *)sqlite3_column_text(stmt, 2),
				(const char *)sqlite3_column_text(stmt, 3),
				(const char *)sqlite3_column_text(stmt, 4));
		}
		count++;
	}
	if (rc != SQLITE_DONE){
		fprintf(stderr, "sqlite: %s\n", sqlite3_errmsg(db));
		count = -1;
	}

	sqlite3_finalize(stmt);
	return count;
}

int db_message_add(sqlite3 *db, long user_id, const char *name, const char *message,
                   MessageFn fn, void *ctx){
	// RETURNING : SQLite renvoie la ligne créée (avec son id et sa date)
	sqlite3_stmt *stmt = prepare(db,
		"INSERT INTO messages (user_id, name, message) VALUES (?, ?, ?) "
		"RETURNING " MESSAGE_COLUMNS ";");
	if (stmt == NULL){return -1;}

	sqlite3_bind_int64(stmt, 1, user_id);
	sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 3, message, -1, SQLITE_STATIC);

	return each_message(db, stmt, fn, ctx) == 1 ? 0 : -1;
}

int db_messages_each(sqlite3 *db, int limit, MessageFn fn, void *ctx){
	sqlite3_stmt *stmt = prepare(db,
		"SELECT " MESSAGE_COLUMNS " "
		"FROM messages ORDER BY id DESC LIMIT ?;");
	if (stmt == NULL){return -1;}

	sqlite3_bind_int(stmt, 1, limit);
	return each_message(db, stmt, fn, ctx);
}

int db_message_delete(sqlite3 *db, long id, long user_id){
	// "AND user_id = ?" : on ne peut supprimer que ses propres messages
	sqlite3_stmt *stmt = prepare(db, "DELETE FROM messages WHERE id = ? AND user_id = ?;");
	if (stmt == NULL){return -1;}
	sqlite3_bind_int64(stmt, 1, id);
	sqlite3_bind_int64(stmt, 2, user_id);

	if (run(db, stmt) < 0){return -1;}
	return sqlite3_changes(db) > 0 ? 1 : 0;
}
