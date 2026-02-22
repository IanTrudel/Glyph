# Glyph Examples

Complete programs with CLI workflow and expected output.

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
