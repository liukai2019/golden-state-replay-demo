#!/usr/bin/env python3
"""List namespace-scoped C/C++ globals using a Clang compilation database.

Requires the Python clang bindings compatible with the installed libclang.
This is deliberately an inventory step, not a memory crawler: production code
should intersect its output with an explicit roots allowlist before capture.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

try:
    from clang import cindex
except ImportError as exc:  # pragma: no cover - environment-specific
    raise SystemExit(
        "clang Python bindings are required (for example: pip install clang)"
    ) from exc


PAIR_OPTIONS = {"-o", "-MF", "-MT", "-MQ", "--serialize-diagnostics"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--namespace", default="call_demo")
    parser.add_argument("--source-root", type=Path, default=Path.cwd())
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def clang_arguments(command: cindex.CompileCommand, source: Path) -> list[str]:
    raw = list(command.arguments)[1:]
    result: list[str] = []
    skip_next = False
    for argument in raw:
        if skip_next:
            skip_next = False
            continue
        if argument in PAIR_OPTIONS:
            skip_next = True
            continue
        if argument == "-c" or Path(argument).resolve() == source.resolve():
            continue
        result.append(argument)
    return result


def qualified_name(cursor: cindex.Cursor) -> str:
    names = [cursor.spelling]
    parent = cursor.semantic_parent
    while parent and parent.kind != cindex.CursorKind.TRANSLATION_UNIT:
        if parent.spelling:
            names.append(parent.spelling)
        parent = parent.semantic_parent
    return "::".join(reversed(names))


def walk(cursor: cindex.Cursor):
    yield cursor
    for child in cursor.get_children():
        yield from walk(child)


def main() -> int:
    args = parse_args()
    source_root = args.source_root.resolve()
    database = cindex.CompilationDatabase.fromDirectory(str(args.build_dir))
    index = cindex.Index.create()
    discovered: dict[str, dict[str, str]] = {}

    for command in database.getAllCompileCommands():
        source = Path(command.filename).resolve()
        if source_root not in source.parents and source != source_root:
            continue
        translation_unit = index.parse(
            str(source),
            args=clang_arguments(command, source),
            options=cindex.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES,
        )
        for cursor in walk(translation_unit.cursor):
            if cursor.kind != cindex.CursorKind.VAR_DECL:
                continue
            parent = cursor.semantic_parent
            if parent.kind not in (
                cindex.CursorKind.NAMESPACE,
                cindex.CursorKind.TRANSLATION_UNIT,
            ):
                continue
            name = qualified_name(cursor)
            if not name.startswith(args.namespace + "::"):
                continue
            if cursor.location.file is None:
                continue
            location = Path(cursor.location.file.name).resolve()
            try:
                relative_location = location.relative_to(source_root)
            except ValueError:
                continue
            discovered[name] = {
                "qualified_name": name,
                "type": cursor.type.spelling,
                "file": str(relative_location),
                "line": cursor.location.line,
            }

    payload = {
        "generated_from": str((args.build_dir / "compile_commands.json").resolve()),
        "namespace_filter": args.namespace,
        "globals": [discovered[name] for name in sorted(discovered)],
    }
    rendered = json.dumps(payload, indent=2, ensure_ascii=False) + "\n"
    if args.output:
        args.output.write_text(rendered, encoding="utf-8")
    else:
        sys.stdout.write(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
