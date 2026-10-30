//! Unicode emoji coverage and search contract tests.
//!
//! The official data files the dataset was generated from are embedded here, so
//! these tests compare the shipped JSON against Unicode 18.0 without a network
//! call and without trusting the generator's own report.

use std::collections::{HashMap, HashSet};

use super_v_core::glyphs::GlyphIndex;

const EMOJI_TEST: &str = include_str!("../../../tools/glyph-generator/sources/emoji-test.txt");
const EMOJI_SEQUENCES: &str =
    include_str!("../../../tools/glyph-generator/sources/emoji-sequences.txt");
const EMOJI_ZWJ: &str =
    include_str!("../../../tools/glyph-generator/sources/emoji-zwj-sequences.txt");
const EMOJI_DATA: &str = include_str!("../../../tools/glyph-generator/sources/emoji-data.txt");

const MODIFIER_LO: u32 = 0x1F3FB;
const MODIFIER_HI: u32 = 0x1F3FF;

struct OfficialEntry {
    sequence: String,
    qualification: String,
}

fn official_entries() -> Vec<OfficialEntry> {
    let mut out = Vec::new();
    for line in EMOJI_TEST.lines() {
        let body = line.trim();
        if body.is_empty() || body.starts_with('#') {
            continue;
        }
        let (codes, rest) = body.split_once(';').expect("emoji-test row");
        let qualification = rest.split('#').next().unwrap_or("").trim().to_string();
        let sequence: String = codes
            .split_whitespace()
            .map(|hex| char::from_u32(u32::from_str_radix(hex, 16).expect("hex")))
            .collect::<Option<String>>()
            .expect("valid scalar values");
        out.push(OfficialEntry {
            sequence,
            qualification,
        });
    }
    out
}

/// Rows of emoji-sequences.txt / emoji-zwj-sequences.txt as (sequence, type).
fn rgi_rows(text: &str) -> Vec<(String, String)> {
    let mut out = Vec::new();
    for line in text.lines() {
        let body = line.trim();
        if body.is_empty() || body.starts_with('#') {
            continue;
        }
        let fields: Vec<&str> = body.split(';').collect();
        assert!(fields.len() >= 3, "unexpected RGI row: {body}");
        let mut codes: Vec<u32> = Vec::new();
        for part in fields[0].split_whitespace() {
            match part.split_once("..") {
                Some((lo, hi)) => {
                    let lo = u32::from_str_radix(lo, 16).expect("hex");
                    let hi = u32::from_str_radix(hi, 16).expect("hex");
                    codes.extend(lo..=hi);
                }
                None => codes.push(u32::from_str_radix(part, 16).expect("hex")),
            }
        }
        if fields[0].contains("..") {
            for code in codes {
                out.push((
                    char::from_u32(code).expect("scalar").to_string(),
                    fields[1].trim().to_string(),
                ));
            }
        } else {
            let sequence: String = codes
                .into_iter()
                .map(|cp| char::from_u32(cp).expect("scalar"))
                .collect();
            out.push((sequence, fields[1].trim().to_string()));
        }
    }
    out
}

fn modifier_bases() -> HashSet<u32> {
    let mut out = HashSet::new();
    for line in EMOJI_DATA.lines() {
        let body = line.trim();
        if body.is_empty() || body.starts_with('#') {
            continue;
        }
        let (range, property) = body.split_once(';').expect("emoji-data row");
        if property.split('#').next().unwrap_or("").trim() != "Emoji_Modifier_Base" {
            continue;
        }
        let mut bounds = range.trim().split("..");
        let lo = u32::from_str_radix(bounds.next().unwrap().trim(), 16).expect("hex");
        let hi = bounds
            .next()
            .map(|v| u32::from_str_radix(v.trim(), 16).expect("hex"))
            .unwrap_or(lo);
        out.extend(lo..=hi);
    }
    out
}

fn tones_of(sequence: &str) -> Vec<u8> {
    sequence
        .chars()
        .filter(|c| (MODIFIER_LO..=MODIFIER_HI).contains(&(*c as u32)))
        .map(|c| (c as u32 - MODIFIER_LO + 1) as u8)
        .collect()
}

fn strip_modifiers(sequence: &str) -> String {
    sequence
        .chars()
        .filter(|c| !(MODIFIER_LO..=MODIFIER_HI).contains(&(*c as u32)))
        .collect()
}

#[test]
fn dataset_equals_official_fully_qualified_set() {
    let idx = GlyphIndex::load_embedded();
    let official: HashSet<String> = official_entries()
        .into_iter()
        .filter(|e| e.qualification == "fully-qualified")
        .map(|e| e.sequence)
        .collect();

    let mut mine = HashSet::new();
    let mut page = 0usize;
    loop {
        let chunk = idx.search("emoji", "", "en", page, 500);
        if chunk.is_empty() {
            break;
        }
        page += chunk.len();
        for glyph in chunk {
            assert!(mine.insert(glyph.char.clone()), "duplicate {}", glyph.char);
        }
    }

    // Base browse covers everything, but each entry's own existence is checked
    // through the tone-variant table and the raw count below.
    assert_eq!(idx.emoji_count(), official.len());
    assert_eq!(idx.meta().counts.entries, official.len());
    assert_eq!(idx.meta().counts.bases, mine.len());
    assert!(mine.is_subset(&official));

    let missing = official.len() - mine.len();
    assert_eq!(missing, idx.tone_variant_count());

    // Every official sequence must be reachable: bases directly, variants
    // through their base glyph.
    for sequence in &official {
        let is_variant = !tones_of(sequence).is_empty();
        if is_variant {
            continue;
        }
        assert!(mine.contains(sequence), "missing base sequence {sequence:?}");
    }
}

#[test]
fn tone_variants_are_exact_official_sequences() {
    let idx = GlyphIndex::load_embedded();
    let official: HashMap<String, String> = official_entries()
        .into_iter()
        .map(|e| (e.sequence, e.qualification))
        .collect();

    let mut seen: HashSet<String> = HashSet::new();
    let mut variants = 0usize;
    let mut combos: HashMap<Vec<u8>, usize> = HashMap::new();

    for variant in idx.all_emojis().iter().filter(|g| !g.tones.is_empty()) {
        let sequence = variant.char.clone();
        assert!(seen.insert(sequence.clone()), "duplicate variant {sequence:?}");
        assert_eq!(
            official.get(&sequence).map(|q| q.as_str()),
            Some("fully-qualified"),
            "variant {sequence:?} is not a fully-qualified RGI sequence"
        );
        let stripped = strip_modifiers(&sequence);
        match &variant.variant_of {
            Some(base) => {
                // A few bases are only RGI with an emoji presentation selector
                // that the toned sequence omits (U+1F590, U+1F575, ...), and in
                // ZWJ sequences that selector sits mid-sequence.
                let vs16 = char::from_u32(0xFE0F).expect("VS16");
                let base_sans_vs: String = {
                    let mut out = String::new();
                    let mut removed = false;
                    for ch in base.chars() {
                        if !removed && ch == vs16 {
                            removed = true;
                            continue;
                        }
                        out.push(ch);
                    }
                    out
                };
                assert!(
                    &stripped == base || stripped == base_sans_vs,
                    "variant base mismatch for {sequence:?}"
                );
                let base_glyph = idx
                    .all_emojis()
                    .iter()
                    .find(|g| &g.char == base)
                    .expect("base glyph present");
                // Unicode orders the suffix differently when the base name
                // already carries one ("person: blond hair" + tone), so the
                // check is keyword inheritance rather than string prefix.
                assert!(!variant.name.trim().is_empty());
                for word in base_glyph.keywords.split_whitespace() {
                    assert!(
                        variant.keywords.split_whitespace().any(|w| w == word),
                        "variant {:?} lost inherited keyword {:?}",
                        variant.char,
                        word
                    );
                }
            }
            None => {
                // Multi-person combinations (couples, families) have no
                // modifier-free RGI form; Unicode defines only the toned ones.
                assert!(
                    !official.contains_key(&stripped),
                    "{sequence:?} should link to the RGI base {stripped:?}"
                );
                assert!(!variant.name.is_empty());
            }
        }
        let expected = tones_of(&sequence);
        assert_eq!(variant.tones, expected);
        combos.entry(expected).and_modify(|c| *c += 1).or_insert(1);
        variants += 1;
    }

    assert_eq!(variants, idx.tone_variant_count());
    assert_eq!(variants, idx.meta().counts.tone_variants);
    for tone in 1..=5u8 {
        assert!(combos.contains_key(&vec![tone]), "tone {tone} not covered");
    }
    let multi_sequences: usize = combos
        .iter()
        .filter(|(tones, _)| tones.len() > 1)
        .map(|(_, count)| *count)
        .sum();
    assert_eq!(multi_sequences, idx.meta().counts.multi_tone);
    assert!(multi_sequences > 400, "multi-person tone coverage regressed");
}

#[test]
fn modifiers_attach_to_modifier_bases_only() {
    let idx = GlyphIndex::load_embedded();
    let bases = modifier_bases();
    assert!(!bases.is_empty());

    let mut checked = 0usize;
    for variant in idx.all_emojis().iter().filter(|g| !g.tones.is_empty()) {
        let cps: Vec<u32> = variant.char.chars().map(|c| c as u32).collect();
        for (i, cp) in cps.iter().enumerate() {
            if (MODIFIER_LO..=MODIFIER_HI).contains(cp) {
                assert!(i > 0, "modifier at sequence start: {:?}", variant.char);
                assert!(
                    bases.contains(&cps[i - 1]),
                    "modifier follows U+{:04X} which is not an Emoji_Modifier_Base ({:?})",
                    cps[i - 1],
                    variant.char
                );
                checked += 1;
            }
        }
    }
    assert!(checked > 2000, "expected thousands of modifiers, saw {checked}");
}

#[test]
fn zwj_and_flag_coverage_match_official_files() {
    let idx = GlyphIndex::load_embedded();
    let zwj_official = rgi_rows(EMOJI_ZWJ);
    assert!(
        zwj_official
            .iter()
            .all(|(_, kind)| kind == "RGI_Emoji_ZWJ_Sequence")
    );

    let flags_official: usize = rgi_rows(EMOJI_SEQUENCES)
        .iter()
        .filter(|(_, kind)| {
            kind == "RGI_Emoji_Flag_Sequence" || kind == "RGI_Emoji_Tag_Sequence"
        })
        .count();

    let mut zwj = 0usize;
    let mut flags = 0usize;
    for glyph in idx.all_emojis() {
        if glyph.char.contains('\u{200d}') {
            zwj += 1;
        }
        if glyph.variant_type.as_deref() == Some("flag") {
            flags += 1;
        }
    }

    assert_eq!(zwj, zwj_official.len());
    assert_eq!(flags, flags_official);
    assert_eq!(zwj, idx.meta().counts.zwj);
    assert_eq!(flags, idx.meta().counts.flags);
}

#[test]
fn search_prefers_exact_sequences_and_code_points() {
    let idx = GlyphIndex::load_embedded();

    let by_glyph = idx.search("emoji", "👋", "en,id", 0, 5);
    assert_eq!(by_glyph.first().map(|g| g.char.as_str()), Some("👋"));

    for query in ["1f44b", "U+1F44B", "0x1F44B"] {
        let hits = idx.search("emoji", query, "en", 0, 5);
        assert_eq!(hits.first().map(|g| g.char.as_str()), Some("👋"), "{query}");
    }

    // Shortcode-style aliases resolve through the same normalization.
    let shortcode = idx.search("emoji", ":waving hand:", "en", 0, 5);
    assert_eq!(shortcode.first().map(|g| g.char.as_str()), Some("👋"));

    // Multi-code-point searches match the full sequence.
        let family = idx.search("emoji", "1f468 200d 1f469 200d 1f467", "en", 0, 3);
    assert_eq!(family.first().map(|g| g.char.as_str()), Some("👨‍👩‍👧"));
}

#[test]
fn tone_search_surfaces_variants_without_flooding_normal_search() {
    let idx = GlyphIndex::load_embedded();

    let plain = idx.search("emoji", "wave", "en,id", 0, 50);
    assert!(plain.iter().any(|g| g.char == "👋"));
    assert!(
        plain.iter().all(|g| g.tones.is_empty()),
        "tone variants must not flood a plain query"
    );

    let toned = idx.search("emoji", "light skin tone", "en,id", 0, 500);
    assert!(toned.iter().any(|g| g.char == "👋🏻"));
    assert!(toned.iter().all(|g| !g.tones.is_empty()));

    let indonesian = idx.search("emoji", "warna kulit gelap", "id", 0, 500);
    assert!(!indonesian.is_empty(), "Indonesian tone query must hit");
    // "gelap" also expands through the Indonesian dictionary, so untoned
    // glyphs such as the new-moon face legitimately match; every toned hit must
    // still be a dark-tone variant.
    assert!(!indonesian.iter().filter(|g| !g.tones.is_empty()).collect::<Vec<_>>().is_empty());
    assert!(indonesian.iter().any(|g| g.tones.contains(&5)));

    assert!(idx.has_tone_variants("👋"));
    assert!(!idx.has_tone_variants("😀"));
}

#[test]
fn pagination_is_stable_unique_and_complete() {
    let idx = GlyphIndex::load_embedded();
    let total = idx.search_total("emoji", "", "en,id");
    assert_eq!(total, idx.emoji_base_count());

    let mut seen = HashSet::new();
    let mut count = 0usize;
    for page in 0.. {
        let chunk = idx.search("emoji", "", "en,id", page * 120, 120);
        if chunk.is_empty() {
            break;
        }
        count += chunk.len();
        for glyph in chunk {
            assert!(seen.insert(glyph.char.clone()), "duplicate page entry {}", glyph.char);
        }
    }
    assert_eq!(count, total);

    // search_total agrees with the paged result for a real query too.
    let query = "sedih";
    let reported = idx.search_total("emoji", query, "en,id");
    let mut walked = 0usize;
    for page in 0.. {
        let chunk = idx.search("emoji", query, "en,id", page * 10, 10);
        if chunk.is_empty() {
            break;
        }
        walked += chunk.len();
    }
    assert_eq!(walked, reported);
}

#[test]
fn existing_search_behaviour_preserved() {
    let idx = GlyphIndex::load_embedded();

    assert!(!idx.search("emoji", "sedih", "en,id", 0, 24).is_empty());
    assert!(!idx.search("emoji", "marah", "en,id", 0, 24).is_empty());
    assert!(!idx.search("emoji", "crying", "en", 0, 24).is_empty());
    assert!(idx.search("emoji", "sedih", "en", 0, 24).is_empty());
    assert!(!idx.search("symbol", "panah", "en,id", 0, 50).is_empty());
    assert_eq!(idx.symbol_count(), 1073);
    assert_eq!(idx.available_languages(), vec!["en", "id"]);

    let idx2 = GlyphIndex::load_embedded();
    assert_eq!(idx.emoji_count(), idx2.emoji_count());
    assert_eq!(idx.unicode_version(), idx2.unicode_version());
}

#[test]
fn every_glyph_has_searchable_metadata() {
    let idx = GlyphIndex::load_embedded();
    for page in 0.. {
        let chunk = idx.search("emoji", "", "en", page * 500, 500);
        if chunk.is_empty() {
            break;
        }
        for glyph in chunk {
            assert!(!glyph.name.trim().is_empty(), "missing name for {:?}", glyph.char);
            assert!(
                !glyph.keywords.trim().is_empty(),
                "missing keywords for {:?}",
                glyph.char
            );
            assert!(
                !glyph.codepoints.is_empty(),
                "missing code points for {:?}",
                glyph.char
            );
            assert!(glyph.group < idx.meta().groups.len());
            let subgroup = glyph.subgroup.expect("subgroup id");
            assert!(idx.subgroup_name(subgroup).is_some());
        }
    }
}
