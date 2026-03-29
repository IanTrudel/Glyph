# XML Library Feasibility Assessment

## Verdict: Feasible

Glyph can support a useful XML library today. The stdlib, scan, and JSON libraries provide the right primitives and a proven architecture pattern. The main constraints are byte-oriented strings (no native Unicode) and the pool-based node model (no direct recursive types at MIR level), but both are manageable — the JSON library already solves the same problems.

## What XML Parsing Requires

An XML library serving the stated use cases (generic XML, Excel XML/SpreadsheetML, SVG) needs:

| Capability | XML Need | Excel XML | SVG |
|---|---|---|---|
| Tag parsing (`<tag>`, `</tag>`, `<tag/>`) | Core | Core | Core |
| Attribute parsing (`key="value"`) | Core | Heavy (namespaces, styles) | Heavy (transforms, paths) |
| Text content extraction | Core | Cell values | Text elements |
| Nested element trees | Core | Workbook→Sheet→Row→Cell | Groups, nested transforms |
| Namespace prefixes (`ss:`, `svg:`) | Optional | Required (`ss:`, `x:`, `o:`) | Optional (`xlink:`) |
| CDATA sections (`<![CDATA[...]]>`) | Needed | Sometimes | Rare |
| Entity decoding (`&lt;`, `&amp;`, `&#xNN;`) | Core | Core | Core |
| Processing instructions (`<?xml ...?>`) | Needed | Required (declaration) | Required |
| Comments (`<!-- ... -->`) | Needed | Present | Present |
| DTD/Schema validation | Not needed | Not needed | Not needed |
| Streaming/SAX-style events | Nice to have | Useful for large sheets | Not needed |

## What Glyph Already Provides

### Direct match: JSON library architecture (59 fn, 22 test)

The JSON library establishes the exact pattern an XML library would follow:

```
json_decode(src) → {jd_pool: [JNode], jd_root: I}
```

- **Pool-based tree**: Nodes stored in a flat `[JNode]` array, children referenced by index. Avoids recursive type limitations at the MIR level entirely.
- **JNode record**: `{tag:I, nval:I, sval:S, items:[I], keys:[S]}` — a uniform node type where `tag` discriminates meaning and fields carry variant-specific data.
- **Two-phase parse**: `json_tokenize(src) → [Token]`, then `json_parse(src, tokens, pos, pool) → {node:I, pos:I}`. Tokenizer handles lexical details; parser handles structure.
- **Builder API**: `jb_obj()`, `jb_push()`, `jb_put()` for constructing JSON programmatically.
- **Generator**: `json_gen(pool, idx) → S` for serialization back to text.

An XML library can reuse this architecture directly, substituting XML token types for JSON ones.

### Direct match: scan library combinators

| Scanner | XML Use |
|---|---|
| `sc_literal(s, pos)` | Match `<`, `</`, `/>`, `?>`, `<!` |
| `sc_ident(s, pos)` | Tag names, attribute names |
| `sc_quoted(s, pos)` | Attribute values (double-quoted) |
| `sc_upto(cs, s, pos)` | Text content (scan to next `<`) |
| `sc_skip_ws(s, pos)` | Whitespace between attributes |
| `sc_take0(cs, s, pos)` | Consume name characters |
| `sc_or(a, b, s, pos)` | Self-closing vs open tag |
| `sc_map(sc, f, s, pos)` | Transform parsed values |
| `sc_sep_by(item, sep, s, pos)` | Attribute lists (whitespace-separated) |
| `scan_bal(open, close, s, pos)` | Balanced delimiters (not directly applicable to XML nesting, but useful for CDATA brackets) |

### Direct match: stdlib string operations

| Function | XML Use |
|---|---|
| `str_index_of(h, n)` | Find `<`, `>`, `</` boundaries |
| `str_slice(s, a, b)` | Extract tag names, text content, attribute values |
| `str_split(s, sep)` | Namespace splitting (`ss:Cell` → `["ss", "Cell"]`) |
| `starts_with(s, p)` | Detect `<!DOCTYPE`, `<?xml`, `<!--` |
| `contains(h, n)` | Search within attribute values |
| `str_replace(s, old, new)` | Entity decoding chain |
| `str_trim(s)` | Normalize whitespace in text nodes |
| `sb_new/sb_append/sb_build` | Efficient XML generation |
| `is_alpha/is_digit/is_alnum` | Name character validation |

### Direct match: runtime hash maps

```glyph
attrs = hm_new()
_ = hm_set(attrs, "class", "header")
_ = hm_set(attrs, "id", "main")
val = hm_get(attrs, "class")  -- returns "header"
```

Hash maps store string keys natively. For XML attributes, values would be stored as pool indices or string values depending on the API design.

**Limitation**: `hm_get` returns `I` (GVal), not `S`. Attribute values are strings, so they'd need to be stored as GVal string pointers (which they already are at the C level — GVal is `intptr_t`, and Glyph strings are pointer-based). This works without conversion.

## Proposed Architecture

### Node type

```glyph
XNode = {xn_tag: I, xn_name: S, xn_ns: S, xn_text: S,
         xn_attrs: I, xn_children: [I]}
```

| `xn_tag` | Meaning | Fields used |
|---|---|---|
| 1 | Element | `xn_name`, `xn_ns`, `xn_attrs` (hm handle), `xn_children` (pool indices) |
| 2 | Text | `xn_text` |
| 3 | CDATA | `xn_text` |
| 4 | Comment | `xn_text` |
| 5 | Processing Instruction | `xn_name` (target), `xn_text` (data) |
| 6 | Document | `xn_children` (root elements + PIs + comments) |

### Parse result

```glyph
xml_decode src =
  pool = []
  tokens = xml_tokenize(src)
  result = xml_parse(tokens, 0, pool)
  {xd_pool: pool, xd_root: result.node}
```

### Token types

```glyph
xt_lt       = 1   -- <
xt_lt_slash = 2   -- </
xt_gt       = 3   -- >
xt_slash_gt = 4   -- />
xt_eq       = 5   -- =
xt_name     = 6   -- tag/attr names (sval carries string)
xt_string   = 7   -- quoted attribute values
xt_text     = 8   -- text between tags
xt_comment  = 9   -- <!-- ... -->
xt_cdata    = 10  -- <![CDATA[ ... ]]>
xt_pi       = 11  -- <?target data?>
xt_eof      = 12
```

### API surface (estimated ~80-100 definitions)

**Parsing (core):**
```
xml_decode        : S -> {xd_pool:[XNode], xd_root:I}
xml_tokenize      : S -> [Token]
xml_parse         : [Token] -> I -> [XNode] -> {node:I, pos:I}
xml_parse_element : [Token] -> I -> [XNode] -> {node:I, pos:I}
xml_parse_attrs   : [Token] -> I -> {attrs:I, pos:I}
xml_parse_children: [Token] -> I -> S -> [XNode] -> {children:[I], pos:I}
xml_entity_decode : S -> S
xml_entity_encode : S -> S
```

**Navigation:**
```
xml_tag           : [XNode] -> I -> S          -- element tag name
xml_ns            : [XNode] -> I -> S          -- namespace prefix
xml_text          : [XNode] -> I -> S          -- text content (direct)
xml_text_content  : [XNode] -> I -> S          -- all descendant text concatenated
xml_attr          : [XNode] -> I -> S -> S     -- get attribute by name
xml_has_attr      : [XNode] -> I -> S -> B     -- attribute exists?
xml_attrs         : [XNode] -> I -> I          -- get attrs hashmap handle
xml_children      : [XNode] -> I -> [I]        -- child node indices
xml_child_elements: [XNode] -> I -> [I]        -- child elements only (skip text/comments)
```

**Query:**
```
xml_find          : [XNode] -> I -> S -> [I]        -- find elements by tag name (recursive)
xml_find_first    : [XNode] -> I -> S -> I           -- first match or -1
xml_find_by_attr  : [XNode] -> I -> S -> S -> [I]   -- elements where attr=value
xml_select        : [XNode] -> I -> (XNode -> B) -> [I]  -- filter by predicate
```

**Generation:**
```
xml_encode        : [XNode] -> I -> S          -- serialize to XML string
xml_encode_pretty : [XNode] -> I -> I -> S     -- with indentation
xb_element        : S -> I                     -- builder: create element
xb_set_attr       : I -> S -> S -> I           -- builder: set attribute
xb_add_child      : I -> I -> I                -- builder: append child
xb_text           : S -> I                     -- builder: create text node
xb_comment        : S -> I                     -- builder: create comment
```

**Excel XML convenience (optional layer):**
```
xlsx_worksheets   : [XNode] -> I -> [I]        -- find all Worksheet elements
xlsx_rows         : [XNode] -> I -> [I]        -- rows in a worksheet
xlsx_cells        : [XNode] -> I -> [I]        -- cells in a row
xlsx_cell_value   : [XNode] -> I -> S          -- text content of a cell
xlsx_cell_type    : [XNode] -> I -> S          -- ss:Type attribute
xlsx_cell_index   : [XNode] -> I -> I          -- ss:Index attribute (1-based)
```

**SVG convenience (optional layer):**
```
svg_elements      : [XNode] -> I -> S -> [I]   -- find SVG elements by tag
svg_attr_float    : [XNode] -> I -> S -> F     -- numeric attribute
svg_viewbox       : [XNode] -> I -> {x:F, y:F, w:F, h:F}
svg_path_data     : [XNode] -> I -> S          -- d attribute of <path>
```

## Gap Analysis

### No gaps (ready now)

| Requirement | Solution |
|---|---|
| Tokenization | scan.glyph combinators + custom `xml_tok_*` functions |
| Recursive tree | Pool-based `[XNode]` array (JSON pattern) |
| Attributes | `hm_new/hm_set/hm_get` runtime hash maps |
| Text extraction | `str_slice`, `str_index_of`, `sc_upto` |
| Entity decoding | Chain of `str_replace` calls (5 standard entities) |
| String building | `sb_new/sb_append/sb_build` for generation |
| File I/O | `read_file` / `write_file` |
| Namespaces | `str_split(name, ":")` or `str_index_of(name, ":")` + `str_slice` |

### Minor gaps (workarounds exist)

| Gap | Impact | Workaround |
|---|---|---|
| No single-quoted attribute values | Some XML uses `attr='value'` | Extend tokenizer to handle `'` as delimiter alongside `"` |
| `sc_quoted` only handles `"..."` | Same as above | Write `xml_scan_quoted` that accepts both quote chars |
| No `str_index_of` from offset | Scanning for `]]>` in CDATA needs offset search | Use `str_slice` + `str_index_of` on substring, add offset back |
| Hash map values are GVal, not typed | Attribute values are strings but stored as GVal | Works transparently — Glyph strings are GVal-sized pointers |
| No character reference decoding (`&#123;`, `&#x7B;`) | Numeric entity references | Write `xml_decode_charref` using `str_to_int` / hex parsing |
| `scan_bal` works on single bytes, not multi-char delimiters | Can't use for `<!-- -->` or `<![CDATA[ ]]>` | Write dedicated `xml_scan_comment` and `xml_scan_cdata` |

### Not feasible (and not needed)

| Feature | Why not | Impact |
|---|---|---|
| Full DTD validation | Would require a DTD parser, entity expansion engine, content model validator — enormous scope | Not needed for the stated use cases |
| XPath/XQuery | Full query language is a project unto itself | Simple `xml_find` / `xml_find_by_attr` covers practical needs |
| XSLT transformation | Turing-complete transform language | Out of scope |
| XML Schema (XSD) validation | Complex type system mapping | Not needed |
| Streaming/SAX for gigabyte files | Glyph's string model loads full content into memory | Files up to ~50-100MB work fine; Excel XML files are typically <10MB |
| Full Unicode normalization | Byte-oriented strings | ASCII XML (the vast majority of machine-generated XML) works perfectly; UTF-8 passes through unmodified for display purposes |

## Use Case Deep Dives

### Generic XML

Fully supported. The core library handles elements, attributes, text, CDATA, comments, and processing instructions. Entity encoding/decoding covers the 5 standard entities (`&lt;`, `&gt;`, `&amp;`, `&quot;`, `&apos;`) plus numeric character references.

Example usage:
```glyph
main =
  src = read_file("config.xml")
  doc = xml_decode(src)
  pool = doc.xd_pool
  root = doc.xd_root
  servers = xml_find(pool, root, "server")
  each(\idx ->
    name = xml_attr(pool, idx, "name")
    port = xml_attr(pool, idx, "port")
    println("{name}:{port}")
  , servers)
```

### Excel XML (SpreadsheetML)

Excel's XML Spreadsheet format (`.xml`, not `.xlsx` which is a ZIP container) uses namespaced elements:

```xml
<?xml version="1.0"?>
<Workbook xmlns="urn:schemas-microsoft-com:office:spreadsheet"
          xmlns:ss="urn:schemas-microsoft-com:office:spreadsheet">
  <Worksheet ss:Name="Sheet1">
    <Table>
      <Row>
        <Cell><Data ss:Type="String">Name</Data></Cell>
        <Cell><Data ss:Type="Number">42</Data></Cell>
      </Row>
    </Table>
  </Worksheet>
</Workbook>
```

**Feasibility**: Fully supported. Namespace prefixes are just string prefixes on tag/attribute names — `xml_ns` splits on `:`. The convenience layer (`xlsx_*` functions) would wrap navigation patterns specific to this schema.

**Limitation**: `.xlsx` files (Office Open XML) are ZIP archives containing multiple XML files. Glyph has no ZIP decompression. For `.xlsx` support, either:
- Use a C FFI binding to `libzip` or `miniz`
- Pre-extract with `glyph_system("unzip ...")`
- Focus on the plain XML Spreadsheet format (which Excel can save as)

### SVG

SVG documents are well-formed XML with graphical semantics:

```xml
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100">
  <circle cx="50" cy="50" r="40" fill="red"/>
  <text x="50" y="55" text-anchor="middle">Hello</text>
</svg>
```

**Feasibility**: Fully supported for parsing and querying. The convenience layer would add attribute accessors that parse numeric values (`svg_attr_float`) and structured attributes (`svg_viewbox` parsing `"0 0 100 100"` into a record).

**Limitation**: SVG path data (`d="M 10 10 L 90 90 Z"`) is a mini-language requiring its own parser. This is achievable with scan.glyph combinators but would add ~15-20 definitions for a path command parser.

## Implementation Estimate

| Component | Definitions | Complexity |
|---|---|---|
| Token types + helpers | ~15 | Low — constants and constructors |
| Tokenizer (`xml_tok_*`) | ~15 | Medium — must handle `<`, `</`, `/>`, `<?`, `<!--`, `<![CDATA[`, quoted strings, text content |
| Entity codec | ~8 | Low — `str_replace` chains + charref parser |
| Parser core | ~15 | Medium — recursive descent, follows JSON pattern exactly |
| Navigation API | ~10 | Low — index into pool, hash map lookups |
| Query functions | ~8 | Low-Medium — recursive tree traversal |
| Generation/serialization | ~12 | Medium — escape handling, indentation |
| Builder API | ~8 | Low — pool manipulation |
| Tests | ~25-30 | Standard — one per public function + integration tests |
| **Subtotal (core)** | **~90-95 fn + 25-30 test** | |
| Excel XML convenience | ~10 | Low — wrapper functions |
| SVG convenience | ~10 | Low-Medium — numeric parsing |
| SVG path parser | ~15-20 | Medium — mini scanner for path commands |
| **Grand total** | **~125-155 definitions** | |

For reference: `json.glyph` is 59 fn + 22 test = 81 definitions. XML is roughly 1.5-2x that complexity, which is reasonable given XML's greater syntactic variety (namespaces, CDATA, PIs, comments, entity references vs JSON's minimal syntax).

## Recommended Implementation Order

1. **Core tokenizer + parser** (~30 fn, ~15 test) — get `xml_decode` working on simple well-formed XML. Validate with a round-trip test: parse → generate → parse again.

2. **Navigation + query API** (~18 fn, ~8 test) — `xml_tag`, `xml_attr`, `xml_children`, `xml_find`, `xml_text_content`. This makes the library immediately useful.

3. **Generation + builder** (~20 fn, ~7 test) — `xml_encode`, `xb_element`, `xb_set_attr`. Enables creating XML programmatically.

4. **Excel XML convenience** (~10 fn, ~5 test) — thin wrappers for SpreadsheetML navigation patterns.

5. **SVG convenience** (~10 fn, ~5 test) — attribute parsing, viewBox, basic element queries.

6. **SVG path parser** (~15-20 fn, ~5 test) — optional, only if path manipulation is needed.

## Risk Assessment

| Risk | Likelihood | Mitigation |
|---|---|---|
| Deep nesting causes stack overflow (recursive parsing) | Low — TCO handles tail calls; XML nesting >100 levels is rare | Iterative parsing with explicit stack if needed |
| Large files exhaust memory | Low — 50MB XML ≈ 50MB string + ~2x for pool/tokens | Accept size limit; document it |
| Malformed XML causes infinite loop | Medium | Defensive position-advancement checks in tokenizer (if pos doesn't advance, bail with error) |
| Namespace complexity grows beyond simple prefix splitting | Low for stated use cases | Full namespace resolution (xmlns inheritance) would add ~10-15 more definitions if ever needed |
| Performance bottleneck in entity decoding | Low — chained `str_replace` is O(n) per entity × 5 entities | Use single-pass scanner if profiling shows a problem |

## Conclusion

The XML library is **well within Glyph's current capabilities**. The JSON library proves the architecture works. The scan library provides the lexical primitives. The stdlib provides the string manipulation. The runtime provides hash maps for attributes. The main work is writing the XML-specific tokenizer and parser logic (~90 core definitions), which follows directly from the JSON library pattern with more token types and namespace awareness.

The optional Excel XML and SVG layers add practical value with modest additional effort. The entire library could ship as `libraries/xml.glyph` alongside the existing JSON and scan libraries, using the same `glyph link` mechanism for distribution.
