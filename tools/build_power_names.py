"""Adds the English name to analysis/sno_powers.tsv (third column) from the game's own text.

    python tools/extract_game_strings.py <pid> strings.tsv      (key<TAB>text lines read out of the running game)
    python tools/build_power_names.py strings.tsv [analysis/sno_powers.tsv]

A power's English name is looked up under its internal name with "_name" added ("DemonHunter_SmokeScreen_name" -> "Smoke
Screen"), then under the name itself, then, for item powers, under the item's name ("ItemPassive_Unique_Gem_002_x1" ->
"Unique_Gem_002_x1" -> "Bane of the Trapped"). Powers the game has no text for keep an empty column and are named from
their internal name by the host.
"""
import re
import sys


def load_strings(path):
    strings = {}
    with open(path, encoding='utf-8') as f:
        next(f)
        for line in f:
            line = line.rstrip('\n')
            if '\t' in line:
                key, text = line.split('\t', 1)
                strings[key.lower()] = text
    return strings


# Powers whose text is not under a name we can derive: the item a power belongs to is linked by game data, not by name.
# 483552: the buff of Squirt's Necklace (appears and disappears with the necklace, 10 stacks; its text is "While not taking
# damage, damage dealt is increased by up to 100% ..."), confirmed 2026-11-01.
OVERRIDES = {483552: "Squirt's Necklace"}


def short(text):
    # no markup, no placeholders, no "[TEMP] ..." test strings
    return text if text and len(text) <= 60 and not re.search(r'[{}\\\[]', text) else None


def english(code, strings):
    stripped = re.sub(r'^Generic_', '', code)
    for key in (code + '_name', stripped + '_name', code, stripped):
        text = short(strings.get(key.lower()))
        if text:
            return text
    item = re.sub(r'^(?:P\d+_)?(?:Generic_)?(?:P\d+)?[iI]temPassive_?', '', code)
    if item != code:
        for key in (item, re.sub(r'_x1$', '', item), item + '_x1'):
            text = short(strings.get(key.lower()))
            if text:
                return text
    return None


def main():
    strings = load_strings(sys.argv[1])
    path = sys.argv[2] if len(sys.argv) > 2 else 'analysis/sno_powers.tsv'
    rows = []
    with open(path, encoding='utf-8') as f:
        next(f)
        for line in f:
            parts = line.rstrip('\r\n').split('\t')
            if len(parts) >= 2:
                rows.append((int(parts[0]), parts[1]))
    named = 0
    with open(path, 'w', encoding='utf-8', newline='') as f:
        f.write('sno\tname\tenglish\n')
        for sno, code in rows:
            text = OVERRIDES.get(sno) or english(code, strings) or ''
            named += bool(text)
            f.write('%d\t%s\t%s\n' % (sno, code, text))
    print('%d powers, %d with an English name' % (len(rows), named))


if __name__ == '__main__':
    main()
