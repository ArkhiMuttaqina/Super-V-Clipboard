fn main() {
    println!("cargo:rerun-if-changed=c/wlr_clip.c");
    println!("cargo:rerun-if-changed=c/wlr_clip.h");

    cc::Build::new()
        .file("c/wlr_clip.c")
        .compile("wlr_clip");

    println!("cargo:rustc-link-lib=wayland-client");
}
