#!/usr/bin/env python3
"""
Validates the core gamedata against CS2's Linux and Windows binaries.

  * signatures -- scanned in the library's .text the way DynLibUtils does:
    exactly one match is good, more is ambiguous, none is broken
  * symbols (an "@name" signature) -- looked up in the exports
  * vtable offsets -- the class's primary vtable is found through RTTI and the
    index checked against its size; a size that changed since the last run is
    flagged, since every index after the change may have moved
  * field offsets (config.json) and interface offsets whose implementation
    class is not known are listed as not checked

Writes results.json and a Markdown report, optionally posts to a Discord
webhook, and exits 1 when anything is broken.

Modelled on swiftly-solution/gamedata-validator -- see README.md.
"""

import argparse
import datetime
import fnmatch
import json
import os
import sys
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from binaries import open_binary  # noqa: E402

PLATFORMS = ('windows', 'linux')
OK, AMBIGUOUS, CHANGED, BROKEN, UNCHECKED, NONE = 'ok', 'ambiguous', 'changed', 'broken', 'unchecked', 'none'
ICON = {OK: '🟢', AMBIGUOUS: '🟡', CHANGED: '🟡', BROKEN: '🔴', UNCHECKED: '⚪', NONE: '➖'}


# ------------------------------------------------------------------------------
# inputs
# ------------------------------------------------------------------------------

def load_gamedata(folder):
    """Every .json in the folder, gamedata.json first, first definition wins --
    the order CGameConfig::InitAll loads them in."""
    files = sorted(f for f in os.listdir(folder) if f.endswith('.json'))
    files.sort(key=lambda f: f != 'gamedata.json')
    merged = {}
    for f in files:
        with open(os.path.join(folder, f), encoding='utf-8-sig') as fh:
            for k, v in json.load(fh).items():
                merged.setdefault(k, v)
    return merged


def index_binaries(roots, libraries):
    """{platform: {library: path}} for the libraries found under the roots."""
    found = {p: {} for p in PLATFORMS}
    wanted = {}
    for lib in libraries:
        wanted['lib%s.so' % lib] = ('linux', lib)
        wanted['%s.dll' % lib] = ('windows', lib)
    walk = [w for root in roots for w in os.walk(root)]
    for dirpath, _dirs, files in walk:
        norm = dirpath.replace('\\', '/')
        if '/addons/' in norm or not norm.endswith(('/linuxsteamrt64', '/win64')):
            continue
        for f in files:
            if f in wanted:
                plat, lib = wanted[f]
                found[plat].setdefault(lib, os.path.join(dirpath, f))
    return found


class Libraries:
    """Opens binaries lazily and caches vtable sizes."""

    def __init__(self, paths):
        self.paths = paths
        self.open = {}
        self.vtables = {p: {} for p in PLATFORMS}

    def get(self, plat, lib):
        if lib == 'engine':
            lib = 'engine2'
        key = (plat, lib)
        if key not in self.open:
            path = self.paths[plat].get(lib)
            self.open[key] = open_binary(path) if path else None
        return self.open[key]

    def vtable(self, plat, cls):
        """(size, library) of the class's vtable in the first library that has it."""
        cache = self.vtables[plat]
        if cls in cache:
            return cache[cls]
        result = (None, None)
        for lib in self.paths[plat]:
            b = self.get(plat, lib)
            size = b.vtable_size(cls) if b else None
            if size:
                result = (size, lib)
                break
        cache[cls] = result
        return result


# ------------------------------------------------------------------------------
# checks
# ------------------------------------------------------------------------------

def check_signature(libs, plat, name, sig, cfg):
    lib = sig.get('library')
    value = sig.get(plat)
    if not isinstance(value, str) or not value:
        return {'status': NONE}
    b = libs.get(plat, lib)
    if b is None:
        return {'status': UNCHECKED, 'detail': 'library %s not in the download' % lib}
    if value.startswith('@'):
        va = b.symbol(value[1:])
        if va:
            return {'status': OK, 'detail': 'symbol', 'address': hex(va)}
        return {'status': BROKEN, 'detail': 'symbol %s not exported' % value[1:]}
    try:
        matches = b.scan(value)
    except ValueError as e:
        return {'status': BROKEN, 'detail': 'invalid pattern: %s' % e}
    if not matches:
        return {'status': BROKEN, 'detail': 'no match', 'matches': 0}
    res = {'matches': len(matches), 'address': hex(matches[0])}
    if len(matches) == 1 or name in cfg.get('multi_match_signatures', {}):
        res['status'] = OK
        if len(matches) > 1:
            res['detail'] = '%s matches, expected: %s' % (len(matches) if len(matches) < 10 else '10+', cfg['multi_match_signatures'][name])
    else:
        res['status'] = AMBIGUOUS
        res['detail'] = '%s matches, the first is used' % (len(matches) if len(matches) < 10 else '10+')
    return res


def check_offset(libs, plat, name, offsets, cfg, previous):
    value = offsets.get(plat)
    if not isinstance(value, int):
        return {'status': NONE}
    if '::' not in name or any(fnmatch.fnmatchcase(name, p) for p in cfg['field_offsets']):
        return {'status': UNCHECKED, 'detail': 'field offset', 'value': value}
    cls = name.split('::', 1)[0]
    cls = cfg['class_aliases'].get(cls, cls)
    size, lib = libs.vtable(plat, cls)
    if not size:
        return {'status': UNCHECKED, 'detail': 'no RTTI for %s' % cls, 'value': value}
    res = {'value': value, 'class': cls, 'library': lib, 'vtable_size': size}
    prev = previous.get('vtables', {}).get(plat, {}).get(cls)
    if value >= size:
        res['status'] = BROKEN
        res['detail'] = 'index %d past the end of %s (%d functions)' % (value, cls, size)
    elif prev and prev != size:
        res['status'] = CHANGED
        res['detail'] = '%s changed from %d to %d functions -- check the index' % (cls, prev, size)
    else:
        res['status'] = OK
    return res


def validate(gamedata, libs, cfg, previous):
    entries = []
    for name in sorted(gamedata):
        v = gamedata[name]
        if 'signatures' in v:
            kind = 'signature'
            per = {p: check_signature(libs, p, name, v['signatures'], cfg) for p in PLATFORMS}
        elif 'offsets' in v:
            kind = 'offset'
            per = {p: check_offset(libs, p, name, v['offsets'], cfg, previous) for p in PLATFORMS}
        elif 'patches' in v:
            kind = 'patch'
            per = {p: {'status': UNCHECKED, 'detail': 'patches are not validated'} for p in PLATFORMS}
        else:
            continue
        entries.append({'name': name, 'kind': kind, **per})
    return entries


# ------------------------------------------------------------------------------
# reporting
# ------------------------------------------------------------------------------

def counts(entries, kind=None):
    c = {p: {} for p in PLATFORMS}
    for e in entries:
        if kind and e['kind'] != kind:
            continue
        for p in PLATFORMS:
            s = e[p]['status']
            c[p][s] = c[p].get(s, 0) + 1
    return c


def worst(entries):
    """OK, AMBIGUOUS/CHANGED or BROKEN. "Not checked" and "no entry for the
    platform" are neither: they say nothing about whether the gamedata works."""
    order = [OK, AMBIGUOUS, CHANGED, BROKEN]
    w = OK
    for e in entries:
        for p in PLATFORMS:
            s = e[p]['status']
            if s in order and order.index(s) > order.index(w):
                w = s
    return w


def fmt_counts(c):
    parts = []
    for s in (OK, AMBIGUOUS, CHANGED, BROKEN, UNCHECKED):
        if c.get(s):
            parts.append('%s %d' % (ICON[s], c[s]))
    return ' '.join(parts) or '—'


def line(e):
    d = []
    for p in PLATFORMS:
        r = e[p]
        if e['kind'] == 'offset' and r.get('vtable_size'):
            d.append('%s %d/%d' % (p[0].upper(), r['value'], r['vtable_size']))
    extra = ' · '.join(d)
    return '%s%s `%s`%s' % (ICON[e['windows']['status']], ICON[e['linux']['status']], e['name'],
                            (' — ' + extra) if extra else '')


def markdown(result):
    b = result['build']
    out = ['# Gamedata validation', '',
           'CS2 build **%s** · Linux manifest `%s` · Windows manifest `%s` · %s' % (
               b.get('buildid') or '?', b.get('linux_manifest') or '?', b.get('windows_manifest') or '?',
               result['generated_at']), '',
           '| | Windows | Linux |', '|---|---|---|']
    for kind in ('signature', 'offset'):
        c = counts(result['entries'], kind)
        out.append('| %ss | %s | %s |' % (kind.capitalize(), fmt_counts(c['windows']), fmt_counts(c['linux'])))
    problems = [e for e in result['entries'] if any(e[p]['status'] in (BROKEN, AMBIGUOUS, CHANGED) for p in PLATFORMS)]
    out += ['', '## Needs attention' if problems else '## Everything resolves', '']
    for e in problems:
        for p in PLATFORMS:
            if e[p]['status'] in (BROKEN, AMBIGUOUS, CHANGED):
                out.append('- %s **%s** (%s): %s' % (ICON[e[p]['status']], e['name'], p, e[p].get('detail', e[p]['status'])))
    out += ['', '<details><summary>All entries (Windows, Linux)</summary>', '']
    out += ['- ' + line(e) for e in result['entries']]
    out += ['', '</details>', '',
            'Legend: 🟢 ok · 🟡 ambiguous or vtable size changed · 🔴 broken · ⚪ not checked · ➖ no entry for the platform']
    return '\n'.join(out) + '\n'


def chunk_lines(lines, limit=1024):
    chunks, cur, size = [], [], 0
    for ln in lines:
        if size + len(ln) + 1 > limit and cur:
            chunks.append('\n'.join(cur))
            cur, size = [], 0
        cur.append(ln)
        size += len(ln) + 1
    if cur:
        chunks.append('\n'.join(cur))
    return chunks


def discord_messages(result, context):
    """A list of webhook payloads, each within Discord's limits
    (10 embeds, 6000 characters, 25 fields, 1024 per field)."""
    b = result['build']
    status = worst(result['entries'])
    color = {OK: 0x3BA55D, AMBIGUOUS: 0xFEE75C, CHANGED: 0xFEE75C, BROKEN: 0xED4245}.get(status, 0x3BA55D)
    sig, off = counts(result['entries'], 'signature'), counts(result['entries'], 'offset')

    head = {
        'title': 'Gamedata validation — %s' % ('all good' if status == OK else 'needs attention' if status != BROKEN else 'broken entries'),
        'description': context,
        'color': color,
        'fields': [
            {'name': 'CS2 build', 'value': str(b.get('buildid') or '?'), 'inline': True},
            {'name': 'Linux manifest', 'value': '`%s`' % (b.get('linux_manifest') or '?'), 'inline': True},
            {'name': 'Windows manifest', 'value': '`%s`' % (b.get('windows_manifest') or '?'), 'inline': True},
            {'name': 'Signatures (Windows)', 'value': fmt_counts(sig['windows']), 'inline': True},
            {'name': 'Signatures (Linux)', 'value': fmt_counts(sig['linux']), 'inline': True},
            {'name': '​', 'value': '​', 'inline': True},
            {'name': 'Offsets (Windows)', 'value': fmt_counts(off['windows']), 'inline': True},
            {'name': 'Offsets (Linux)', 'value': fmt_counts(off['linux']), 'inline': True},
            {'name': '​', 'value': '​', 'inline': True},
        ],
        'timestamp': result['generated_at'],
    }

    problems = []
    for e in result['entries']:
        for p in PLATFORMS:
            if e[p]['status'] in (BROKEN, AMBIGUOUS, CHANGED):
                problems.append('%s `%s` (%s): %s' % (ICON[e[p]['status']], e['name'], p, e[p].get('detail', '')))
    if problems:
        for i, c in enumerate(chunk_lines(problems)[:3]):
            head['fields'].append({'name': 'Needs attention' if i == 0 else '​', 'value': c, 'inline': False})

    embeds = [head]
    for kind, title in (('signature', 'Signatures'), ('offset', 'Offsets')):
        lines = [line(e) for e in result['entries'] if e['kind'] == kind]
        for i, c in enumerate(chunk_lines(lines, 4000)):
            embeds.append({'title': '%s (Windows, Linux)%s' % (title, '' if i == 0 else ' — cont.'),
                           'description': c, 'color': color})

    # pack embeds into messages under 6000 characters / 10 embeds each
    def size(e):
        return len(e.get('title', '')) + len(e.get('description', '')) + len(e.get('footer', {}).get('text', '')) + \
            sum(len(f['name']) + len(f['value']) for f in e.get('fields', []))

    messages, cur, cur_size = [], [], 0
    for e in embeds:
        s = size(e)
        if cur and (cur_size + s > 5800 or len(cur) == 10):
            messages.append({'embeds': cur})
            cur, cur_size = [], 0
        cur.append(e)
        cur_size += s
    if cur:
        messages.append({'embeds': cur})
    return messages


def post_discord(url, messages, attachment):
    for i, msg in enumerate(messages):
        msg = dict(msg, username='Gamedata Validator', allowed_mentions={'parse': []})
        if i == len(messages) - 1 and attachment:
            boundary = 'gdv%d' % os.getpid()
            name, content = attachment
            body = (
                '--%s\r\nContent-Disposition: form-data; name="payload_json"\r\nContent-Type: application/json\r\n\r\n%s\r\n'
                '--%s\r\nContent-Disposition: form-data; name="files[0]"; filename="%s"\r\nContent-Type: application/json\r\n\r\n'
                % (boundary, json.dumps(msg), boundary, name)
            ).encode() + content + ('\r\n--%s--\r\n' % boundary).encode()
            req = urllib.request.Request(url, data=body, headers={'Content-Type': 'multipart/form-data; boundary=' + boundary})
        else:
            req = urllib.request.Request(url, data=json.dumps(msg).encode(), headers={'Content-Type': 'application/json'})
        req.add_header('User-Agent', 'Source2Mod-gamedata-validator')
        with urllib.request.urlopen(req, timeout=30) as r:
            r.read()


# ------------------------------------------------------------------------------

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    ap.add_argument('--binaries', required=True, nargs='+', help='folder(s) with the game files (searched recursively)')
    ap.add_argument('--gamedata', default=os.path.join(here, '..', '..', 'configs', 'addons', 'source2mod', 'gamedata'))
    ap.add_argument('--config', default=os.path.join(here, 'config.json'))
    ap.add_argument('--previous', help='results.json of the last run, for vtable size changes')
    ap.add_argument('--out', default='results.json')
    ap.add_argument('--markdown', help='write the Markdown report here')
    ap.add_argument('--buildid', default='')
    ap.add_argument('--linux-manifest', default='')
    ap.add_argument('--windows-manifest', default='')
    ap.add_argument('--context', default='', help='one line for the Discord message (why this ran)')
    ap.add_argument('--discord', default=os.environ.get('GAMEDATA_WEBHOOK', ''), help='webhook URL (default: $GAMEDATA_WEBHOOK)')
    args = ap.parse_args()

    with open(args.config, encoding='utf-8') as f:
        cfg = json.load(f)
    previous = {}
    if args.previous and os.path.exists(args.previous):
        with open(args.previous, encoding='utf-8') as f:
            previous = json.load(f)

    paths = index_binaries(args.binaries, cfg['libraries'])
    for p in PLATFORMS:
        print('%s: %s' % (p, ', '.join(sorted(paths[p])) or 'no libraries found'))
    if not paths['linux'] and not paths['windows']:
        print('No game libraries under %s' % ', '.join(args.binaries))
        return 2

    libs = Libraries(paths)
    entries = validate(load_gamedata(args.gamedata), libs, cfg, previous)

    # every class whose vtable was looked up, for the next run's comparison
    vtables = {p: {c: s for c, (s, _l) in libs.vtables[p].items() if s} for p in PLATFORMS}
    result = {
        'generated_at': datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0).isoformat(),
        'build': {'buildid': args.buildid, 'linux_manifest': args.linux_manifest, 'windows_manifest': args.windows_manifest},
        'entries': entries,
        'vtables': vtables,
    }
    with open(args.out, 'w', encoding='utf-8') as f:
        json.dump(result, f, indent=2)
        f.write('\n')

    md = markdown(result)
    if args.markdown:
        with open(args.markdown, 'w', encoding='utf-8') as f:
            f.write(md)
    print(md)

    if args.discord:
        try:
            with open(args.out, 'rb') as f:
                post_discord(args.discord, discord_messages(result, args.context), ('gamedata-%s.json' % (args.buildid or 'results'), f.read()))
            print('Posted to Discord.')
        except Exception as e:  # a failed notification does not fail the validation
            print('Discord webhook failed: %s' % e)

    return 1 if worst(entries) == BROKEN else 0


if __name__ == '__main__':
    sys.exit(main())
