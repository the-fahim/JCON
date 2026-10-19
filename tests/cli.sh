#!/bin/sh
set -eu
cli=$1
test "$(printf ' { "a": [true, null, 1e9999] } ' | "$cli")" = '{"a":[true,null,1e9999]}'
printf '[1,2]' | "$cli" --pretty | "$cli" --check
test -z "$(printf 'null' | "$cli" --check)"
if printf '[1,]' | "$cli" --check 2>/dev/null; then
    echo 'Invalid JSON was accepted' >&2
    exit 1
else
    test "$?" -eq 1
fi
if "$cli" --unknown 2>/dev/null; then
    exit 1
else
    test "$?" -eq 2
fi
echo 'All CLI tests passed.'
