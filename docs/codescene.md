# Code Health

flowi uses CodeScene's Code Health score as its maintainability gate. The target is 10.0 for every first-party source file.

## Scoring

Score many files with the script. It takes about two seconds for the whole tree:

```sh
tools/scripts/code_health.sh                 # every first-party file, lowest first
tools/scripts/code_health.sh changed         # files that differ from origin/main
tools/scripts/code_health.sh src/ui/ui.c     # named files
tools/scripts/code_health.sh origin/main:./src/ui/ui.c   # a file as it is at a revision
```

The last form is how to get a before score. CodeScene only reads files inside the repository, so a copy of the old file in a temporary directory fails.

For the findings behind one file's score, use the `code_health_review` MCP tool. Before a commit, run `pre_commit_code_health_safeguard`. It judges the change, so also score each touched file: a change can pass the safeguard and still lower the file's score.

Both the script and the MCP tools need `CS_ACCESS_TOKEN`. If a tool answers "No access token configured", stop and report it. The `login` tool opens a browser on the owner's machine and times out when nobody is there.

## Reading a score

- 9.68 means one finding is left. It is nearly always an argument count.
- Most headers have no score. The script leaves them out.
- A score is per file. The file-level findings (lines of code, mean complexity, number of functions) only move when code leaves the file.

The thresholds for C, with flowi's overrides applied:

| Finding | Triggers at |
|---|---|
| Excess Number of Function Arguments | more than 5 arguments |
| Complex Method | cyclomatic complexity 9 |
| Complex Conditional | 2 or more branches in one condition |
| Bumpy Road Ahead | 2 nested blocks of depth 2 in one function |
| Deep, Nested Complexity | nesting depth 4 |
| Large Method | 70 lines |
| Overall Code Complexity | mean complexity 4 across the file |
| Lines of Code in a Single File | 1000 lines |
| Large Assertion Blocks | 4 consecutive asserts, in 4 or more tests of one file |
| Code Duplication | 10 lines at 75% similarity |

## Working on Code Health

A request for a Code Health pass means score and report. List the files below 10.0 with a recommendation and wait for the owner to pick. Then change one file or area per pull request.

A Code Health change keeps behaviour: the same guards, the same early returns, the same thread owning each field. Run `/code-review` on it before opening the pull request.

### Argument counts

Five arguments are allowed everywhere. For a function that takes more:

- Six, where the function mirrors a public scalar call or a fixed callback signature: add the file to one of the six-argument rule sets in `.codescene/code-health-rules.json`.
- Seven or more: put a struct in the public IDL (`api/*.def`) and have the internal function take the same type. Ask the owner first, because it changes the public API.

A struct earns its place by holding state that several steps use. A struct that a wrapper fills and its one callee unpacks is the argument list again with more code, so keep the scalars.

### Splitting files

When a file is split, the parts go in the family's directory (`src/ui/list/`, `tests/vfs/`), with the directory created if it is new. A split of a test file is checked by comparing the test names and the assertion counts before and after.

### Code that is exempt

These paths have every rule switched off in the rules file:

- `external/**`: vendored code keeps upstream's shape.
- `src/render/software/**` and `src/core/simd.h`: the software renderer's inner loops are tuned by hand for branch count and inlining. Splitting a loop or bundling its arguments for a score has cost up to 9% on real frames. A change there needs old-against-new timing on full frames, whatever its reason.

## The rules file

`.codescene/code-health-rules.json` holds the overrides. CodeScene applies only the first rule set whose path matches a file, so the sets run from specific to general and end with a catch-all. A new set goes above the catch-all and repeats the argument limit it needs.

The path globs take one level of braces. Nested braces make every score call fail.

After editing the file, run the `rules_config_validate` MCP tool, then compare all scores against a run from before the edit:

```sh
tools/scripts/code_health.sh > /tmp/before.tsv    # before the edit
tools/scripts/code_health.sh > /tmp/after.tsv     # after it
diff /tmp/before.tsv /tmp/after.tsv
```
