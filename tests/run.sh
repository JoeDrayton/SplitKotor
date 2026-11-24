#!/usr/bin/env bash
# Runs the unit tests and writes .build-truth/junit.xml.
#   tests/run.sh [<filter>]   filter: part of "<file>::<test>", e.g. build_test.sh or build_test.sh::rejects_x64_image
# A test is a function in tests/unit/*_test.sh whose name does not start with an underscore.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1
filter="${1:-}"
junit=.build-truth/junit.xml
mkdir -p .build-truth
rm -f "$junit"

total=0 failed=0 cases=""
for file in tests/unit/*_test.sh; do
    name="$(basename "$file")"
    while read -r test; do
        id="$name::$test"
        [[ "$id" == *"$filter"* ]] || continue
        total=$((total + 1))
        output="$(bash -euo pipefail -c '. "$1"; "$2"' _ "$file" "$test" 2>&1)" && rc=0 || rc=$?
        if [ "$rc" -eq 0 ]; then
            echo "ok   $id"
            cases+="<testcase classname=\"$name\" name=\"$test\"/>"
        else
            failed=$((failed + 1))
            echo "FAIL $id (rc $rc)"
            printf '%s\n' "$output" | sed 's/^/     /'
            cases+="<testcase classname=\"$name\" name=\"$test\"><failure message=\"rc $rc\"/></testcase>"
        fi
    done < <(sed -nE 's/^([a-z][a-z0-9_]*)\(\) \{.*/\1/p' "$file")
done

printf '<testsuite name="unit" tests="%d" failures="%d">%s</testsuite>\n' "$total" "$failed" "$cases" >"$junit"
echo "$total tests, $failed failed"
[ "$total" -gt 0 ] && [ "$failed" -eq 0 ]
