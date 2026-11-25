"""Post the useful part of a failed CI step's log as GitHub annotations (public via the API, unlike logs).

Two annotations per log, so one can't crowd out the other:
  1. error lines with a little context, skipping configure-probe noise
  2. the last lines of the log
"""
import re
import sys

NOISE = re.compile(r"Performing Test|Looking for|^checking |checking for|common\.copy|-- Found|Could NOT find|"
                   r"Werror|strerror|error\.hpp|error_code|get_last_error|regex_error|error_category")
ERROR = re.compile(r"(error:|error \d|FAILED|fatal|\*\*\*|undefined (reference|symbol)|ld: |No such file|"
                   r"Permission denied|not recognized|Error \d)", re.I)


def emit(title: str, lines: list[str]) -> None:
    body = "%0A".join(l.replace("%", "%25")[:300] for l in lines)
    print(f"::error title={title}::{body}")


def main(path: str) -> None:
    with open(path, errors="replace") as f:
        lines = [l.rstrip("\r\n") for l in f]
    hits = [i for i, l in enumerate(lines) if ERROR.search(l) and not NOISE.search(l)]
    picked, seen = [], set()
    for i in hits[:12]:
        for k in range(max(0, i - 2), min(len(lines), i + 3)):
            if k not in seen:
                seen.add(k)
                picked.append(lines[k])
        picked.append("···")
    if picked:
        emit(f"{path}: errors", picked[:80])
    emit(f"{path}: last lines", lines[-50:])


if __name__ == "__main__":
    for p in sys.argv[1:]:
        main(p)
