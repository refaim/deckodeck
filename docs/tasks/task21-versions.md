Small release chore for the `pvdkit` repository (current directory C:\Users\Roma\Dev\PictureView3\pvdkit).
NOTE: another Codex session (Task 20, performance of `src/core/colour` + `src/core/FileSession.cpp` +
`tests/core/colour` + `plugins/avif/tests/e2e/E2eTests.cpp`) is working in the same tree right now —
do NOT touch those paths and do NOT run any cmake/ctest/lint (it owns the build machine); expect
`git status` to show its files and leave them alone. Your change is text-only. Read `AGENTS.md`
(never commit). `pwsh` not on PATH.

Roma's decision: **1.0.0 of both plugins was released; everything since (1.0.1, 1.1.0, 1.2.0)
was NOT — it all becomes ONE new version, 1.1.0, dated 14.09.2026** (the performance work of
Task 20 lands in the same 1.1.0). Concretely:
1. `plugins/avif/CMakeLists.txt`: `VERSION 1.1.0` (currently 1.2.0). `plugins/rpgmvp/CMakeLists.txt`:
   stays `VERSION 1.1.0`.
2. Version assertions: `plugins/avif/tests/adapters/DefaultPluginTests.cpp` and any other AVIF
   version literal `1.2.0` → `1.1.0` (`git grep -n '1\.2\.0' -- ':!docs/tasks' ':!docs/host'`;
   `plugins/avif/tests/e2e/E2eTests.cpp` belongs to Task 20 — if it holds a `"1.2.0"` literal,
   leave it and SAY SO in the report so the orchestrator fixes it after Task 20 lands).
3. ChangeLogs — UTF-8 BOM + CRLF, edit byte-exactly (PowerShell `[IO.File]::ReadAllBytes/WriteAllBytes`;
   verify afterwards: BOM present, count(LF) == count(CR), no lone LF). Each file gets exactly TWO
   entries: the new `1.1.0 14.09.2026` merging everything since 1.0.0, and the original `1.0.0`
   entry unchanged. First line exactly `AVIF 1.1.0 14.09.2026` / `RPGMVP 1.1.0 14.09.2026`, dash
   line of the same length, Roma-style plain Russian bullets (` + ` features, ` * ` fixes), wrapped
   at ~76 columns like the existing entries:
   AVIF 1.1.0: ` * прозрачность теперь показывается: в 1.0.0 прозрачные места были непрозрачными`;
   ` + картинки глубже 8 бит отдаются PictureView как 16-битные ...` (keep the existing text);
   ` + HDR-картинки PQ и HLG ... тон-маппингом BT.2390 ... Rec.2020 и P3 переводятся в sRGB`;
   ` + фотографии с EXIF-ориентацией (без irot/imir) ... поворот делает PictureView`.
   RPGMVP 1.1.0: ` * прозрачность теперь показывается: в 1.0.0 прозрачные места были чёрными`;
   ` + 16-битные PNG отдаются PictureView как 16-битные, а не ужимаются до 8`.
   Drop the 1.0.1/1.2.0 entries (their content is merged above).
4. `plugins/avif/README.md` and `plugins/rpgmvp/README.md` "Changes" sections: two lines each —
   `1.1.0 — ...` (merged English summary) and `1.0.0 — initial release.`
5. `readme_en.txt`/`readme_ru.txt` do not mention versions — check and leave.
Verification: the `git grep` above; the byte checks on both ChangeLogs; `git diff --stat`. Do NOT
build. Write `docs/tasks/report-task21.md` (short) and print it as your final message. Never commit.
