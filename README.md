# CServer

Serveur HTTP en C, tout à la main (aucune bibliothèque externe).

## Compiler et lancer

```sh
make
./server
```

Depuis la racine du projet, puis ouvrir http://localhost:8080.

Tests : `./test.sh`

## Plan

Objectif : un livre d'or (formulaire + liste des messages).

- [x] Fichiers statiques
- [x] Plusieurs clients en parallèle (`fork`)
- [x] Routes dynamiques
- [ ] Paramètres d'URL (`?q=...`, décodage `%XX`)
- [ ] Requêtes POST (lecture selon `Content-Length`)
- [ ] Stockage dans des fichiers
- [ ] Échappement HTML (XSS)
- [ ] API JSON
- [ ] Sessions et cookies
