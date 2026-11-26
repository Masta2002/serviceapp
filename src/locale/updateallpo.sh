#!/bin/bash
# Script to generate po files outside of the normal build process
#
# Pre-requisite:
# The following tools must be installed on your system and accessible from path
# gawk, find, xgettext, sed (gsed on macOS), msguniq, msgmerge, msgattrib, msgfmt, msginit
#
# Run this script from within the locale folder.
#
# Author: Pr2 for OpenPLi Team
# Version: 1.0
#          1.1 - GNU find on Linux (-s -X only on macOS); check for the gettext
#                tools up front; drop the xml2po.py step (no such file, no xml
#                with strings); charset UTF-8 in the final pot; msginit with
#                the language, not the file name; skip the po rewrite when
#                only the POT-Creation-Date header changed (no empty git diffs)
#          1.2 - msgmerge --no-fuzzy-matching: no guessed (fuzzy) translations
#
# The languages are the LANGS of Makefile.am: a new language needs its entry
# there, or it is neither updated nor installed.
#
localgsed="sed"
findoptions=""

if [[ "$OSTYPE" == "darwin"* ]]
	then
		# Mac OSX
		printf "Script running on Mac OSX [%s]\n" "$OSTYPE"
		findoptions=" -s -X "
		localgsed="gsed"
fi

# fail early with a clear message instead of a cryptic "command not found"
for tool in gawk xgettext msguniq msgmerge msgattrib msgfmt msginit; do
	if ! command -v $tool > /dev/null 2>&1; then
		printf "Error: '%s' not found in PATH - install the gettext tools first.\n" "$tool" >&2
		exit 1
	fi
done

PluginName=ServiceApp
printf "Po files update/creation from script starting.\n"
languages=($(gawk ' BEGIN { FS=" " }
		/^LANGS/ {
			for (i=3; i<=NF; i++)
				printf "%s ", $i
		} ' Makefile.am ))

#
# Arguments to generate the pot and po files are not retrieved from the Makefile.
# So if parameters are changed in Makefile please report the same changes in this script.
#

# keep the current pot around so we can tell a real string change from a
# pure POT-Creation-Date bump once the new one is built
if [[ -f $PluginName.pot ]]; then
	cp $PluginName.pot $PluginName.pot.bak
fi

printf "Creating temporary file $PluginName-py.pot\n"
find $findoptions .. -name "*.py" -exec xgettext --no-wrap -L Python --from-code=UTF-8 -kpgettext:1c,2 -k_ngettext:1,2 --add-comments="TRANSLATORS:" -d $PluginName -s -o $PluginName-py.pot {} \+
printf "Merging to create: $PluginName.pot\n"
msguniq --no-wrap --no-location -o $PluginName.pot $PluginName-py.pot
$localgsed --in-place $PluginName.pot --expression=s/CHARSET/UTF-8/
rm $PluginName-py.pot

# if nothing but the auto-generated POT-Creation-Date line differs from the
# previous pot, restore it and stop - no merge needed, po files (and git) stay clean
if [[ -f $PluginName.pot.bak ]]; then
	grep -v '^"POT-Creation-Date:' $PluginName.pot     > $PluginName.pot.new
	grep -v '^"POT-Creation-Date:' $PluginName.pot.bak > $PluginName.pot.old
	if diff -q $PluginName.pot.old $PluginName.pot.new > /dev/null; then
		printf "No string changes (only POT-Creation-Date) - leaving all files as they are.\n"
		mv $PluginName.pot.bak $PluginName.pot
		rm -f $PluginName.pot.old $PluginName.pot.new
		printf "Po files update/creation from script finished!\n"
		exit 0
	fi
	rm -f $PluginName.pot.bak $PluginName.pot.old $PluginName.pot.new
fi

OLDIFS=$IFS
IFS=" "
for lang in "${languages[@]}" ; do
	if [[ -f $lang.po ]]; then
		printf "Updating existing translation file %s.po\n" $lang
		msgmerge --backup=none --no-wrap --no-fuzzy-matching -s -U $lang.po $PluginName.pot && touch $lang.po
		msgattrib --no-wrap --no-obsolete $lang.po -o $lang.po
		msgfmt -c -o $lang.mo $lang.po
	else
		printf "New file created: %s.po, please add it to github before commit\n" $lang
		msginit -l $lang -o $lang.po -i $PluginName.pot --no-translator
		msgfmt -c -o $lang.mo $lang.po
	fi
done
IFS=$OLDIFS
# the .mo files are built by make, these were only a check
find . -maxdepth 1 -name "*.mo" -type f -delete
printf "Po files update/creation from script finished!\n"
