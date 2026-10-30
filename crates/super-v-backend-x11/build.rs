fn main() {
    println!("cargo:rerun-if-changed=c/x11_paste.c");
    println!("cargo:rerun-if-changed=c/x11_paste.h");

    cc::Build::new()
        .file("c/x11_paste.c")
        .compile("x11_paste");

    println!("cargo:rustc-link-lib=X11");
    println!("cargo:rustc-link-lib=Xtst");
    println!("cargo:rustc-link-lib=Xfixes");
}
