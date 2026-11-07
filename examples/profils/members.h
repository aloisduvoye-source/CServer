#ifndef MEMBERS_H
#define MEMBERS_H

// la base du site : profils (e-mail) et avatars. Aucune page ici, seulement du SQL.

#include"cserver.h"

#define EMAIL_MAX  254            // la limite des adresses e-mail
#define AVATAR_MAX (256 * 1024)   // 256 Ko

typedef struct {
	long id;
	char name[32];
	char email[EMAIL_MAX + 1];   // "" si pas de profil
	char since[16];              // date d'inscription : "2026-11-08"
	long avatar;                 // version de l'avatar ; 0 : pas d'avatar
} member;

// 1 si l'adresse a une forme acceptable (vérification simple : un seul @, un point
// dans le domaine, pas d'espace). Elle n'est pas prouvée : il faudrait envoyer un e-mail.
int email_valid(const char *email);

// crée ou change l'e-mail ; 1 si OK, 0 si l'adresse est déjà prise, -1 si erreur
int profile_set_email(sqlite3 *db, long user_id, const char *email);
// le nom du compte qui a cet e-mail ; 1 si trouvé, 0 sinon, -1 si erreur
int profile_name_by_email(sqlite3 *db, const char *email, char *name, size_t size);

// 1 si trouvé (m rempli), 0 sinon, -1 si erreur
int member_by_id(sqlite3 *db, long id, member *m);
int member_by_name(sqlite3 *db, const char *name, member *m);
// appelle fn pour chaque membre (par ordre d'inscription) ; leur nombre, ou -1
typedef void (*member_fn)(void *ctx, const member *m);
int members_each(sqlite3 *db, member_fn fn, void *ctx);

// remplace l'avatar (type : celui donné par cs_image_type) ; 0, ou -1 si erreur
int avatar_set(sqlite3 *db, long user_id, const char *type, const char *data, size_t len);
int avatar_delete(sqlite3 *db, long user_id);
// une copie de l'image (à libérer avec free) ; 1 si trouvée, 0 sinon, -1 si erreur
int avatar_get(sqlite3 *db, long user_id, char *type, size_t type_size, char **data, size_t *len);

#endif
