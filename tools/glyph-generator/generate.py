#!/usr/bin/env python3
"""Deterministic generator for Super-V Clipboard's bundled emoji dataset.

Inputs are pinned copies of the official Unicode emoji data files plus the CLDR
annotations for the shipped languages, all under tools/glyph-generator/sources/.
Nothing is fetched at build time or runtime: the generated JSON is embedded in
the binary via include_str!.

Outputs
  data/glyphs/emojis.json        compact emoji dataset (all fully-qualified RGI)
  data/glyphs/glyph-meta.json    dataset metadata: versions, groups, subgroups,
                                 tone tables, counts, source digests
  docs/glyph-coverage.md         human-readable coverage report

Usage
  python3 tools/glyph-generator/generate.py            # regenerate
  python3 tools/glyph-generator/generate.py --check    # verify committed data

The generator is deterministic: identical sources produce byte-identical output.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
from collections import Counter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC_DIR = os.path.join(ROOT, "tools", "glyph-generator", "sources")
OUT_EMOJI = os.path.join(ROOT, "data", "glyphs", "emojis.json")
OUT_META = os.path.join(ROOT, "data", "glyphs", "glyph-meta.json")
OUT_REPORT = os.path.join(ROOT, "docs", "glyph-coverage.md")
CURATED = os.path.join(ROOT, "tools", "glyph-generator", "curated-keywords.json")

# Group order is pinned: it is the legacy order used by data/glyphs/emojis.json
# and therefore by the shipped UI. Never reorder.
GROUPS = [
    "Smileys & Emotion",
    "People & Body",
    "Animals & Nature",
    "Food & Drink",
    "Travel & Places",
    "Activities",
    "Objects",
    "Symbols",
    "Flags",
]
GROUP_INDEX = {name: i for i, name in enumerate(GROUPS)}

TONE_NAMES = {
    1: "light skin tone",
    2: "medium-light skin tone",
    3: "medium skin tone",
    4: "medium-dark skin tone",
    5: "dark skin tone",
}
MODIFIER_BASE = 0x1F3FA  # 1F3FB..1F3FF -> tones 1..5

ZWJ = 0x200D
GENDER_SIGNS = {0x2640, 0x2642}
PEOPLE = set(range(0x1F464, 0x1F46D)) | set(range(0x1F471, 0x1F47D)) | {
    0x1F93C, 0x1F9D1, 0x1F9D2, 0x1F9D3, 0x1F9D4, 0x1F9D5, 0x1F9D6,
    0x1F9D7, 0x1F9D8, 0x1F9D9, 0x1F9DA, 0x1F9DB, 0x1F9DC, 0x1F9DD,
    0x1F9DE, 0x1F9DF, 0x1FAC3, 0x1FAC4, 0x1FAC5, 0x1FAF1, 0x1FAF2,
    0x1FAF3, 0x1FAF4, 0x1FAF5, 0x1FAF6, 0x1FAF7, 0x1FAF8,
}
HEART = {0x2764, 0x1F48B, 0x1F5E8}
COUPLE_MARK = {0x1F91D}

NAME_RE = re.compile(r"^(\S+)\s+E([0-9.]+)\s+(.+)$")

PUNCT = {'"', "'", "(", ")", "[", "]", "{", "}", ",", ".", ";", ":", "!", "?"}


def die(msg):
    print("error: " + msg, file=sys.stderr)
    raise SystemExit(1)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def norm_word(word):
    w = word.strip().lower()
    while w and w[0] in PUNCT:
        w = w[1:]
    while w and w[-1] in PUNCT:
        w = w[:-1]
    return w


def add_words(bucket, seen, text):
    for raw in text.split():
        w = norm_word(raw)
        if w and w not in seen:
            seen.add(w)
            bucket.append(w)


def parse_emoji_test(path):
    version = None
    entries = []
    group = subgroup = None
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if line.startswith("# Version:"):
                version = line.split(":", 1)[1].strip()
            elif line.startswith("# group:"):
                group = line.split(":", 1)[1].strip()
            elif line.startswith("# subgroup:"):
                subgroup = line.split(":", 1)[1].strip()
            elif not line.strip() or line.lstrip().startswith("#"):
                continue
            else:
                cps, rest = line.split(";", 1)
                status, comment = rest.split("#", 1)
                codepoints = [int(c, 16) for c in cps.split()]
                match = NAME_RE.match(comment.strip())
                if not match:
                    die("unparsable emoji-test line: %r" % (line,))
                entries.append(
                    {
                        "chars": "".join(chr(cp) for cp in codepoints),
                        "codepoints": codepoints,
                        "name": match.group(3).strip(),
                        "group": group,
                        "subgroup": subgroup,
                        "qualification": status.strip(),
                        "since": "E" + match.group(2),
                    }
                )
    if version is None:
        die("emoji-test.txt has no version header")
    for e in entries:
        if e["qualification"] == "fully-qualified" and e["group"] not in GROUP_INDEX:
            die("unknown group in emoji-test.txt: %r" % (e["group"],))
    return version, entries


def parse_rgi_file(path):
    """emoji-sequences.txt / emoji-zwj-sequences.txt.

    Row format: <codepoints> ; <type_field> ; <name> # <version> [n] (<chars>)
    Ranges ("231A..231B") expand to one row per code point.
    """
    version = None
    rows = []
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                if line.startswith("# Version:"):
                    version = line.split(":", 1)[1].strip()
                continue
            fields = [f.strip() for f in line.split(";")]
            if len(fields) < 3:
                die("unparsable RGI line: %r" % (line,))
            cps = fields[0]
            type_field = fields[1]
            name = fields[2].split("#", 1)[0].strip()
            parts = cps.split()
            if any(".." in part for part in parts):
                for part in parts:
                    bounds = part.split("..")
                    lo = int(bounds[0], 16)
                    hi = int(bounds[-1], 16)
                    for cp in range(lo, hi + 1):
                        rows.append({"codepoints": [cp], "type": type_field, "name": name})
            else:
                rows.append(
                    {"codepoints": [int(c, 16) for c in parts], "type": type_field, "name": name}
                )
    return version, rows


MODIFIER_BASES = set()


def parse_emoji_data(path):
    version = None
    props = Counter()
    modifier_chars = set()
    modifier_bases = set()
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                if line.startswith("# Version:"):
                    version = line.split(":", 1)[1].strip()
                continue
            rng, prop = line.split(";", 1)
            prop = prop.split("#", 1)[0].strip()
            if not prop:
                continue
            bounds = rng.split("..")
            lo = int(bounds[0], 16)
            hi = int(bounds[-1], 16)
            props[prop] += hi - lo + 1
            if prop == "Emoji_Modifier":
                modifier_chars.update(range(lo, hi + 1))
            elif prop == "Emoji_Modifier_Base":
                modifier_bases.update(range(lo, hi + 1))
    return version, props, modifier_chars, modifier_bases


def parse_variation_sequences(path):
    version = None
    count = 0
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                if line.startswith("# Version:"):
                    version = line.split(":", 1)[1].strip()
                continue
            # Every row of emoji-variation-sequences.txt is standardized; the
            # second field only distinguishes text style from emoji style.
            line.split(";", 1)
            count += 1
    return version, count


def load_cldr(lang):
    path = os.path.join(SRC_DIR, "cldr-annotations-%s.json" % lang)
    with open(path, encoding="utf-8") as fh:
        payload = json.load(fh)
    return payload["annotations"]["annotations"]


def tones_of(codepoints):
    return [cp - MODIFIER_BASE for cp in codepoints if MODIFIER_BASE < cp <= 0x1F3FF]


def strip_modifiers(codepoints):
    return [cp for cp in codepoints if not (MODIFIER_BASE < cp <= 0x1F3FF)]


def find_base(base_chars, base_cps, fq_by_char):
    """Map a toned sequence back to its RGI base.

    Stripping the modifiers is not always enough: several bases are only RGI
    with an emoji presentation selector (U+1F590 FE0F, U+1F575 FE0F, ...) that
    the toned sequence omits. Try the bare form first, then every single
    insertion of U+FE0F, in code-point order, so the result is deterministic.
    """
    if base_chars in fq_by_char:
        return base_chars
    for position in range(len(base_cps)):
        if base_cps[position] not in MODIFIER_BASES:
            continue
        candidate = (
            "".join(chr(cp) for cp in base_cps[: position + 1])
            + "\uFE0F"
            + "".join(chr(cp) for cp in base_cps[position + 1 :])
        )
        if candidate in fq_by_char:
            return candidate
    return None


def classify_variant(codepoints, tones):
    if tones:
        return "tone"
    if any(0x1F1E6 <= cp <= 0x1F1FF for cp in codepoints):
        return "flag"
    if any(0xE0020 <= cp <= 0xE007F for cp in codepoints):
        return "flag"
    if GENDER_SIGNS & set(codepoints):
        return "gender"
    if ZWJ in codepoints:
        if HEART & set(codepoints):
            return "couple"
        if COUPLE_MARK & set(codepoints):
            return "couple"
        people = sum(1 for cp in codepoints if cp in PEOPLE)
        if people >= 2:
            return "family"
        return "zwj"
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="fail if committed data is stale")
    args = ap.parse_args()

    emoji_test = os.path.join(SRC_DIR, "emoji-test.txt")
    seq_file = os.path.join(SRC_DIR, "emoji-sequences.txt")
    zwj_file = os.path.join(SRC_DIR, "emoji-zwj-sequences.txt")
    data_file = os.path.join(SRC_DIR, "emoji-data.txt")
    var_file = os.path.join(SRC_DIR, "emoji-variation-sequences.txt")

    test_version, entries = parse_emoji_test(emoji_test)
    seq_version, rgi_rows = parse_rgi_file(seq_file)
    zwj_version, zwj_rows = parse_rgi_file(zwj_file)
    data_version, props, modifiers, modifier_bases = parse_emoji_data(data_file)
    MODIFIER_BASES.update(modifier_bases)
    var_version, var_count = parse_variation_sequences(var_file)

    if len({test_version, seq_version, zwj_version}) != 1:
        die(
            "mixed Unicode emoji versions: test=%s sequences=%s zwj=%s"
            % (test_version, seq_version, zwj_version)
        )
    if len(modifiers) != 5:
        die("expected 5 Emoji_Modifier codepoints, got %d" % len(modifiers))
    if not MODIFIER_BASES:
        die("emoji-data.txt has no Emoji_Modifier_Base property")

    fully = [e for e in entries if e["qualification"] == "fully-qualified"]
    components = [e for e in entries if e["qualification"] == "component"]
    fq_chars = {e["chars"] for e in fully}
    fq_by_char = {e["chars"]: e for e in fully}

    ann_en = load_cldr("en")
    ann_id = load_cldr("id")
    with open(CURATED, encoding="utf-8") as fh:
        curated = json.load(fh)

    tone_en = {t: TONE_NAMES[t] for t in range(1, 6)}
    tone_id = {}
    for t in range(1, 6):
        ch = chr(MODIFIER_BASE + t)
        ann = ann_id.get(ch, {})
        words = ann.get("tts") or ann.get("default") or []
        tone_id[t] = words[0] if words else TONE_NAMES[t]

    subgroups = []
    subgroup_index = {}
    for e in fully:
        key = (e["group"], e["subgroup"])
        if key not in subgroup_index:
            subgroup_index[key] = len(subgroups)
            subgroups.append(
                {"id": len(subgroups), "g": GROUP_INDEX[e["group"]], "name": e["subgroup"]}
            )

    def keywords_for(chars, tone_list, lang):
        """Searchable words for one language. English folds in the curated
        overrides; Indonesian stays separate so the picker's language setting
        still decides whether Indonesian words match (the pre-existing
        contract)."""
        bucket, seen = [], set()
        ann = ann_en.get(chars) if lang == "en" else ann_id.get(chars)
        if lang == "en":
            add_words(bucket, seen, curated.get(chars, ""))
        if ann:
            for w in ann.get("default", []):
                add_words(bucket, seen, w)
            for w in ann.get("tts", []):
                add_words(bucket, seen, w)
        for t in tone_list:
            add_words(bucket, seen, tone_en[t] if lang == "en" else tone_id[t])
        return bucket

    dataset = []
    derived_kw = 0
    derived_name = 0
    for e in fully:
        chars = e["chars"]
        cps = e["codepoints"]
        tones = tones_of(cps)
        base_cps = strip_modifiers(cps)
        base_chars = "".join(chr(cp) for cp in base_cps)
        record = {
            "c": chars,
            "g": GROUP_INDEX[e["group"]],
            "sg": subgroup_index[(e["group"], e["subgroup"])],
        }

        variant_of = None
        if tones and base_chars != chars:
            variant_of = find_base(base_chars, base_cps, fq_by_char)

        if variant_of:
            base_rec = fq_by_char[variant_of]
            expect_name = base_rec["name"] + ": " + ", ".join(tone_en[t] for t in tones)
            if e["name"] == expect_name:
                derived_name += 1
                record["n"] = ""
            else:
                record["n"] = e["name"]
            for field, lang in (("k", "en"), ("ki", "id")):
                inherited = set(keywords_for(variant_of, [], lang))
                for t in tones:
                    phrase = tone_en[t] if lang == "en" else tone_id[t]
                    inherited.update(phrase.split())
                extras = []
                for word in keywords_for(chars, tones, lang):
                    if word not in inherited:
                        inherited.add(word)
                        extras.append(word)
                record[field] = " ".join(extras)
            if not record["k"]:
                derived_kw += 1
            record["v"] = variant_of
            record["vt"] = "tone"
            record["tn"] = tones
        else:
            record["n"] = e["name"]
            record["k"] = " ".join(keywords_for(chars, tones, "en"))
            record["ki"] = " ".join(keywords_for(chars, tones, "id"))
            if not record["k"]:
                # Guarantee at least the Unicode name words are searchable,
                # for sequences newer than the pinned CLDR release.
                record["k"] = " ".join(norm_word(w) for w in e["name"].split() if norm_word(w))
            vt = classify_variant(cps, tones)
            if vt:
                record["vt"] = vt
            if tones:
                record["tn"] = tones

        dataset.append(record)

    # Empty keyword fields are omitted: absent means "inherited from the base
    # glyph plus the tone vocabulary" to the Rust loader.
    for record in dataset:
        for field in ("k", "ki"):
            if record.get(field) == "":
                del record[field]

    # Legacy tone flag: 1 when the glyph owns tone variants. The UI uses it to
    # decide whether a cell can open the skin-tone popover; Rust recomputes it
    # from the variant table, so the JSON value is informational.
    variant_bases = Counter(r["v"] for r in dataset if r.get("v"))
    for r in dataset:
        if r["c"] in variant_bases:
            r["t"] = 1

    # ---- validation -----------------------------------------------------
    seen = set()
    for r in dataset:
        if r["c"] in seen:
            die("duplicate sequence in dataset: %r" % (r["c"],))
        seen.add(r["c"])
        if not r["c"]:
            die("empty sequence")
    missing = fq_chars - seen
    extra = seen - fq_chars
    if missing:
        die("%d fully-qualified sequences missing, e.g. %r" % (len(missing), sorted(missing)[:3]))
    if extra:
        die("%d sequences are not fully-qualified RGI, e.g. %r" % (len(extra), sorted(extra)[:3]))
    modifier_bases = MODIFIER_BASES
    for r in dataset:
        cps = [ord(ch) for ch in r["c"]]
        mods = tones_of(cps)
        if len(mods) != len(r.get("tn", [])):
            die("tone metadata mismatch: %r" % (r["c"],))
        for i, cp in enumerate(cps):
            if MODIFIER_BASE < cp <= 0x1F3FF:
                if i == 0 or cps[i - 1] not in modifier_bases:
                    die("modifier not attached to a modifier base: %r" % (r["c"],))
        if r.get("v") and r["v"] not in fq_by_char:
            die("variant base %r is not an RGI sequence" % (r["v"],))
        if r.get("tn") and not (1 <= min(r["tn"]) and max(r["tn"]) <= 5):
            die("tone outside 1..5: %r" % (r.get("tn"),))

    official_zwj = sum(1 for r in zwj_rows if r["type"] == "RGI_Emoji_ZWJ_Sequence")
    dataset_zwj = sum(1 for r in dataset if ZWJ in [ord(ch) for ch in r["c"]])
    if dataset_zwj != official_zwj:
        die("ZWJ coverage mismatch: dataset=%d official=%d" % (dataset_zwj, official_zwj))
    official_flags = sum(
        1
        for r in rgi_rows
        if r["type"] in ("RGI_Emoji_Flag_Sequence", "RGI_Emoji_Tag_Sequence")
    )
    dataset_flags = sum(1 for r in dataset if r.get("vt") == "flag")
    if dataset_flags != official_flags:
        die("flag coverage mismatch: dataset=%d official=%d" % (dataset_flags, official_flags))

    # ---- metadata -------------------------------------------------------
    counts = {
        "entries": len(dataset),
        "bases": sum(1 for r in dataset if not r.get("tn")),
        "tone_variants": sum(1 for r in dataset if r.get("tn")),
        "tone_bases": len(variant_bases),
        "zwj": sum(1 for r in dataset if ZWJ in [ord(ch) for ch in r["c"]]),
        "zwj_rgi": len(zwj_rows),
        "flag_sequences": sum(1 for r in rgi_rows if r["type"] == "RGI_Emoji_Flag_Sequence"),
        "tag_sequences": sum(1 for r in rgi_rows if r["type"] == "RGI_Emoji_Tag_Sequence"),
        "flags": sum(1 for r in dataset if r.get("vt") == "flag"),
        "modifier_sequences_rgi": sum(
            1 for r in rgi_rows if r["type"] == "RGI_Emoji_Modifier_Sequence"
        ),
        "gender_variants": sum(1 for r in dataset if r.get("vt") == "gender"),
        "couples": sum(1 for r in dataset if r.get("vt") == "couple"),
        "families": sum(1 for r in dataset if r.get("vt") == "family"),
        "multi_tone": sum(1 for r in dataset if r.get("tn") and len(r["tn"]) > 1),
        "components_excluded": len(components),
        "variation_sequences": var_count,
    }
    by_group = Counter(r["g"] for r in dataset)
    id_covered = sum(1 for r in dataset if ann_id.get(r["c"]))
    base_rows = [r for r in dataset if not r.get("tn")]
    id_covered_bases = sum(1 for r in base_rows if ann_id.get(r["c"]))
    group_rows = [(name, by_group[i]) for i, name in enumerate(GROUPS)]

    sources = []
    for fname, ver in (
        ("emoji-test.txt", test_version),
        ("emoji-sequences.txt", seq_version),
        ("emoji-zwj-sequences.txt", zwj_version),
        ("emoji-data.txt", data_version),
        ("emoji-variation-sequences.txt", var_version),
        ("cldr-annotations-en.json", "CLDR 46"),
        ("cldr-annotations-id.json", "CLDR 46"),
    ):
        path = os.path.join(SRC_DIR, fname)
        sources.append(
            {"file": "tools/glyph-generator/sources/" + fname, "version": ver, "sha256": sha256(path)}
        )
    sources.append(
        {
            "file": "tools/glyph-generator/curated-keywords.json",
            "version": "curated",
            "sha256": sha256(CURATED),
        }
    )

    meta = {
        "schema": 2,
        "unicode_emoji_version": test_version,
        "cldr_version": "46",
        "groups": GROUPS,
        "subgroups": subgroups,
        "tones": [{"n": t, "en": tone_en[t], "id": tone_id[t]} for t in range(1, 6)],
        "counts": counts,
        "sources": sources,
    }

    emoji_json = json.dumps(dataset, ensure_ascii=False, separators=(",", ":"))
    meta_json = json.dumps(meta, ensure_ascii=False, separators=(",", ":"), sort_keys=True)
    report = build_report(
        meta, counts, group_rows, id_covered, len(dataset), derived_name, derived_kw,
        id_covered_bases, len(base_rows),
    )

    outputs = ((OUT_EMOJI, emoji_json), (OUT_META, meta_json), (OUT_REPORT, report))

    if args.check:
        stale = []
        for path, want in outputs:
            try:
                with open(path, encoding="utf-8") as fh:
                    have = fh.read()
            except FileNotFoundError:
                have = ""
            if have != want:
                stale.append(os.path.relpath(path, ROOT))
        if stale:
            print("stale generated glyph data: " + ", ".join(stale), file=sys.stderr)
            return 1
        print("glyph data up to date (Unicode Emoji %s, %d entries)" % (test_version, len(dataset)))
        return 0

    os.makedirs(os.path.dirname(OUT_REPORT), exist_ok=True)
    for path, payload in outputs:
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(payload)
        print("wrote %s (%d bytes)" % (os.path.relpath(path, ROOT), len(payload.encode("utf-8"))))

    print(
        "entries=%d bases=%d tone_variants=%d zwj=%d flags=%d"
        % (counts["entries"], counts["bases"], counts["tone_variants"], counts["zwj"], counts["flags"])
    )
    return 0


def build_report(
    meta, counts, group_rows, id_covered, total, derived_name, derived_kw,
    id_covered_bases, total_bases,
):
    version = meta["unicode_emoji_version"]
    lines = []
    a = lines.append
    a("# Emoji Dataset Coverage")
    a("")
    a("Generated by `tools/glyph-generator/generate.py`. Do not edit by hand.")
    a("")
    a("Unicode Emoji version: **%s**. CLDR annotations: **%s**." % (version, meta["cldr_version"]))
    a("")
    a("## Totals")
    a("")
    a("| Metric | Value |")
    a("| --- | --- |")
    a("| Dataset entries (all fully-qualified RGI) | %d |" % counts["entries"])
    a("| Base glyphs (no skin-tone modifier) | %d |" % counts["bases"])
    a("| Skin-tone variants | %d |" % counts["tone_variants"])
    a("| Base glyphs with tone variants | %d |" % counts["tone_bases"])
    a("| Multi-person tone combinations | %d |" % counts["multi_tone"])
    a("| ZWJ sequences (dataset / official RGI) | %d / %d |" % (counts["zwj"], counts["zwj_rgi"]))
    a(
        "| Flag sequences (RI pairs + tag sequences) | %d (%d + %d) |"
        % (counts["flags"], counts["flag_sequences"], counts["tag_sequences"])
    )
    a("| Gender variants | %d |" % counts["gender_variants"])
    a("| Couple / family sequences | %d / %d |" % (counts["couples"], counts["families"]))
    a("| Standardized variation sequences | %d |" % counts["variation_sequences"])
    a("| Standalone components excluded | %d |" % counts["components_excluded"])
    a("| Derived names / keywords (omitted from JSON) | %d / %d |" % (derived_name, derived_kw))
    a("")
    a("## Coverage by group")
    a("")
    a("| Group | Entries |")
    a("| --- | --- |")
    for name, n in group_rows:
        a("| %s | %d |" % (name, n))
    a("")
    a("## Localization")
    a("")
    a(
        "CLDR Indonesian annotations available for %d of %d base glyphs (%.1f%%)"
        " and %d of %d sequences including tone variants (%.1f%%)."
        % (
            id_covered_bases,
            total_bases,
            100.0 * id_covered_bases / max(total_bases, 1),
            id_covered,
            total,
            100.0 * id_covered / max(total, 1),
        )
    )
    a("Sequences introduced after CLDR %s fall back to Unicode English names; their" % meta["cldr_version"])
    a("Indonesian search still hits curated keyword overrides in")
    a("`tools/glyph-generator/curated-keywords.json`.")
    a("")
    a("## Sources")
    a("")
    a("| File | Version | SHA-256 |")
    a("| --- | --- | --- |")
    for s in meta["sources"]:
        a("| `%s` | %s | `%s` |" % (s["file"], s["version"], s["sha256"]))
    a("")
    a("Unicode data files are © Unicode, Inc. and distributed under the")
    a("[Unicode License](https://www.unicode.org/license.txt). CLDR data is © Unicode,")
    a("Inc. and distributed under the Unicode License as well.")
    a("")
    return "\n".join(lines)


if __name__ == "__main__":
    raise SystemExit(main())
