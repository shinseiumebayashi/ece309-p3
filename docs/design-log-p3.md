# Project 3 Design Log
**Author:** <your name> · **P2 code used:** instructor reference (`-DUSE_P2_REFERENCE=ON`); my P2 files were not copied into this starter.
**Tools used:** Claude (Anthropic AI assistant) — drafted `tool_registry`, `workspace.cpp`, `calculator_eval.cpp`, the test suite and a first draft of this log. I reviewed, built and ran everything on Linux with ASan + UBSan and `-DMH_WERROR=ON`. <edit this to describe exactly how you used it>

## 1. Hash table

**Collision strategy: separate chaining.** Each bucket is a `std::vector<Entry>`, where an `Entry` holds the tool's name, its full 64-bit hash, and the `std::unique_ptr<Tool>`. I chose chaining because it degrades gracefully (a cluster slows one bucket, not every probe sequence through it) and allows a load factor up to 1.0, whereas linear probing needs a threshold below 1 or `find` on a missing key never terminates. Caching the name and hash means lookups compare hashes before strings, never call the virtual `name()`, and a rehash reuses the cached hash.

**Hash function: FNV-1a (64-bit).** It XORs in every byte and then multiplies by the FNV prime, so it uses every character and is order-sensitive (`"ab"` ≠ `"ba"`). `hash()` returns the full-width value; the index is computed separately as `hash & (bucket_count - 1)`, which is valid because the bucket count is always a power of two (8, 16, 32, …).

**Threshold and resize policy.** Maximum load factor 1.0. Before inserting, `add()` checks whether `(size + 1) / bucket_count` would exceed 1.0; if so it doubles the bucket count and re-inserts every entry at its new index, then inserts. Checking *before* the insert is what guarantees `load_factor() <= 1.0` whenever `add()` returns. A duplicate is detected before the resize check, so a rejected add never grows the table. With a good hash the expected chain length is about α ≤ 1, so lookups stay O(1) with less memory than a lower threshold.

**Measurements (10,000 names `tool_0` … `tool_9999`):**

| Hash | bucket_count | load_factor | longest chain |
|---|---|---|---|
| FNV-1a (my table) | 16,384 | 0.6104 | **6** |
| djb2 (same buckets, for comparison) | 16,384 | — | 4 |
| `s[0] + s.size()` (weak) | 16,384 | — | **9,000** (only 4 buckets used) |

The weak hash has only four possible values here (every name starts with `t`; lengths are 6–9), so 9,000 names share one bucket and lookup becomes linear.

**Why `add()` is amortized O(1).** A resize from *n* to *2n* buckets costs O(n), but it only happens after the table has filled from *n/2* to *n* entries, i.e. after about *n/2* cheap O(1) inserts. Charging the O(n) resize to those *n/2* inserts adds a constant to each, so the average cost per `add()` stays O(1).

## 2. Pointer stability

The buckets store `std::unique_ptr<Tool>`, never `Tool` objects. Each tool is allocated once on the heap when it is created. During a rehash, entries are *moved* into the new bucket array: moving a `unique_ptr` copies a pointer value and nulls the source, but the `Tool` it points to never moves. So a `Tool*` returned by `find()` keeps pointing at the same live object for the lifetime of the registry. The test `reg_10000_tools` checks this: the pointer for `tool_0` taken after the first insert is still what `find("tool_0")` returns after 11 resizes. `reg_ownership_each_tool_destroyed_once` confirms every tool is destroyed exactly once.

## 3. Calculator trace: `2 * (3 + 4) - 5`

1. `2` → push onto values: `[2]`, ops `[]`.
2. `*` → ops is empty, so nothing to apply; push: ops `[*]`.
3. `(` → push: ops `[*, (]`.
4. `3` → values `[2, 3]`.
5. `+` → top of ops is `(`, which is not an operator, so stop; push: ops `[*, (, +]`.
6. `4` → values `[2, 3, 4]`.
7. `)` → apply until `(`: pop `+`, right = 4, left = 3, push 7 → values `[2, 7]`; then pop the `(` → ops `[*]`.
8. `-` → top `*` has precedence 2 ≥ 1, so apply: right = 7, left = 2, push 14 → values `[14]`, ops `[]`; then push `-` → ops `[-]`.
9. `5` → values `[14, 5]`.
10. End → apply `-`: right = 5, left = 14 → values `[9]`. Answer **9**.

## 4. Errors as values

In an agent loop, a failing tool is an expected outcome, not a bug: the model asks for a file that doesn't exist, tries `../etc/passwd`, or divides by zero. The right response is to tell the model what went wrong so it can try something else. Returning `ToolResult::failure(...)` (or `false` plus a reason) makes that path explicit in the signature and maps directly onto the `"ok":false` JSON the model reads. Exceptions would unwind across the tool/dispatcher boundary, are easy to forget to catch, and make a normal event look like a crash. Exceptions are still right for programmer errors — calling `as_string()` on a JSON number is a bug in C++ code that should fail loudly — and the dispatcher's catch-all is only a last line of defense.

## 5. What I would design differently

I would make the maximum load factor a constructor parameter so the chaining/probing trade-off could be measured from the tests instead of by editing a constant. I would also replace the vector-per-bucket layout with one contiguous entry array to cut thousands of small allocations.
