"""Builds analysis/names_en.tsv: the internal name of anything the HUD sees -> the English name the game shows.

    python tools/extract_game_strings.py <pid> strings.tsv      (the game's key/text pairs, read out of the running game)
    python tools/build_names_en.py strings.tsv

Sources, in this order (the first that has a name wins):
  * analysis/items_gbid.tsv: items (internal name -> English name, already in the game's own table);
  * analysis/sno_powers.tsv: powers (the third column, see build_power_names.py);
  * the game's text, looked up under the actor's internal name (monsters, NPCs, shrines, chests, ...): "<name>_name", then
    "<name>" itself. Only texts that look like a name are taken (capital letter, letters/spaces/punctuation, 3-48
    characters), because the pooled strings also hold unrelated pairs.
Output: name<TAB>english, sorted by name; names are matched without regard to case by the host.
"""
import re
import sys

NAME_LIKE = re.compile(r"^[A-Z0-9][A-Za-z0-9 ,.'!?:&()\-]{2,47}$")


def load_strings(path):
    strings = {}
    with open(path, encoding='utf-8') as f:
        next(f)
        for line in f:
            line = line.rstrip('\n')
            if '\t' in line:
                key, text = line.split('\t', 1)
                strings.setdefault(key.lower(), []).append(text)
    return strings


GENERIC = {'rift guardian'}   # titles the game puts on a whole class of actors, not their own name


def pick(strings, key):
    """The text of `key`: the first that is not a generic title (a key can be in several lists), else the first."""
    texts = strings.get(key.lower())
    if not texts:
        return None
    for t in texts:
        if t.lower() not in GENERIC and looks_like_name(t):
            return t
    return texts[0]


def looks_like_name(text):
    return bool(text) and bool(NAME_LIKE.match(text)) and '_' not in text and not text.lower().startswith(('test', '[', 'debug'))


def main():
    strings = load_strings(sys.argv[1])
    names = {}

    with open('analysis/items_gbid.tsv', encoding='utf-8') as f:   # gbid, sno, type, internal name, English name, ...
        next(f)
        for line in f:
            p = line.rstrip('\r\n').split('\t')
            if len(p) >= 5 and p[3] and p[4] and p[4] != '-' and p[4] != p[3] and '_' not in p[4]:
                names.setdefault(p[3], p[4])

    with open('analysis/sno_powers.tsv', encoding='utf-8') as f:
        next(f)
        for line in f:
            p = line.rstrip('\r\n').split('\t')
            if len(p) >= 3 and p[2]:
                names.setdefault(p[1], p[2])

    with open('analysis/sno_actors.tsv', encoding='utf-8') as f:
        next(f)
        for line in f:
            p = line.rstrip('\r\n').split('\t')
            if len(p) < 2 or p[1] in names:
                continue
            stripped = re.sub(r'^x1_', '', p[1], flags=re.I)
            for key in (p[1] + '_name', stripped + '_name', p[1], stripped):
                text = pick(strings, key)
                if looks_like_name(text):
                    names[p[1]] = text
                    break

    # Rift guardians: the game's text has two entries for an "*_LR_Boss_*" actor, the title "Rift Guardian" and the boss's own
    # name ("Voracity"); pick() takes the name. A guardian the game has no own name for keeps "Rift Guardian: <internal name>".
    for code in list(names):
        if names[code].lower() in GENERIC and re.search(r'LR_Boss_', code, re.I):
            readable = re.sub(r'(?<=[a-z])(?=[A-Z])', ' ', re.sub(r'^.*?LR_Boss_', '', code, flags=re.I).replace('_', ' ')).strip()
            names[code] = 'Rift Guardian: ' + readable[:1].upper() + readable[1:]

    # the generated names of rares: the game stores the GBID (hash of the key, h = h * 33 + lower(c)) of a prefix and a suffix
    def gbid(key):
        h = 0
        for c in key.lower():
            h = (h * 33 + ord(c)) & 0xFFFFFFFF
        return h
    with open('analysis/rare_names.tsv', 'w', encoding='utf-8', newline='\n') as f:
        f.write('gbid\ttext\n')
        parts = [(k, t[0]) for k, t in strings.items() if re.match(r'^raregeneric(prefix|suffix)\d+$', k)]
        for key, text in sorted(parts):
            f.write('%d\t%s\n' % (gbid(key), text))
        print('%d rare name parts' % len(parts))

    with open('analysis/names_en.tsv', 'w', encoding='utf-8', newline='\n') as f:
        f.write('name\tenglish\n')
        for name in sorted(names, key=str.lower):
            f.write('%s\t%s\n' % (name, names[name]))
    print('%d names' % len(names))


if __name__ == '__main__':
    main()
