# Emoji & Symbol Glyph Engine

How Super-V Clipboard stores, indexes, searches, and renders the emoji picker.
Everything here is generated or verified by code in `tools/glyph-generator/` and
`crates/super-v-core/tests/glyph_tests.rs`; nothing is a hand-maintained list.

## 1. Dataset version and provenance

| Item | Value |
| --- | --- |
| Unicode Emoji version | **18.0** (files dated 2026-04-30) |
| CLDR annotations | **46** (`cldr-json` tag `release-46`) |
| Emoji entries | **3,963** (every fully-qualified RGI sequence) |
| Base glyphs | **1,923** (no skin-tone modifier) |
| Skin-tone variants | **2,040** (1,800 linked to a base, 240 multi-person only) |
| Multi-person tone combinations | **415** |
| ZWJ sequences | **1,614** (matches `emoji-zwj-sequences.txt` exactly) |
| Flag sequences | **262** (259 regional-indicator pairs + 3 tag sequences) |
| Symbols (separate tab) | **1,073** |

Pinned inputs live in `tools/glyph-generator/sources/` and are committed:

- `emoji-test.txt` - the qualification lists (fully-qualified / minimally-qualified / unqualified / component)
- `emoji-sequences.txt`, `emoji-zwj-sequences.txt` - RGI sequence lists
- `emoji-data.txt` - `Emoji_Modifier`, `Emoji_Modifier_Base`, and friends
- `emoji-variation-sequences.txt` - standardized variation sequences
- `cldr-annotations-en.json`, `cldr-annotations-id.json` - CLDR 46 keyword annotations

`data/glyphs/glyph-meta.json` records the version of each input plus its SHA-256
digest, so a dataset can always be traced back to exact source bytes.

## 2. What is in the dataset

Only **fully-qualified RGI** sequences are included. Minimally-qualified and
unqualified forms are dropped (they render differently across fonts), and the 9
standalone components (skin-tone modifiers, hair components, ZWJ) are excluded
because they are not emoji on their own. `data/glyphs/emojis.json` is therefore
set-equal to the fully-qualified entries of the pinned `emoji-test.txt` - the
test suite asserts this both ways.

## 3. JSON schema

The dataset is an array of compact objects. Keys are short because the file is
embedded in the binary with `include_str!`.

| Key | Meaning |
| --- | --- |
| `c` | The exact character sequence |
| `n` | Unicode/CLDR name; empty on a variant that derives its name |
| `g` | Group index (0-8, pinned legacy order: Smileys ... Flags) |
| `sg` | Subgroup index into `glyph-meta.json` (`hand-fingers-open`, ...) |
| `k` | English + curated keywords; on a variant, only words not inherited |
| `ki` | Indonesian keywords; matched only when `id` is an active language |
| `v` | Base glyph this sequence is a tone variant of |
| `vt` | Variant kind: `tone`, `gender`, `family`, `couple`, `flag`, `zwj` |
| `tn` | Skin tones in sequence order, 1-5 (e.g. `[4,2]`) |
| `t` | Legacy flag: 1 when the glyph owns tone variants |

Derived at load time, never stored: the uppercase code-point string used for
`1F44B` style lookups, and the resolved name/keyword text of variants.

Key derivation rule: a variant stores a name only when it is *not* exactly
`<base name>: <tone names in sequence order>`, and stores keywords only when it
adds words beyond the base keywords plus the tone vocabulary. That keeps the
file at ~470 KB instead of duplicating ~2,000 name/keyword strings.

Compatibility: the four original keys (`c`, `n`, `g`, `k`) keep their old meaning,
so a consumer that ignores unknown keys keeps working; `t` still marks a glyph
that can show tone variants. Everything else (`sg`, `ki`, `v`, `vt`, `tn`) is
optional and defaults to empty when absent, which is what every symbol entry does.

## 4. Skin-tone model

Unicode defines five modifiers: `U+1F3FB` light, `U+1F3FC` medium-light,
`U+1F3FD` medium, `U+1F3FE` medium-dark, `U+1F3FF` dark.

- A variant is stored exactly as Unicode defines it, including the position of
each modifier. Multi-person sequences carry two or more (e.g. two people holding
hands with different tones), which is why the old code-point-range heuristic in
C produced broken sequences and was removed.
- `v` links a variant to its base only when the base is itself RGI. For bases
that are only valid with an emoji presentation selector (`U+1F590 FE0F`,
`U+1F575 FE0F`, ...) the generator tries inserting `U+FE0F` after each
`Emoji_Modifier_Base` code point and links the first candidate that exists.
- 240 toned sequences (couples, families, some gender combinations) have no
modifier-free RGI form. They are first-class entries: they carry their own name
and keywords, and they appear in search but not as variants of another glyph.
- The picker asks Rust for the variant list; C never composes a sequence.

## 5. Search

`GlyphIndex::search(tab, query, langs, offset, limit)` ranks results in a stable
order and pages within the ranked list, so `search_total` always agrees with the
pages.

Ranking:

1. exact glyph match (`👋`)
2. direct code points (`1f44b`, `U+1F44B`, `0x1F44B`, space-separated sequences)
3. exact name
4. name prefix
5. name substring
6. keyword substring
7. all query tokens match (with `i18n.json` expansions)

Behaviour notes:

- Matching is case-insensitive and Unicode-safe; the query is lowercased, the
dataset is stored lowercase.
- Shortcodes work: `:waving hand:` and `:wave:` normalize by trimming colons and
underscores are treated as word separators.
- The Indonesian dictionary (`data/glyphs/i18n.json`) still expands everyday
words (`sedih` -> `sad`, `cry`, ...). Indonesian *keywords* come from CLDR 46 and
only participate when `id` is in the language list, preserving the pre-existing
language switch behaviour.
- Browsing (empty query) shows base glyphs only: 1,923 cells instead of 3,963, so
paging and widget creation stay as cheap as before.
- Tone queries (`light skin tone`, `warna kulit gelap`, ...) pull variants into the
result set; without a tone word, variants are hidden and their base is shown, so
`wave` returns one hand, not six.
- Direct code-point search uses a hash lookup: ~0.4 us instead of a full scan.

## 6. Regenerating the dataset

```sh
python3 tools/glyph-generator/generate.py           # rewrite the JSON + report
python3 tools/glyph-generator/generate.py --check   # fail if committed data is stale
```

The generator is deterministic: the same sources produce byte-identical output
(no timestamps, sorted metadata, pinned group order). To move to a new Unicode
release, replace the files in `tools/glyph-generator/sources/`, re-run the
generator, and commit the regenerated JSON together with the new sources.

`tools/glyph-generator/curated-keywords.json` holds the curated keyword
overrides keyed by sequence; it is merged with the CLDR annotations for English.

## 7. Validation

```sh
cargo test --workspace                              # includes the glyph contract tests
cargo test -p super-v-core --test glyph_tests
cargo run --release -p super-v-core --bin bench_glyphs
```

`glyph_tests.rs` embeds the pinned Unicode files and checks:

- the dataset equals the fully-qualified RGI set of the pinned `emoji-test.txt`,
  with no duplicates and no extra sequences
- every toned entry is a fully-qualified sequence, its tone list matches the
  modifiers in the sequence, and each modifier follows an `Emoji_Modifier_Base`
- every linked variant strips back to its base (allowing the single `U+FE0F`
  difference described above) and inherits the base keywords
- ZWJ and flag coverage equal the counts computed from the official files
- tone search returns variants, plain search does not, Indonesian tone words hit
- pagination is stable, complete, and duplicate-free; `search_total` matches
- existing Indonesian/English search and the symbol tab are unchanged

The UI has a live end-to-end check as well:

```sh
SUPER_V_UI_SELFTEST=1 ./target/release/super-v-ui
```

It prints `VERIFY: variant_count=5` and
`VERIFY: variant_codepoints=U+1F468 U+1F3FB U+200D U+1F3EB` for the man-teacher
glyph, proving the exact RGI sequence crosses the Rust/C boundary intact.

## 8. Performance

Measured with `bench_glyphs` on this machine (release build), compared with the
same benchmark built against the previous 1,914-entry dataset and the previous
search implementation:

| Metric | Before | After |
| --- | --- | --- |
| Embedded emoji + meta + symbols | 195 KB | 543 KB |
| Index build (`load_embedded`) | 1.3 ms | 8-10 ms (background thread at startup) |
| RSS after index build | 0.9 MB | 3.9 MB |
| Browse page (`search(..., 0, 120)`) | 0.1 us | 0.1 us |
| English name query | 94 us | 202 us |
| Indonesian dictionary query (`sedih`) | 594 us | 800 us |
| Tone query (`light skin tone`) | 553 us (28 guessed hits) | 300 us (120 exact hits) |
| Code-point query (`1f44b`) | 130 us (not supported) | 0.4 us |
| ZWJ name query (`family`) | n/a | 218 us |

Search cost grew sub-linearly with dataset size: 2.07x more sequences for
<=1.4x per-query time, because browsing works on a precomputed index list and
code-point lookups are hashed. The 8-10 ms index build runs once on a background
thread when the UI starts, so opening the emoji tab never blocks the GTK main
thread.

## 9. Licensing and attribution

The Unicode data files and the CLDR annotations are copyright Unicode, Inc. and
distributed under the [Unicode License](https://www.unicode.org/license.txt).
They are vendored unmodified in `tools/glyph-generator/sources/` together with
their SHA-256 digests. The generated `data/glyphs/emojis.json` and
`glyph-meta.json` are derivative data under the same terms.

## 10. Known limitations

- CLDR 46 predates some Emoji 17/18 sequences, so ~35% of base glyphs have no
Indonesian annotation yet. They still carry English names and curated keywords;
regenerating against a newer CLDR fills them in.
- Emoji rendering depends on the system colour-emoji font (Noto Color Emoji).
The app does not bundle fonts or SVG glyphs. Ubuntu ships 2.051, which covers
Emoji 15.1, so the 9 base sequences introduced later (U+1F6D9, U+1FA8B,
U+1FA8C, U+1FA8D, U+1FACC, U+1FADD, U+1FAEB, U+1FAF9, U+1FAFA) have no glyph in
it. The picker probes the emoji font once at startup and draws a dimmed
broken-image placeholder for those cells instead of Pango's hex tofu; they stay
searchable and paste the exact sequence, so a client with a newer font renders
them correctly. Installing a newer colour font and restarting the UI replaces
the placeholders automatically.
- The dataset intentionally excludes unqualified and minimally-qualified forms,
standalone components, and non-RGI combinations. A sequence missing from the
picker is missing for the same reason.
- Tag-sequence flags (England, Scotland, Wales) rely on font support; they are
in the dataset and searchable either way.
