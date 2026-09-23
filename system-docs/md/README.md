# PS1 SDK reference manuals (converted to Markdown)

These are machine-converted from the PDFs in `system-docs/`, so that tooling and AI agents
can search them. Regenerate with `gen-docs.bat` at the repo root. **Do not hand-edit —
your changes will be overwritten.**

## The one thing to know first: PSn00bSDK is not PsyQ

This project builds against **PSn00bSDK**, an open-source PS1 SDK that is *mostly* API
compatible with Sony's original PsyQ SDK. Mostly is not entirely. Many functions
documented in the two Sony manuals below **do not exist in PSn00bSDK at all**, and some
that do exist differ in detail.

Order of authority, highest first:

1. **`toolchain-win64/include/libpsn00b/`** — the actual headers. If a function is not
   declared here, it does not exist, whatever the manuals say.
2. **`psn00b.md`** — the manual for the SDK actually in use.
3. **`libref.md` / `libover47.md`** — Sony's PsyQ documentation. Excellent for
   understanding the hardware and for per-function detail, but always confirm the symbol
   exists in the headers before you write code against it.

## The files

| File | Source PDF | What it is | Size |
| --- | --- | --- | --- |
| `psn00b.md` | `LibPSn00b Reference.pdf` | PSn00bSDK's own library reference (runtime v0.15b). Covers the CD-ROM, Graphics, Miscellaneous and Serial I/O libraries. | 152 KB |
| `libref.md` | `Libref.pdf` | Sony *Run-Time Library Reference* (Jan 1999). Per-function reference across 19 chapters — kernel, graphics, geometry, CD, sound, controllers, memory card. | 1.2 MB |
| `libover47.md` | `LibOver47.pdf` | Sony *Run-Time Library Overview 4.7*. Conceptual, not per-function — the place to learn how a subsystem actually works. | 627 KB |

## How to search these

They total ~2 MB. **Grep for what you want and read a slice around the hit** — do not read
them end to end.

```sh
# Find a function's entry. Entries start with the bare name on its own line,
# so anchoring to start-of-line skips the hundreds of cross-references.
grep -n "^SetDefDrawEnv$" system-docs/md/libref.md

# Does PSn00bSDK actually have it? Check its own manual, then the headers.
grep -n "^SetDefDrawEnv$" system-docs/md/psn00b.md
grep -rn "SetDefDrawEnv" toolchain-win64/include/libpsn00b/

# Conceptual search - how does a subsystem work?
grep -in "ordering table" system-docs/md/libover47.md
```

A function entry runs roughly 20-40 lines: the name, a one-line description, a metadata
table (library, header, introduced), `Syntax` with the prototype and an argument table,
`Explanation`, `Return value`, and `See also`.

## Known limitations of the conversion

- **Running headers and footers survive** as stray lines (`Run-Time Library Reference`,
  `Basic Graphics Library Functions 7-95`) and, in `psn00b.md`, as junk one-row tables
  containing `LACKING CONFIDENCE`. These are page furniture from the PDFs; ignore them.
- **Figures and diagrams are not extracted.** Where a manual leans on a diagram, open the
  source PDF.
- Most tables convert cleanly, but a long table that spanned a page break can come out
  split into two table blocks with a stray line between them.
