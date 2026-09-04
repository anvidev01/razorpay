#!/usr/bin/env python3
"""Render docs/03-DEMO-SCRIPT.md to a print-optimised page for reading while recording.

    python3 scripts/render-script.py docs/03-DEMO-SCRIPT.md /tmp/script.html
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless \
      --no-pdf-header-footer --print-to-pdf=script.pdf file:///tmp/script.html


Two hard requirements learned the hard way:
  1. Code blocks must WRAP, never clip. A PDF cannot scroll, so `overflow-x:auto`
     silently truncates -- that is how `--wal wal/rig.wal` became `--wal wal/rig.`
     and wrote a log to a file named "rig.".
  2. Spoken lines must be visually unmistakable from typed commands, because the
     operator is reading this while talking to a camera.
"""
import html
import re
import sys

SRC, OUT = sys.argv[1], sys.argv[2]
lines = open(SRC).read().split("\n")

CSS = """
:root{
  --ink:#12161C; --mute:#5A6472; --faint:#98A2B3; --rule:#E4E8EE;
  --bg:#FFFFFF; --band:#F5F7FA;
  --accent:#0B5FD0; --accent-soft:#EAF1FC;
  --cmd-bg:#171B22; --cmd-ink:#E8EDF4; --cmd-dim:#8792A4;
  --out-bg:#F7F9FB;
  --say-bar:#0B5FD0; --say-bg:#F7FAFE;
  --warn:#B25E00; --warn-bg:#FFF6EA;
  --do-bg:#F4F6F8; --do-line:#C6CFDA; --do-ink:#5A6472; --do-strong:#B25E00;
}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);
  font:15px/1.62 "Iowan Old Style","Palatino Linotype",Palatino,Georgia,serif;
  -webkit-font-smoothing:antialiased}
.page{max-width:760px;margin:0 auto;padding:44px 40px 60px}

h1{font-size:27px;line-height:1.2;letter-spacing:-.015em;margin:0 0 6px;
  font-weight:600;text-wrap:balance}
.sub{color:var(--mute);font-size:13.5px;margin:0 0 4px}
.sub b{color:var(--ink);font-weight:600}
.lede{color:var(--mute);font-size:13px;margin:14px 0 0;padding-top:14px;
  border-top:1px solid var(--rule)}

h2{font-size:15.5px;font-weight:600;margin:34px 0 12px;padding-top:16px;
  border-top:1px solid var(--rule);letter-spacing:-.005em;
  break-after:avoid;page-break-after:avoid}
h2 .clock{display:inline-block;background:var(--accent);color:#fff;
  font:600 11.5px/1 ui-monospace,"SF Mono",Menlo,monospace;
  padding:5px 8px;border-radius:4px;margin-right:10px;vertical-align:2px;
  letter-spacing:.02em}
h3{font-size:13px;font-weight:600;margin:22px 0 8px;color:var(--mute);
  text-transform:uppercase;letter-spacing:.07em;break-after:avoid}

p{margin:10px 0}
strong{font-weight:600}
em{color:var(--mute)}
a{color:var(--accent)}

/* typed commands */
pre.cmd{background:var(--cmd-bg);color:var(--cmd-ink);border-radius:6px;
  padding:12px 15px;margin:11px 0;
  font:12.5px/1.65 ui-monospace,"SF Mono",Menlo,Consolas,monospace;
  white-space:pre-wrap;overflow-wrap:anywhere;word-break:break-word;
  break-inside:avoid;page-break-inside:avoid}
pre.cmd .c{color:var(--cmd-dim)}

/* terminal output */
pre.out{background:var(--out-bg);color:var(--mute);
  border:1px solid var(--rule);border-left:3px solid var(--faint);
  border-radius:0 5px 5px 0;padding:11px 14px;margin:11px 0;
  font:11.5px/1.6 ui-monospace,"SF Mono",Menlo,Consolas,monospace;
  white-space:pre-wrap;overflow-wrap:anywhere;
  break-inside:avoid;page-break-inside:avoid}
pre.out b{color:var(--ink);font-weight:600}

/* ── WHAT YOU SAY ─────────────────────────────────────────────────────────
   The only serif prose on the page set at this size. Large, dark, generous.
   If it looks like this, read it out loud.                                */
.say{position:relative;margin:14px 0 16px;padding:16px 20px 16px 22px;
  background:var(--say-bg);border-left:5px solid var(--say-bar);
  border-radius:0 6px 6px 0;break-inside:avoid;page-break-inside:avoid}
.say p{margin:0 0 11px;font-size:16.5px;line-height:1.66;color:var(--ink)}
.say p:last-child{margin-bottom:0}
.say strong{color:var(--accent);font-weight:600}
.say .tag{position:absolute;top:-8px;left:20px;background:var(--say-bar);color:#fff;
  font:600 9px/1 ui-monospace,"SF Mono",Menlo,monospace;letter-spacing:.14em;
  padding:4px 7px;border-radius:3px}

/* ── WHAT YOU DO ──────────────────────────────────────────────────────────
   Monospace, small, muted, dashed. Deliberately does not read as prose.   */
.do{margin:10px 0;padding:9px 13px;background:var(--do-bg);
  border:1px dashed var(--do-line);border-radius:5px;
  break-inside:avoid;page-break-inside:avoid}
.do p{margin:0 0 4px;font:12px/1.6 ui-monospace,"SF Mono",Menlo,Consolas,monospace;
  color:var(--do-ink)}
.do p:last-child{margin-bottom:0}
.do strong{color:var(--do-strong);font-weight:600}
.do code{background:transparent;color:var(--do-ink);padding:0;font-size:12px}
.do del{opacity:.6}

/* stage direction */
p.stage{background:var(--warn-bg);border-left:3px solid var(--warn);
  border-radius:0 5px 5px 0;padding:9px 14px;margin:11px 0;
  color:var(--warn);font-size:13px;font-style:italic}

table{border-collapse:collapse;width:100%;margin:14px 0;font-size:12.5px;
  break-inside:avoid;page-break-inside:avoid}
th{text-align:left;font-weight:600;font-size:10.5px;text-transform:uppercase;
  letter-spacing:.06em;color:var(--faint);padding:0 10px 7px;
  border-bottom:1px solid var(--rule)}
td{padding:7px 10px;border-bottom:1px solid var(--rule);vertical-align:top}
tr:last-child td{border-bottom:none}
td:first-child{font:600 11.5px/1.5 ui-monospace,"SF Mono",Menlo,monospace;
  color:var(--accent);white-space:nowrap}

ul{margin:10px 0;padding-left:20px}
li{margin:5px 0;font-size:13.5px}
code{background:var(--accent-soft);color:var(--accent);padding:1px 5px;
  border-radius:3px;font:12px ui-monospace,"SF Mono",Menlo,monospace}
li code,td code,p code{font-size:11.5px}
hr{border:0;border-top:1px solid var(--rule);margin:26px 0}

.legend{display:grid;gap:7px;margin:16px 0 22px;padding:14px 16px;
  background:var(--band);border-radius:7px;break-inside:avoid}
.lg{display:flex;align-items:flex-start;gap:10px;font-size:12px;color:var(--mute);
  line-height:1.5}
.lg b{color:var(--ink);font-weight:600}
.sw{flex:none;width:15px;height:15px;border-radius:4px;margin-top:1px}
.sw-say{background:var(--say-bg);border-left:4px solid var(--say-bar);
  border-top:1px solid var(--say-bar);border-right:1px solid var(--say-bar);
  border-bottom:1px solid var(--say-bar)}
.sw-do{background:var(--do-bg);border:1px dashed var(--do-line)}
.sw-cmd{background:var(--cmd-bg)}
.sw-out{background:var(--out-bg);border:1px solid var(--rule);
  border-left:3px solid var(--faint)}

.kw{background:var(--band);border-radius:6px;padding:14px 17px;margin:12px 0;
  font-size:11.5px;line-height:1.85;color:var(--mute)}

@page{margin:14mm}
@media print{ .page{padding:0} body{font-size:14px} }
"""


def inline(t):
    t = html.escape(t)
    t = re.sub(r"`([^`]+)`", r"<code>\1</code>", t)
    t = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", t)
    t = re.sub(r"(?<![\w*])\*([^*\n]+)\*(?![\w*])", r"<em>\1</em>", t)
    t = re.sub(r"~~([^~]+)~~", r"<del>\1</del>", t)
    t = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r'<a href="\2">\1</a>', t)
    return t


out, i = [], 0
in_quote = []


def flush_quote():
    """A blockquote is either words to SPEAK or an instruction to FOLLOW.

    They must never look alike -- reading an instruction aloud on camera is the
    exact failure this colour coding exists to prevent. Spoken blocks always open
    with a double quote; everything else is an instruction.
    """
    global in_quote
    if not in_quote:
        return
    paras, cur = [], []
    for x in in_quote:
        if x.strip():
            cur.append(x.strip())
        elif cur:
            paras.append(" ".join(cur)); cur = []
    if cur:
        paras.append(" ".join(cur))
    first = paras[0].lstrip() if paras else ""
    spoken = first.startswith('"') or first.startswith('&quot;')
    body = "".join("<p>%s</p>" % inline(x) for x in paras)
    if spoken:
        out.append('<div class="say"><span class="tag">SAY</span>%s</div>' % body)
    else:
        steps = "".join("<p>%s</p>" % inline(x.strip())
                        for x in in_quote if x.strip())
        out.append('<div class="do">%s</div>' % steps)
    in_quote = []


while i < len(lines):
    ln = lines[i]

    if ln.startswith(">"):
        in_quote.append(ln.lstrip("> ").rstrip())
        i += 1
        continue
    flush_quote()

    # fenced code
    if ln.startswith("```"):
        i += 1
        buf = []
        while i < len(lines) and not lines[i].startswith("```"):
            buf.append(lines[i])
            i += 1
        i += 1
        raw = "\n".join(buf)
        body = html.escape(raw, quote=False)
        # terminal output vs a command you type
        looks_out = bool(re.search(
            r"(p50=|ALLOW|DENY|divergent|OK |REFUSED|passed,|cart_lines|"
            r"decisions in|fsyncs|of authorised value|\|- R_|repair \{|chain +:|replay +:)", raw))
        if looks_out:
            body = re.sub(r"\b(ALLOW|DENY|REFUSED|OK|divergent : 0|0x[0-9A-Fa-f]{4,})\b",
                          r"<b>\1</b>", body)
            out.append('<pre class="out">%s</pre>' % body)
        else:
            body = re.sub(r"(?m)(?<![\w&])(#\s[^\n]*)", r'<span class="c">\1</span>', body)
            out.append('<pre class="cmd">%s</pre>' % body)
        continue

    # headings
    if ln.startswith("## "):
        t = ln[3:].strip()
        m = re.match(r"^(\d+:\d{2})\s*[–-]\s*(\d+:\d{2})\s*·\s*(.+)$", t)
        if m:
            out.append('<h2><span class="clock">%s</span>%s</h2>'
                       % (m.group(1), inline(m.group(3))))
        else:
            out.append("<h2>%s</h2>" % inline(t))
        i += 1
        continue
    if ln.startswith("### "):
        out.append("<h3>%s</h3>" % inline(ln[4:].strip()))
        i += 1
        continue
    if ln.startswith("# "):
        out.append("<h1>%s</h1>" % inline(ln[2:].strip()))
        i += 1
        continue

    # table
    if ln.startswith("|") and i + 1 < len(lines) and set(lines[i+1].replace("|", "").strip()) <= set("-: "):
        head = [c.strip() for c in ln.strip("|").split("|")]
        i += 2
        rows = []
        while i < len(lines) and lines[i].startswith("|"):
            rows.append([c.strip() for c in lines[i].strip("|").split("|")])
            i += 1
        th = "".join("<th>%s</th>" % inline(c) for c in head)
        tb = "".join("<tr>%s</tr>" % "".join("<td>%s</td>" % inline(c) for c in r) for r in rows)
        out.append("<table><thead><tr>%s</tr></thead><tbody>%s</tbody></table>" % (th, tb))
        continue

    # list
    if ln.startswith("- "):
        items = []
        while i < len(lines) and (lines[i].startswith("- ") or lines[i].startswith("  ")):
            if lines[i].startswith("- "):
                items.append(lines[i][2:].strip())
            elif items:
                items[-1] += " " + lines[i].strip()
            i += 1
        out.append("<ul>%s</ul>" % "".join("<li>%s</li>" % inline(x) for x in items))
        continue

    if ln.strip() == "---":
        out.append("<hr>")
        i += 1
        continue

    if ln.strip() in ("**SAY**",):
        i += 1
        continue
    if ln.strip().startswith("**SAY**") and "(" in ln:
        out.append('<p class="stage">%s</p>' % inline(re.sub(r"^\*\*SAY\*\*\s*", "", ln.strip()).strip("*")))
        i += 1
        continue

    if ln.strip():
        # stage direction: a fully-italic line
        if ln.strip().startswith("*") and ln.strip().endswith("*") and not ln.strip().startswith("**"):
            out.append('<p class="stage">%s</p>' % inline(ln.strip().strip("*")))
        else:
            para = [ln.strip()]
            i += 1
            while i < len(lines) and lines[i].strip() and not re.match(
                    r"^(#|>|```|\||- |---)", lines[i]):
                para.append(lines[i].strip())
                i += 1
            out.append("<p>%s</p>" % inline(" ".join(para)))
            continue
    i += 1

flush_quote()

LEGEND = ('<div class="legend">'
  '<div class="lg"><span class="sw sw-say"></span>'
  '<b>Blue, large, serif — SAY THIS ALOUD.</b> Every word.</div>'
  '<div class="lg"><span class="sw sw-do"></span>'
  '<b>Grey dashed, monospace — DO, never say.</b> Press space, wait, cut to the browser.</div>'
  '<div class="lg"><span class="sw sw-cmd"></span>'
  '<b>Black — the command being run.</b> record.sh types it for you.</div>'
  '<div class="lg"><span class="sw sw-out"></span>'
  '<b>Pale — expected output.</b> Check your screen matches.</div>'
  '</div>')
body = "\n".join(out)
body = body.replace("</h1>", "</h1>" + LEGEND, 1)
# the keyword block reads better as a soft band than as a paragraph
body = re.sub(r"<h2>Keyword coverage</h2>\s*<p>(.*?)</p>",
              r'<h2>Keyword coverage</h2><div class="kw">\1</div>', body, flags=re.S)

open(OUT, "w").write(
    "<title>Intent Gateway Recording Script</title>\n"
    "<style>%s</style>\n<div class=page>\n%s\n</div>\n" % (CSS, body))
print("wrote", OUT)
