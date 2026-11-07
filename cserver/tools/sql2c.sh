#!/bin/sh
# Transforme des fichiers .sql en un tableau C de migrations (cs_migration),
# pour les intégrer au binaire. Appelé par le Makefile :
#
#   sql2c.sh <nom du tableau> 001_a.sql 002_b.sql ... > migrations.c
#
# Le nom de chaque migration est celui de son fichier, sans le .sql.
# Les fichiers sont pris dans l'ordre donné (le Makefile les trie).

name=$1
shift

echo "// généré par cserver/tools/sql2c.sh : ne pas modifier, modifier les .sql"
echo '#include"cserver.h"'
echo
echo "const cs_migration $name[] = {"

for f in "$@"; do
	base=$(basename "$f" .sql)
	case $base in
		*[!A-Za-z0-9_-]*)
			echo "sql2c.sh : $f : nom invalide (seulement lettres, chiffres, _ et -)" >&2
			exit 1 ;;
	esac

	echo "	{ \"$base\","
	echo "		\"\""
	# chaque ligne devient "ligne\n" ; on échappe d'abord \ puis "
	sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/		"/' -e 's/$/\\n"/' "$f"
	echo "	},"
done

echo "	{ NULL, NULL }"
echo "};"
