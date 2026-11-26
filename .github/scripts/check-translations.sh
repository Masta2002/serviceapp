#!/bin/bash
# Checks src/locale without changing the tree (run from the repository root):
#   1. every LANGS entry of src/locale/Makefile.am has a .po file and every
#      .po file is in LANGS (otherwise it is neither updated nor installed)
#   2. msgfmt -c on every .po (syntax, format strings, header)
#   3. ServiceApp.pot is up to date with the _() strings of the Python files
#      (xgettext as in updateallpo.sh, POT-Creation-Date ignored)
#   4. every .po has exactly the msgids of the .pot (msgmerge was run)
#   5. no compiled .mo files are committed (make builds them)
# Fuzzy entries are reported as warnings only: msgfmt drops them at build
# time, the English text is shown instead.
# Errors: exit 1. Output uses GitHub annotations when GITHUB_ACTIONS is set.

set -u
export LC_ALL=C
cd src/locale || exit 1
rc=0

err() {
	if [ -n "${GITHUB_ACTIONS:-}" ]; then echo "::error file=src/locale/$1::$2"; else echo "ERROR src/locale/$1: $2"; fi
	rc=1
}
warn() {
	if [ -n "${GITHUB_ACTIONS:-}" ]; then echo "::warning file=src/locale/$1::$2"; else echo "WARNING src/locale/$1: $2"; fi
}

# 1. LANGS <-> .po files
langs=$(sed -n 's/^LANGS[[:space:]]*=[[:space:]]*//p' Makefile.am)
for l in $langs; do
	[ -f "$l.po" ] || err Makefile.am "LANGS has '$l' but $l.po is missing"
done
for f in *.po; do
	l=${f%.po}
	case " $langs " in *" $l "*) ;; *) err "$f" "$l is not in LANGS of Makefile.am (not installed)";; esac
done

# 2. msgfmt -c, statistics
for f in *.po; do
	if ! out=$(msgfmt -c --statistics -o /dev/null "$f" 2>&1); then
		err "$f" "msgfmt -c failed: $(echo "$out" | head -3 | tr '\n' ' ')"
		continue
	fi
	echo "$f: $out"
	case "$out" in *fuzzy*) warn "$f" "fuzzy entries (not used at runtime): $out";; esac
done

# 3. pot up to date (same xgettext call as updateallpo.sh)
tmp=$(mktemp -d)
find .. -name "*.py" -exec xgettext --no-wrap -L Python --from-code=UTF-8 -kpgettext:1c,2 -k_ngettext:1,2 \
	--add-comments="TRANSLATORS:" -d ServiceApp -s -o "$tmp/py.pot" {} + 2> "$tmp/xgettext.log"
# texts of the C++ part: SAPP_TR("...")
find ../serviceapp \( -name "*.cpp" -o -name "*.h" \) -exec xgettext --no-wrap -L C++ --from-code=UTF-8 -kSAPP_TR \
	--add-comments="TRANSLATORS:" -d ServiceApp -s -o "$tmp/cpp.pot" {} + 2>> "$tmp/xgettext.log"
if [ -f "$tmp/cpp.pot" ]; then
	msgcat --no-wrap --use-first "$tmp/py.pot" "$tmp/cpp.pot" -o "$tmp/all.pot"
else
	cp "$tmp/py.pot" "$tmp/all.pot"
fi
msguniq --no-wrap --no-location -s -o "$tmp/new.pot" "$tmp/all.pot"
sed -i 's/CHARSET/UTF-8/' "$tmp/new.pot"
grep -v '^"POT-Creation-Date:' "$tmp/new.pot" > "$tmp/new.cmp"
grep -v '^"POT-Creation-Date:' ServiceApp.pot > "$tmp/old.cmp"
if ! diff -u "$tmp/old.cmp" "$tmp/new.cmp" > "$tmp/pot.diff"; then
	err ServiceApp.pot "out of date with the _() strings: run updateallpo.sh in src/locale"
	head -40 "$tmp/pot.diff"
fi

# 4. po msgids == pot msgids
for f in *.po; do
	n=$(msgcomm --no-wrap --less-than=2 -u ServiceApp.pot "$f" 2>/dev/null | grep -c '^msgid "' || true)
	# msgcomm prints nothing for equal sets, else the header (msgid "") plus
	# the differing entries
	if [ "$n" != 0 ]; then
		err "$f" "$((n - 1)) msgid(s) differ from ServiceApp.pot: run updateallpo.sh in src/locale"
	fi
done

# 5. no committed .mo
if git ls-files --error-unmatch '*.mo' > /dev/null 2>&1; then
	err . "compiled .mo files are committed: $(git ls-files '*.mo' | tr '\n' ' ')"
fi

rm -rf "$tmp"
[ "$rc" = 0 ] && echo "translations OK"
exit $rc
