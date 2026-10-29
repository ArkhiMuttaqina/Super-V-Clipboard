use std::time::Instant;
use super_v_core::Database;

fn main() {
    let mut db = Database::open(":memory:").expect("failed to open memory db");
    
    // Benchmark 1,000 clipboard insertions & deduplications
    let start = Instant::now();
    let n = 1000;
    for i in 0..n {
        let text = format!("clipboard entry content {}", i % 100);
        db.insert_or_touch("text/plain", text.as_bytes(), Some(&text))
            .expect("insert failed");
    }
    let duration = start.elapsed();
    
    let items = db.list_recent(25).expect("list failed");
    assert!(!items.is_empty());
    
    let latency_us = (duration.as_micros() as f64) / (n as f64);
    let ops_per_sec = (n as f64) / duration.as_secs_f64();
    
    println!("METRIC insert_latency_us={:.2}", latency_us);
    println!("METRIC throughput_ops_sec={:.0}", ops_per_sec);
}
