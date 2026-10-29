use anyhow::Result;
use super_v_core::Database;

#[tokio::main]
async fn main() -> Result<()> {
    env_logger::init();
    log::info!("Starting super-v-clipboard daemon...");
    
    let db_path = std::env::var("SUPER_V_DB").unwrap_or_else(|_| ":memory:".to_string());
    let mut db = Database::open(&db_path)?;
    
    // Sample test entry
    let item = db.insert_or_touch("text/plain", b"Hello, super-v-clipboard!", Some("Hello, super-v-clipboard!"))?;
    log::info!("Stored clipboard entry: id={:?}, hash={}", item.id, item.hash);
    
    let items = db.list_recent(10)?;
    log::info!("Recent items: count={}", items.len());
    
    Ok(())
}
