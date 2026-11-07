#ifndef DB_H
#define DB_H

#include<sqlite3.h>

// une seule fois au démarrage, avant les fork : crée la base et les tables
int db_init(void);

// ouvre une connexion ; chaque enfant ouvre la sienne
// (une connexion SQLite ne doit jamais être partagée entre processus)
sqlite3 *db_open(void);

// incrémente le compteur `name` et renvoie sa nouvelle valeur, -1 si erreur
long db_counter_incr(sqlite3 *db, const char *name);

// livre d'or : enregistre un message, renvoie 0 si OK, -1 si erreur
int db_message_add(sqlite3 *db, const char *name, const char *message);

// appelle fn pour chacun des `limit` derniers messages, du plus récent au plus ancien
// renvoie le nombre de messages, -1 si erreur
typedef void (*MessageFn)(void *ctx, const char *name, const char *message, const char *date);
int db_messages_each(sqlite3 *db, int limit, MessageFn fn, void *ctx);

#endif
