#!/usr/bin/env python3
################################################################################
#         Copyright 2023 Lawrence Livermore National Security, LLC             #
#         See the top-level LICENSE file for details.                          #
#                                                                              #
#         SPDX-License-Identifier: MIT                                         #
################################################################################

"""
Extracts every protobuf-config example from docs/user-guide/protobuf-config.md
and actually runs each one through the real simulator, rather than trusting
that a hand-inspected example is correct.

This exists because run_configs_tests.sh (tests/run_configs_tests.sh) only
ever exercises hand-verified fixtures under tests/test_configs/ - never the
documentation's own prose examples. That gap let two real bugs sit in the
docs undetected: every example wrapped its fields in a fictional
"sim_setup { ... }" block (protobuf actually expects flat, top-level fields
- see Simulation_Params in src/proto/dr_evt_params.proto), and several
"Run with" instructions omitted the required positional trace-file argument
(it always overrides whatever `infile` the config itself sets, so it's
required even when the config already sets `infile`). Both are exactly the
kind of error a hand-crafted, already-correct fixture can never expose,
since the fixture is written to already avoid them.

This is intentionally narrow: it only checks that each example actually
*parses* and *runs* (no protobuf syntax error, no CLI usage-dump from a
missing/wrong positional argument) - not that the simulated schedule is
"correct" for whatever scenario the example claims to represent (most
examples reference a trace file that doesn't exist on a real system, e.g.
"production_trace.csv" - a synthetic stand-in with real content is written
here instead, one job, satisfying whatever `infile` the example itself
names). A `trace_format: "lassen"` example (this doc's only one, "Production
Replay") won't correctly parse the synthetic simple-format stand-in as real
job data, but that's a data-shape mismatch this test deliberately doesn't
chase - what matters is that the config itself was valid protobuf and the
CLI invocation was well-formed enough to reach the simulation loop at all,
not that the loop found jobs to run in the stand-in file.
"""

import os
import re
import subprocess
import sys
import tempfile

DOC_PATH = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..",
    "docs", "user-guide", "protobuf-config.md")

SIMULATOR = os.path.abspath(os.environ.get("SIMULATOR", "./build/simulator"))

# Lines that would make the config text non-parseable if left in place -
# not something a config file itself contains, but text this markdown
# file's surrounding prose sometimes leaves inside a fenced block by
# accident (e.g. a trailing blank line is fine, but nothing else should
# survive this filter). None expected currently; kept as a defensive filter,
# not a workaround for anything actually wrong today.
_COMMENT_ONLY_LINE = re.compile(r'^\s*#.*$')


def extract_config_blocks(doc_text):
    """Extract every ```text fenced block that looks like a protobuf
    config (references `infile` somewhere), tagged with the section
    heading it appears under and the "Run with" ```bash block (if any)
    found within the next few lines, for error reporting and for
    verify_run_with_command() below."""
    blocks = []
    current_heading = "(no heading)"
    lines = doc_text.split("\n")
    i = 0
    while i < len(lines):
        line = lines[i]
        heading_match = re.match(r'^#{2,4}\s+(.*)', line)
        if heading_match:
            current_heading = heading_match.group(1)
        if line.strip() == "```text":
            j = i + 1
            content_lines = []
            while j < len(lines) and lines[j].strip() != "```":
                content_lines.append(lines[j])
                j += 1
            content = "\n".join(content_lines)
            if re.search(r'^\s*infile\s*:', content, re.MULTILINE):
                run_with_cmd = find_following_run_with_command(lines, j + 1)
                blocks.append((current_heading, content, run_with_cmd))
            i = j
        i += 1
    return blocks


def find_following_run_with_command(lines, start):
    """Look at most a handful of lines past a config block's closing
    fence for a "Run with:" ```bash block, and return its `simulator`
    invocation line if found - None if no such block follows closely
    (some examples, e.g. mid-doc snippets, have no standalone "run
    this" instruction at all, and that's fine; nothing to check).
    Tolerates blank lines and short prose lines (e.g. the literal
    "Run with:" line itself) in between - only gives up early on
    another fenced block or a section heading, since either means
    we've moved past this example without finding one."""
    for k in range(start, min(start + 6, len(lines))):
        stripped = lines[k].strip()
        if stripped == "```bash":
            for m in range(k + 1, min(k + 5, len(lines))):
                if lines[m].strip() == "```":
                    break
                if "simulator" in lines[m]:
                    return lines[m].strip()
            break
        if stripped.startswith("```") or stripped.startswith("#"):
            break
    return None


def command_has_positional_arg(cmd_line):
    """Heuristic, but a deliberately simple one matching what this
    doc's own examples actually look like: tokenize the command,
    dropping the simulator path itself, then any `--flag value` pair
    (every long flag used in this doc's "Run with" lines takes a
    value - none use a bare boolean flag like --verbose here). Any
    token left over that doesn't start with `-` is a positional
    argument."""
    tokens = cmd_line.split()
    if not tokens:
        return False
    tokens = tokens[1:]  # drop the simulator path itself
    i = 0
    while i < len(tokens):
        tok = tokens[i]
        if tok.startswith("--"):
            i += 2  # flag and its value
            continue
        if tok.startswith("-"):
            i += 1
            continue
        return True  # first non-flag, non-flag-value token found
    return False


def verify_run_with_command(heading, config_content, run_with_cmd):
    """A config missing infile_list requires a positional trace-file
    argument on the command line regardless of what infile is set to
    inside the config (it always wins) - see this file's own docstring
    for why. Returns None if the check passes or doesn't apply (no
    "Run with" line found at all, or the config sets infile_list
    instead), or an error string describing the problem."""
    if run_with_cmd is None:
        return None
    if re.search(r'^\s*infile_list\s*:', config_content, re.MULTILINE):
        return None
    if not command_has_positional_arg(run_with_cmd):
        return (f"'Run with' command is missing the required positional "
                f"trace-file argument (infile_list isn't set, so the "
                f"positional argument is required even though the config "
                f"sets infile): {run_with_cmd!r}")
    return None


def extract_infile(content):
    m = re.search(r'^\s*infile\s*:\s*"([^"]+)"', content, re.MULTILINE)
    return m.group(1) if m else None


def run_one_example(heading, content, tmpdir, index):
    config_path = os.path.join(tmpdir, f"config_{index}.textproto")
    with open(config_path, "w") as f:
        f.write(content)

    infile = extract_infile(content)
    cmd = [SIMULATOR]
    if infile:
        # A synthetic, always-valid stand-in for whatever trace file the
        # example names - real content, one job, simple format - so the
        # positional argument (see this file's own docstring) is
        # satisfiable without needing every referenced trace file to
        # actually exist on this machine.
        infile_path = os.path.join(tmpdir, os.path.basename(infile))
        with open(infile_path, "w") as f:
            f.write("job_submit_time,num_nodes,time_limit\n0,10,100\n")
        cmd.append(infile_path)
    cmd += ["--config", config_path]

    result = subprocess.run(cmd, capture_output=True, text=True, cwd=tmpdir)
    # stderr first: the diagnostically useful part (a protobuf parse
    # error, or nothing) always lands there, while stdout's normal
    # "------ Sim params ------" dump is comparatively uninteresting
    # for diagnosing *why* something failed - showing it first (as a
    # naive stdout-then-stderr concatenation would) buries the actual
    # error under noise in any truncated display.
    output = result.stderr + result.stdout

    # A protobuf syntax error (e.g. the fictional "sim_setup { ... }"
    # wrapping this test exists to catch) or a print_usage() dump (e.g.
    # from a missing/wrong positional argument) are both failures this
    # test cares about; a normal simulation outcome against the synthetic
    # stand-in (however many jobs it "found") is not.
    parse_failed = "Failed to parse" in output
    usage_dumped = "OPTIONS:" in output and "Usage:" in output
    if parse_failed or usage_dumped:
        return False, output
    return True, output


def main():
    with open(DOC_PATH) as f:
        doc_text = f.read()

    blocks = extract_config_blocks(doc_text)
    if not blocks:
        print("ERROR: no config example blocks found in "
              f"{DOC_PATH} - extraction itself may be broken, or the "
              "doc's examples no longer use a ```text fence")
        return 1

    passed = 0
    failed = 0
    with tempfile.TemporaryDirectory() as tmpdir:
        for index, (heading, content, run_with_cmd) in enumerate(blocks):
            ok, output = run_one_example(heading, content, tmpdir, index)
            run_with_error = verify_run_with_command(heading, content, run_with_cmd)

            if ok and run_with_error is None:
                print(f"  \u2713 PASS: example under '{heading}'")
                passed += 1
            else:
                print(f"  \u2717 FAIL: example under '{heading}' did not run cleanly")
                if not ok:
                    print(f"    config:\n" +
                          "\n".join(f"      {l}" for l in content.splitlines()))
                    print(f"    output (first 8 lines):")
                    for l in output.splitlines()[:8]:
                        print(f"      {l}")
                if run_with_error:
                    print(f"    {run_with_error}")
                failed += 1

    print()
    print(f"Results: {passed} passed, {failed} failed "
          f"({len(blocks)} example(s) found in protobuf-config.md)")
    if failed:
        print("\u2717 SOME PROTOBUF CONFIG DOC EXAMPLES FAILED")
        return 1
    print("\u2713 ALL PROTOBUF CONFIG DOC EXAMPLES PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
