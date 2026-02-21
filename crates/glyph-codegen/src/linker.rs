use std::io::Write;
use std::path::Path;
use std::process::Command;

/// Link an object file into an executable using the system linker.
pub fn link(
    object_bytes: &[u8],
    output_path: &Path,
    extern_libs: &[String],
    runtime_c: Option<&str>,
) -> Result<(), String> {
    let dir = tempfile::tempdir().map_err(|e| format!("failed to create temp dir: {e}"))?;

    // Write the main object file
    let obj_path = dir.path().join("glyph_program.o");
    std::fs::write(&obj_path, object_bytes)
        .map_err(|e| format!("failed to write object file: {e}"))?;

    // Write and compile the runtime if provided
    let mut cc_args: Vec<String> = vec![obj_path.to_string_lossy().to_string()];

    if let Some(rt_source) = runtime_c {
        let rt_c_path = dir.path().join("runtime.c");
        let mut f = std::fs::File::create(&rt_c_path)
            .map_err(|e| format!("failed to create runtime.c: {e}"))?;
        f.write_all(rt_source.as_bytes())
            .map_err(|e| format!("failed to write runtime.c: {e}"))?;
        cc_args.push(rt_c_path.to_string_lossy().to_string());
    }

    cc_args.push("-o".to_string());
    cc_args.push(output_path.to_string_lossy().to_string());

    // Add extern libraries
    for lib in extern_libs {
        cc_args.push(format!("-l{lib}"));
    }

    // Invoke the C compiler as a linker
    let cc = std::env::var("CC").unwrap_or_else(|_| "cc".to_string());
    let status = Command::new(&cc)
        .args(&cc_args)
        .status()
        .map_err(|e| format!("failed to invoke linker '{cc}': {e}"))?;

    if !status.success() {
        return Err(format!("linker failed with {status}"));
    }

    Ok(())
}
