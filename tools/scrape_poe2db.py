#!/usr/bin/env python3
"""Scrape runeshape combinations from poe2db.tw into combinations.json.

Usage:
    pip install requests beautifulsoup4
    python tools/scrape_poe2db.py                       # writes RuneHelper/resources/combinations.json
    python tools/scrape_poe2db.py -o out.json           # custom output path
    python tools/scrape_poe2db.py --dump-html page.html # also save raw HTML for debugging
    python tools/scrape_poe2db.py --from-html page.html # parse a saved HTML file instead of fetching
    python tools/scrape_poe2db.py --dump-localized DIR  # also save the localized pages and trade data
    python tools/scrape_poe2db.py --from-localized DIR  # parse saved localized pages and trade data instead

Every combination also gets "names": the output as the game shows it in each
client language, keyed by language code. Names the official trade site knows
come from its static item data, matched to English through the item id; that
data follows the live game. poe2db's localized pages fill in the rest, like
gems and uniques. There an output that links to an item page is matched
through that link, and outputs without a link, like "Unique Wand" or
"5x Random Currency", by their runes and count in page order. Levels are
ignored: the localized pages can be older than the English one and still carry
old levels. The braces poe2db puts around some Thai gem names are dropped.

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
"""

from __future__ import annotations

import argparse
import datetime as _dt
import json
import re
import sys
from pathlib import Path

URL = "https://poe2db.tw/Runeshape_Combinations"
LOCALIZED_URL = "https://poe2db.tw/{prefix}/Runeshape_Combinations"

LANGUAGES = {
    "ru": "ru",
    "ko": "kr",
    "ja": "jp",
    "de": "de",
    "fr": "fr",
    "es": "sp",
    "pt": "pt",
    "th": "th",
}

TRADE_URL = "https://{host}/api/trade2/data/static"
TRADE_HOSTS = {
    "en": "www.pathofexile.com",
    "ru": "ru.pathofexile.com",
    "ko": "poe.kakaogames.com",
    "ja": "jp.pathofexile.com",
    "de": "de.pathofexile.com",
    "fr": "fr.pathofexile.com",
    "es": "es.pathofexile.com",
    "pt": "br.pathofexile.com",
    "th": "th.pathofexile.com",
}
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


def fetch_html(url: str, dump_path: str | None) -> str:
    import requests

    resp = requests.get(url, headers={"User-Agent": UA}, timeout=30)
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


def make_soup(html: str):
    from bs4 import BeautifulSoup

    return BeautifulSoup(html, "html.parser")


def output_link(entry) -> str:
    headrow = entry_header(entry)
    left = headrow.find("div") if headrow is not None else None
    name_span = left.find("span") if left is not None else None
    if name_span is None:
        return ""
    for a in name_span.find_all("a", href=True):
        href = a["href"].split("?")[0].split("#")[0]
        if href.startswith("Economy_") or href in EXCLUDED_OUTPUT_HREFS:
            continue
        return re.sub(r"^/?(?:[a-z]{2}/)?", "", href)
    return ""


def page_outputs(soup) -> tuple[dict[str, str], dict[tuple, list[str]]]:
    best: list = []
    for card in soup.select("div.card"):
        entries = card.select("div.d-flex.border-top")
        if len(entries) > len(best):
            best = entries
    by_link: dict[str, str] = {}
    by_runes: dict[tuple, list[str]] = {}
    for entry in best:
        parsed = parse_entry(entry)
        if parsed is None:
            continue
        link = output_link(entry)
        if link:
            by_link.setdefault(link, parsed["output"])
            continue
        group = by_runes.setdefault((tuple(sorted(parsed["runes"])), parsed["count"]), [])
        if parsed["output"] not in group:
            group.append(parsed["output"])
    return by_link, by_runes


def localized_names(soup, localized_soup) -> tuple[dict[str, str], int]:
    english_links, english_runes = page_outputs(soup)
    localized_links, localized_runes = page_outputs(localized_soup)
    pairs = [(output, localized_links.get(link)) for link, output in english_links.items()]
    for key, outputs in english_runes.items():
        pairs.extend(zip(outputs, localized_runes.get(key, [])))
    names: dict[str, str] = {}
    conflicts = 0
    for output, name in pairs:
        if name is not None and name.startswith("{") and name.endswith("}"):
            name = name[1:-1]
        if name is not None and names.setdefault(output, name) != name:
            conflicts += 1
    return names, conflicts


def localized_html(prefix: str, from_dir: str | None, dump_dir: str | None) -> str | None:
    if from_dir:
        path = Path(from_dir) / f"{prefix}.html"
        return path.read_text(encoding="utf-8") if path.exists() else None
    html = fetch_html(LOCALIZED_URL.format(prefix=prefix), None)
    if dump_dir:
        Path(dump_dir).mkdir(parents=True, exist_ok=True)
        (Path(dump_dir) / f"{prefix}.html").write_text(html, encoding="utf-8")
    return html


def trade_items(language: str, from_dir: str | None, dump_dir: str | None) -> dict[str, str]:
    if from_dir:
        path = Path(from_dir) / f"trade_{language}.json"
        if not path.exists():
            return {}
        data = json.loads(path.read_text(encoding="utf-8"))
    else:
        import requests

        resp = requests.get(TRADE_URL.format(host=TRADE_HOSTS[language]), headers={"User-Agent": UA}, timeout=30)
        resp.raise_for_status()
        data = resp.json()
        if dump_dir:
            Path(dump_dir).mkdir(parents=True, exist_ok=True)
            (Path(dump_dir) / f"trade_{language}.json").write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")
    return {entry["id"]: entry["text"] for group in data["result"] for entry in group["entries"] if "id" in entry}


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
    ap.add_argument("--dump-localized", default=None)
    ap.add_argument("--from-localized", default=None)
    args = ap.parse_args()

    html = Path(args.from_html).read_text(encoding="utf-8") if args.from_html else fetch_html(URL, args.dump_html)
    soup = make_soup(html)
    combos = parse(soup)

    if not combos:
        print("ERROR: parsed 0 combinations — poe2db markup likely changed.", file=sys.stderr)
        print("Re-run with --dump-html page.html and inspect/adjust selectors.", file=sys.stderr)
        return 1

    outputs = {c["output"] for c in combos}
    trade_ids: dict[str, str] = {}
    for item_id, text in trade_items("en", args.from_localized, args.dump_localized).items():
        trade_ids.setdefault(text, item_id)
    for language, prefix in LANGUAGES.items():
        html_localized = localized_html(prefix, args.from_localized, args.dump_localized)
        if html_localized is None:
            print(f"WARNING: no {language} page, skipped", file=sys.stderr)
            continue
        names, conflicts = localized_names(soup, make_soup(html_localized))
        trade = trade_items(language, args.from_localized, args.dump_localized)
        from_trade = 0
        for output in outputs:
            if trade_ids.get(output) in trade:
                names[output] = trade[trade_ids[output]]
                from_trade += 1
        for c in combos:
            if c["output"] in names:
                c.setdefault("names", {})[language] = names[c["output"]]
        missing = len(outputs - names.keys())
        print(
            f"{language}: {len(outputs) - missing} of {len(outputs)} outputs named, {from_trade} from the trade site"
            + (f", {conflicts} conflicting pairs" if conflicts else "")
        )

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
    out.write_text(json.dumps(doc, indent=2, ensure_ascii=False), encoding="utf-8")

    print(f"wrote {len(combos)} combinations to {out}")
    print(f"rare runes: {len(doc['rareRunes'])}")
    print("per category:", json.dumps(by_cat, indent=2))
    print("sanity-check these against the page header counts (e.g. 'Currency /92').")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
