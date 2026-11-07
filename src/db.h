#ifndef DB_H
#define DB_H

#include<stddef.h>
#include<sqlite3.h>

typedef struct {
	long id;
	char name[32];
} User;

// une seule fois au démarrage, avant les fork : crée la base, les tables,
// et applique les migrations
int db_init(void);

// ouvre une connexion ; chaque enfant ouvre la sienne
// (une connexion SQLite ne doit jamais être partagée entre processus)
sqlite3 *db_open(void);

// incrémente le compteur `name` et renvoie sa nouvelle valeur, -1 si erreur
long db_counter_incr(sqlite3 *db, const char *name);

/* ---------------------------------------------------------------- comptes */

// renvoie l'id du nouvel utilisateur, 0 si le nom est déjà pris, -1 si erreur
long db_user_create(sqlite3 *db, const char *name, const char *password_hash);

// 1 si trouvé (user et hash remplis), 0 si absent, -1 si erreur
int db_user_find(sqlite3 *db, const char *name, User *user, char *hash, size_t hash_size);

int  db_session_create(sqlite3 *db, const char *token, long user_id, long expires_at);  // 0 ou -1
int  db_session_user(sqlite3 *db, const char *token, long now, User *user);            // 1, 0 ou -1
void db_session_delete(sqlite3 *db, const char *token);

// nombre d'échecs de connexion depuis `ip` après `since`, -1 si erreur
int  db_login_failures(sqlite3 *db, const char *ip, long since);
// enregistre un échec, et oublie ceux d'avant `forget_before`
void db_login_failure_add(sqlite3 *db, const char *ip, long now, long forget_before);

/* ---------------------------------------------------------------- livre d'or */

// fonction appelée pour chaque message lu ; author_id vaut 0 pour les anciens
// messages anonymes
typedef void (*MessageFn)(void *ctx, long id, long author_id,
                          const char *name, const char *message, const char *date);

// enregistre un message puis appelle fn avec le message créé (fn peut être NULL)
// renvoie 0 si OK, -1 si erreur
int db_message_add(sqlite3 *db, long user_id, const char *name, const char *message,
                   MessageFn fn, void *ctx);

// appelle fn pour chacun des `limit` derniers messages, du plus récent au plus ancien
// renvoie le nombre de messages, -1 si erreur
int db_messages_each(sqlite3 *db, int limit, MessageFn fn, void *ctx);

// supprime le message `id` s'il appartient à `user_id` ; 1 si supprimé, 0 sinon, -1 si erreur
int db_message_delete(sqlite3 *db, long id, long user_id);

#endif
