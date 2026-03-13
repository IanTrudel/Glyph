use clap::{Parser, Subcommand};
use std::path::PathBuf;

mod build;
mod import;

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
        /// Target generation (default: 1)
        #[arg(long = "gen", default_value = "1")]
        target_gen: i64,
    },
    /// Build and execute main
    Run {
        /// Path to the .glyph file
        path: PathBuf,
        /// Target generation (default: 1)
        #[arg(long = "gen", default_value = "1")]
        target_gen: i64,
    },
    /// Type-check only
    Check {
        /// Path to the .glyph file
        path: PathBuf,
        /// Target generation (default: 1)
        #[arg(long = "gen", default_value = "1")]
        target_gen: i64,
    },
    /// Import definitions from a split source directory into a .glyph database
    Import {
        /// Path to the source directory (containing src/ and schema.sql)
        src_dir: PathBuf,
        /// Path to the .glyph file to create
        db_path: PathBuf,
    },
    /// Run all test definitions
    Test {
        /// Path to the .glyph file
        path: PathBuf,
        /// Target generation (default: 1)
        #[arg(long = "gen", default_value = "1")]
        target_gen: i64,
    },
}

fn main() -> miette::Result<()> {
    let cli = Cli::parse();

    match cli.command {
        Command::Init { path } => cmd_init(&path),
        Command::Build { path, full, emit_mir, target_gen } => build::cmd_build(&path, full, emit_mir, target_gen),
        Command::Run { path, target_gen } => cmd_run(&path, target_gen),
        Command::Check { path, target_gen } => build::cmd_check(&path, target_gen),
        Command::Import { src_dir, db_path } => import::cmd_import(&src_dir, &db_path),
        Command::Test { path, target_gen } => cmd_test(&path, target_gen),
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

fn cmd_run(path: &PathBuf, target_gen: i64) -> miette::Result<()> {
    // Build first
    build::cmd_build(path, false, false, target_gen)?;

    // Determine output path
    let exe_path = path.with_extension("");

    // Execute
    let status = std::process::Command::new(&exe_path)
        .status()
        .map_err(|e| miette::miette!("failed to execute {}: {e}", exe_path.display()))?;

    std::process::exit(status.code().unwrap_or(1));
}

fn cmd_test(path: &PathBuf, target_gen: i64) -> miette::Result<()> {
    let db = glyph_db::Database::open(path)
        .map_err(|e| miette::miette!("failed to open database: {e}"))?;

    let tests = db
        .defs_by_kind_gen(glyph_db::DefKind::Test, target_gen)
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
