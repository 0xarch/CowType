#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Reproduce the bundled CLDR keyword index from explicitly prepared, pinned data.
No runtime network access, transliteration, or model dependencies.
"""
import argparse
import hashlib
import json
import re
import string
from pathlib import Path
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent

def normalize(keyword):
    return ''.join(c for c in keyword.lower() if c not in string.whitespace + string.punctuation)

def compile_keywords(sources, aliases=""):
    presentations = {}
    for line in sources['emoji-test.txt'].splitlines():
        fields = line.split('#', 1)[0].split(';')
        if len(fields) == 2 and fields[1].strip() == 'fully-qualified':
            sequence = ''.join(chr(int(cp, 16)) for cp in fields[0].split())
            presentations[sequence.replace('\ufe0f', '')] = sequence
    entries = {}
    for name in ('annotations/zh.xml', 'annotations/en.xml', 'annotationsDerived/zh.xml', 'annotationsDerived/en.xml'):
        root = ET.fromstring(sources[name])
        for annotation in root.findall('.//annotation'):
            sequence = annotation.attrib.get('cp', '')
            if not sequence or annotation.text is None:
                continue
            sequence = presentations.get(sequence, sequence)
            rank = 0 if annotation.attrib.get('type') == 'tts' else 1
            for keyword in annotation.text.split('|'):
                keyword = normalize(keyword.strip())
                if not keyword or keyword == '↑↑↑' or any(ord(c) < 32 or ord(c) == 127 for c in keyword + sequence):
                    continue
                pair = (keyword, sequence)
                entries[pair] = min(entries.get(pair, rank), rank)
    for line in aliases.splitlines():
        if not line or line.startswith('#'):
            continue
        keyword, sequence = line.split('\t')
        entries[(normalize(keyword), sequence)] = -1
    # Grouped keys and explicit-name-first order make runtime parsing cheap and stable.
    return ''.join(f'{key}\t{sequence}\n' for (key, sequence), rank in
                   sorted(entries.items(), key=lambda item: (item[0][0], item[1], item[0][1])))

def compile_tone_families(sources):
    groups = {}
    for line in sources['emoji-test.txt'].splitlines():
        definition, separator, comment = line.partition('#')
        fields = definition.split(';')
        if not separator or len(fields) != 2 or fields[1].strip() != 'fully-qualified':
            continue
        match = re.match(r'\s*\S+\s+E[0-9.]+\s+(.+)', comment)
        if not match:
            continue
        name = match.group(1)
        # Unicode names retain gender/role qualifiers. Remove only the exact
        # skin-tone qualifiers to bridge atomic and ZWJ forms (e.g. handshake).
        name = re.sub(r'(?:medium-light|medium-dark|light|medium|dark) skin tone', '', name)
        base, _, qualifiers = name.partition(':')
        remaining = [part.strip() for part in qualifiers.split(',') if part.strip()]
        name = base.strip() + (': ' + ', '.join(remaining) if remaining else '')
        sequence = ''.join(chr(int(cp, 16)) for cp in fields[0].split())
        groups.setdefault(name, set()).add(sequence)
    rows = []
    for name, choices in sorted(groups.items()):
        if len(choices) < 2 or not any(any('\U0001f3fb' <= c <= '\U0001f3ff' for c in seq) for seq in choices):
            continue
        choices = sorted(choices, key=lambda seq: ([ord(c) for c in seq if '\U0001f3fb' <= c <= '\U0001f3ff'], seq))
        rows.append('\t'.join(choices) + '\n')
    return ''.join(rows)

def read_sources(directory):
    registry = json.loads((HERE / 'sources.json').read_text())
    result = {}
    for record in registry['files']:
        data = (directory / record['path']).read_bytes()
        if len(data) != record['bytes'] or hashlib.sha256(data).hexdigest() != record['sha256']:
            raise ValueError('CLDR source integrity mismatch: ' + record['path'])
        result[record['path']] = data.decode('utf-8')
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=HERE / 'keywords.tsv')
    parser.add_argument('--families-output', type=Path, default=HERE / 'tone-families.tsv')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    sources = read_sources(args.source_dir)
    output = compile_keywords(sources, (HERE / 'aliases.tsv').read_text())
    families = compile_tone_families(sources)
    if args.check:
        if args.families_output.read_text() != families:
            raise ValueError('Bundled emoji tone families differ from pinned sources')
        if args.output.read_text() != output:
            raise ValueError('Bundled CLDR keyword index differs from pinned sources')
    else:
        args.output.write_text(output)
        args.families_output.write_text(families)
    print(f'CLDR keyword index verified: {len(output.splitlines())} pairs, {len(output.encode())} bytes')

if __name__ == '__main__':
    main()
