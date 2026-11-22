## CodeScene
The target is Code Health 10.0 on every first-party file. Before a Code Health pass, a refactor made for a score, or an edit to `.codescene/`, read `docs/codescene.md` and follow it.

After changing code, run `code_health_review` on the changed files. If Code Health drops, fix the issues, run the review again, and use `code_health_score` to confirm the result. Before finishing, run `pre_commit_code_health_safeguard` and only stop when it passes. To score more than three files, run `tools/scripts/code_health.sh`.

## API documentation
Before writing or changing a `///` comment in `api/*.def`, read `docs/how_to_document_apis.md` and follow it.
