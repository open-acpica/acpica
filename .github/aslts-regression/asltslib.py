"""Shared helpers of the ASLTS regression check (compare.py).

Standard library only.
"""

import os
import re
import sys

STATUSES = ("PASS", "FAIL", "BLOCKED", "SKIPPED")

# ASLTS mode name -> RESULTS subdirectory
MODE_DIRS = {
    "n32": "nopt/32",
    "n64": "nopt/64",
    "o32": "opt/32",
    "o64": "opt/64",
}
DEFAULT_MODES = ("n64", "o64")

# ':STST:<collection>:<case>:<test>:<STATUS>:' - the test key may contain
# spaces and colons ("bdemo:Demo of bug 257:m17d"), so match the status from
# the right-hand side.
_STST_RE = re.compile(r":STST:(.+?):(PASS|FAIL|BLOCKED|SKIPPED):")


class ResultsError(Exception):
    """Infrastructure / configuration problem (exit code 2), not a test result."""


def warn(msg):
    print("WARNING: " + msg, file=sys.stderr)


def parse_modes(text):
    modes = text.split()
    for m in modes:
        if m not in MODE_DIRS:
            raise ResultsError("unknown mode %r (known: %s)" % (m, " ".join(MODE_DIRS)))
    return modes


def latest_results_dir(root):
    """Newest RESULTS/<run> directory (names sort chronologically)."""
    if not os.path.isdir(root):
        raise ResultsError("no RESULTS directory: " + root)
    runs = sorted(d for d in os.listdir(root) if os.path.isdir(os.path.join(root, d)))
    if not runs:
        raise ResultsError("no run directory in " + root)
    return os.path.join(root, runs[-1])


def status_file(results_dir, mode):
    return os.path.join(results_dir, MODE_DIRS[mode], "__STATUS_OF_TESTS")


_status_cache = {}


def load_status_file(path):
    """Return {key: status} from an ASLTS __STATUS_OF_TESTS file.

    Duplicate keys with different statuses are reported (once) and resolved to
    FAIL (conservative). The 'Errors # ...' suffix is ignored on purpose.
    """
    if path in _status_cache:
        return dict(_status_cache[path])
    if not os.path.isfile(path):
        raise ResultsError("missing status file: " + path)
    result = {}
    with open(path, errors="replace") as f:
        for line in f:
            m = _STST_RE.search(line)
            if not m:
                continue
            key, status = m.group(1), m.group(2)
            if key in result and result[key] != status:
                warn("duplicate key with different status in %s: %s (%s vs %s) -> FAIL"
                     % (path, key, result[key], status))
                result[key] = "FAIL"
            else:
                result[key] = status
    _status_cache[path] = dict(result)
    return result


def load_expected_file(path):
    """Return {key: status} from an expected/<mode>.txt file ('key<TAB>STATUS')."""
    if not os.path.isfile(path):
        raise ResultsError("missing expected file: " + path)
    result = {}
    with open(path, errors="replace") as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            key, sep, status = line.rpartition("\t")
            if not sep or status not in STATUSES or not key:
                raise ResultsError("%s:%d: malformed line: %r" % (path, n, line))
            if key in result:
                raise ResultsError("%s:%d: duplicate key: %s" % (path, n, key))
            result[key] = status
    return result


def format_expected(statuses):
    """Serialize {key: status} deterministically (byte order, like LC_ALL=C sort)."""
    lines = ["%s\t%s\n" % (k, statuses[k]) for k in sorted(statuses, key=lambda s: s.encode())]
    return "".join(lines)


def read_summary(results_dir):
    path = os.path.join(results_dir, "Summary")
    if not os.path.isfile(path):
        return None
    with open(path, errors="replace") as f:
        return f.read()


def check_complete(results_dir, modes, expected=None, log_path=None, max_drop=0.02):
    """Validate that a run finished and produced usable results.

    Returns a list of problems (empty if OK). 'expected' is {mode: {key: status}}
    used for the test-count sanity check.
    """
    problems = []
    summary = read_summary(results_dir)
    if summary is None:
        problems.append("Summary file is missing in " + results_dir)
    elif "Execution finished" not in summary:
        problems.append("Summary has no 'Execution finished' line - the run did not complete")
    for mode in modes:
        path = status_file(results_dir, mode)
        if not os.path.isfile(path):
            problems.append("mode %s: missing %s" % (mode, path))
            continue
        statuses = load_status_file(path)
        if not statuses:
            problems.append("mode %s: no ':STST:' lines in %s" % (mode, path))
            continue
        if expected and mode in expected and expected[mode]:
            exp_n = len(expected[mode])
            if len(statuses) < exp_n * (1 - max_drop):
                problems.append("mode %s: only %d tests reported, expected about %d "
                                "(interrupted test case?)" % (mode, len(statuses), exp_n))
    if log_path and os.path.isfile(log_path):
        with open(log_path, errors="replace") as f:
            log = f.read()
        for marker in ("ASLTS Compile Failure", "Could not find iASL/acpiexec tools",
                       "iasl compiler not found", "acpiexec utility not found"):
            if marker in log:
                problems.append("aslts.sh log contains: " + marker)
    return problems


def tail(path, n):
    try:
        with open(path, errors="replace") as f:
            lines = f.readlines()
    except OSError:
        return []
    return lines[-n:]


def git_head(repo_dir):
    """HEAD SHA without invoking git if possible (works on a detached checkout)."""
    env = os.environ.get("GITHUB_SHA")
    if env:
        return env
    try:
        with open(os.path.join(repo_dir, ".git", "HEAD")) as f:
            head = f.read().strip()
        if head.startswith("ref: "):
            with open(os.path.join(repo_dir, ".git", head[5:])) as f:
                return f.read().strip()
        return head
    except OSError:
        return "unknown"
