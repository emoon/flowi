# How to document the API

The `///` comments in `api/*.def` are the public reference. api_gen copies them into the C headers and the
Rust bindings, so they are what a person sees in their editor when they call flowi.

Write each comment for that person. They want to do something with the call in front of them and need to know
what to pass, what comes back and what to do with it. They never see the `.def` or the implementation.

The style follows the Rust standard library docs. When in doubt, read how `Vec`, `OpenOptions` or
`process::Output` document a similar item and do the same. `api/list.def` and `api/ui_list.def` are the
examples to copy inside this repository.

## Shape

Start with one summary sentence that makes sense alone. rustdoc shows it in item listings. If there is more to
say, add an empty `///` line and then short paragraphs, each with one fact: an edge case, what the caller does
next, or which call to use instead.

Most items need only the summary line. Stop when the caller has what they need.

### Functions

The summary starts with a third-person verb and says what the call does and returns.

```
/// Starts a list and returns the range of rows to draw.
///
/// `ordered_ids` holds the ids of all `count` rows in display order. Each id must be unique and not 0.
///
/// Selection goes into `set`. If `set` is null, read the changes from `ListResult.requests` and draw your
/// own highlight.
[manual] begin(id: LayoutId, ordered_ids: *const u64, count: u32, set: *SelectionSet, config: ListConfig) -> ListRange
```

### Structs

The summary is a noun phrase. The next paragraph says where the value comes from or where it goes.

```
/// The outcome of a list for one frame.
///
/// This is returned by `List::end` and `Input::list`. The ids are the ones you passed in, and 0 means no row.
struct ListResult {
```

### Fields

A noun phrase. Put units, limits and defaults in parentheses. A bool field starts with "Whether". If the
caller has to act on the field, say so in a second sentence.

```
/// The height of every row, in logical pixels (0 uses 32).
row_height: f32,
/// The index of the first row to draw (inclusive).
start: u32,
/// Whether the user pressed Up on the first row. Use it to move focus above the list.
nav_up_edge: bool,
```

### Enum variants

A short phrase that completes the enum's own summary.

```
/// The side of `ListResult.drag_target_id` that dragged rows land on.
enum DropRelationship {
    /// Before the target row.
    Before = 0,
```

### The `api` block

Say what the thing is in one line. Then give the order of the calls once, so the function comments do not have
to repeat it.

```
/// A scrolling list where you draw each row.
///
/// Each frame, call `begin` with the ids of all rows. For each index in the range it returns, call
/// `item_begin`, draw the row, and call `item_end`. Then call `end` and act on the result.
```

## Wording

- Put identifiers in backticks: `set`, `ListResult.requests`, `List::begin`.
- Use the names the API uses. If the field is `expanded`, write expanded and collapsed.
- Use "you" for advice to the caller: "If you keep the selection yourself, ...".
- Say each fact once, on the item where the caller needs it.
- Check every claim against the code. A comment that is wrong about what happens with a null argument is worse
  than no comment.
- Leave out how the call is implemented, why it was designed this way, and its history. Design reasons go in an
  ADR under `docs/adr/`.
- Leave out code blocks. The text also goes into the C headers, and rustdoc would compile a code block as a
  test.
- api_gen rejects issue numbers, `ADR` references and `(*.def)` or `(*.h)` file pointers in a comment.

## After editing

Run `cargo regen`, then build. `cargo regen-check` fails if the generated headers and bindings no longer match
the `.def` files.
