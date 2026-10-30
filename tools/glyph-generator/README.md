# Glyph dataset generator

Deterministic builder for `data/glyphs/emojis.json`, `data/glyphs/glyph-meta.json`
and `docs/glyph-coverage.md` from pinned official Unicode and CLDR data.

```sh
python3 tools/glyph-generator/generate.py            # regenerate
python3 tools/glyph-generator/generate.py --check    # CI check: fail if stale
```

Python 3 standard library only; no third-party packages, no network access at
run time.

## Inputs (`sources/`, committed)

| File | Origin |
| --- | --- |
| `emoji-test.txt` | https://unicode.org/Public/emoji/latest/emoji-test.txt |
| `emoji-sequences.txt` | https://unicode.org/Public/emoji/latest/emoji-sequences.txt |
| `emoji-zwj-sequences.txt` | https://unicode.org/Public/emoji/latest/emoji-zwj-sequences.txt |
| `emoji-data.txt` | https://unicode.org/Public/UCD/latest/ucd/emoji/emoji-data.txt |
| `emoji-variation-sequences.txt` | https://unicode.org/Public/UCD/latest/ucd/emoji/emoji-variation-sequences.txt |
| `cldr-annotations-en.json` | `cldr-json` tag `release-46`, `cldr-annotations-full/annotations/en` |
| `cldr-annotations-id.json` | `cldr-json` tag `release-46`, `cldr-annotations-full/annotations/id` |
| `curated-keywords.json` | hand-maintained overrides, keyed by sequence |

Current pin: **Unicode Emoji 18.0**, **CLDR 46**.

## What it guarantees

- Every output entry is a fully-qualified RGI sequence from the pinned
  `emoji-test.txt`; the generator dies on a missing or extra sequence.
- No duplicate sequences, no standalone components.
- Skin-tone metadata matches the modifiers present in the sequence, and every
  modifier follows a code point listed as `Emoji_Modifier_Base`.
- A tone variant links to its base when the base is RGI, trying `U+FE0F`
  insertion when the base requires emoji presentation.
- ZWJ and flag coverage are cross-checked against the counts computed from the
  RGI files; a mismatch aborts the run.
- Identical inputs produce byte-identical outputs.

## Updating to a new Unicode release

1. Replace the five `.txt` files in `sources/` with the new release.
2. Optionally refresh the CLDR annotation files (newer `cldr-json` tag).
3. `python3 tools/glyph-generator/generate.py`
4. `cargo test -p super-v-core --test glyph_tests` - the embedded copies in the
   test fall back to the same `sources/` files, so this verifies the new pin.
5. Commit sources, JSON and the regenerated `docs/glyph-coverage.md` together.

Licensing: Unicode data and CLDR annotations are © Unicode, Inc., distributed
under the [Unicode License](https://www.unicode.org/license.txt).
