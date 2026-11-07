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

#endif
