# json_util module

`json_util.c` provides JSON-C helpers used by transactions.

- `json_deep_copy_object()` creates an independent tree.
- `json_deep_merge()` recursively replaces scalar values and merges objects.

Arrays are treated as complete values rather than element-wise patches. Deltas are always validated after merging into a complete configuration.
