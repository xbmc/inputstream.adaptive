# inputstream.adaptive Agent Guidelines

## Review Protocol

Automated review agents should initialize their session by posting a brief
start marker:

```bash
gh pr comment $PR_NUMBER --repo $GITHUB_REPOSITORY --body "**Review started** — $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
```

This creates a visible review start time in the PR timeline.

## Code Review Focus

- C++ standards compliance and Kodi coding guidelines
- Memory management and resource safety (RAII patterns)
- Thread safety in multi-threaded streaming contexts
- Proper error handling and logging via Kodi log levels
- Performance implications for media streaming workloads
