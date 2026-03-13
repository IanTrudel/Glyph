use std::path::{Path, PathBuf};

use glyph_db::{Database, DefKind, NewDef};

pub fn cmd_import(src_dir: &PathBuf, db_path: &PathBuf) -> miette::Result<()> {
    if db_path.exists() {
        std::fs::remove_file(db_path)
            .map_err(|e| miette::miette!("failed to remove {}: {e}", db_path.display()))?;
    }

    let db = Database::create(db_path)
        .map_err(|e| miette::miette!("failed to create database: {e}"))?;

    // Execute only INSERT statements from schema.sql (DDL already handled by create())
    let schema_path = src_dir.join("schema.sql");
    if schema_path.exists() {
        let sql = std::fs::read_to_string(&schema_path)
            .map_err(|e| miette::miette!("failed to read schema.sql: {e}"))?;
        let inserts: String = sql
            .split(';')
            .map(str::trim)
            .filter(|s| s.to_ascii_uppercase().starts_with("INSERT"))
            .map(|s| format!("{s};"))
            .collect::<Vec<_>>()
            .join("\n");
        if !inserts.is_empty() {
            db.conn()
                .execute_batch(&inserts)
                .map_err(|e| miette::miette!("schema.sql INSERT failed: {e}"))?;
        }
    }

    let src_path = src_dir.join("src");
    let count1 = import_dir(&db, &src_path, 1)?;
    let count2 = import_dir(&db, &src_path.join("gen2"), 2)?;


    eprintln!(
        "imported {} definitions ({} gen=1, {} gen=2)",
        count1 + count2,
        count1,
        count2
    );
    Ok(())
}

fn import_dir(db: &Database, dir: &Path, generation: i64) -> miette::Result<usize> {
    if !dir.exists() {
        return Ok(0);
    }

    let mut paths: Vec<PathBuf> = std::fs::read_dir(dir)
        .map_err(|e| miette::miette!("failed to read {}: {e}", dir.display()))?
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .filter(|p| p.extension().and_then(|s| s.to_str()) == Some("gl") && p.is_file())
        .collect();
    paths.sort();

    for path in &paths {
        let filename = path.file_name().unwrap().to_string_lossy();
        // filename format: name.kind.gl
        let without_gl = &filename[..filename.len() - 3];
        let last_dot = without_gl.rfind('.').ok_or_else(|| {
            miette::miette!("invalid filename (expected name.kind.gl): {filename}")
        })?;
        let name = &without_gl[..last_dot];
        let kind_str = &without_gl[last_dot + 1..];

        let kind = DefKind::from_str(kind_str).ok_or_else(|| {
            miette::miette!("unknown kind '{kind_str}' in {filename}")
        })?;

        let body = std::fs::read_to_string(path)
            .map_err(|e| miette::miette!("failed to read {}: {e}", path.display()))?;
        let body = body.trim_end_matches('\n').to_string();

        db.insert_def(&NewDef {
            name: name.to_string(),
            kind,
            sig: None,
            body,
            generation,
        })
        .map_err(|e| miette::miette!("failed to insert '{name}': {e}"))?;
    }

    Ok(paths.len())
}
