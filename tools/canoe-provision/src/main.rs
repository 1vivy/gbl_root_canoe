use clap::Parser;
fn main() {
    std::process::exit(canoe_provision::cli::Cli::parse().run());
}
