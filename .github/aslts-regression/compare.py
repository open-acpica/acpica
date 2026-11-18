#!/usr/bin/env python3
"""Compare an ASLTS run against the expected per-test results.

Exit codes:
  0  no discrepancies, or only FIXED ones (expected FAIL that now PASSes - warning only)
  1  discrepancies: REGRESSION / CHANGED / MISSING / NEW
  2  infrastructure or configuration problem (incomplete run, broken or missing baseline)

Writes a GitHub Step Summary (when $GITHUB_STEP_SUMMARY or --summary is set),
workflow-command annotations and '::group::' blocks on stdout, and
<out>/compare-diff.txt with every discrepancy. Standard library only.

Categories of a discrepancy (expected status -> actual status):
  REGRESSION  PASS -> FAIL                          error
  FIXED       FAIL -> PASS                          warning only (error with --strict-fixed);
                                                    update expected/, until then a re-break of
                                                    that test is invisible
  CHANGED     any other change (BLOCKED/SKIPPED involved: a Yxxx flag flipped, a test
              stopped running)                      error
  MISSING     in expected/, not in the run          error (interrupted test case, removed test)
  NEW         in the run, not in expected/          error (new test needs an expected entry)

expected/<mode>.txt format: "<collection>:<case>:<test><TAB><STATUS>", sorted bytewise,
empty lines and '#' comments ignored. Updating it from a run: apply the lines of the
"How to update expected/" block of the Step Summary (or of the 'compare-diff' log group)
- MISSING: remove the line, NEW: add "<test><TAB><actual>", otherwise set the actual
status - re-sort (LC_ALL=C sort) and commit separately, mentioning the run URL.

Known quirks of the input: ASLTS reports a few tests twice with different statuses
(resolved to FAIL with a warning); the "Errors # ..." suffix of a FAIL line is ignored,
so a test that fails in a different way is not detected; aslts.sh exits 0 regardless of
the test results, only --check-complete decides whether the run was usable.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asltslib as lib  # noqa: E402

ERROR_CATEGORIES = ("REGRESSION", "CHANGED", "MISSING", "NEW")
ALL_CATEGORIES = ("REGRESSION", "FIXED", "CHANGED", "MISSING", "NEW")

SUMMARY_LIMIT = 900 * 1024      # GitHub allows 1 MiB per step summary
LIST_LIMIT = 200                # discrepancy lines per category in the summary
SNIPPET_LIMIT = 20              # test log snippets in the summary
SNIPPET_LINES = 60              # lines per snippet
DIFF_IN_SUMMARY = 2000          # compare-diff lines embedded in the summary


def run_url():
    srv, repo, rid = (os.environ.get(k, "") for k in ("GITHUB_SERVER_URL", "GITHUB_REPOSITORY", "GITHUB_RUN_ID"))
    return "%s/%s/actions/runs/%s" % (srv, repo, rid) if rid else "local run"


def classify(expected, actual):
    """Return {category: [(key, expected_status, actual_status)]}."""
    out = {c: [] for c in ALL_CATEGORIES}
    for key in sorted(set(expected) | set(actual), key=lambda s: s.encode()):
        e, a = expected.get(key), actual.get(key)
        if e is None:
            out["NEW"].append((key, "-", a))
        elif a is None:
            out["MISSING"].append((key, e, "-"))
        elif e == a:
            continue
        elif e == "PASS" and a == "FAIL":
            out["REGRESSION"].append((key, e, a))
        elif e == "FAIL" and a == "PASS":
            out["FIXED"].append((key, e, a))
        else:
            out["CHANGED"].append((key, e, a))
    return out


def counts(statuses):
    c = {s: 0 for s in lib.STATUSES}
    for v in statuses.values():
        c[v] += 1
    return c


def split_key(key):
    parts = key.split(":")
    return parts[0], parts[1] if len(parts) > 1 else "", ":".join(parts[2:])


def log_snippet(results_dir, mode, key):
    """Lines of the test's section in the acpiexec log of its test case."""
    collection, case, test = split_key(key)
    mode_dir = os.path.join(results_dir, lib.MODE_DIRS[mode])
    candidates = [os.path.join(mode_dir, case)]
    try:
        candidates += [os.path.join(mode_dir, f) for f in sorted(os.listdir(mode_dir))
                       if not f.startswith("__") and f != "Summary"]
    except OSError:
        return None, []
    marker = '"%s test started"' % test
    end_marker = ":%s:" % test
    for path in candidates:
        if not os.path.isfile(path):
            continue
        try:
            with open(path, errors="replace") as f:
                lines = f.readlines()
        except OSError:
            continue
        start = next((i for i, l in enumerate(lines) if marker in l), None)
        if start is None:
            continue
        end = next((i for i in range(start, len(lines))
                    if ":CTST:" in lines[i] and end_marker in lines[i]), len(lines) - 1)
        chunk = lines[start:end + 1]
        keep = [l.rstrip("\n") for l in chunk
                if "ACPI Error" in l or "Firmware Error" in l or "Exception" in l
                or "Expected Result" in l or "Actual Result" in l or "File      :" in l
                or "Line      :" in l or "Line   :" in l or "ERROR    :" in l
                or ":CTST:" in l or "Unexpected" in l]
        if len(keep) > SNIPPET_LINES:
            keep = keep[:SNIPPET_LINES] + ["... (%d more lines)" % (len(keep) - SNIPPET_LINES)]
        return os.path.relpath(path, results_dir), keep
    return None, []


class Summary:
    """Accumulates markdown for the Step Summary with a size cap."""

    def __init__(self):
        self.parts = []
        self.size = 0
        self.truncated = False

    def add(self, text):
        if self.truncated:
            return
        if self.size + len(text) > SUMMARY_LIMIT:
            self.parts.append("\n> Step Summary truncated (size limit); the full lists are in the job log.\n")
            self.truncated = True
            return
        self.parts.append(text)
        self.size += len(text)

    def text(self):
        return "".join(self.parts)


def group(title, lines):
    print("::group::" + title)
    for l in lines:
        print(l)
    print("::endgroup::")
    sys.stdout.flush()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--expected", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "expected"))
    ap.add_argument("--results", help="RESULTS/<run> directory (default: newest under --results-root)")
    ap.add_argument("--results-root", default="tests/aslts/tmp/RESULTS")
    ap.add_argument("--modes", default=" ".join(lib.DEFAULT_MODES))
    ap.add_argument("--out", default=os.environ.get("RUNNER_TEMP", "/tmp"))
    ap.add_argument("--log", help="stdout of aslts.sh (for failure markers and the tail on infra errors)")
    ap.add_argument("--env-file", help="file with runner environment description for the summary header")
    ap.add_argument("--summary", default=os.environ.get("GITHUB_STEP_SUMMARY"), help="Step Summary file")
    ap.add_argument("--max-annotations", type=int, default=10)
    ap.add_argument("--check-complete", action="store_true", help="validate the run before comparing")
    ap.add_argument("--strict-fixed", action="store_true", help="treat FIXED as an error (exit 1)")
    args = ap.parse_args()

    summary = Summary()
    sha = lib.git_head(os.getcwd())
    summary.add("## ASLTS regression\n\nTested commit: `%s`\n\n" % sha)
    if args.env_file and os.path.isfile(args.env_file):
        with open(args.env_file, errors="replace") as f:
            summary.add("<details><summary>Runner environment</summary>\n\n```\n%s```\n</details>\n\n" % f.read())

    def infra_error(msg, extra_lines=()):
        print("::error title=ASLTS infrastructure::" + msg)
        summary.add("### :no_entry: Infrastructure problem\n\n%s\n\n" % msg)
        if extra_lines:
            summary.add("<details><summary>Last lines of aslts.sh log</summary>\n\n```\n%s```\n</details>\n\n"
                        % "".join(extra_lines))
        finish(summary, args)
        return 2

    try:
        modes = lib.parse_modes(args.modes)
        results_dir = args.results or lib.latest_results_dir(args.results_root)
    except lib.ResultsError as e:
        return infra_error(str(e), lib.tail(args.log, 100) if args.log else ())

    summary.add("Results: `%s`, modes: %s\n\n" % (results_dir, ", ".join("`%s`" % m for m in modes)))

    # Baseline
    expected = {}
    try:
        for mode in modes:
            expected[mode] = lib.load_expected_file(os.path.join(args.expected, mode + ".txt"))
    except lib.ResultsError as e:
        return infra_error("baseline problem: %s. Fix or (re)create the expected/ files." % e)

    # Completeness
    if args.check_complete:
        problems = lib.check_complete(results_dir, modes, expected, args.log)
        if problems:
            return infra_error("run incomplete: " + "; ".join(problems),
                               lib.tail(args.log, 100) if args.log else ())

    # Compare
    errors = 0
    fixed = 0
    diff_lines = []
    table = ["| mode | tests | PASS exp/act | FAIL exp/act | BLOCKED exp/act | SKIPPED exp/act | REGRESSION | FIXED | CHANGED | MISSING | NEW |",
             "|---|---|---|---|---|---|---|---|---|---|---|"]
    per_mode = {}
    for mode in modes:
        try:
            actual = lib.load_status_file(lib.status_file(results_dir, mode))
        except lib.ResultsError as e:
            return infra_error(str(e), lib.tail(args.log, 100) if args.log else ())
        cats = classify(expected[mode], actual)
        per_mode[mode] = cats
        ce, ca = counts(expected[mode]), counts(actual)
        table.append("| %s | %d | %d/%d | %d/%d | %d/%d | %d/%d | %d | %d | %d | %d | %d |" % (
            mode, len(actual), ce["PASS"], ca["PASS"], ce["FAIL"], ca["FAIL"], ce["BLOCKED"], ca["BLOCKED"],
            ce["SKIPPED"], ca["SKIPPED"], len(cats["REGRESSION"]), len(cats["FIXED"]), len(cats["CHANGED"]),
            len(cats["MISSING"]), len(cats["NEW"])))
        for cat in ALL_CATEGORIES:
            for key, e, a in cats[cat]:
                diff_lines.append("%s\t%s\t%s\texpected=%s\tactual=%s" % (cat, mode, key, e, a))
            if cat in ERROR_CATEGORIES:
                errors += len(cats[cat])
            else:
                fixed += len(cats[cat])

    print("\n".join(table).replace("|", " "))
    fixed_is_error = args.strict_fixed and fixed
    if fixed and not args.strict_fixed:
        summary.add("> :warning: **%d test(s) with expected `FAIL` now `PASS` (FIXED)** - update `expected/` "
                    "(see 'How to update' below); until then a re-break of these tests is invisible.\n\n" % fixed)
    summary.add("\n".join(table) + "\n\n")

    # Discrepancy lists
    for cat in ALL_CATEGORIES:
        items = [(m, k, e, a) for m in modes for (k, e, a) in per_mode[m][cat]]
        if not items:
            continue
        label = "error" if cat in ERROR_CATEGORIES or (cat == "FIXED" and args.strict_fixed) else "warning"
        summary.add("<details><summary><b>%s</b>: %d (%s)</summary>\n\n| mode | test | expected | actual |\n|---|---|---|---|\n"
                    % (cat, len(items), label))
        for m, k, e, a in items[:LIST_LIMIT]:
            summary.add("| %s | `%s` | %s | %s |\n" % (m, k.replace("|", "\\|"), e, a))
        if len(items) > LIST_LIMIT:
            summary.add("\n... %d more in the job log (group 'compare-diff')\n" % (len(items) - LIST_LIMIT))
        summary.add("\n</details>\n\n")

    # Annotations
    emitted = 0
    for cat in ERROR_CATEGORIES:
        for m in modes:
            for k, e, a in per_mode[m][cat]:
                if emitted < args.max_annotations:
                    print("::error title=ASLTS %s::%s %s: expected %s, actual %s" % (cat, m, k, e, a))
                emitted += 1
    if emitted > args.max_annotations:
        print("::error title=ASLTS::... and %d more discrepancies, see the Step Summary" % (emitted - args.max_annotations))
    wemitted = 0
    for m in modes:
        for k, e, a in per_mode[m]["FIXED"]:
            level = "error" if args.strict_fixed else "warning"
            if wemitted < args.max_annotations:
                print("::%s title=ASLTS FIXED::%s %s: expected FAIL, now PASS - update expected/" % (level, m, k))
            wemitted += 1

    # Log snippets for REGRESSION / CHANGED
    shown = 0
    for cat in ("REGRESSION", "CHANGED"):
        for m in modes:
            for k, e, a in per_mode[m][cat]:
                if shown >= SNIPPET_LIMIT:
                    break
                rel, lines = log_snippet(results_dir, m, k)
                if not lines:
                    continue
                shown += 1
                summary.add("<details><summary>log: %s %s (%s)</summary>\n\n`%s`\n\n```\n%s\n```\n</details>\n\n"
                            % (m, k.replace("|", "\\|"), cat, rel, "\n".join(lines)))

    # compare-diff in the summary: one line per discrepancy, the input for updating expected/
    if diff_lines:
        shown_lines = diff_lines[:DIFF_IN_SUMMARY]
        summary.add("<details><summary><b>How to update expected/</b> (%d discrepancies)</summary>\n\n"
                    "Apply the list below to `.github/aslts-regression/expected/<mode>.txt` "
                    "(MISSING: remove the line, NEW: add `<test><TAB><actual>`, otherwise set the actual status; "
                    "keep the file sorted with `LC_ALL=C sort`), "
                    "review `git diff` and commit it separately, mentioning this run: %s\n\n"
                    "Format: `CATEGORY<TAB>mode<TAB>test<TAB>expected=X<TAB>actual=Y`"
                    " (the full list is also in the 'compare-diff' group of the job log).\n\n"
                    "```\n%s\n%s```\n</details>\n\n"
                    % (len(diff_lines), run_url(), "\n".join(shown_lines),
                       "... %d more lines in the job log\n" % (len(diff_lines) - DIFF_IN_SUMMARY)
                       if len(diff_lines) > DIFF_IN_SUMMARY else ""))

    # Files and log groups
    os.makedirs(args.out, exist_ok=True)
    diff_path = os.path.join(args.out, "compare-diff.txt")
    with open(diff_path, "w") as f:
        f.write("\n".join(diff_lines) + ("\n" if diff_lines else ""))
    group("compare-diff (%d lines)" % len(diff_lines), diff_lines or ["(no discrepancies)"])
    s = lib.read_summary(results_dir)
    if s:
        group("ASLTS Summary", s.splitlines())

    if errors or fixed_is_error:
        summary.add("**Result: FAILED** - %d error discrepancies%s.\n" %
                    (errors, ", %d FIXED (strict)" % fixed if fixed_is_error else ""))
        finish(summary, args)
        return 1
    summary.add("**Result: OK** - every test matches the expected status%s.\n" %
                (" (%d FIXED warnings)" % fixed if fixed else ""))
    finish(summary, args)
    return 0


def finish(summary, args):
    if args.summary:
        with open(args.summary, "a") as f:
            f.write(summary.text())


if __name__ == "__main__":
    sys.exit(main())
