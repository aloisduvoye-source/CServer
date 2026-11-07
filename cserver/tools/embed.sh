#!/bin/sh
# Intègre des fichiers texte au binaire : les transforme en un tableau C de paires
# { nom, contenu }. Appelé par le Makefile, pour les migrations (.sql) et les
# modèles HTML par défaut du framework (.html) :
#
#   embed.sh <en-tête> <type> <nom du tableau> <extension à retirer> fichiers... > x.c
#
#   embed.sh cserver.h cs_migration site_migrations .sql 001_a.sql 002_b.sql
#     -> { "001_a", "...contenu..." }, ...
#
# Les fichiers sont pris dans l'ordre donné (le Makefile les trie).

header=$1
type=$2
name=$3
ext=$4
shift 4

echo "// généré par cserver/tools/embed.sh : ne pas modifier, modifier les fichiers d'origine"
echo "#include\"$header\""
echo
echo "const $type $name[] = {"

for f in "$@"; do
	base=$(basename "$f" "$ext")
	case $base in
		""|*[!A-Za-z0-9_.-]*)
			echo "embed.sh : $f : nom invalide (seulement lettres, chiffres, _ - et .)" >&2
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
