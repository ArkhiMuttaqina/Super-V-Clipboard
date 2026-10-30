use super_v_core::{
    classifier::ContentCategory,
    positioning::{Point, Rect, WindowPlacer},
    secret_filter::SecretFilter,
    BackupManager, BlobStore, Database, SortOrder,
};

#[test]
fn test_classifier_all_variants() {
    assert_eq!(ContentCategory::classify("image/png", None), ContentCategory::Image);
    assert_eq!(ContentCategory::classify("text/plain", Some("#308b53")), ContentCategory::ColorHex);
    assert_eq!(ContentCategory::classify("text/plain", Some("https://example.com/api")), ContentCategory::Url);
    assert_eq!(ContentCategory::classify("text/plain", Some("user@domain.com")), ContentCategory::Email);
    assert_eq!(ContentCategory::classify("text/plain", Some("fn main() { println!(); }")), ContentCategory::CodeSnippet);
    assert_eq!(ContentCategory::classify("text/plain", Some("Ordinary text")), ContentCategory::PlainText);
}

#[test]
fn test_secret_filter_detection_and_masking() {
    assert!(SecretFilter::is_sensitive("application/x-secret", Some("plain")));
    assert!(SecretFilter::is_sensitive("text/plain", Some("-----BEGIN PRIVATE KEY-----\nMIIE...")));
    assert!(SecretFilter::is_sensitive("text/plain", Some("ghp_1234567890abcdefghijklmnopqrstuvwxyz")));
    assert!(SecretFilter::is_sensitive("text/plain", Some("xoxb-1234567890-abcdefghij")));
    assert!(SecretFilter::is_sensitive("text/plain", Some("8628677107:AAHH6NDDPkE3koiQQcmA50I_8DcC7WX6H4I")));
    assert!(!SecretFilter::is_sensitive("text/plain", Some("Just a normal shopping list")));

    let masked = SecretFilter::mask_preview("ghp_1234567890abcdefghijklmnopqrstuvwxyz");
    assert!(masked.contains("••••••••"));
    assert!(masked.starts_with("ghp"));
}

#[test]
fn test_database_crud_and_sorting() {
    let mut db = Database::open(":memory:").expect("Failed to open db");

    let item1 = db.insert_or_touch("text/plain", b"Alpha item", Some("Alpha item")).unwrap();
    let item2 = db.insert_or_touch("text/plain", b"Beta item", Some("Beta item")).unwrap();

    // Test pinning
    let is_pinned = db.toggle_pin(item1.id.unwrap()).unwrap();
    assert!(is_pinned);

    // Test list_ordered with PinnedFirst
    let items = db.list_ordered(10, SortOrder::PinnedFirst).unwrap();
    assert_eq!(items.len(), 2);
    assert_eq!(items[0].id, item1.id);

    // Test list_ordered with Alphabetical
    let items_alpha = db.list_ordered(10, SortOrder::Alphabetical).unwrap();
    assert_eq!(items_alpha[0].text.as_deref(), Some("Alpha item"));
    assert_eq!(items_alpha[1].text.as_deref(), Some("Beta item"));

    // Test search FTS5
    let search_res = db.search("Alpha*", 5).unwrap();
    assert_eq!(search_res.len(), 1);
    assert_eq!(search_res[0].id, item1.id);

    // Test delete
    db.delete_entry(item2.id.unwrap()).unwrap();
    assert_eq!(db.list_recent(10).unwrap().len(), 1);
}

#[test]
fn test_blob_store_and_eviction() {
    let dir = "/tmp/super-v-unit-test-blobs";
    let store = BlobStore::new(dir).unwrap();

    let (hash, path) = store.write_blob(b"sample media content", "gif").unwrap();
    assert!(path.exists());

    let read_back = store.read_blob(&hash, "gif").unwrap();
    assert_eq!(read_back, b"sample media content");

    let total = store.total_storage_bytes().unwrap();
    assert!(total > 0);

    let deleted = store.delete_blob(&path.to_string_lossy()).unwrap();
    assert!(deleted);
    let _ = std::fs::remove_dir_all(dir);
}

#[test]
fn test_backup_restore_round_trip() {
    let mut db = Database::open(":memory:").expect("Failed to open db");
    db.insert_or_touch("text/plain", b"Item 1", Some("Item 1")).unwrap();
    db.insert_or_touch("text/plain", b"Item 2", Some("Item 2")).unwrap();

    let path = "/tmp/super-v-unit-test-backup.json";
    let exported = BackupManager::export_to_json(&db, path).unwrap();
    assert_eq!(exported, 2);

    let mut db_new = Database::open(":memory:").expect("Failed to open new db");
    let imported = BackupManager::import_from_json(&mut db_new, path).unwrap();
    assert_eq!(imported, 2);
    assert_eq!(db_new.list_recent(10).unwrap().len(), 2);

    let _ = std::fs::remove_file(path);
}

#[test]
fn test_window_placer_clamping() {
    let mon = Rect { x: 0, y: 0, width: 1920, height: 1080 };

    // Standard placement
    let p1 = Point { x: 100, y: 100 };
    let pos1 = WindowPlacer::compute_position(p1, (420, 520), mon, 10);
    assert_eq!(pos1, Point { x: 108, y: 108 });

    // Edge overflow placement (clamped & flipped)
    let p2 = Point { x: 1900, y: 1050 };
    let pos2 = WindowPlacer::compute_position(p2, (420, 520), mon, 10);
    assert!(pos2.x + 420 <= 1920 - 10);
    assert!(pos2.y + 520 <= 1080 - 10);
}
