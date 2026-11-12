# Golden report CSVs

These files are byte-exact fixtures for `tests/host/reports_test.cpp`.

- `*_excel.csv` start with a UTF-8 BOM (`EF BB BF`), contain the `sep=;` hint
  and use CRLF line endings.
- `*_raw.csv` are UTF-8 without BOM and use LF.
- `.gitattributes` marks the directory `-text` so Git never normalizes them.
  `.editorconfig` keeps editors from rewriting BOM/line endings.
- Do not edit them by hand. Regenerate after an intentional output change:

```sh
cmake -S tests/host -B tests/host/build
cmake --build tests/host/build -j
UPDATE_GOLDEN=1 ./tests/host/build/kaffee_tests
```

Then review `git diff -- tests/host/golden/reports` before committing.
