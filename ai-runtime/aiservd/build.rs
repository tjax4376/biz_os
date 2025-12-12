fn main() {
    println!("cargo:rerun-if-changed=proto/ingestion.proto");
    tonic_build::configure()
        .build_server(true)
        .compile(&["proto/ingestion.proto"], &["proto"])
        .expect("failed to compile ingestion proto");
}
