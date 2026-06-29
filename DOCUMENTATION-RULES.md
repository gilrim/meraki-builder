# Documentation rules

These rules apply to every Markdown document in this repository.

## 1. Document the current release as current state

- Active documentation describes only behavior implemented by the latest repository revision.
- Use present tense and describe what the system does, not how a defect was discovered or repaired.
- Do not leave obsolete commands, old branch defaults, migration-only steps, temporary workarounds, or superseded architecture in active guides.
- Unimplemented ideas belong in an issue or design proposal, not in an operating guide.

## 2. Put historical material under `docs/history/`

- Incident reports, root-cause analyses, old release notes, retired workflows, migration instructions, and superseded designs belong under `docs/history/`.
- Historical documents must identify themselves as historical and must link readers back to the active documentation database.
- Research evidence and unresolved hardware questions belong under `docs/research/`; they must not be presented as supported production behavior.

## 3. Keep the root README limited to onboarding

`README.md` may contain only:

- a short project description;
- essential safety and compatibility warnings;
- the shortest supported build/install entry path;
- links into `docs/` and this rules file.

Detailed features, protocols, architecture, troubleshooting, release notes, validation results, and hardware evidence belong in the documentation database.

## 4. Maintain one authoritative location per subject

- Put complete information in the most specific active document and link to it elsewhere.
- Avoid copying long procedures between the root README, package READMEs, and `docs/`.
- Package READMEs may document package-local interfaces, files, build flags, and tests; user-facing behavior belongs in `docs/user-guide/`.
- Protocol details belong in the protocol reference; architecture rationale belongs in `docs/architecture/`.

## 5. Update all affected documentation with code changes

A behavior or schema change must update, as applicable:

- the user guide;
- architecture and protocol references;
- configuration-schema documentation;
- package/module documentation;
- compatibility or hardware evidence documentation;
- the historical changelog or release notes.

Do not merge a behavior change while documentation still describes the superseded behavior.

## 6. State support and evidence precisely

- Distinguish `validated`, `confirmed`, `untested`, and `known-incompatible` model states.
- Label hardware facts as verified only when the evidence is exact-model and reproducible.
- Clearly separate safe read-only discovery from destructive authority.
- State when a feature fails closed because evidence or runtime data is unavailable.

## 7. Keep examples current and safe

- Examples must use supported commands, current file paths, and current branch/source policy.
- Destructive examples must include the required safety preconditions and must not imply that force bypasses compatibility or geometry checks.
- Never include credentials, private keys, device-specific secrets, or unredacted customer data.

## 8. Preserve navigation and link integrity

- Add every active document to [`docs/README.md`](docs/README.md).
- Link historical records from a relevant history index or changelog when they remain useful.
- Use relative repository links and verify anchors after renaming headings.
- Remove orphaned redirect files once inbound links have been updated.

## 9. Required validation

Run before committing documentation changes:

```sh
make test-docs
python3 scripts/check-docs.py .
```

For cross-repository UI documentation changes, also run from the UI repository:

```sh
npm run docs:check
```

The documentation checks enforce local-link integrity, root-document hygiene, and separation of current-state and historical narratives.
