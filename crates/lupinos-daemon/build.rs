fn main() {
    println!(" ");
    println!("cargo:rustc-link-search=native=.");
    println!("cargo:rustc-link-search=native=crates/lupinos-daemon");
}
