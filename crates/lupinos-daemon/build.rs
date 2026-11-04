fn main() {
    println!("cargo:rustc-link-lib=static=mojokernels");
    println!("cargo:rustc-link-search=native=.");
    println!("cargo:rustc-link-search=native=crates/lupinos-daemon");
}
