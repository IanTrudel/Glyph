#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Span {
    pub start: u32,
    pub end: u32,
}

impl Span {
    pub fn new(start: u32, end: u32) -> Self {
        Self { start, end }
    }

    pub fn merge(self, other: Span) -> Span {
        Span {
            start: self.start.min(other.start),
            end: self.end.max(other.end),
        }
    }
}

/// Convert a byte offset to 1-based (line, column).
pub fn line_col(source: &str, offset: u32) -> (usize, usize) {
    let offset = offset as usize;
    let mut line = 1;
    let mut col = 1;
    for (i, ch) in source.char_indices() {
        if i >= offset {
            break;
        }
        if ch == '\n' {
            line += 1;
            col = 1;
        } else {
            col += 1;
        }
    }
    (line, col)
}

/// Extract the source line containing the given offset, plus its line number and column.
pub fn source_line_at(source: &str, offset: u32) -> (&str, usize, usize) {
    let (line_num, col) = line_col(source, offset);
    let off = offset as usize;
    let line_start = source[..off].rfind('\n').map(|p| p + 1).unwrap_or(0);
    let line_end = source[off..].find('\n').map(|p| off + p).unwrap_or(source.len());
    (&source[line_start..line_end], line_num, col)
}

/// Format a diagnostic message with source context and caret.
pub fn format_diagnostic(
    def_name: &str,
    source: &str,
    span: Span,
    level: &str,
    message: &str,
) -> String {
    let (line_text, line_num, col) = source_line_at(source, span.start);
    let pad = format!("{}", line_num).len();
    format!(
        "{level} in '{def_name}' at {line_num}:{col}: {message}\n\
         {line_num:>pad$} | {line_text}\n\
         {:>pad$} | {:>col$}",
        "", "^",
        pad = pad,
        col = col,
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_line_col_first_char() {
        assert_eq!(line_col("hello", 0), (1, 1));
    }

    #[test]
    fn test_line_col_second_line() {
        assert_eq!(line_col("abc\ndef", 5), (2, 2));
    }

    #[test]
    fn test_line_col_end_of_first_line() {
        assert_eq!(line_col("abc\ndef", 3), (1, 4));
    }

    #[test]
    fn test_source_line_at() {
        let src = "abc\ndef\nghi";
        let (line, num, col) = source_line_at(src, 5);
        assert_eq!(line, "def");
        assert_eq!(num, 2);
        assert_eq!(col, 2);
    }

    #[test]
    fn test_format_diagnostic() {
        let src = "x = 1\ny = bar(a, )\nz = 3";
        let diag = format_diagnostic("foo", src, Span::new(15, 16), "error", "unexpected token: )");
        assert!(diag.contains("error in 'foo' at 2:10"));
        assert!(diag.contains("y = bar(a, )"));
        assert!(diag.contains("^"));
    }
}
