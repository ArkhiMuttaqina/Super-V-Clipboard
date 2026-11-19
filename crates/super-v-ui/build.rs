fn main() {
    println!("cargo:rerun-if-changed=c/overlay.c");
    println!("cargo:rerun-if-changed=c/overlay.h");

    let adw = pkg_config::probe_library("libadwaita-1").expect("libadwaita-1 not found");
    let gtk = pkg_config::probe_library("gtk4").expect("gtk4 not found");

    let mut build = cc::Build::new();
    build.file("c/overlay.c");

    for p in adw.include_paths.iter().chain(gtk.include_paths.iter()) {
        build.include(p);
    }

    build.compile("overlay");
}
