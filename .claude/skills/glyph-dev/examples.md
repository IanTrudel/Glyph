# Glyph Examples

Complete programs with CLI workflow and expected output. Examples 1-7 use anonymous records. Example 8 shows named record types. Example 9 shows Result types with error propagation.

## 1. Hello World

```bash
./glyph init hello.glyph
./glyph put hello.glyph fn -b 'main = println("Hello, world")'
./glyph run hello.glyph
```
Output: `Hello, world`

## 2. Factorial + Fibonacci (Recursion, Match)

```bash
./glyph init math.glyph

./glyph put math.glyph fn -b 'factorial n =
  match n
    0 -> 1
    _ -> n * factorial(n - 1)'

./glyph put math.glyph fn -b 'fib n =
  match n
    0 -> 0
    1 -> 1
    _ -> fib(n - 1) + fib(n - 2)'

./glyph put math.glyph fn -b 'main =
  println("fact(10) = " + int_to_str(factorial(10)))
  println("fib(10) = " + int_to_str(fib(10)))'

./glyph run math.glyph
```
Output:
```
fact(10) = 3628800
fib(10) = 55
```

## 3. String Operations (Interpolation, Slicing)

```bash
./glyph init str.glyph

./glyph put str.glyph fn -b 'greet name age =
  println("Hello, {name}! Age: {int_to_str(age)}")'

./glyph put str.glyph fn -b 'main =
  s = "Hello, Glyph"
  println("length: " + int_to_str(str_len(s)))
  println("first 5: " + str_slice(s, 0, 5))
  println("last 5: " + str_slice(s, 7, 12))
  greet("Alice", 30)'

./glyph run str.glyph
```
Output:
```
length: 12
first 5: Hello
last 5: Glyph
Hello, Alice! Age: 30
```

## 4. Array Processing (Push, Iterate, Sum)

```bash
./glyph init arr.glyph

./glyph put arr.glyph fn -b 'sum_rec arr i =
  match i >= array_len(arr)
    true -> 0
    false -> arr[i] + sum_rec(arr, i + 1)'

./glyph put arr.glyph fn -b 'main =
  a = [10, 20, 30, 40]
  array_push(a, 50)
  println("len: " + int_to_str(array_len(a)))
  println("sum: " + int_to_str(sum_rec(a, 0)))
  println("a[2]: " + int_to_str(a[2]))'

./glyph run arr.glyph
```
Output:
```
len: 5
sum: 150
a[2]: 30
```

## 5. Enums and Pattern Matching

```bash
./glyph init enum.glyph

./glyph put enum.glyph type -b 'Shape = | Circle(I) | Rect(I, I)'

./glyph put enum.glyph fn -b 'area s =
  match s
    Circle(r) -> r * r * 3
    Rect(w, h) -> w * h'

./glyph put enum.glyph fn -b 'describe s =
  match s
    Circle(r) -> "circle r=" + int_to_str(r)
    Rect(w, h) -> "rect " + int_to_str(w) + "x" + int_to_str(h)'

./glyph put enum.glyph fn -b 'main =
  c = Circle(5)
  r = Rect(3, 4)
  println(describe(c) + " area=" + int_to_str(area(c)))
  println(describe(r) + " area=" + int_to_str(area(r)))'

./glyph run enum.glyph
```
Output:
```
circle r=5 area=75
rect 3x4 area=12
```

## 6. Database Access (SQLite, using -f for shell escaping)

When definitions contain single quotes (common with SQL), use `-f` with a file:

```bash
./glyph init dbapp.glyph

cat > /tmp/dbmain.gl << 'EOF'
main =
  db = glyph_db_open("/tmp/test_kv.db")
  glyph_db_exec(db, "CREATE TABLE IF NOT EXISTS kv (key TEXT, val TEXT)")
  glyph_db_exec(db, "DELETE FROM kv")
  glyph_db_exec(db, "INSERT INTO kv VALUES ('lang', 'Glyph')")
  glyph_db_exec(db, "INSERT INTO kv VALUES ('ver', '0.1')")
  result = glyph_db_query_one(db, "SELECT val FROM kv WHERE key='lang'")
  println("language: " + result)
  glyph_db_close(db)
EOF
./glyph put dbapp.glyph fn -f /tmp/dbmain.gl

./glyph run dbapp.glyph
```
Output: `language: Glyph`

## 7. Tests

```bash
./glyph init tested.glyph

./glyph put tested.glyph fn -b 'add a b = a + b'
./glyph put tested.glyph fn -b 'mul a b = a * b'

./glyph put tested.glyph test -b 'test_add =
  assert_eq(add(1, 2), 3)
  assert_eq(add(0, 0), 0)
  assert_eq(add(-1, 1), 0)'

./glyph put tested.glyph test -b 'test_mul =
  assert_eq(mul(3, 4), 12)
  assert_eq(mul(0, 5), 0)'

./glyph test tested.glyph
```
Output:
```
PASS test_add
PASS test_mul
2/2 passed
```

## 8. Records with Named Types

Named record types generate `typedef struct` in C (gen=2 struct codegen). Field access uses `->field` instead of offset-based indexing.

```bash
./glyph init shapes.glyph

# Define named record types
./glyph put shapes.glyph type -b 'Point = {x: I, y: I}'
./glyph put shapes.glyph type -b 'Rect = {x: I, y: I, w: I, h: I}'

# Functions that create and use records
./glyph put shapes.glyph fn -b 'make_point x y = {x: x, y: y}'
./glyph put shapes.glyph fn -b 'rect_area r = r.w * r.h'

./glyph put shapes.glyph fn -b 'main =
  p = make_point(10, 20)
  r = {x: 0, y: 0, w: 30, h: 40}
  println("point: " + int_to_str(p.x) + "," + int_to_str(p.y))
  println("area: " + int_to_str(rect_area(r)))'

./glyph run shapes.glyph
```
Output:
```
point: 10,20
area: 1200
```

The compiler matches record aggregates `{x: ..., y: ...}` against type definitions by sorted field set. `Point` matches `{x, y}`, `Rect` matches `{h, w, x, y}`. In generated C:
- `Glyph_Point` struct with `->x`, `->y` access
- `Glyph_Rect` struct with `->h`, `->w`, `->x`, `->y` access

## 9. Result Types and Error Propagation

```bash
./glyph init calc.glyph

./glyph put calc.glyph fn -b 'safe_div a b =
  match b == 0
    true -> Err("division by zero")
    _ -> Ok(a / b)'

# ? propagates Err, extracts Ok payload
./glyph put calc.glyph fn -b 'compute x =
  r = safe_div(100, x)?
  Ok(r + 1)'

# ! unwraps Ok or panics on Err
./glyph put calc.glyph fn -b 'force_div a b = safe_div(a, b)!'

./glyph put calc.glyph fn -b 'main =
  match compute(5)
    Ok(v) -> println("ok: " + int_to_str(v))
    Err(e) -> eprintln("err: " + e)
  match compute(0)
    Ok(v) -> println("ok: " + int_to_str(v))
    Err(e) -> eprintln("err: " + e)
  println("forced: " + int_to_str(force_div(10, 2)))'

./glyph run calc.glyph
```
Output:
```
ok: 21
err: division by zero
forced: 5
```

`Ok(val)` and `Err(msg)` are built-in enum constructors. `?` checks the tag — if Err, returns the whole Result to the caller; if Ok, extracts the payload. `!` extracts Ok or panics with "unwrap failed".
