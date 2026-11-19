fn main() {
    println!("cargo:rustc-link-lib=X11");
    cc::Build::new()
        .file("c/x11watch.c")
        .warnings(true)
        .compile("x11watch");
    println!("cargo:rerun-if-changed=c/x11watch.c");
}
