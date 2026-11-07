# CServer

Mini framework web en C, tout à la main (bibliothèques : SQLite, et libcrypt pour les mots de passe).

Ce dépôt est un **modèle** : on le clone, et le clone devient son projet.

```sh
sudo apt install libsqlite3-dev libcrypt-dev   # une seule fois
git clone <url> mon-site && cd mon-site
make new          # crée le projet à la racine : main.c, templates/, www/...
make run          # le lance, puis ouvrir http://localhost:8080
```

| Commande | Effet |
|---|---|
| `make run`, `make test` | ton projet (à la racine) |
| `make run DIR=examples/guestbook` | un exemple : `examples/guestbook` (livre d'or, comptes, API JSON) ou `examples/todo` (liste de tâches) |
| `make new DIR=../autre-site` | un projet à part, avec sa propre copie du framework |
| `make test-all` | les tests du framework, des exemples et de ton projet |

- `cserver/` : le framework — **[guide du framework](cserver/README.md)**
- `examples/` : des sites complets, pour s'inspirer

**Ton dépôt git** : après le clone, `origin` est le dépôt de cserver. Pour avoir le tien, tout en pouvant récupérer les nouvelles versions du framework :

```sh
git remote rename origin cserver      # le framework, pour les mises à jour
git remote add origin <ton dépôt>     # ton projet
git pull cserver main                 # plus tard : une nouvelle version de cserver
```

Tes fichiers (`main.c`, `templates/`...) ne sont pas ceux du framework (`cserver/`, `examples/`) : les mises à jour ne les touchent pas.

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
