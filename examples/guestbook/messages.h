#ifndef MESSAGES_H
#define MESSAGES_H

// requêtes SQL du livre d'or (la table messages)

#include"cserver.h"

// fonction appelée pour chaque message lu ; author_id vaut 0 pour les anciens
// messages anonymes
typedef void (*message_fn)(void *ctx, long id, long author_id,
                           const char *name, const char *message, const char *date);

// enregistre un message puis appelle fn avec le message créé (fn peut être NULL)
// renvoie 0 si OK, -1 si erreur
int messages_add(sqlite3 *db, long user_id, const char *name, const char *message,
                 message_fn fn, void *ctx);

// appelle fn pour chacun des `limit` derniers messages, du plus récent au plus ancien
// renvoie le nombre de messages, -1 si erreur
int messages_each(sqlite3 *db, int limit, message_fn fn, void *ctx);

// supprime le message `id` s'il appartient à `user_id` ; 1 si supprimé, 0 sinon, -1 si erreur
int messages_delete(sqlite3 *db, long id, long user_id);

#endif
