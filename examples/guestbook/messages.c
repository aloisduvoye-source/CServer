#include<stdio.h>
#include"messages.h"

// colonnes lues par les deux requêtes ci-dessous, dans cet ordre
#define MESSAGE_COLUMNS \
	"id, IFNULL(user_id, 0), name, message, strftime('%Y-%m-%d %H:%M', created_at, 'localtime')"

// exécute stmt et appelle fn pour chaque ligne ; renvoie le nombre de lignes, -1 si erreur
static int each_message(sqlite3 *db, sqlite3_stmt *stmt, message_fn fn, void *ctx){
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

int messages_add(sqlite3 *db, long user_id, const char *name, const char *message,
                 message_fn fn, void *ctx){
	// RETURNING : SQLite renvoie la ligne créée (avec son id et sa date)
	sqlite3_stmt *stmt = cs_db_prepare(db,
		"INSERT INTO messages (user_id, name, message) VALUES (?, ?, ?) "
		"RETURNING " MESSAGE_COLUMNS ";");
	if (stmt == NULL){return -1;}

	sqlite3_bind_int64(stmt, 1, user_id);
	sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 3, message, -1, SQLITE_STATIC);

	return each_message(db, stmt, fn, ctx) == 1 ? 0 : -1;
}

int messages_each(sqlite3 *db, int limit, message_fn fn, void *ctx){
	sqlite3_stmt *stmt = cs_db_prepare(db,
		"SELECT " MESSAGE_COLUMNS " "
		"FROM messages ORDER BY id DESC LIMIT ?;");
	if (stmt == NULL){return -1;}

	sqlite3_bind_int(stmt, 1, limit);
	return each_message(db, stmt, fn, ctx);
}

int messages_delete(sqlite3 *db, long id, long user_id){
	// "AND user_id = ?" : on ne peut supprimer que ses propres messages
	sqlite3_stmt *stmt = cs_db_prepare(db, "DELETE FROM messages WHERE id = ? AND user_id = ?;");
	if (stmt == NULL){return -1;}
	sqlite3_bind_int64(stmt, 1, id);
	sqlite3_bind_int64(stmt, 2, user_id);

	if (cs_db_run(db, stmt) < 0){return -1;}
	return sqlite3_changes(db) > 0 ? 1 : 0;
}
