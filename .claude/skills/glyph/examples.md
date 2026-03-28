# Glyph Examples

Complete programs with MCP tool workflow and expected output. CLI equivalents shown as comments.

## 1. Hello World

```
mcp__glyph__init(db="hello.glyph")
mcp__glyph__put_def(db="hello.glyph", name="main", kind="fn",
  body='main = println("Hello, world")')
mcp__glyph__run(db="hello.glyph")
```
CLI: `./glyph init hello.glyph && ./glyph put hello.glyph fn -b 'main = println("Hello, world")' && ./glyph run hello.glyph`

Output: `Hello, world`

## 2. Factorial + Fibonacci (Recursion, Match)

```
mcp__glyph__init(db="math.glyph")
mcp__glyph__put_def(db="math.glyph", name="factorial", kind="fn",
  body="factorial n =
  match n
    0 -> 1
    _ -> n * factorial(n - 1)")
mcp__glyph__put_def(db="math.glyph", name="fib", kind="fn",
  body="fib n =
  match n
    0 -> 0
    1 -> 1
    _ -> fib(n - 1) + fib(n - 2)")
mcp__glyph__put_def(db="math.glyph", name="main", kind="fn",
  body='main =
  println("fact(10) = " + int_to_str(factorial(10)))
  println("fib(10) = " + int_to_str(fib(10)))')
mcp__glyph__run(db="math.glyph")
```
Output:
```
fact(10) = 3628800
fib(10) = 55
```

## 3. String Operations (Interpolation, Slicing)

```
mcp__glyph__init(db="str.glyph")
mcp__glyph__put_def(db="str.glyph", name="greet", kind="fn",
  body='greet name age =
  println("Hello, {name}! Age: {int_to_str(age)}")')
mcp__glyph__put_def(db="str.glyph", name="main", kind="fn",
  body='main =
  s = "Hello, Glyph"
  println("length: " + int_to_str(str_len(s)))
  println("first 5: " + str_slice(s, 0, 5))
  println("last 5: " + str_slice(s, 7, 12))
  greet("Alice", 30)')
mcp__glyph__run(db="str.glyph")
```
Output:
```
length: 12
first 5: Hello
last 5: Glyph
Hello, Alice! Age: 30
```

## 4. Array Processing (Push, Iterate, Sum)

```
mcp__glyph__init(db="arr.glyph")
mcp__glyph__put_def(db="arr.glyph", name="sum_rec", kind="fn",
  body="sum_rec arr i =
  match i >= array_len(arr)
    true -> 0
    _ -> arr[i] + sum_rec(arr, i + 1)")
mcp__glyph__put_def(db="arr.glyph", name="main", kind="fn",
  body='main =
  a = [10, 20, 30, 40]
  array_push(a, 50)
  println("len: " + int_to_str(array_len(a)))
  println("sum: " + int_to_str(sum_rec(a, 0)))
  println("a[2]: " + int_to_str(a[2]))')
mcp__glyph__run(db="arr.glyph")
```
Output:
```
len: 5
sum: 150
a[2]: 30
```

## 5. Enums and Pattern Matching

```
mcp__glyph__init(db="enum.glyph")
mcp__glyph__put_def(db="enum.glyph", name="Shape", kind="type",
  body="Shape = | Circle(I) | Rect(I, I)")
mcp__glyph__put_def(db="enum.glyph", name="area", kind="fn",
  body="area s =
  match s
    Circle(r) -> r * r * 3
    Rect(w, h) -> w * h")
mcp__glyph__put_def(db="enum.glyph", name="describe", kind="fn",
  body='describe s =
  match s
    Circle(r) -> "circle r=" + int_to_str(r)
    Rect(w, h) -> "rect " + int_to_str(w) + "x" + int_to_str(h)')
mcp__glyph__put_def(db="enum.glyph", name="main", kind="fn",
  body='main =
  c = Circle(5)
  r = Rect(3, 4)
  println(describe(c) + " area=" + int_to_str(area(c)))
  println(describe(r) + " area=" + int_to_str(area(r)))')
mcp__glyph__run(db="enum.glyph")
```
Output:
```
circle r=5 area=75
rect 3x4 area=12
```

## 6. Database Access (SQLite)

```
mcp__glyph__init(db="dbapp.glyph")
mcp__glyph__put_def(db="dbapp.glyph", name="main", kind="fn", body="main =
  db = glyph_db_open(\"/tmp/test_kv.db\")
  glyph_db_exec(db, \"CREATE TABLE IF NOT EXISTS kv (key TEXT, val TEXT)\")
  glyph_db_exec(db, \"DELETE FROM kv\")
  glyph_db_exec(db, \"INSERT INTO kv VALUES ('lang', 'Glyph')\")
  glyph_db_exec(db, \"INSERT INTO kv VALUES ('ver', '0.2')\")
  result = glyph_db_query_one(db, \"SELECT val FROM kv WHERE key='lang'\")
  println(\"language: \" + result)
  glyph_db_close(db)")
mcp__glyph__run(db="dbapp.glyph")
```

CLI: use `-f` with a file to avoid shell-quoting issues with SQL strings.

Output: `language: Glyph`

## 7. Tests

```
mcp__glyph__init(db="tested.glyph")
mcp__glyph__put_def(db="tested.glyph", name="add", kind="fn", body="add a b = a + b")
mcp__glyph__put_def(db="tested.glyph", name="mul", kind="fn", body="mul a b = a * b")
mcp__glyph__put_def(db="tested.glyph", name="test_add", kind="test",
  body="test_add u =
  assert_eq(add(1, 2), 3)
  assert_eq(add(0, 0), 0)
  assert_eq(add(-1, 1), 0)")
mcp__glyph__put_def(db="tested.glyph", name="test_mul", kind="test",
  body="test_mul u =
  assert_eq(mul(3, 4), 12)
  assert_eq(mul(0, 5), 0)")
mcp__glyph__put_def(db="tested.glyph", name="main", kind="fn", body="main = 0")
```
Then run tests:
```
mcp__glyph__test(db="tested.glyph")
```
CLI: `./glyph test tested.glyph`
Output:
```
PASS test_add
PASS test_mul
2/2 passed
```

## 8. Records with Named Types

```
mcp__glyph__init(db="shapes.glyph")
mcp__glyph__put_def(db="shapes.glyph", name="Point", kind="type",
  body="Point = {x: I, y: I}")
mcp__glyph__put_def(db="shapes.glyph", name="Rect", kind="type",
  body="Rect = {x: I, y: I, w: I, h: I}")
mcp__glyph__put_def(db="shapes.glyph", name="make_point", kind="fn",
  body="make_point x y = {x: x, y: y}")
mcp__glyph__put_def(db="shapes.glyph", name="rect_area", kind="fn",
  body="rect_area r = r.w * r.h")
mcp__glyph__put_def(db="shapes.glyph", name="main", kind="fn",
  body='main =
  p = make_point(10, 20)
  r = {x: 0, y: 0, w: 30, h: 40}
  println("point: " + int_to_str(p.x) + "," + int_to_str(p.y))
  println("area: " + int_to_str(rect_area(r)))')
mcp__glyph__run(db="shapes.glyph")
```
Output:
```
point: 10,20
area: 1200
```

## 9. Result Types and Error Propagation

```
mcp__glyph__init(db="calc.glyph")
mcp__glyph__put_def(db="calc.glyph", name="safe_div", kind="fn",
  body='safe_div a b =
  match b == 0
    true -> Err("division by zero")
    _ -> Ok(a / b)')
mcp__glyph__put_def(db="calc.glyph", name="compute", kind="fn",
  body="compute x =
  r = safe_div(100, x)?
  Ok(r + 1)")
mcp__glyph__put_def(db="calc.glyph", name="main", kind="fn",
  body='main =
  match compute(5)
    Ok(v) -> println("ok: " + int_to_str(v))
    Err(e) -> eprintln("err: " + e)
  match compute(0)
    Ok(v) -> println("ok: " + int_to_str(v))
    Err(e) -> eprintln("err: " + e)')
mcp__glyph__run(db="calc.glyph")
```
Output:
```
ok: 21
err: division by zero
```

`?` checks the tag — if Err, returns the whole Result to the caller; if Ok, extracts the payload.

## 10. Closures and Field Accessors

```
mcp__glyph__init(db="closure.glyph")
mcp__glyph__put_def(db="closure.glyph", name="make_adder", kind="fn",
  body="make_adder n = \x -> x + n")
mcp__glyph__put_def(db="closure.glyph", name="map_array", kind="fn",
  body="map_array arr f =
  out = []
  map_loop(arr, f, out, 0)
  out")
mcp__glyph__put_def(db="closure.glyph", name="map_loop", kind="fn",
  body="map_loop arr f out i =
  match i >= array_len(arr)
    true -> 0
    _ ->
      array_push(out, f(arr[i]))
      map_loop(arr, f, out, i + 1)")
mcp__glyph__put_def(db="closure.glyph", name="main", kind="fn",
  body='main =
  add5 = make_adder(5)
  println(int_to_str(add5(10)))    -- 15
  println(int_to_str(add5(20)))    -- 25
  nums = [1, 2, 3, 4]
  doubled = map_array(nums, \x -> x * 2)
  println(int_to_str(doubled[0]))  -- 2
  println(int_to_str(doubled[3]))  -- 8')
mcp__glyph__run(db="closure.glyph")
```
Output:
```
15
25
2
8
```

The `.field` shorthand creates a closure too: `map_array(records, .name)` extracts the `name` field from each record.

## 11. Match Guards and Or-Patterns

```
mcp__glyph__init(db="guards.glyph")
mcp__glyph__put_def(db="guards.glyph", name="classify", kind="fn",
  body='classify n =
  match n
    0 -> "zero"
    n ? n < 0 -> "negative"
    n ? n > 100 -> "huge"
    _ -> "normal"')
mcp__glyph__put_def(db="guards.glyph", name="parse_cmd", kind="fn",
  body='parse_cmd cmd =
  match cmd
    "quit" | "exit" | "q" -> "quitting"
    "help" | "?" | "h" -> "showing help"
    _ -> "unknown: " + cmd')
mcp__glyph__put_def(db="guards.glyph", name="main", kind="fn",
  body='main =
  println(classify(0))
  println(classify(-5))
  println(classify(200))
  println(classify(42))
  println(parse_cmd("quit"))
  println(parse_cmd("?"))
  println(parse_cmd("foo"))')
mcp__glyph__run(db="guards.glyph")
```
Output:
```
zero
negative
huge
normal
quitting
showing help
unknown: foo
```

## 12. Let Destructuring

```
mcp__glyph__init(db="destr.glyph")
mcp__glyph__put_def(db="destr.glyph", name="Point", kind="type",
  body="Point = {x: I, y: I}")
mcp__glyph__put_def(db="destr.glyph", name="distance", kind="fn",
  body="distance p1 p2 =
  {x, y} = p1
  {x, y} = p2
  dx = p1.x - p2.x
  dy = p1.y - p2.y
  dx * dx + dy * dy")
mcp__glyph__put_def(db="destr.glyph", name="main", kind="fn",
  body='main =
  p = {x: 3, y: 4}
  {x, y} = p
  println("x=" + int_to_str(x) + " y=" + int_to_str(y))
  d2 = distance({x: 0, y: 0}, {x: 3, y: 4})
  println("dist^2=" + int_to_str(d2))')
mcp__glyph__run(db="destr.glyph")
```
Output:
```
x=3 y=4
dist^2=25
```

## 13. Maps

```
mcp__glyph__init(db="maps.glyph")
mcp__glyph__put_def(db="maps.glyph", name="main", kind="fn",
  body='main =
  m = hm_new()
  hm_set(m, "alice", 30)
  hm_set(m, "bob", 25)
  hm_set(m, "carol", 35)
  println("len: " + int_to_str(hm_len(m)))
  println("alice: " + int_to_str(hm_get(m, "alice")))
  println("has bob: " + int_to_str(hm_has(m, "bob")))
  println("has dave: " + int_to_str(hm_has(m, "dave")))
  hm_del(m, "bob")
  println("len after del: " + int_to_str(hm_len(m)))
  keys = hm_keys(m)
  println("keys: " + int_to_str(array_len(keys)))')
mcp__glyph__run(db="maps.glyph")
```
Output:
```
len: 3
alice: 30
has bob: 1
has dave: 0
len after del: 2
keys: 2
```

Maps use `hm_new()` — there is no map literal syntax. String keys only in the current implementation. Values are stored as `I` (GVal); cast pointers with `alloc` for complex value types.

## 14. Record Updates

```
mcp__glyph__init(db="rupd.glyph")
mcp__glyph__put_def(db="rupd.glyph", name="Point", kind="type",
  body="Point = {x: I, y: I}")
mcp__glyph__put_def(db="rupd.glyph", name="main", kind="fn",
  body='main =
  p = {x: 1, y: 2}
  p2 = p{x: 10}
  println("p.x=" + int_to_str(p.x) + " p.y=" + int_to_str(p.y))
  println("p2.x=" + int_to_str(p2.x) + " p2.y=" + int_to_str(p2.y))')
mcp__glyph__run(db="rupd.glyph")
```
Output:
```
p.x=1 p.y=2
p2.x=10 p2.y=2
```

`p{x: 10}` creates a new record with `x` changed. The original `p` is unchanged.

## 15. Pipe and Compose

```
mcp__glyph__init(db="pipe.glyph")
mcp__glyph__put_def(db="pipe.glyph", name="double", kind="fn", body="double x = x * 2")
mcp__glyph__put_def(db="pipe.glyph", name="add1", kind="fn", body="add1 x = x + 1")
mcp__glyph__put_def(db="pipe.glyph", name="main", kind="fn",
  body='main =
  result = 5 |> double |> add1
  println(int_to_str(result))
  transform = double >> add1
  println(int_to_str(transform(5)))')
mcp__glyph__run(db="pipe.glyph")
```
Output:
```
11
11
```

`|>` pipes the left value into the right function. `>>` composes two functions left-to-right.

## 16. Using Libraries

```
mcp__glyph__init(db="libex.glyph")
mcp__glyph__put_def(db="libex.glyph", name="main", kind="fn",
  body='main =
  nums = [3, 1, 4, 1, 5, 9, 2, 6]
  sorted = sort(\a b -> a - b, nums)
  println("sorted: " + join(", ", map(sorted, \x -> int_to_str(x))))
  evens = filter(nums, \x -> x % 2 == 0)
  println("evens: " + join(", ", map(evens, \x -> int_to_str(x))))
  total = fold(nums, 0, \acc x -> acc + x)
  println("sum: " + int_to_str(total))')
```
Then register stdlib and run:
```
mcp__glyph__use(db="libex.glyph", lib="libraries/stdlib.glyph")
mcp__glyph__run(db="libex.glyph")
```
Output:
```
sorted: 1, 1, 2, 3, 4, 5, 6, 9
evens: 4, 2, 6
sum: 31
```

## 17. Web API (CRUD)

Requires `web.glyph`, `json.glyph`, `stdlib.glyph`, and `network.glyph` libraries.

```
mcp__glyph__init(db="api.glyph")
mcp__glyph__put_def(db="api.glyph", name="handle_list", kind="fn",
  body='handle_list req =
  items = web_collection_list("items")
  web_ok(req, "[" + join(",", items) + "]")')
mcp__glyph__put_def(db="api.glyph", name="handle_create", kind="fn",
  body='handle_create req =
  id = web_next_id(0)
  item = json_set(req.wbody, "id", int_to_str(id))
  web_collection_add("items", item)
  web_ok(req, item)')
mcp__glyph__put_def(db="api.glyph", name="routes", kind="fn",
  body='routes u = [
  web_get("/items", \req -> handle_list(req)),
  web_post("/items", \req -> handle_create(req))]')
mcp__glyph__put_def(db="api.glyph", name="main", kind="fn",
  body='main =
  handler = web_log(web_app(routes(0)))
  web_serve(web_default_config(0), handler)')
```
Then register libraries and build:
```
mcp__glyph__use(db="api.glyph", lib="libraries/stdlib.glyph")
mcp__glyph__use(db="api.glyph", lib="libraries/json.glyph")
mcp__glyph__use(db="api.glyph", lib="libraries/network.glyph")
mcp__glyph__use(db="api.glyph", lib="libraries/web.glyph")
mcp__glyph__build(db="api.glyph")
```

**Critical pitfall:** Route handlers MUST be wrapped in lambdas (`\req -> handler(req)`), not passed as raw references (`handle_list`). Raw function references crash due to the closure calling convention.
