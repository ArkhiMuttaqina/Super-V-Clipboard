# Emoji Dataset Audit (pre-upgrade baseline)

Baseline commit: `4fcc50c`. Reference: Unicode Emoji 18.0 (`emoji-test.txt`,
2026-04-30). Every number below was measured against the shipped dataset with
the parser in `tools/glyph-generator/generate.py` and the assertions now enforced
by `crates/super-v-core/tests/glyph_tests.rs`.

## 1. Original state

| Property | Value |
| --- | --- |
| Entries in `data/glyphs/emojis.json` | **1,914** |
| Unique sequences | 1,914 (no duplicates) |
| Entries that are fully-qualified RGI | 1,914 (100%) |
| Entries not in the official file | 0 |
| Symbols in `symbols.json` | 1,073 (no duplicates) |
| Indonesian dictionary terms | 229 |
| English dictionary terms | 9 |

Group distribution matched the official grouping exactly (Smileys 171, People
388, Animals 160, Food 131, Travel 219, Activities 85, Objects 266, Symbols 224,
Flags 270), so category assignment was not a defect.

## 2. Coverage gaps against Emoji 18.0

Official fully-qualified RGI entries: **3,963**. Missing: **2,049**.

| Group | Missing |
| --- | --- |
| People & Body | **2,042** |
| Smileys & Emotion | 1 |
| Animals & Nature | 1 |
| Food & Drink | 1 |
| Travel & Places | 2 |
| Objects | 2 |

The gap was almost entirely skin tones: the old dataset contained **zero**
sequences with a modifier (`U+1F3FB..U+1F3FF`), while Emoji 18.0 defines **2,040**
toned sequences (415 of them multi-person combinations). The remaining 9 misses
were emoji added in recent releases.

Flags were complete (259 regional-indicator pairs + 3 tag sequences = 262, and
the old file held 270 flag-group entries including the grey/black/rainbow
singles). ZWJ coverage was partial: 249 of the 1,614 official ZWJ sequences.

## 3. Defects found

1. **Skin-tone model was binary.** The only tone information was
   `"t": 1` on 306 glyphs: a "can take a modifier" flag. There was no variant
   data, no base relationship, and no way to represent the five official tones
   or a two-person combination.
2. **The C overlay invented sequences.** `tone_capable()` used hand-written
   code-point ranges and `skin_variant()` inserted a modifier after the *first*
   matching code point. For `man teacher` + dark tone this produced
   `U+1F468 U+1F3EB U+1F3FF` - the modifier landed after the ZWJ-joined object,
   which is not a valid sequence and renders as two glyphs. Multi-person
   combinations were unrepresentable.
3. **No code-point search.** Typing `1f44b` or `U+1F44B` returned nothing; the
   old search only compared lowercase name/keyword substrings.
4. **`search_total` disagreed with `search`.** `search()` fell back to
   any-token matching when an all-token query found nothing; `search_total()`
   did not, so pagination could stop early or claim hits that no page contained.
5. **No ranking.** Results came back in dataset order, so `👋` typed directly
   was buried among every glyph whose keywords contain the character's name.
6. **Keywords were curated only.** 1,914 hand-written keyword strings, no CLDR
   names, no per-language separation: Indonesian words lived inside the English
   blob, and any glyph added later would have had no keywords at all.
7. **No provenance.** Nothing recorded which Unicode version the JSON came from,
   so drift was undetectable and there was no way to regenerate or verify it.

## 4. After the upgrade

| Property | Before | After |
| --- | --- | --- |
| Dataset entries | 1,914 | **3,963** (all fully-qualified RGI) |
| Bases shown in the grid | 1,914 | 1,923 |
| Skin-tone variants in data | 0 | **2,040** |
| Base glyphs with tone variants | 306 flagged | **332** with exact variants |
| Multi-person tone combinations | 0 | **415** |
| ZWJ sequences | 249 | **1,614** (= official) |
| Flags | 259 pairs | **262** (259 pairs + 3 tag) |
| Duplicate sequences | 0 | 0 |
| Version pinning | none | Unicode 18.0 + CLDR 46, SHA-256 per source |
| Code-point search | unsupported | hash lookup, 0.4 us |
| Tone/gender query support | none | dataset-driven, en + id |
| Language separation | mixed blob | `k` (en+curated) / `ki` (id), gated by setting |
| Automated coverage tests | none | 9 contract tests vs official files |

See `docs/GLYPHS.md` for the schema, ranking rules, validation matrix and
benchmarks, and `docs/glyph-coverage.md` for the generated coverage report.
