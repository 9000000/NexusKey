---
name: instantgrep
description: Use the instantgrep MCP tool (instantgrep_search) for ultra-fast, index-assisted code searching. Use when (1) looking up classes, functions, or variable definitions, (2) searching for strings or usage patterns in the NexusKey project, (3) trying to find specific lines of code without scanning files manually.
---

# instantgrep (Trigram Index Search)

`instantgrep` is a high-performance, index-assisted code search tool built specifically for this workspace. It uses a trigram index database to filter candidate files before running the regex, returning results in milliseconds.

## When to Use

1.  **Always prefer** `instantgrep_search` over generic system searches (like `grep -r`, `find`, or slow Python/Node scripts).
2.  Use it when you need to find specific definitions, imports, logs, or usage of classes/structs/functions in `NexusKey`.
3.  Use it to avoid scanning binary files, build artifacts (like `.obj` or `.pdb` inside `build/`), and files listed in `.gitignore`.

## Writing Effective Search Patterns

Because `instantgrep` operates on a **trigram index** (3-byte slices), the search pattern works best when it contains at least one **literal string segment of 3 or more characters**.

*   **Fast (Index-assisted):**
    *   `"class TypingEngine"` (contains literals `"class"` and `"TypingEngine"`)
    *   `"void Apply"` (contains literals `"void"` and `"Apply"`)
    *   `"TODO|FIXME"` (contains literal `"TODO"` and `"FIXME"`)
*   **Slow (Fallback to brute force):**
    *   `".*"` (no literals)
    *   `"[a-z]+"` (no literals)
    *   `"ab"` (literal is too short, length < 3)

## Tool Arguments

The tool name is `instantgrep_search` and expects:
*   `pattern` (string, required): The regex pattern.
*   `path` (string, required): The directory to search, usually `.`.
*   `ignoreCase` (boolean, optional): Set to `true` for case-insensitive matches.

## Efficient File Viewing After Search

When `instantgrep_search` returns matches, they are in the format `file:line:content`. 
To minimize token usage and avoid reading large files into context:
1. **Do not** view the entire file.
2. Resolve `file` to an absolute path.
3. Use the `line` returned to call `view_file` with a narrow range (e.g., `StartLine` = `line - 15`, `EndLine` = `line + 15`) to inspect the surrounding context.

