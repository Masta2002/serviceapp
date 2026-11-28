#!/bin/bash
# Line ends, file modes and whitespace (run from the repository root):
#   1. no text file with CRLF line ends (whole tree)
#   2. *.sh executable, other files not, *.py as they are (whole tree)
#   3. git diff --check over the given commit range: no trailing whitespace,
#      space before tab in indent, blank lines at EOF, conflict markers -
#      only in the lines the range changes, so old code does not fail it
# Usage: check-hygiene.sh [<range>]   e.g. origin/master...HEAD
# Errors: exit 1. Output uses GitHub annotations when GITHUB_ACTIONS is set.

set -u
rc=0
range=${1:-}

err() {
	if [ -n "${GITHUB_ACTIONS:-}" ]; then echo "::error file=$1::$2"; else echo "ERROR $1: $2"; fi
	rc=1
}

# 1. CRLF (Git's own text test: no NUL byte)
while IFS= read -r -d '' f; do
	[ -f "$f" ] && [ ! -L "$f" ] || continue
	grep -Iq . "$f" || continue
	grep -q $'\r$' "$f" && err "$f" "CRLF line ends"
done < <(git ls-files -z)

# 2. file modes
while read -r mode _ _ path; do
	case "$path" in
		*.sh) [ "$mode" = 100755 ] || err "$path" "shell script is not executable (git update-index --chmod=+x)" ;;
		*.py) ;;
		*) [ "$mode" = 100755 ] && err "$path" "executable bit set (git update-index --chmod=-x)" ;;
	esac
done < <(git ls-files -s)

# 3. whitespace errors in the changed lines
if [ -n "$range" ]; then
	echo "git diff --check $range"
	while IFS= read -r line; do
		case "$line" in
			+*) ;;  # the offending line itself, printed after its location
			*:[0-9]*:*) f=${line%%:*}; rest=${line#*:}; n=${rest%%:*}; msg=${rest#*:}
				if [ -n "${GITHUB_ACTIONS:-}" ]; then echo "::error file=$f,line=$n::${msg# }"; else echo "ERROR $f:$n:$msg"; fi
				rc=1 ;;
		esac
	done < <(git -c core.whitespace=trailing-space,space-before-tab,blank-at-eof diff --check "$range" -- . ':(exclude)src/serviceapp/cJSON')
fi

[ "$rc" = 0 ] && echo "hygiene OK"
exit $rc
