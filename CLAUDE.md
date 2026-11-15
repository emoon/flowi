## CodeScene
After changing code, run `code_health_review` on the changed files. If Code Health drops, fix the issues, run the review again, and use `code_health_score` to confirm the result. Before finishing, run `pre_commit_code_health_safeguard` and only stop when it passes.

## API documentation
Before writing or changing a `///` comment in `api/*.def`, read `docs/how_to_document_apis.md` and follow it.
