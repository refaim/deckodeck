# Task 25 — a short README; move the developer content to docs/BUILD.md

Repository: `C:\Users\Roma\Dev\PictureView3\pvdkit` (product deckodeck, `https://github.com/refaim/deckodeck`).
HEAD `6b47984`, tree clean. **No git state changes** (no commit/stage/reset/checkout/push); never ask
the user to run a command. Documentation-only task; no C++ changes.

Roma's requirement, verbatim: "ридми у проекта гигантское, надо простое и короткое: что это, ссылки
на скачивание, лицензия. всё". The current `README.md` is 318 lines (build, packaging, CI, layout,
tests). Model the tone and length on his other project's README: `C:\Users\Roma\Dev\burlak\README.md`
(read it) — plain prose, no wall of headings.

## New `README.md` (everything the user needs, nothing more)

1. Title `# deckodeck` and two or three sentences: decoder plugins for PictureView 3, the picture
   viewer plugin for Far Manager 3 (by Pavel Skakov); today `AVIF.pvd` (AVIF photos from phones and
   the web, animation as pages, 10/12-bit and HDR) and `RPGMVP.pvd` (RPG Maker MV/MZ encrypted PNG,
   `.rpgmvp`/`.png_`, no key needed); x64 and x86 Far; each plugin is one self-contained `.pvd`
   (no runtime, no codecs, only Windows). Take the wording from `plugins/*/package/readme_en.txt`
   and the plugin READMEs; do not invent claims.
2. `## Download` — keep the existing table **byte-for-byte inside the anchors**
   `<!-- downloads:begin -->` … `<!-- downloads:end -->` (`scripts/update-readme-downloads.ps1`
   and `release.yml` rewrite the rows between them; verify with a dry run on a copy that the
   script still says `unchanged` for AVIF 1.1.0 and `updated` for a bumped version). One sentence
   before it: each release has a zip per architecture, x64 for 64-bit Far and x86 for 32-bit,
   SHA-256 in the release notes. Drop the seed comment about tags not yet existing only if the
   orchestrator says the tags are pushed — they are NOT yet, so keep that HTML comment (it is
   invisible on GitHub).
3. `## Install` — three lines like the readmes: unpack the archive into PictureView's folder (the
   one with `0PictureView.dll`, usually `Plugins\PictureView` in your Far), restart Far.
4. `## Licence` — MIT (`LICENSE`); the bundled codec libraries keep their own licences, listed in
   each archive's `LICENSES.txt`; the PictureView plugin interface header is © Pavel Skakov.
5. One closing line: "Building, packaging and CI: [docs/BUILD.md](docs/BUILD.md)."
Target: about 30–40 lines. No "Layout", no "Tests", no toolchain talk, no explanation of the
pvdkit/deckodeck naming (that moves to BUILD.md).

## `docs/BUILD.md`

Move the removed sections there unchanged in substance (Build incl. the 32-bit build, Packaging,
Continuous integration and releases, Layout, Tests, and the paragraph explaining that the shared
layer is still called pvdkit). Fix every reference that pointed at README sections: `AGENTS.md`
(lines mentioning README.md, e.g. "README.md explains the split", the layout table row), `docs/ARCHITECTURE.md`
(same), `plugins/avif/README.md` ("see the top-level README.md for the build"), `plugins/rpgmvp/README.md`,
comments in `scripts/*.ps1`, `.github/workflows/*.yml` and `.github/actions/toolchain/action.yml`
that cite README sections — grep for `README` across the tree (excluding `docs/tasks/`,
`third_party/`, `build/`) and update each hit that meant the developer content. The Downloads-table
mechanics (script + workflow) keep pointing at `README.md`.

## Checks
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/lint.ps1 -Tools psscriptanalyzer` clean
  (only if you touched a script comment; otherwise skip).
- The update-readme dry run described above.
- `cmake --preset debug` with `PVDKIT_BUILD_SUFFIX=-t25` is NOT needed — no code changed. Do not build.
- Markdown: LF line endings like the current README (check with a byte count of CR), UTF-8, no BOM.
- Read the final README top to bottom once as a user would.

## Report
`docs/tasks/report-task25.md`: the new README in full, the list of moved sections, every reference
you fixed (file:line), the dry-run output. Final message: a short summary.
