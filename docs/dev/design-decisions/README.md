# Design Decisions

This directory contains architectural decisions and design rationales for DR_EVT.

## Documents

- **[Simulation vs Replay Modes](SIMULATION_VS_REPLAY_MODES.md)** - Why we have two distinct operating modes and how they differ
- **[Timezone Support](TIMEZONE_SUPPORT.md)** - How timezone handling works for ISO timestamp traces
- **[Resource-History Streaming](RESOURCE_HISTORY_STREAMING.md)** (proposed, not yet implemented) - Plan for replacing the resource-history vector with a circular buffer to support long-running streaming use

## Purpose

These documents record the "why" behind non-obvious design choices. They help:
- New contributors understand context
- Future maintainers avoid re-litigating settled questions
- Reviewers see what alternatives were considered

## Adding New Decisions

When making a significant design choice, consider documenting:
1. **The problem** - What constraint or requirement drove this?
2. **Alternatives considered** - What else did we try or think about?
3. **Trade-offs** - What did we gain? What did we lose?
4. **Decision** - What did we choose and why?
5. **Consequences** - What does this decision mean for future work?
