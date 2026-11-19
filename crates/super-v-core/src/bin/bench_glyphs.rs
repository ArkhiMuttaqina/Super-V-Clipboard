//! Glyph dataset load cost and search latency.
//!
//! `cargo run --release -p super-v-core --bin bench_glyphs`
//!
//! Prints the embedded dataset size, resident-set impact of building the index,
//! and per-query latency (mean and p99) for the query shapes the picker uses.

use std::time::Instant;

use super_v_core::glyphs::{GlyphIndex, EMOJIS_RAW, META_RAW, SYMBOLS_RAW};

fn rss_kb() -> u64 {
    std::fs::read_to_string("/proc/self/status")
        .ok()
        .and_then(|status| {
            status.lines().find_map(|line| {
                line.strip_prefix("VmRSS:")
                    .and_then(|rest| rest.split_whitespace().next())
                    .and_then(|kb| kb.parse().ok())
            })
        })
        .unwrap_or(0)
}

fn percentile(sorted: &[f64], pct: f64) -> f64 {
    if sorted.is_empty() {
        return 0.0;
    }
    let index = ((sorted.len() - 1) as f64 * pct).round() as usize;
    sorted[index]
}

fn main() {
    let dataset_bytes = EMOJIS_RAW.len() + SYMBOLS_RAW.len() + META_RAW.len();
    println!("dataset_bytes={dataset_bytes}");

    let rss_before = rss_kb();
    let start = Instant::now();
    let index = GlyphIndex::load_embedded();
    let load_ms = start.elapsed().as_secs_f64() * 1000.0;
    let rss_after = rss_kb();

    println!("load_ms={load_ms:.2}");
    println!("rss_delta_kb={}", rss_after.saturating_sub(rss_before));
    println!("unicode_emoji={}", index.unicode_version());
    println!("emoji_entries={}", index.emoji_count());
    println!("emoji_bases={}", index.emoji_base_count());
    println!("tone_variants={}", index.tone_variant_count());
    println!("symbols={}", index.symbol_count());

    let cases: [(&str, &str, &str); 8] = [
        ("browse_first_page", "", "en,id"),
        ("browse_deep_page", "", "en,id"),
        ("en_name", "grinning", "en"),
        ("id_dictionary", "sedih", "en,id"),
        ("id_keyword", "warna kulit gelap", "id"),
        ("tone_variants", "light skin tone", "en"),
        ("codepoints", "1f44b", "en"),
        ("zwj_sequence", "family", "en"),
    ];

    for (label, query, langs) in cases {
        let offset = if label == "browse_deep_page" { 1800 } else { 0 };
        let iterations = 200;
        let mut samples = Vec::with_capacity(iterations);
        let mut hits = 0usize;
        for _ in 0..iterations {
            let started = Instant::now();
            hits = index.search("emoji", query, langs, offset, 120).len();
            samples.push(started.elapsed().as_secs_f64() * 1_000_000.0);
        }
        samples.sort_by(|a, b| a.partial_cmp(b).unwrap());
        let mean = samples.iter().sum::<f64>() / samples.len() as f64;
        println!(
            "search query={label} mean_us={mean:.1} p99_us={:.1} page_rows={hits}",
            percentile(&samples, 0.99)
        );
    }

    let started = Instant::now();
    let total = index.search_total("emoji", "", "en,id");
    println!(
        "search_total_us={:.1} total={total}",
        started.elapsed().as_secs_f64() * 1_000_000.0
    );
}
