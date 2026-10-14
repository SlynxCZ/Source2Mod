#!/usr/bin/env python3
# vim: set sts=4 ts=8 sw=4 tw=99 et:
"""Generates version_gen.h from plugin-metadata.json.

plugin-metadata.json is the one place a plugin's name, author, description and
version live; this turns it into the defines plugin.h returns. Both build
systems call in here rather than each formatting the header their own way and
drifting apart -- CMake runs this as a script, AMBuild imports render() and
feeds the result to builder.AddOutputFile() so a metadata edit retriggers the
compile.

A version string may contain {{git-shorthash}}, which is replaced with the
short hash of the working tree it is generated from, or "0" outside a git
checkout.
"""

import argparse
import json
import os
import subprocess
import sys

FIELDS = [
    ('name', 'PLUGIN_NAME'),
    ('alias', 'PLUGIN_ALIAS'),
    ('display_name', 'PLUGIN_DISPLAY_NAME'),
    ('description', 'PLUGIN_DESCRIPTION'),
    ('author', 'PLUGIN_AUTHOR'),
    ('url', 'PLUGIN_URL'),
    ('log_tag', 'PLUGIN_LOGTAG'),
    ('license', 'PLUGIN_LICENSE'),
    ('version', 'PLUGIN_FULL_VERSION'),
]


def git_shorthash(path):
    try:
        out = subprocess.run(['git', 'rev-parse', '--short', 'HEAD'],
                             cwd=path, capture_output=True, text=True, timeout=10)
        if out.returncode == 0:
            return out.stdout.strip() or '0'
    except Exception:
        pass
    return '0'


def load(metadata_path):
    with open(metadata_path, 'r', encoding='utf-8') as fp:
        return json.load(fp)


def render(metadata, shorthash='0'):
    lines = [
        '/* Generated from plugin-metadata.json -- do not edit. */',
        '',
        '#ifndef _PLUGIN_METADATA_INFORMATION_H_',
        '#define _PLUGIN_METADATA_INFORMATION_H_',
        '',
    ]

    width = max(len(define) for _, define in FIELDS)

    for key, define in FIELDS:
        value = str(metadata.get(key, '')).replace('{{git-shorthash}}', shorthash)
        value = value.replace('\\', '\\\\').replace('"', '\\"')
        lines.append('#define {} "{}"'.format(define.ljust(width), value))

    lines += ['', '#endif /* _PLUGIN_METADATA_INFORMATION_H_ */', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', required=True, help='path to plugin-metadata.json')
    parser.add_argument('--output', required=True, help='path of the version_gen.h to write')
    args = parser.parse_args()

    metadata = load(args.metadata)
    text = render(metadata, git_shorthash(os.path.dirname(os.path.abspath(args.metadata))))

    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)

    # Only rewrite on change, so an unchanged metadata file does not force a rebuild.
    if os.path.exists(args.output):
        with open(args.output, 'r', encoding='utf-8') as fp:
            if fp.read() == text:
                return 0

    with open(args.output, 'w', encoding='utf-8') as fp:
        fp.write(text)
    return 0


if __name__ == '__main__':
    sys.exit(main())
