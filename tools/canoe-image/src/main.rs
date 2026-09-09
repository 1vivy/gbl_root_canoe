use clap::Parser;
fn main() {
    std::process::exit(canoe_image::cli::Cli::parse().run());
}
