#!/usr/bin/env python3
"""
DANOS-Open YANG Model Validator (E4)

A lightweight YANG syntax checker that validates YANG 1.1 model files
without requiring pyang. Checks:
  - Module/submodule declaration
  - Namespace and prefix
  - Balanced braces
  - Required statements (namespace, prefix for module)
  - Valid statement syntax
  - Type references

Usage: python3 yang_validator.py <file.yang> [<file2.yang> ...]
       python3 yang_validator.py --all   (validate all models)
"""

import re
import sys
from pathlib import Path


class YangError:
    def __init__(self, line, msg):
        self.line = line
        self.msg = msg

    def __str__(self):
        return f"  line {self.line}: {self.msg}"


def strip_comments(text):
    """Remove YANG comments (/* */ and //)."""
    # Block comments
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.DOTALL)
    # Line comments
    text = re.sub(r'//[^\n]*', '', text)
    return text


def check_braces(text, errors):
    """Check that braces are balanced."""
    depth = 0
    line = 1
    for i, c in enumerate(text):
        if c == '\n':
            line += 1
        elif c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth < 0:
                errors.append(YangError(line, "unexpected '}'"))
                return False
    if depth != 0:
        errors.append(YangError(line, f"unbalanced braces (depth={depth})"))
        return False
    return True


def parse_statements(text):
    """Parse YANG statements into a tree.
    Returns list of (keyword, argument, line, children)."""
    # Tokenize
    tokens = []
    i = 0
    line = 1
    while i < len(text):
        c = text[i]
        if c == '\n':
            line += 1
            i += 1
        elif c.isspace():
            i += 1
        elif c == '{' or c == '}':
            tokens.append((c, None, line))
            i += 1
        elif c == ';':
            tokens.append((';', None, line))
            i += 1
        elif c == '"':
            # String literal
            j = i + 1
            while j < len(text) and text[j] != '"':
                if text[j] == '\\':
                    j += 1
                j += 1
            tokens.append(('STR', text[i+1:j], line))
            i = j + 1
        elif c == "'":
            # Single-quoted string
            j = i + 1
            while j < len(text) and text[j] != "'":
                j += 1
            tokens.append(('STR', text[i+1:j], line))
            i = j + 1
        else:
            # Identifier or keyword
            j = i
            while j < len(text) and not text[j].isspace() and text[j] not in '{};"\'':
                j += 1
            tokens.append(('ID', text[i:j], line))
            i = j
    return tokens


def build_tree(tokens, idx=0):
    """Build statement tree from tokens. Returns (statements, next_idx)."""
    statements = []
    while idx < len(tokens):
        tok_type, tok_val, tok_line = tokens[idx]
        if tok_type == '}':
            return statements, idx + 1
        if tok_type == 'ID' or tok_type == 'STR':
            keyword = tok_val
            idx += 1
            argument = None
            if idx < len(tokens):
                ttype, tval, _ = tokens[idx]
                if ttype in ('ID', 'STR'):
                    argument = tval
                    idx += 1
            if idx < len(tokens):
                ttype, _, tline = tokens[idx]
                if ttype == ';':
                    statements.append((keyword, argument, tok_line, []))
                    idx += 1
                elif ttype == '{':
                    idx += 1
                    children, idx = build_tree(tokens, idx)
                    statements.append((keyword, argument, tok_line, children))
                else:
                    statements.append((keyword, argument, tok_line, []))
        else:
            idx += 1
    return statements, idx


def validate_module(stmts, filename, errors):
    """Validate a YANG module/submodule."""
    if not stmts:
        errors.append(YangError(0, "empty file"))
        return

    top = stmts[0]
    keyword, argument, line, children = top

    if keyword not in ('module', 'submodule'):
        errors.append(YangError(line, f"expected 'module' or 'submodule', got '{keyword}'"))
        return

    if not argument:
        errors.append(YangError(line, f"{keyword} missing name"))
        return

    # Check required statements for module
    if keyword == 'module':
        has_namespace = False
        has_prefix = False
        for k, a, l, c in children:
            if k == 'namespace':
                has_namespace = True
            elif k == 'prefix':
                has_prefix = True
        if not has_namespace:
            errors.append(YangError(line, "module missing 'namespace'"))
        if not has_prefix:
            errors.append(YangError(line, "module missing 'prefix'"))

    # Check for at least one revision or description
    has_revision = any(k == 'revision' for k, a, l, c in children)
    if not has_revision:
        errors.append(YangError(line, f"warning: {keyword} '{argument}' has no revision"))

    # Validate typedefs and leaf types
    for k, a, l, c in children:
        if k == 'typedef':
            has_type = any(ck == 'type' for ck, ca, cl, cc in c)
            if not has_type:
                errors.append(YangError(l, f"typedef '{a}' missing 'type'"))
        elif k == 'leaf':
            has_type = any(ck == 'type' for ck, ca, cl, cc in c)
            if not has_type:
                errors.append(YangError(l, f"leaf '{a}' missing 'type'"))
        elif k == 'container':
            # Recursively validate
            validate_container(children=c, errors=errors, path=a)


def validate_container(children, errors, path):
    """Recursively validate container/list contents."""
    for k, a, l, c in children:
        if k == 'leaf':
            has_type = any(ck == 'type' for ck, ca, cl, cc in c)
            if not has_type:
                errors.append(YangError(l, f"leaf '{path}/{a}' missing 'type'"))
        elif k in ('container', 'list'):
            validate_container(c, errors, f"{path}/{a}")


def validate_file(filepath):
    """Validate one YANG file. Returns list of errors."""
    errors = []
    try:
        text = Path(filepath).read_text()
    except Exception as e:
        return [YangError(0, f"cannot read: {e}")]

    text = strip_comments(text)

    if not check_braces(text, errors):
        return errors

    tokens = parse_statements(text)
    stmts, _ = build_tree(tokens)
    validate_module(stmts, str(filepath), errors)

    return errors


def main():
    if len(sys.argv) < 2:
        print("Usage: yang_validator.py <file.yang> [...] | --all")
        return 1

    if sys.argv[1] == '--all':
        yang_dir = Path(__file__).parent
        files = sorted(yang_dir.rglob("*.yang"))
    else:
        files = [Path(f) for f in sys.argv[1:]]

    total_errors = 0
    for f in files:
        errors = validate_file(f)
        if errors:
            print(f"\n{f}:")
            for e in errors:
                print(e)
            total_errors += len(errors)
        else:
            print(f"{f}: OK")

    if total_errors == 0:
        print(f"\n=== All {len(files)} YANG models valid ===")
        return 0
    else:
        print(f"\n=== {total_errors} errors in {len(files)} files ===")
        return 1


if __name__ == "__main__":
    sys.exit(main())
