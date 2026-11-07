# CServer

Mini framework web en C, tout à la main (bibliothèques : SQLite, et libcrypt pour les mots de passe).

Ce dépôt est un **modèle** : on le clone, et le clone devient son projet.

```sh
sudo apt install libsqlite3-dev libcrypt-dev   # une seule fois
git clone https://github.com/aloisduvoye-source/CServer.git mon-site && cd mon-site
make new          # crée le projet à la racine : main.c, templates/, www/...
make run          # le lance, puis ouvrir http://localhost:8080
```

| Commande | Effet |
|---|---|
| `make run`, `make test` | ton projet (à la racine) |
| `make run DIR=examples/guestbook` | un exemple : `examples/guestbook` (livre d'or, comptes, API JSON) ou `examples/todo` (liste de tâches) |
| `make new ../autre-site` | un projet à part, avec sa propre copie du framework |
| `make test-all` | les tests du framework, des exemples et de ton projet |
| `make fuzz` | le fuzzing du framework (demande clang) |
| `docker build -t mon-site .` | une image prête à mettre en ligne ([détails](cserver/README.md#mise-en-ligne)) |

- `cserver/` : le framework — **[guide du framework](cserver/README.md)**
- `examples/` : des sites complets, pour s'inspirer

**Ton dépôt git** : après le clone, `origin` est le dépôt de cserver. Pour avoir le tien, tout en pouvant récupérer les nouvelles versions du framework :

```sh
git remote rename origin cserver      # le framework, pour les mises à jour
git remote add origin <ton dépôt>     # ton projet
git pull cserver master                # plus tard : une nouvelle version de cserver
```

Tes fichiers (`main.c`, `templates/`...) ne sont pas ceux du framework (`cserver/`, `examples/`) : les mises à jour ne les touchent pas.
