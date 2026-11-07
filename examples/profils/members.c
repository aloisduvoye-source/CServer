#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include"members.h"

int email_valid(const char *email){
	size_t n = strlen(email);
	if (n < 3 || n > EMAIL_MAX){return 0;}

	const char *at = strchr(email, '@');
	if (at == NULL || at == email || strchr(at + 1, '@') != NULL){return 0;}   // un seul @, pas au début
	const char *domain = at + 1;
	const char *dot = strrchr(domain, '.');
	if (dot == NULL || dot == domain || dot[1] == '\0'){return 0;}             // "a@b.c" au minimum

	// des caractères visibles seulement : ni espace, ni retour à la ligne, ni caractère
	// de contrôle (l'adresse est réaffichée et pourrait un jour finir dans un e-mail)
	for (const char *p = email; *p != '\0'; p++){
		if ((unsigned char)*p <= ' ' || (unsigned char)*p >= 0x7F){return 0;}
	}
	return 1;
}

int profile_set_email(sqlite3 *db, long user_id, const char *email){
	// crée le profil, ou change l'e-mail s'il existe déjà
	sqlite3_stmt *stmt = cs_db_prepare(db,
		"INSERT INTO profiles (user_id, email) VALUES (?, ?) "
		"ON CONFLICT (user_id) DO UPDATE SET email = excluded.email;");
	if (stmt == NULL){return -1;}
	sqlite3_bind_int64(stmt, 1, user_id);
	sqlite3_bind_text(stmt, 2, email, -1, SQLITE_STATIC);

	int rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	if (rc == SQLITE_DONE){return 1;}
	if (rc == SQLITE_CONSTRAINT){return 0;}   // UNIQUE : un autre compte a cette adresse
	fprintf(stderr, "sqlite: %s\n", sqlite3_errmsg(db));
	return -1;
}

int profile_name_by_email(sqlite3 *db, const char *email, char *name, size_t size){
	sqlite3_stmt *stmt = cs_db_prepare(db,
		"SELECT u.name FROM profiles p JOIN cs_users u ON u.id = p.user_id WHERE p.email = ?;");
	if (stmt == NULL){return -1;}
	sqlite3_bind_text(stmt, 1, email, -1, SQLITE_STATIC);

	int found = 0;
	int rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW){
		const char *n = (const char *)sqlite3_column_text(stmt, 0);
		if (n != NULL && strlen(n) < size){
			strcpy(name, n);
			found = 1;
		}
	}
	else if (rc != SQLITE_DONE){
		fprintf(stderr, "sqlite: %s\n", sqlite3_errmsg(db));
		found = -1;
	}
	sqlite3_finalize(stmt);
	return found;
}

/* ---------------------------------------------------------------- membres */

// les colonnes lues par read_member, dans cet ordre
#define MEMBER_SELECT \
	"SELECT u.id, u.name, COALESCE(p.email, ''), substr(u.created_at, 1, 10), COALESCE(a.version, 0) " \
	"FROM cs_users u " \
	"LEFT JOIN profiles p ON p.user_id = u.id " \
	"LEFT JOIN avatars  a ON a.user_id = u.id "

static void copy_text(sqlite3_stmt *stmt, int col, char *out, size_t size){
	const char *s = (const char *)sqlite3_column_text(stmt, col);
	snprintf(out, size, "%s", s != NULL ? s : "");
}

static void read_member(sqlite3_stmt *stmt, member *m){
	memset(m, 0, sizeof(*m));
	m->id = (long)sqlite3_column_int64(stmt, 0);
	copy_text(stmt, 1, m->name, sizeof(m->name));
	copy_text(stmt, 2, m->email, sizeof(m->email));
	copy_text(stmt, 3, m->since, sizeof(m->since));
	m->avatar = (long)sqlite3_column_int64(stmt, 4);
}

// exécute une requête qui renvoie au plus un membre
static int one_member(sqlite3 *db, sqlite3_stmt *stmt, member *m){
	int rc = sqlite3_step(stmt);
	int found = 0;
	if (rc == SQLITE_ROW){
		read_member(stmt, m);
		found = 1;
	}
	else if (rc != SQLITE_DONE){
		fprintf(stderr, "sqlite: %s\n", sqlite3_errmsg(db));
		found = -1;
	}
	sqlite3_finalize(stmt);
	return found;
}

int member_by_id(sqlite3 *db, long id, member *m){
	sqlite3_stmt *stmt = cs_db_prepare(db, MEMBER_SELECT "WHERE u.id = ?;");
	if (stmt == NULL){return -1;}
	sqlite3_bind_int64(stmt, 1, id);
	return one_member(db, stmt, m);
}

int member_by_name(sqlite3 *db, const char *name, member *m){
	// cs_users.name est COLLATE NOCASE : /u/Jean et /u/jean donnent le même membre
	sqlite3_stmt *stmt = cs_db_prepare(db, MEMBER_SELECT "WHERE u.name = ?;");
	if (stmt == NULL){return -1;}
	sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
	return one_member(db, stmt, m);
}

int members_each(sqlite3 *db, member_fn fn, void *ctx){
	sqlite3_stmt *stmt = cs_db_prepare(db, MEMBER_SELECT "ORDER BY u.id LIMIT 500;");
	if (stmt == NULL){return -1;}

	int count = 0;
	int rc;
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW){
		member m;
		read_member(stmt, &m);
		fn(ctx, &m);
		count++;
	}
	if (rc != SQLITE_DONE){
		fprintf(stderr, "sqlite: %s\n", sqlite3_errmsg(db));
		count = -1;
	}
	sqlite3_finalize(stmt);
	return count;
}

/* ---------------------------------------------------------------- avatars */

int avatar_set(sqlite3 *db, long user_id, const char *type, const char *data, size_t len){
	sqlite3_stmt *stmt = cs_db_prepare(db,
		"INSERT INTO avatars (user_id, type, data) VALUES (?, ?, ?) "
		"ON CONFLICT (user_id) DO UPDATE SET type = excluded.type, data = excluded.data, "
		"version = version + 1;");
	if (stmt == NULL){return -1;}
	sqlite3_bind_int64(stmt, 1, user_id);
	sqlite3_bind_text(stmt, 2, type, -1, SQLITE_STATIC);
	sqlite3_bind_blob(stmt, 3, data, (int)len, SQLITE_STATIC);   // len <= AVATAR_MAX
	return cs_db_run(db, stmt);
}

int avatar_delete(sqlite3 *db, long user_id){
	sqlite3_stmt *stmt = cs_db_prepare(db, "DELETE FROM avatars WHERE user_id = ?;");
	if (stmt == NULL){return -1;}
	sqlite3_bind_int64(stmt, 1, user_id);
	return cs_db_run(db, stmt);
}

int avatar_get(sqlite3 *db, long user_id, char *type, size_t type_size, char **data, size_t *len){
	sqlite3_stmt *stmt = cs_db_prepare(db, "SELECT type, data FROM avatars WHERE user_id = ?;");
	if (stmt == NULL){return -1;}
	sqlite3_bind_int64(stmt, 1, user_id);

	int found = 0;
	int rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW){
		// les octets appartiennent à SQLite jusqu'au finalize : on les copie
		const void *blob = sqlite3_column_blob(stmt, 1);
		size_t n = (size_t)sqlite3_column_bytes(stmt, 1);
		*data = malloc(n > 0 ? n : 1);
		if (*data == NULL){
			found = -1;
		}
		else {
			if (n > 0){memcpy(*data, blob, n);}
			*len = n;
			copy_text(stmt, 0, type, type_size);
			found = 1;
		}
	}
	else if (rc != SQLITE_DONE){
		fprintf(stderr, "sqlite: %s\n", sqlite3_errmsg(db));
		found = -1;
	}
	sqlite3_finalize(stmt);
	return found;
}
