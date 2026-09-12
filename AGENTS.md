# AGENTS.md

This file applies to the entire repository. It defines the working rules for automated coding agents and contributors acting through an agent.

## Project overview

MNCE (Master New Concept English) is a lightweight desktop language-learning application. The MVP centers on local audio files, local transcription with whisper.cpp, sentence segmentation, and three learning modes.

The current technical baseline is:

- C++20
- Qt 6.10.2 or newer Widgets
- CMake
- SQLite
- whisper.cpp
- FFmpeg
- MIT

Read `doc/README.md` before making product or architectural changes. Treat the documents linked from it as the current source of truth. A decision recorded in the documentation takes precedence over an assumption in code or a generic convention.

## General working rules

- Keep changes focused on the requested task. Do not mix unrelated cleanup, formatting, or refactoring into the same change.
- Inspect existing code and documentation before introducing a new pattern.
- Preserve user changes and unrelated uncommitted work. Do not overwrite or discard work that you did not create.
- Prefer simple, explicit implementations suitable for an MVP. Add abstractions only when they solve a current requirement.
- When implementation reveals an unresolved product or engineering decision, update `doc/open-questions.md` or ask for a decision instead of silently choosing a materially different direction.
- Keep text files in UTF-8. Avoid changing line endings across an entire file unless the task requires it.
- Never place API keys, credentials, tokens, private paths, model files, or generated user data in the repository.

## Collaboration and decision checkpoints

Do not work through an entire milestone silently. Keep the user involved at the points where their intent or a durable decision matters.

- Treat user ideas as inputs to classify, not automatically as requirements for the current milestone. A newly mentioned idea may belong to the current milestone, a later milestone, a cross-cutting topic document, or the open-questions backlog.
- Before writing an idea into a milestone document, determine whether it is necessary for that milestone's goal and acceptance criteria. If its timing or scope is ambiguous, ask the user where it belongs instead of assuming the active milestone.
- Put durable rules that affect multiple milestones in the relevant topic document and let milestone documents reference them. Put unresolved ideas in `doc/open-questions.md`; do not invent a milestone number or implementation schedule without user agreement.
- Keep future ideas out of current implementation scope unless the user explicitly approves bringing them forward. Recording an idea does not authorize implementing it.
- When moving or recording an idea, tell the user where it was placed and why, so they can correct the classification before implementation begins.
- At the start of a milestone, summarize the proposed goal, scope, exclusions, and acceptance criteria. Resolve the open product and engineering questions before starting implementation that depends on them.
- After drafting or materially changing a milestone document, ask the user to review its decisions before treating the document as approved for implementation.
- Ask before choosing behavior that affects user workflow, visible UI, stored data, schema compatibility, privacy, security, licensing, external services, dependency selection, packaging, or platform support.
- Ask as soon as an unresolved decision is discovered. Do not implement dependent work while waiting and do not silently convert a recommendation into a decision.
- Before a destructive data operation, irreversible migration, external publication, release, or other difficult-to-reverse action, restate the effect and obtain explicit user direction.
- During implementation, report progress at meaningful checkpoints such as completion of persistence, services, UI integration, and tests. Report unexpected findings or scope changes before continuing into affected work.
- At the end of a milestone, report what changed, what was verified, what remains unresolved, and any checks that could not run. Ask whether to commit unless the user already requested a commit.
- Keep questions focused. State the decision needed, give a recommendation with its main tradeoff, and preferably resolve one product decision at a time.
- Routine implementation details that are already determined by approved documentation and do not alter behavior or architecture may proceed without additional confirmation.

## Build and dependency rules

- Use C++20 and Qt 6.10.2 or newer on every supported platform.
- Use the Windows `msvc2022_64` Qt kit as the Windows development baseline.
- Prefer CMake targets and target-scoped settings. Avoid global include directories, compiler flags, and link directories when a target-scoped alternative exists.
- Keep third-party versions, source revisions, checksums, patches, and build options reproducible and explicit.
- Do not download a moving branch such as `main` as part of a release build.
- Do not commit Whisper model binaries, generated build trees, IDE caches, packaged artifacts, or user databases.
- Production releases must dynamically link Qt and FFmpeg under their LGPL-compatible paths. Project-built FFmpeg configurations must not enable `gpl` or `nonfree` components. Preserve all required GPL, LGPL, MIT, and third-party notices.
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

- Maintain a single source of truth for every durable decision. Define a product rule, identifier, version, algorithm, format list, schema convention, or other shared constant in one canonical topic document only.
- Prefer links to canonical documents over copying their values or restating their full rules. A milestone document should describe which canonical rules it implements, plus milestone-specific scope and acceptance criteria; it should not redefine those rules.
- Keep summaries brief when context requires them, and label the canonical document explicitly. Do not let a summary become a second normative specification.
- Before adding a decision to a document, identify its canonical owner. If no suitable document exists, create a focused topic document and link it from `doc/README.md` rather than distributing the decision across milestone files.
- When a canonical decision changes, update its owner and then check all references for consistency. Search the documentation for duplicated literal values and remove stale copies where practical.
- Update the relevant document when a product, platform, dependency, packaging, licensing, or architectural decision changes.
- Remove a resolved item from `doc/open-questions.md` and record the decision in the appropriate topic document.
- Keep documentation concise and describe current behavior or an explicit decision. Clearly label proposals and unresolved items.
- When adding a new document, link it from `doc/README.md` if it belongs to the project documentation set.
