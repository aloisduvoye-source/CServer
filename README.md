# CServer

Mini framework web en C, tout à la main (bibliothèques : SQLite, et libcrypt pour les mots de passe).

- `cserver/` : le framework, compilé en `libcserver.a` — **[guide du framework](cserver/README.md)**
- `examples/guestbook/` : livre d'or avec comptes et API JSON
- `examples/todo/` : liste de tâches personnelle

## Compiler et lancer

```sh
sudo apt install libsqlite3-dev libcrypt-dev   # une seule fois
make run                # le livre d'or
make SITE=todo run      # la liste de tâches
```

Puis ouvrir http://localhost:8080.

Tests : `make test-all` (le framework, puis chaque site ; sur d'autres ports que le serveur de dev)

Pour un nouveau projet, hors de ce dépôt : `make new DIR=../mon-site` (voir [Créer son projet](cserver/README.md#créer-son-projet)).

## Plan

Objectif : un livre d'or (formulaire + liste des messages).

- [x] Fichiers statiques
- [x] Plusieurs clients en parallèle (`fork`)
- [x] Routes dynamiques
- [x] Paramètres d'URL (`?q=...`, décodage `%XX`)
- [x] Requêtes POST (lecture selon `Content-Length`)
- [x] Base de données SQLite
- [x] Échappement HTML (XSS)
- [x] Livre d'or (enregistrer et afficher les messages)
- [x] API JSON
- [x] Sessions et cookies

## Framework

Objectif : réutiliser `cserver/` pour d'autres sites, sans y toucher.

- [x] Séparer le framework et le site
- [x] Configuration et `cs_run()`
- [x] Contexte `cs_ctx` passé aux handlers
- [x] Migrations par nom
- [x] Modèles HTML
- [x] Un deuxième site d'exemple
