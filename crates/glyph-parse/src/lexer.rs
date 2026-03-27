use crate::span::Span;
use crate::token::{Token, TokenKind};

/// Indentation-sensitive lexer for Glyph.
///
/// Maintains an indent stack and emits synthetic Indent/Dedent/Newline tokens.
/// Brackets suppress indentation handling (Python rule).
pub struct Lexer {
    source: Vec<char>,
    pos: usize,
    tokens: Vec<Token>,
    indent_stack: Vec<usize>,
    bracket_depth: usize,
}

impl Lexer {
    pub fn new(source: &str) -> Self {
        Self {
            source: source.chars().collect(),
            pos: 0,
            tokens: Vec::new(),
            indent_stack: vec![0],
            bracket_depth: 0,
        }
    }

    pub fn tokenize(mut self) -> Vec<Token> {
        self.scan_all();
        // Emit remaining dedents
        while self.indent_stack.len() > 1 {
            self.indent_stack.pop();
            self.tokens.push(Token {
                kind: TokenKind::Dedent,
                span: Span::new(self.pos as u32, self.pos as u32),
            });
        }
        self.tokens.push(Token {
            kind: TokenKind::Eof,
            span: Span::new(self.pos as u32, self.pos as u32),
        });
        self.tokens
    }

    fn scan_all(&mut self) {
        // Handle first line's indentation
        if !self.at_end() {
            self.handle_line_start();
        }
        while !self.at_end() {
            self.skip_whitespace();
            if self.at_end() {
                break;
            }
            if self.peek() == '\n' {
                self.advance();
                if self.bracket_depth == 0 {
                    // Skip blank lines
                    while !self.at_end() && self.peek() == '\n' {
                        self.advance();
                    }
                    if !self.at_end() {
                        self.emit_newline();
                        self.handle_line_start();
                    }
                }
                continue;
            }
            if self.peek() == '-' && self.peek_at(1) == Some('-') {
                self.skip_comment();
                continue;
            }
            self.scan_token();
        }
    }

    fn handle_line_start(&mut self) {
        if self.bracket_depth > 0 {
            self.skip_whitespace();
            return;
        }
        let indent = self.measure_indent();
        let current = *self.indent_stack.last().unwrap();

        if indent > current {
            self.indent_stack.push(indent);
            self.tokens.push(Token {
                kind: TokenKind::Indent,
                span: Span::new(self.pos as u32, self.pos as u32),
            });
        } else {
            while indent < *self.indent_stack.last().unwrap() {
                self.indent_stack.pop();
                self.tokens.push(Token {
                    kind: TokenKind::Dedent,
                    span: Span::new(self.pos as u32, self.pos as u32),
                });
            }
        }
    }

    fn measure_indent(&mut self) -> usize {
        let mut count = 0;
        while !self.at_end() && (self.peek() == ' ' || self.peek() == '\t') {
            if self.peek() == '\t' {
                count += 4; // Tab = 4 spaces
            } else {
                count += 1;
            }
            self.advance();
        }
        count
    }

    fn emit_newline(&mut self) {
        // Don't emit duplicate newlines
        if let Some(last) = self.tokens.last() {
            if matches!(last.kind, TokenKind::Newline | TokenKind::Indent) {
                return;
            }
        }
        self.tokens.push(Token {
            kind: TokenKind::Newline,
            span: Span::new(self.pos as u32, self.pos as u32),
        });
    }

    fn scan_token(&mut self) {
        let start = self.pos;
        let ch = self.peek();

        match ch {
            '(' => {
                self.advance();
                self.bracket_depth += 1;
                self.push(TokenKind::LParen, start);
            }
            ')' => {
                self.advance();
                self.bracket_depth = self.bracket_depth.saturating_sub(1);
                self.push(TokenKind::RParen, start);
            }
            '[' => {
                self.advance();
                self.bracket_depth += 1;
                self.push(TokenKind::LBracket, start);
            }
            ']' => {
                self.advance();
                self.bracket_depth = self.bracket_depth.saturating_sub(1);
                self.push(TokenKind::RBracket, start);
            }
            '{' => {
                self.advance();
                self.bracket_depth += 1;
                self.push(TokenKind::LBrace, start);
            }
            '}' => {
                self.advance();
                self.bracket_depth = self.bracket_depth.saturating_sub(1);
                self.push(TokenKind::RBrace, start);
            }
            ',' => {
                self.advance();
                self.push(TokenKind::Comma, start);
            }
            '\\' => {
                self.advance();
                self.push(TokenKind::Backslash, start);
            }
            '@' => {
                self.advance();
                self.push(TokenKind::At, start);
            }
            ':' => {
                self.advance();
                self.push(TokenKind::Colon, start);
            }
            '.' => {
                self.advance();
                if !self.at_end() && self.peek() == '.' {
                    self.advance();
                    self.push(TokenKind::DotDot, start);
                } else {
                    self.push(TokenKind::Dot, start);
                }
            }
            '=' => {
                self.advance();
                if !self.at_end() && self.peek() == '=' {
                    self.advance();
                    self.push(TokenKind::EqEq, start);
                } else {
                    self.push(TokenKind::Eq, start);
                }
            }
            '!' => {
                self.advance();
                if !self.at_end() && self.peek() == '=' {
                    self.advance();
                    self.push(TokenKind::BangEq, start);
                } else {
                    self.push(TokenKind::Bang, start);
                }
            }
            '<' => {
                self.advance();
                if !self.at_end() && self.peek() == '=' {
                    self.advance();
                    self.push(TokenKind::LtEq, start);
                } else {
                    self.push(TokenKind::Lt, start);
                }
            }
            '>' => {
                self.advance();
                if !self.at_end() && self.peek() == '>' {
                    self.advance();
                    self.push(TokenKind::GtGt, start);
                } else if !self.at_end() && self.peek() == '=' {
                    self.advance();
                    self.push(TokenKind::GtEq, start);
                } else {
                    self.push(TokenKind::Gt, start);
                }
            }
            '|' => {
                self.advance();
                if !self.at_end() && self.peek() == '>' {
                    self.advance();
                    self.push(TokenKind::PipeGt, start);
                } else if !self.at_end() && self.peek() == '|' {
                    self.advance();
                    self.push(TokenKind::Or, start);
                } else {
                    self.push(TokenKind::Pipe, start);
                }
            }
            '&' => {
                self.advance();
                if !self.at_end() && self.peek() == '&' {
                    self.advance();
                    self.push(TokenKind::And, start);
                } else {
                    self.push(TokenKind::Ampersand, start);
                }
            }
            '+' => {
                self.advance();
                self.push(TokenKind::Plus, start);
            }
            '-' => {
                self.advance();
                if !self.at_end() && self.peek() == '>' {
                    self.advance();
                    self.push(TokenKind::Arrow, start);
                } else {
                    self.push(TokenKind::Minus, start);
                }
            }
            '*' => {
                self.advance();
                self.push(TokenKind::Star, start);
            }
            '/' => {
                self.advance();
                self.push(TokenKind::Slash, start);
            }
            '%' => {
                self.advance();
                self.push(TokenKind::Percent, start);
            }
            '?' => {
                self.advance();
                self.push(TokenKind::Question, start);
            }
            '"' => self.scan_string(start),
            'r' if self.peek_at(1) == Some('"') => self.scan_raw_string(start),
            'b' if self.peek_at(1) == Some('"') => self.scan_byte_string(start),
            '0'..='9' => self.scan_number(start),
            c if c.is_ascii_uppercase() => self.scan_type_ident(start),
            c if c.is_ascii_lowercase() || c == '_' => self.scan_ident(start),
            _ => {
                self.advance();
                self.push(
                    TokenKind::Error(format!("unexpected character: '{ch}'")),
                    start,
                );
            }
        }
    }

    fn scan_ident(&mut self, start: usize) {
        while !self.at_end() && (self.peek().is_ascii_alphanumeric() || self.peek() == '_') {
            self.advance();
        }
        let text: String = self.source[start..self.pos].iter().collect();
        let kind = match text.as_str() {
            "match" => TokenKind::Match,
            "trait" => TokenKind::Trait,
            "impl" => TokenKind::Impl,
            "const" => TokenKind::Const,
            "extern" => TokenKind::Extern,
            "fsm" => TokenKind::Fsm,
            "srv" => TokenKind::Srv,
            "test" => TokenKind::Test,
            "as" => TokenKind::As,
            "true" => TokenKind::Ident("true".into()),
            "false" => TokenKind::Ident("false".into()),
            _ => TokenKind::Ident(text),
        };
        self.push(kind, start);
    }

    fn scan_type_ident(&mut self, start: usize) {
        while !self.at_end() && (self.peek().is_ascii_alphanumeric() || self.peek() == '_') {
            self.advance();
        }
        let text: String = self.source[start..self.pos].iter().collect();
        self.push(TokenKind::TypeIdent(text), start);
    }

    fn scan_number(&mut self, start: usize) {
        while !self.at_end() && (self.peek().is_ascii_digit() || self.peek() == '_') {
            self.advance();
        }
        let is_float = !self.at_end()
            && self.peek() == '.'
            && self.peek_at(1).map_or(false, |c| c.is_ascii_digit());
        if is_float {
            self.advance(); // consume '.'
            while !self.at_end() && (self.peek().is_ascii_digit() || self.peek() == '_') {
                self.advance();
            }
            let text: String = self.source[start..self.pos]
                .iter()
                .filter(|c| **c != '_')
                .collect();
            match text.parse::<f64>() {
                Ok(v) => self.push(TokenKind::Float(v), start),
                Err(_) => self.push(TokenKind::Error(format!("invalid float: {text}")), start),
            }
        } else {
            let text: String = self.source[start..self.pos]
                .iter()
                .filter(|c| **c != '_')
                .collect();
            match text.parse::<i64>() {
                Ok(v) => self.push(TokenKind::Int(v), start),
                Err(_) => self.push(TokenKind::Error(format!("invalid integer: {text}")), start),
            }
        }
    }

    fn scan_string(&mut self, start: usize) {
        self.advance(); // consume opening '"'
        let mut buf = String::new();
        let mut parts: Vec<(bool, String)> = Vec::new(); // (is_expr, content)
        let mut has_interp = false;

        while !self.at_end() && self.peek() != '"' {
            if self.peek() == '\\' {
                self.advance();
                if self.at_end() {
                    break;
                }
                match self.peek() {
                    'n' => buf.push('\n'),
                    't' => buf.push('\t'),
                    'r' => buf.push('\r'),
                    '\\' => buf.push('\\'),
                    '"' => buf.push('"'),
                    '{' => buf.push('{'),
                    '0' => buf.push('\0'),
                    'x' => {
                        self.advance();
                        let hex: String = self.take_while(|c| c.is_ascii_hexdigit(), 2);
                        if let Ok(byte) = u8::from_str_radix(&hex, 16) {
                            buf.push(byte as char);
                        }
                        continue; // already advanced past hex digits
                    }
                    c => {
                        buf.push('\\');
                        buf.push(c);
                    }
                }
                self.advance();
            } else if self.peek() == '{' {
                has_interp = true;
                if !buf.is_empty() {
                    parts.push((false, std::mem::take(&mut buf)));
                }
                self.advance(); // consume '{'
                // Gather expression text until matching '}'
                let mut depth = 1;
                let mut expr_text = String::new();
                while !self.at_end() && depth > 0 {
                    if self.peek() == '{' {
                        depth += 1;
                    } else if self.peek() == '}' {
                        depth -= 1;
                        if depth == 0 {
                            break;
                        }
                    }
                    expr_text.push(self.peek());
                    self.advance();
                }
                if !self.at_end() {
                    self.advance(); // consume closing '}'
                }
                parts.push((true, expr_text));
            } else {
                buf.push(self.peek());
                self.advance();
            }
        }
        if !self.at_end() {
            self.advance(); // consume closing '"'
        }

        if has_interp {
            if !buf.is_empty() {
                parts.push((false, buf));
            }
            // Emit StrInterpStart, then alternating Str/Ident tokens, then StrInterpEnd
            self.push(TokenKind::StrInterpStart, start);
            for (is_expr, content) in parts {
                if is_expr {
                    // Lex the expression inside interpolation
                    let inner_tokens = Lexer::new(&content).tokenize();
                    for tok in inner_tokens {
                        if tok.kind == TokenKind::Eof {
                            break;
                        }
                        // Adjust spans relative to the interpolation position
                        self.tokens.push(tok);
                    }
                } else {
                    self.tokens.push(Token {
                        kind: TokenKind::Str(content),
                        span: Span::new(start as u32, self.pos as u32),
                    });
                }
            }
            self.push(TokenKind::StrInterpEnd, start);
        } else {
            self.push(TokenKind::Str(buf), start);
        }
    }

    fn scan_byte_string(&mut self, start: usize) {
        self.advance(); // 'b'
        self.advance(); // '"'
        let mut bytes = Vec::new();
        while !self.at_end() && self.peek() != '"' {
            if self.peek() == '\\' {
                self.advance();
                if self.at_end() {
                    break;
                }
                match self.peek() {
                    'n' => bytes.push(b'\n'),
                    't' => bytes.push(b'\t'),
                    'r' => bytes.push(b'\r'),
                    '\\' => bytes.push(b'\\'),
                    '"' => bytes.push(b'"'),
                    '0' => bytes.push(0),
                    _ => {
                        bytes.push(b'\\');
                        bytes.push(self.peek() as u8);
                    }
                }
                self.advance();
            } else {
                bytes.push(self.peek() as u8);
                self.advance();
            }
        }
        if !self.at_end() {
            self.advance(); // closing '"'
        }
        self.push(TokenKind::ByteStr(bytes), start);
    }

    fn scan_raw_string(&mut self, start: usize) {
        self.advance(); // 'r'
        self.advance(); // first '"'

        // Check for triple-quote: r"""..."""
        if self.peek_at(0) == Some('"') && self.peek_at(1) == Some('"') {
            self.advance(); // second '"'
            self.advance(); // third '"'
            let mut buf = String::new();
            while !self.at_end() {
                if self.peek() == '"'
                    && self.peek_at(1) == Some('"')
                    && self.peek_at(2) == Some('"')
                {
                    self.advance(); // closing first '"'
                    self.advance(); // closing second '"'
                    self.advance(); // closing third '"'
                    break;
                }
                buf.push(self.peek());
                self.advance();
            }
            self.push(TokenKind::Str(buf), start);
            return;
        }

        // Single-quote raw string (existing behavior)
        let mut buf = String::new();
        while !self.at_end() && self.peek() != '"' {
            buf.push(self.peek());
            self.advance();
        }
        if !self.at_end() {
            self.advance(); // closing '"'
        }
        self.push(TokenKind::Str(buf), start);
    }

    fn take_while(&mut self, pred: impl Fn(char) -> bool, max: usize) -> String {
        let mut s = String::new();
        let mut count = 0;
        while !self.at_end() && pred(self.peek()) && count < max {
            s.push(self.peek());
            self.advance();
            count += 1;
        }
        s
    }

    fn skip_whitespace(&mut self) {
        while !self.at_end() && self.peek() == ' ' || (!self.at_end() && self.peek() == '\t') {
            self.advance();
        }
    }

    fn skip_comment(&mut self) {
        while !self.at_end() && self.peek() != '\n' {
            self.advance();
        }
    }

    fn push(&mut self, kind: TokenKind, start: usize) {
        self.tokens.push(Token {
            kind,
            span: Span::new(start as u32, self.pos as u32),
        });
    }

    fn peek(&self) -> char {
        self.source[self.pos]
    }

    fn peek_at(&self, offset: usize) -> Option<char> {
        self.source.get(self.pos + offset).copied()
    }

    fn advance(&mut self) -> char {
        let ch = self.source[self.pos];
        self.pos += 1;
        ch
    }

    fn at_end(&self) -> bool {
        self.pos >= self.source.len()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn lex(input: &str) -> Vec<TokenKind> {
        Lexer::new(input)
            .tokenize()
            .into_iter()
            .map(|t| t.kind)
            .collect()
    }

    #[test]
    fn test_simple_ident() {
        let tokens = lex("hello");
        assert_eq!(tokens, vec![TokenKind::Ident("hello".into()), TokenKind::Eof]);
    }

    #[test]
    fn test_number() {
        let tokens = lex("42");
        assert_eq!(tokens, vec![TokenKind::Int(42), TokenKind::Eof]);
    }

    #[test]
    fn test_float() {
        let tokens = lex("3.14");
        assert_eq!(tokens, vec![TokenKind::Float(3.14), TokenKind::Eof]);
    }

    #[test]
    fn test_string() {
        let tokens = lex("\"hello world\"");
        assert_eq!(
            tokens,
            vec![TokenKind::Str("hello world".into()), TokenKind::Eof]
        );
    }

    #[test]
    fn test_operators() {
        let tokens = lex("|> >> ? !");
        assert_eq!(
            tokens,
            vec![
                TokenKind::PipeGt,
                TokenKind::GtGt,
                TokenKind::Question,
                TokenKind::Bang,
                TokenKind::Eof,
            ]
        );
    }

    #[test]
    fn test_indentation() {
        let tokens = lex("a\n  b\n  c\nd");
        assert_eq!(
            tokens,
            vec![
                TokenKind::Ident("a".into()),
                TokenKind::Newline,
                TokenKind::Indent,
                TokenKind::Ident("b".into()),
                TokenKind::Newline,
                TokenKind::Ident("c".into()),
                TokenKind::Newline,
                TokenKind::Dedent,
                TokenKind::Ident("d".into()),
                TokenKind::Eof,
            ]
        );
    }

    #[test]
    fn test_keywords() {
        let tokens = lex("match trait impl const extern");
        assert_eq!(
            tokens,
            vec![
                TokenKind::Match,
                TokenKind::Trait,
                TokenKind::Impl,
                TokenKind::Const,
                TokenKind::Extern,
                TokenKind::Eof,
            ]
        );
    }

    #[test]
    fn test_pipe_chain() {
        let tokens = lex("x |> f |> g");
        assert_eq!(
            tokens,
            vec![
                TokenKind::Ident("x".into()),
                TokenKind::PipeGt,
                TokenKind::Ident("f".into()),
                TokenKind::PipeGt,
                TokenKind::Ident("g".into()),
                TokenKind::Eof,
            ]
        );
    }

    #[test]
    fn test_arrow_and_lambda() {
        let tokens = lex("\\x -> x + 1");
        assert_eq!(
            tokens,
            vec![
                TokenKind::Backslash,
                TokenKind::Ident("x".into()),
                TokenKind::Arrow,
                TokenKind::Ident("x".into()),
                TokenKind::Plus,
                TokenKind::Int(1),
                TokenKind::Eof,
            ]
        );
    }

    #[test]
    fn test_type_ident() {
        let tokens = lex("MyType Int I32");
        assert_eq!(
            tokens,
            vec![
                TokenKind::TypeIdent("MyType".into()),
                TokenKind::TypeIdent("Int".into()),
                TokenKind::TypeIdent("I32".into()),
                TokenKind::Eof,
            ]
        );
    }

    #[test]
    fn test_bracket_suppresses_indent() {
        let tokens = lex("f(\n  x,\n  y\n)");
        // Inside brackets, no Indent/Dedent/Newline
        assert_eq!(
            tokens,
            vec![
                TokenKind::Ident("f".into()),
                TokenKind::LParen,
                TokenKind::Ident("x".into()),
                TokenKind::Comma,
                TokenKind::Ident("y".into()),
                TokenKind::RParen,
                TokenKind::Eof,
            ]
        );
    }

    #[test]
    fn test_string_interpolation() {
        let tokens = lex("\"hello {name}\"");
        assert!(matches!(tokens[0], TokenKind::StrInterpStart));
        assert!(matches!(tokens[1], TokenKind::Str(ref s) if s == "hello "));
        assert!(matches!(tokens[2], TokenKind::Ident(ref s) if s == "name"));
        assert!(matches!(tokens[3], TokenKind::StrInterpEnd));
    }

    #[test]
    fn test_raw_string_no_escapes() {
        let tokens = lex(r#"r"hello\nworld""#);
        assert_eq!(
            tokens,
            vec![TokenKind::Str(r"hello\nworld".into()), TokenKind::Eof]
        );
    }

    #[test]
    fn test_raw_string_no_interpolation() {
        let tokens = lex(r#"r"hello {name}""#);
        assert_eq!(
            tokens,
            vec![TokenKind::Str("hello {name}".into()), TokenKind::Eof]
        );
    }

    #[test]
    fn test_raw_multiline_string() {
        let tokens = lex("r\"\"\"hello\nworld\"\"\"");
        assert_eq!(
            tokens,
            vec![TokenKind::Str("hello\nworld".into()), TokenKind::Eof]
        );
    }

    #[test]
    fn test_raw_multiline_no_escapes() {
        let tokens = lex("r\"\"\"\\n\\t\"\"\"");
        assert_eq!(
            tokens,
            vec![TokenKind::Str("\\n\\t".into()), TokenKind::Eof]
        );
    }

    #[test]
    fn test_raw_multiline_empty() {
        let tokens = lex("r\"\"\"\"\"\"");
        assert_eq!(tokens, vec![TokenKind::Str("".into()), TokenKind::Eof]);
    }

    #[test]
    fn test_raw_multiline_with_braces() {
        let tokens = lex("r\"\"\"int main() { return 0; }\"\"\"");
        assert_eq!(
            tokens,
            vec![
                TokenKind::Str("int main() { return 0; }".into()),
                TokenKind::Eof,
            ]
        );
    }

    #[test]
    fn test_raw_multiline_with_quotes() {
        let tokens = lex("r\"\"\"she said \"hi\" ok\"\"\"");
        assert_eq!(
            tokens,
            vec![
                TokenKind::Str("she said \"hi\" ok".into()),
                TokenKind::Eof,
            ]
        );
    }

    #[test]
    fn test_bare_r_is_ident() {
        let tokens = lex("r");
        assert_eq!(tokens, vec![TokenKind::Ident("r".into()), TokenKind::Eof]);
    }

    #[test]
    fn test_comment() {
        let tokens = lex("x -- this is a comment\ny");
        assert_eq!(
            tokens,
            vec![
                TokenKind::Ident("x".into()),
                TokenKind::Newline,
                TokenKind::Ident("y".into()),
                TokenKind::Eof,
            ]
        );
    }
}
