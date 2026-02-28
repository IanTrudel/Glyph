# Unification Algorithm

Standard Hindley-Milner unification with union-find.

```
unify(a, b):
  a' = walk(a)        -- resolve through substitution
  b' = walk(b)
  if a' == b': done
  if a' is Var: bind(a', b')
  if b' is Var: bind(b', a')
  if both are Fn: unify(param_a, param_b) && unify(ret_a, ret_b)
  if both are Array: unify(elem_a, elem_b)
  if both are Record: unify_rows(a', b')
  if both same primitive: done
  else: type error
```

## Row Unification

For two record types `{f1:T1, f2:T2 ..r1}` and `{f2:U2, f3:U3 ..r2}`:

1. **Common fields** (`f2`): unify `T2` with `U2`
2. **Left-only fields** (`f1`): must be absorbed by `r2`
3. **Right-only fields** (`f3`): must be absorbed by `r1`
4. **Both open**: Create fresh `r3`. Bind `r1 = {f3:U3 ..r3}` and `r2 = {f1:T1 ..r3}`
5. **One open, one closed**: The open side's variable gets the missing fields (closed)
6. **Both closed**: All fields must match exactly
