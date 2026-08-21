"""
Reflow prose paragraphs and list items in a markdown file to a target column
width. Preserves:
 - fenced code blocks (```...```)
 - tables (lines starting with |)
 - ATX headers (lines starting with #)
 - blank lines (paragraph breaks)
 - list marker + indentation (- , * , 1. , 2. , etc.)
 - blockquote prefix (>)

Everything else is treated as prose and rewrapped to WIDTH.

Usage: python .reflow.py <input> <output> <width>
"""
import re
import sys
import textwrap


def is_blank(line: str) -> bool:
    return not line.strip()


def is_code_fence(line: str) -> bool:
    return line.lstrip().startswith("```")


def is_table_line(line: str) -> bool:
    return line.lstrip().startswith("|")


def is_header_line(line: str) -> bool:
    return re.match(r"^#{1,6}\s", line.lstrip()) is not None


def list_marker_match(line: str):
    """Return (indent, marker_str, content_indent) if line starts a list item, else None."""
    m = re.match(r"^(\s*)(-|\*|\d+\.)\s+", line)
    if not m:
        return None
    indent = len(m.group(1))
    marker = m.group(2)
    prefix_len = len(m.group(0))  # includes trailing spaces
    return indent, marker, prefix_len


def is_blockquote(line: str) -> bool:
    return line.lstrip().startswith(">")


def wrap_text(text: str, width: int, initial_indent: str = "", subsequent_indent: str = "") -> str:
    """Wrap paragraph text preserving inline markdown."""
    return textwrap.fill(
        text,
        width=width,
        initial_indent=initial_indent,
        subsequent_indent=subsequent_indent,
        break_long_words=False,
        break_on_hyphens=False,
        drop_whitespace=True,
    )


def reflow(source: str, width: int) -> str:
    lines = source.splitlines()
    out = []
    i = 0
    n = len(lines)
    in_fence = False

    while i < n:
        line = lines[i]

        # Fenced code blocks: preserve verbatim
        if is_code_fence(line):
            out.append(line)
            in_fence = not in_fence
            i += 1
            continue

        if in_fence:
            out.append(line)
            i += 1
            continue

        # Blank lines separate blocks
        if is_blank(line):
            out.append("")
            i += 1
            continue

        # Headers and tables — verbatim
        if is_header_line(line) or is_table_line(line):
            out.append(line)
            i += 1
            continue

        # Blockquote block — gather contiguous quoted lines
        if is_blockquote(line):
            block = []
            while i < n and not is_blank(lines[i]) and is_blockquote(lines[i]):
                # strip "> " (or ">"), keep the rest
                stripped = re.sub(r"^\s*>\s?", "", lines[i])
                block.append(stripped)
                i += 1
            joined = " ".join(s.strip() for s in block if s.strip())
            if joined:
                wrapped = wrap_text(joined, width - 2)  # room for "> "
                for wl in wrapped.split("\n"):
                    out.append("> " + wl if wl else ">")
            else:
                out.append(">")
            continue

        # List item — gather item plus continuation lines
        lm = list_marker_match(line)
        if lm is not None:
            indent, marker, prefix_len = lm
            content_indent = prefix_len  # the column at which the item content starts
            content_indent_str = " " * content_indent

            first_content = line[prefix_len:]
            parts = [first_content.strip()]
            i += 1

            # Continuation: lines that are indented at least to content_indent
            # AND are not blank AND are not themselves a new list item.
            while i < n:
                nxt = lines[i]
                if is_blank(nxt):
                    break
                if is_header_line(nxt) or is_table_line(nxt) or is_code_fence(nxt):
                    break
                # A nested list item under this one is a separate block
                nxt_marker = list_marker_match(nxt)
                if nxt_marker is not None:
                    # If the nested item is indented more than the current
                    # content_indent, treat it as a nested block (handled by
                    # the outer loop). Break so it's picked up next iteration.
                    break
                # Continuation must be indented at least to content_indent
                if not nxt.startswith(content_indent_str):
                    break
                parts.append(nxt.strip())
                i += 1

            joined = " ".join(parts)
            available = width - content_indent
            # Ensure at least a minimum wrap width
            available = max(available, 40)
            wrapped = wrap_text(joined, available)
            wl_lines = wrapped.split("\n")
            # First line: original indent + marker + space + first wrapped word(s)
            marker_prefix = " " * indent + marker + " " * (prefix_len - indent - len(marker))
            out.append(marker_prefix + wl_lines[0])
            for wl in wl_lines[1:]:
                out.append(content_indent_str + wl)
            continue

        # Plain prose paragraph: gather until blank / special block boundary
        para = [line.rstrip()]
        i += 1
        while i < n:
            nxt = lines[i]
            if (
                is_blank(nxt)
                or is_header_line(nxt)
                or is_table_line(nxt)
                or is_code_fence(nxt)
                or is_blockquote(nxt)
                or list_marker_match(nxt) is not None
            ):
                break
            para.append(nxt.strip())
            i += 1

        joined = " ".join(p.strip() for p in para)
        wrapped = wrap_text(joined, width)
        for wl in wrapped.split("\n"):
            out.append(wl)

    # Trailing newline for POSIX friendliness
    return "\n".join(out) + "\n"


def main():
    if len(sys.argv) != 4:
        print("Usage: python .reflow.py <input> <output> <width>", file=sys.stderr)
        sys.exit(2)
    inp, outp, width = sys.argv[1], sys.argv[2], int(sys.argv[3])
    with open(inp, "r", encoding="utf-8") as f:
        source = f.read()
    result = reflow(source, width)
    with open(outp, "w", encoding="utf-8", newline="\n") as f:
        f.write(result)


if __name__ == "__main__":
    main()
