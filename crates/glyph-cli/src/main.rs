use clap::{Parser, Subcommand};
use std::path::PathBuf;

mod build;

#[derive(Parser)]
#[command(name = "glyph", version, about = "The Glyph compiler")]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    /// Initialize a new .glyph database
    Init {
        /// Path to the .glyph file to create
        path: PathBuf,
    },
    /// Compile dirty definitions
    Build {
        /// Path to the .glyph file
        path: PathBuf,
        /// Recompile everything
        #[arg(long)]
        full: bool,
        /// Emit MIR for debugging
        #[arg(long)]
        emit_mir: bool,
    },
    /// Build and execute main
    Run {
        /// Path to the .glyph file
        path: PathBuf,
    },
    /// Type-check only
    Check {
        /// Path to the .glyph file
        path: PathBuf,
    },
    /// Run all test definitions
    Test {
        /// Path to the .glyph file
        path: PathBuf,
    },
}

fn main() -> miette::Result<()> {
    let cli = Cli::parse();

    match cli.command {
        Command::Init { path } => cmd_init(&path),
        Command::Build { path, full, emit_mir } => build::cmd_build(&path, full, emit_mir),
        Command::Run { path } => cmd_run(&path),
        Command::Check { path } => build::cmd_check(&path),
        Command::Test { path } => cmd_test(&path),
    }
}

fn cmd_init(path: &PathBuf) -> miette::Result<()> {
    if path.exists() {
        return Err(miette::miette!("{} already exists", path.display()));
    }

    let _db = glyph_db::Database::create(path)
        .map_err(|e| miette::miette!("failed to create database: {e}"))?;

    eprintln!("Created {}", path.display());
    Ok(())
}

fn cmd_run(path: &PathBuf) -> miette::Result<()> {
    // Build first
    build::cmd_build(path, false, false)?;

    // Determine output path
    let exe_path = path.with_extension("");

    // Execute
    let status = std::process::Command::new(&exe_path)
        .status()
        .map_err(|e| miette::miette!("failed to execute {}: {e}", exe_path.display()))?;

    std::process::exit(status.code().unwrap_or(1));
}

fn cmd_test(path: &PathBuf) -> miette::Result<()> {
    let db = glyph_db::Database::open(path)
        .map_err(|e| miette::miette!("failed to open database: {e}"))?;

    let tests = db
        .defs_by_kind(glyph_db::DefKind::Test)
        .map_err(|e| miette::miette!("{e}"))?;

    if tests.is_empty() {
        eprintln!("No test definitions found.");
        return Ok(());
    }

    eprintln!("Found {} test(s)", tests.len());
    for test in &tests {
        eprintln!("  test: {}", test.name);
    }

    // TODO: Compile test defs into a harness binary
    eprintln!("test runner: not yet fully implemented");
    Ok(())
}
