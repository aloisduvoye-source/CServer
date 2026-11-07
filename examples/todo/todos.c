#include<stdio.h>
#include<ctype.h>
#include"cserver.h"
#include"todos.h"

// le formulaire limite en caractères (maxlength="200"), le serveur en octets (UTF-8)
#define TITLE_BYTES (200 * 4)

static int is_blank(const char *s){
	for (; *s != '\0'; s++){
		if (!isspace((unsigned char)*s)){return 0;}
	}
	return 1;
}

// les tâches de l'utilisateur, une par modèle todo.html : d'abord celles à faire
static int render_items(cs_ctx *c, sqlite3 *db, long user_id, cs_buf *out, long *left){
	sqlite3_stmt *stmt = cs_db_prepare(db,
		"SELECT id, title, done FROM todos WHERE user_id = ? ORDER BY done, id DESC;");
	if (stmt == NULL){return -1;}
	sqlite3_bind_int64(stmt, 1, user_id);

	int ok = 1;
	int rc;
	*left = 0;
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW){
		int done = sqlite3_column_int(stmt, 2);
		if (!done){(*left)++;}

		cs_vars v;
		cs_vars_init(&v);
		cs_set_long(&v, "id", (long)sqlite3_column_int64(stmt, 0));
		cs_set(&v, "title", (const char *)sqlite3_column_text(stmt, 1));
		cs_set(&v, "done", done ? "1" : "");
		if (v.error || cs_render(c, out, "todo.html", &v) < 0){ok = 0;}
		cs_vars_free(&v);
	}
	if (rc != SQLITE_DONE){ok = 0;}

	sqlite3_finalize(stmt);
	return ok ? 0 : -1;
}

void todos_list(cs_ctx *c){
	const cs_user *user = cs_require_user(c);
	if (user == NULL){return;}

	cs_buf items;
	cs_buf_init(&items);
	long left;
	sqlite3 *db = cs_db(c);
	if (db == NULL || render_items(c, db, user->id, &items, &left) < 0 || items.error){
		cs_buf_free(&items);
		cs_error(c, 500);
		return;
	}

	cs_vars v;
	cs_vars_init(&v);
	cs_set(&v, "title", "my todos");
	cs_set_html(&v, "items", items.data);
	cs_set_long(&v, "left", left);
	cs_buf_free(&items);
	cs_page(c, 200, "todos.html", &v);
}

void todos_create(cs_ctx *c){
	const cs_user *user = cs_require_user(c);
	if (user == NULL){return;}

	char title[TITLE_BYTES + 1];
	if (cs_form(c, "title", title, sizeof(title)) < 0 || is_blank(title) || !cs_utf8_valid(title)){
		cs_error(c, 400);
		return;
	}

	// au plus 60 tâches par minute et par compte : de quoi taper vite, pas de quoi
	// remplir la base avec un script
	char key[32];
	snprintf(key, sizeof(key), "user:%ld", user->id);
	if (!cs_rate_limit(c, "todo", key, 60, 60)){
		cs_error(c, 429);
		return;
	}

	sqlite3 *db = cs_db(c);
	sqlite3_stmt *stmt = db != NULL ? cs_db_prepare(db, "INSERT INTO todos (user_id, title) VALUES (?, ?);") : NULL;
	if (stmt == NULL){
		cs_error(c, 500);
		return;
	}
	sqlite3_bind_int64(stmt, 1, user->id);
	sqlite3_bind_text(stmt, 2, title, -1, SQLITE_STATIC);
	if (cs_db_run(db, stmt) < 0){
		cs_error(c, 500);
		return;
	}

	cs_redirect(c, "/todos");
}

// exécute `sql` (paramètres : l'id de l'URL, puis l'utilisateur) sur une seule tâche
static void update_todo(cs_ctx *c, const char *sql){
	const cs_user *user = cs_require_user(c);
	if (user == NULL){return;}

	long id = cs_param_id(c, "id");
	if (id < 0){
		cs_error(c, 404);
		return;
	}

	sqlite3 *db = cs_db(c);
	sqlite3_stmt *stmt = db != NULL ? cs_db_prepare(db, sql) : NULL;
	if (stmt == NULL){
		cs_error(c, 500);
		return;
	}
	sqlite3_bind_int64(stmt, 1, id);
	sqlite3_bind_int64(stmt, 2, user->id);
	if (cs_db_run(db, stmt) < 0){
		cs_error(c, 500);
		return;
	}

	// aucune ligne touchée : la tâche n'existe pas, ou n'est pas à lui.
	// 404 dans les deux cas : on ne révèle même pas qu'une tâche d'un autre existe
	if (sqlite3_changes(db) == 0){
		cs_error(c, 404);
		return;
	}
	cs_redirect(c, "/todos");
}

void todos_toggle(cs_ctx *c){
	update_todo(c, "UPDATE todos SET done = 1 - done WHERE id = ? AND user_id = ?;");
}

void todos_remove(cs_ctx *c){
	update_todo(c, "DELETE FROM todos WHERE id = ? AND user_id = ?;");
}
