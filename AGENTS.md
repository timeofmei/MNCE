# AGENTS.md

This file applies to the entire repository. It defines the working rules for automated coding agents and contributors acting through an agent.

## Project overview

MNCE (Master New Concept English) is a lightweight desktop language-learning application. The MVP centers on local audio files, local transcription with whisper.cpp, sentence segmentation, and three learning modes.

The current technical baseline is:

- C++20
- Qt 6.11.2 Widgets
- CMake
- SQLite
- whisper.cpp
- FFmpeg
- GPLv3

Read `doc/README.md` before making product or architectural changes. Treat the documents linked from it as the current source of truth. A decision recorded in the documentation takes precedence over an assumption in code or a generic convention.

## General working rules

- Keep changes focused on the requested task. Do not mix unrelated cleanup, formatting, or refactoring into the same change.
- Inspect existing code and documentation before introducing a new pattern.
- Preserve user changes and unrelated uncommitted work. Do not overwrite or discard work that you did not create.
- Prefer simple, explicit implementations suitable for an MVP. Add abstractions only when they solve a current requirement.
- When implementation reveals an unresolved product or engineering decision, update `doc/open-questions.md` or ask for a decision instead of silently choosing a materially different direction.
- Keep text files in UTF-8. Avoid changing line endings across an entire file unless the task requires it.
- Never place API keys, credentials, tokens, private paths, model files, or generated user data in the repository.

## Build and dependency rules

- Use C++20 and Qt 6.11.2 on every supported platform.
- Use the Windows `msvc2022_64` Qt kit as the Windows development baseline.
- Prefer CMake targets and target-scoped settings. Avoid global include directories, compiler flags, and link directories when a target-scoped alternative exists.
- Keep third-party versions, source revisions, checksums, patches, and build options reproducible and explicit.
- Do not download a moving branch such as `main` as part of a release build.
- Do not commit Whisper model binaries, generated build trees, IDE caches, packaged artifacts, or user databases.
- FFmpeg release configurations must not enable `nonfree` components. Preserve all required GPL, LGPL, MIT, and third-party notices.
- Production code must not assume that a GPU is available. CPU-only transcription remains a supported path.
- Keep long-running work such as hashing, downloading, decoding, and transcription off the UI thread.

## Code organization and behavior

- Keep UI code, application/domain logic, persistence, and external integrations separated where practical.
- Use Qt parent ownership or RAII consistently. Avoid unmanaged owning raw pointers.
- Report recoverable failures through structured errors and user-facing messages. Do not rely on console output for failures a desktop user must act on.
- Do not log API keys, full authorization headers, or sensitive user content.
- Maintain the media identity rules documented in `doc/media-and-transcription.md`: content hash is the primary identity, while path and size are supporting data.
- Treat stored transcription text and timestamps as read-only source results unless the documentation explicitly changes that policy.

## Testing and verification

- Add or update tests for behavior changed by the task when the project has a relevant test target.
- Prefer deterministic tests that do not require a network connection, microphone, GPU, or large Whisper model.
- Put external API, filesystem, audio-device, and transcription boundaries behind testable interfaces where reasonable.
- After code changes, run the narrowest relevant tests first, then the normal project build and test suite.
- If a required check cannot be run, state exactly which check was skipped and why.
- Documentation-only changes do not require a build, but links, paths, terminology, and consistency with related documents should still be checked.

## Git and commit policy

- Do not create a commit unless the user explicitly asks for one.
- Commit messages must be written in English.
- Use a short imperative subject that describes the result, for example `Add media library database schema`.
- Keep the subject focused. For a non-trivial change, use the body to explain motivation, important tradeoffs, or migration impact.
- Do not use vague subjects such as `Update files`, `Fix stuff`, or `Changes`.
- Keep logically distinct work in separate commits when commits are requested.
- Do not amend, rewrite, or otherwise modify existing commits unless the user explicitly requests that exact operation.

## Merge policy

- Every branch merge must create a merge commit, even when a fast-forward is possible. Use `git merge --no-ff <branch>`.
- Never use rebase. This includes `git rebase`, interactive rebase, `git pull --rebase`, and workflows that rebase a branch before merging.
- Synchronize diverged branches with a normal merge, not a rebase.
- Do not use `--ff-only` for branch integration.
- Do not change repository or global Git configuration to work around these rules.
- Resolve merge conflicts by preserving the intent of both sides where possible. If the correct resolution is ambiguous, stop and ask rather than discarding one side.
- Do not force-push unless the user explicitly requests it and the exact remote branch has been confirmed.

## Documentation

- Update the relevant document when a product, platform, dependency, packaging, licensing, or architectural decision changes.
- Remove a resolved item from `doc/open-questions.md` and record the decision in the appropriate topic document.
- Keep documentation concise and describe current behavior or an explicit decision. Clearly label proposals and unresolved items.
- When adding a new document, link it from `doc/README.md` if it belongs to the project documentation set.
