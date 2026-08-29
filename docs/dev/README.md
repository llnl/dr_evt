# Developer Documentation

This directory contains development notes, session documentation, and design decisions that are useful for understanding the implementation history but are not part of the main user documentation.

## Structure

### `session-notes/`
Temporary session-specific documentation from development sessions. These capture:
- Test results from specific runs
- Build setup notes
- Implementation status snapshots
- Verification reports

**Not committed by default** - useful for local reference only.

### `design-decisions/`
Important design decisions worth preserving in the repository:
- Why certain approaches were chosen
- Breaking changes and migration guides
- Terminology changes and rationale

**Can be selectively committed** when documenting important architectural decisions.

## What Goes Here vs. Main Docs

**Main docs/** (always committed):
- User guides and tutorials
- API documentation
- Test descriptions
- Quick start guides

**docs/dev/** (selective):
- Session notes (not committed)
- Design rationale (optionally committed)
- Implementation history (optionally committed)
- Temporary reports (not committed)

## .gitignore

By default, `docs/dev/session-notes/` is gitignored to keep the repository clean. Important design decisions in `design-decisions/` can be selectively committed.
