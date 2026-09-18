#!/usr/bin/env python3
"""Scrape runeshape combinations from poe2db.tw into combinations.json.

Usage:
    pip install requests beautifulsoup4
    python tools/scrape_poe2db.py                       # writes RuneHelper/resources/combinations.json
    python tools/scrape_poe2db.py -o out.json           # custom output path
    python tools/scrape_poe2db.py --icons DIR           # also download rune icons (needs pillow)
    python tools/scrape_poe2db.py --dump-html page.html # also save raw HTML for debugging
    python tools/scrape_poe2db.py --from-html page.html # parse a saved HTML file instead of fetching

Run this after every PoE2 patch that changes combinations, commit the result,
and upload it to the price proxy. RuneHelper embeds this file into the binary
as the offline baseline and refreshes it from the proxy at startup.

Page structure (verified 2026-07-27):
  - div.card with header "Runeshape Combinations /N" holds every combination
    with its real output name.
  - Category cards ("Alloys /14", "Currency /92", "Gems /61", "Runes /131",
    "Uniques /23") repeat the same combinations; unique entries there are
    anonymous ("Very Rare Unique item"), so categories are matched back to
    the combined list by their rune multiset.
  - Each entry is div.d-flex.border-top; the header row is
    div.d-flex.justify-content-between (output link + "xN" stack + "LvNN+"),
    followed by one <a href="X_Rune"> per required rune (duplicates repeated).
  - Every such <a> wraps the rune icon as
    <img src=".../Remnant/RemnantRune<Art>.webp">, where <Art> is the internal
    art name and does not always equal the rune name (Rage is Enrage, Volcanic
    is Gasp). The href is authoritative, the filename is not.
  - Rare runes use the RemnantRareRune prefix instead. That set goes into
    combinations.json as "rareRunes" and drives the overlay highlight.

Rune icons are only downloaded when --icons is given; nothing in RuneHelper
needs them, because a combination is identified by its output name and stack
count alone. poe2db serves the art as a bright glyph on transparency while the
game draws dark ink on parchment, so the icons are composited on black and
inverted to match the screen.
"""

from __future__ import annotations

import argparse
import datetime as _dt
import hashlib
import io
import json
import re
import sys
from pathlib import Path

URL = "https://poe2db.tw/Runeshape_Combinations"
UA = "RuneHelper-scraper/1.1 (+https://github.com/Denzeriko/RuneHelper)"

RUNE_HREF = re.compile(r"^(?:[a-z]{2}/)?([A-Za-z_]+)_Rune$")
COUNT_RE = re.compile(r"\bx\s*(\d+)\b")
LEVEL_RE = re.compile(r"Lv\.?\s*(\d+)", re.IGNORECASE)

CATEGORY_HEADERS = {
    "Alloys": "alloy",
    "Currency": "currency",
    "Gems": "gem",
    "Runes": "rune",
    "Uniques": "unique",
}

EXCLUDED_OUTPUT_HREFS = {"Rarity"}


def fetch_html(dump_path: str | None) -> str:
    import requests

    resp = requests.get(URL, headers={"User-Agent": UA}, timeout=30)
    resp.raise_for_status()
    if dump_path:
        Path(dump_path).write_text(resp.text, encoding="utf-8")
        print(f"raw HTML saved to {dump_path}")
    return resp.text


def entry_runes(entry, headrow) -> list[str]:
    runes = []
    for a in entry.find_all("a", href=True):
        if headrow is not None and headrow in a.parents:
            continue
        href = a["href"].split("?")[0].split("#")[0]
        m = RUNE_HREF.match(href)
        if m:
            runes.append(m.group(1).replace("_", " "))
    return runes


def entry_header(entry):
    return entry.select_one("div.d-flex.justify-content-between")


def parse_entry(entry) -> dict | None:
    headrow = entry_header(entry)
    if headrow is None:
        return None

    runes = entry_runes(entry, headrow)
    if not runes:
        return None

    left = headrow.find("div")
    if left is None:
        return None

    name_span = left.find("span")
    output = ""
    if name_span is not None:
        out_a = None
        for a in name_span.find_all("a", href=True):
            href = a["href"].split("?")[0]
            if href.startswith("Economy_") or href in EXCLUDED_OUTPUT_HREFS:
                continue
            out_a = a
            break
        if out_a is not None:
            output = out_a.get_text(strip=True)
        else:
            output = COUNT_RE.sub("", name_span.get_text(" ", strip=True)).strip()

    if not output:
        return None

    left_text = left.get_text(" ", strip=True)
    count = 1
    mcount = COUNT_RE.search(left_text)
    if mcount:
        count = int(mcount.group(1))

    level = 0
    mlevel = LEVEL_RE.search(left_text)
    if mlevel:
        level = int(mlevel.group(1))

    return {
        "output": output,
        "count": count,
        "level": level,
        "category": "unknown",
        "runes": runes,
    }


ICON_PREFIX = "shape_"


def parse_rare_runes(soup) -> list[str]:
    rare: set[str] = set()

    for a in soup.find_all("a", href=True):
        href = a["href"].split("?")[0].split("#")[0]
        m = RUNE_HREF.match(href)
        if not m:
            continue
        img = a.find("img", src=True)
        if img is None:
            continue
        if "RemnantRareRune" in img["src"]:
            rare.add(m.group(1).replace("_", " "))

    return sorted(rare)


def parse_icons(soup) -> dict[str, str]:
    icons: dict[str, str] = {}
    for a in soup.find_all("a", href=True):
        href = a["href"].split("?")[0].split("#")[0]
        m = RUNE_HREF.match(href)
        if not m:
            continue
        img = a.find("img", src=True)
        if img is None:
            continue
        icons.setdefault(m.group(1).replace("_", " "), img["src"])
    return icons


def download_icons(icons: dict[str, str], order: list[str], dest: Path) -> tuple[list[str], list[str]]:
    import requests
    from PIL import Image

    dest.mkdir(parents=True, exist_ok=True)
    session = requests.Session()
    session.headers["User-Agent"] = UA
    session.headers["Referer"] = URL

    digests: dict[bytes, str] = {}
    skipped: list[str] = []
    written: list[str] = []

    for rune in order:
        resp = session.get(icons[rune], timeout=30)
        resp.raise_for_status()

        key = hashlib.sha256(resp.content).digest()
        if key in digests:
            skipped.append(f"{rune} shares artwork with {digests[key]} and gets no template")
            continue

        digests[key] = rune

        image = Image.open(io.BytesIO(resp.content)).convert("RGBA")
        onBlack = Image.alpha_composite(Image.new("RGBA", image.size, (0, 0, 0, 255)), image)
        ink = onBlack.convert("L").point(lambda v: 255 - v)
        ink.save(dest / f"{ICON_PREFIX}{rune.replace(' ', '_')}.png", "PNG", optimize=True)
        written.append(rune)

    return written, skipped


def make_soup(html: str):
    from bs4 import BeautifulSoup

    return BeautifulSoup(html, "html.parser")


def parse(soup) -> list[dict]:
    combined_card = None
    category_cards = {}

    for card in soup.select("div.card"):
        header_el = card.find(class_="card-header")
        if header_el is None:
            continue
        header = header_el.get_text(strip=True)
        if header.startswith("Runeshape Combinations"):
            combined_card = card
            continue
        for prefix, category in CATEGORY_HEADERS.items():
            if header.startswith(prefix):
                category_cards[category] = card
                break

    if combined_card is None:
        print("ERROR: combined 'Runeshape Combinations' card not found.", file=sys.stderr)
        return []

    # Category lookup from the category cards: exact (output, runes) match
    # where names are available, rune-multiset fallback for anonymous entries
    # (uniques are listed as "Very Rare Unique item").
    category_by_name_runes: dict[tuple, str] = {}
    category_by_runes: dict[tuple, str] = {}
    for category, card in category_cards.items():
        for entry in card.select("div.d-flex.border-top"):
            parsed = parse_entry(entry)
            if parsed is None:
                continue
            rune_key = tuple(sorted(parsed["runes"]))
            category_by_runes.setdefault(rune_key, category)
            if "Unique item" not in parsed["output"]:
                category_by_name_runes[(parsed["output"], rune_key)] = category

    combos: list[dict] = []
    seen: set[tuple] = set()
    uncategorized = 0

    for entry in combined_card.select("div.d-flex.border-top"):
        combo = parse_entry(entry)
        if combo is None:
            continue

        key = (combo["output"], tuple(sorted(combo["runes"])))
        if key in seen:
            continue
        seen.add(key)

        rune_key = tuple(sorted(combo["runes"]))
        combo["category"] = (
            category_by_name_runes.get((combo["output"], rune_key))
            or category_by_runes.get(rune_key, "unknown")
        )
        if combo["category"] == "unknown":
            uncategorized += 1

        combos.append(combo)

    if uncategorized:
        print(f"note: {uncategorized} combinations could not be matched to a category card")

    return combos


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", default="RuneHelper/resources/combinations.json")
    ap.add_argument("--dump-html", default=None)
    ap.add_argument("--from-html", default=None)
    ap.add_argument("--icons", default=None)
    args = ap.parse_args()

    html = Path(args.from_html).read_text(encoding="utf-8") if args.from_html else fetch_html(args.dump_html)
    soup = make_soup(html)
    combos = parse(soup)

    if not combos:
        print("ERROR: parsed 0 combinations — poe2db markup likely changed.", file=sys.stderr)
        print("Re-run with --dump-html page.html and inspect/adjust selectors.", file=sys.stderr)
        return 1

    by_cat: dict[str, int] = {}
    for c in combos:
        by_cat[c["category"]] = by_cat.get(c["category"], 0) + 1

    doc = {
        "source": URL,
        "generated": _dt.date.today().isoformat(),
        "complete": True,
        "rareRunes": parse_rare_runes(soup),
        "combinations": sorted(combos, key=lambda c: (c["category"], c["output"], -c["count"])),
    }
    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(doc, indent=2), encoding="utf-8")

    print(f"wrote {len(combos)} combinations to {out}")
    print(f"rare runes: {len(doc['rareRunes'])}")
    print("per category:", json.dumps(by_cat, indent=2))
    print("sanity-check these against the page header counts (e.g. 'Currency /92').")

    if not args.icons:
        return 0

    icons = parse_icons(soup)
    used = {rune for c in combos for rune in c["runes"]}
    absent = sorted(used - set(icons))

    if absent:
        print(f"ERROR: no icon found for {len(absent)} runes: {', '.join(absent)}", file=sys.stderr)
        return 1

    recipe_count: dict[str, int] = {}
    for c in combos:
        for rune in set(c["runes"]):
            recipe_count[rune] = recipe_count.get(rune, 0) + 1

    order = sorted(used, key=lambda r: (-recipe_count.get(r, 0), r))
    written, skipped = download_icons({r: icons[r] for r in used}, order, Path(args.icons))

    for stale in Path(args.icons).glob(f"{ICON_PREFIX}*.png"):
        if stale.stem[len(ICON_PREFIX):].replace("_", " ") not in written:
            stale.unlink()

    print(f"wrote {len(written)} runeshape icons to {args.icons}")

    for note in skipped:
        print(f"WARNING: {note}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
