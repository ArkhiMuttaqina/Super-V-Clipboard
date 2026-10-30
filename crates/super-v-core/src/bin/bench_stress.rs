use std::fs;
use std::time::Instant;
use super_v_core::Database;

fn get_rss_mb() -> f64 {
    if let Ok(statm) = fs::read_to_string("/proc/self/statm") {
        if let Some(rss_pages) = statm.split_whitespace().nth(1) {
            if let Ok(pages) = rss_pages.parse::<f64>() {
                let page_size_kb = 4.0; // standard x86_64 4KB page
                return (pages * page_size_kb) / 1024.0;
            }
        }
    }
    0.0
}

fn main() {
    let mut db = Database::open(":memory:").expect("failed to open memory db");
    let n = 5000;
    
    let start = Instant::now();
    for i in 0..n {
        let text = format!("stress test clipboard payload {} with keywords fn test() and #308b53", i);
        db.insert_or_touch("text/plain", text.as_bytes(), Some(&text))
            .expect("insert failed");
        
        // Interleave 1 search every 100 inserts
        if i % 100 == 0 {
            let _ = db.search("stress*", 5);
        }
    }
    let duration = start.elapsed();
    
    let throughput = (n as f64) / duration.as_secs_f64();
    let rss = get_rss_mb();
    
    println!("VERIFY: stress_n={}, total_duration_ms={:.2}", n, duration.as_secs_f64() * 1000.0);
    println!("METRIC stress_throughput_ops_sec={:.0}", throughput);
    println!("METRIC memory_rss_mb={:.2}", rss);
}
