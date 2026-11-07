# Image d'un site cserver. Par défaut le projet à la racine ; pour un exemple :
#
#   docker build -t guestbook --build-arg DIR=examples/guestbook .
#   docker run -p 8080:8080 -v guestbook-data:/site/data guestbook
#
# La base SQLite est dans le volume /site/data : elle survit aux mises à jour de l'image.

# 1. compilation, avec tous les outils
FROM debian:trixie-slim AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends gcc libc6-dev make binutils libsqlite3-dev libcrypt-dev \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
ARG DIR=.
# le programme, et ce qu'il lit au lancement (les migrations sont déjà dedans)
RUN make -s DIR="$DIR" \
 && mkdir /site \
 && cp "$DIR/server" /site/ \
 && for d in www templates; do if [ -d "$DIR/$d" ]; then cp -r "$DIR/$d" /site/; fi; done

# 2. l'image finale : seulement le programme et les bibliothèques dont il a besoin
FROM debian:trixie-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends libsqlite3-0 libcrypt1 \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --system --no-create-home --shell /usr/sbin/nologin cserver
WORKDIR /site
COPY --from=build /site .
# le serveur ne peut écrire que dans data/ (la base), pas dans son code ni ses pages
RUN mkdir data && chown cserver:cserver data
USER cserver
VOLUME /site/data
EXPOSE 8080
CMD ["./server"]
