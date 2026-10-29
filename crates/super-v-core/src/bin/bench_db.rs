use std::time::Instant;
use super_v_core::Database;

fn main() {
    let mut db = Database::open(":memory:").expect("failed to open memory db");
    
    let n = 1000;
    let start = Instant::now();
    for i in 0..n {
        let text = format!("clipboard entry content {}", i % 100);
        db.insert_or_touch("text/plain", text.as_bytes(), Some(&text))
            .expect("insert failed");
    }
    let duration = start.elapsed();
    
    let latency_us = (duration.as_micros() as f64) / (n as f64);
    let throughput_ops_sec = (n as f64) / duration.as_secs_f64();
    
    // Test search latency
    let search_start = Instant::now();
    let search_results = db.search("clipboard*", 25).expect("search failed");
    let search_latency_us = search_start.elapsed().as_micros();
    
    println!("VERIFY: bench_n={}, items_found={}", n, search_results.len());
    println!("METRIC insert_latency_us={:.2}", latency_us);
    println!("METRIC throughput_ops_sec={:.0}", throughput_ops_sec);
    println!("METRIC search_latency_us={}", search_latency_us);
}
