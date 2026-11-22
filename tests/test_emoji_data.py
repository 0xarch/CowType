# SPDX-License-Identifier: GPL-3.0-or-later
"""CLDR conversion tests use only synthetic XML, never network or user files."""
import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location('emoji_prepare', Path(__file__).resolve().parents[1] / 'src-data-tools/emoji/prepare.py')
prepare = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(prepare)

class EmojiDataTests(unittest.TestCase):
    def test_deduplication_names_inheritance_and_fully_qualified_sequences(self):
        xml = '<ldml><annotations><annotation cp="❤">heart | love | ↑↑↑</annotation><annotation cp="❤" type="tts">Red Heart</annotation><annotation cp="↑">up arrow</annotation></annotations></ldml>'
        sources = {name: xml for name in ('annotations/zh.xml', 'annotations/en.xml', 'annotationsDerived/zh.xml', 'annotationsDerived/en.xml')}
        sources['emoji-test.txt'] = '2764 FE0F ; fully-qualified # red heart\n'
        rows = prepare.compile_keywords(sources).splitlines()
        self.assertEqual(rows, ['heart\t❤️', 'love\t❤️', 'redheart\t❤️', 'uparrow\t↑'])
    def test_explicit_names_rank_before_broad_keywords(self):
        xml = '<ldml><annotation cp="😃">smile</annotation><annotation cp="😁" type="tts">smile</annotation></ldml>'
        sources = {name: xml for name in ('annotations/zh.xml', 'annotations/en.xml', 'annotationsDerived/zh.xml', 'annotationsDerived/en.xml')}
        sources['emoji-test.txt'] = ''
        self.assertEqual(prepare.compile_keywords(sources), 'smile\t😁\nsmile\t😃\n')

    def test_local_direction_alias_precedes_broad_cldr_annotation(self):
        xml = '<ldml><annotations><annotation cp="»">右</annotation></annotations></ldml>'
        sources = {name: xml for name in ('annotations/zh.xml', 'annotations/en.xml', 'annotationsDerived/zh.xml', 'annotationsDerived/en.xml')}
        sources['emoji-test.txt'] = ''
        self.assertEqual(prepare.compile_keywords(sources, '右\t→\n右箭头\t→\n'), '右\t→\n右\t»\n右箭头\t→\n')

    def test_atomic_and_zwj_variants_group_by_official_name(self):
        data = ('1F91D ; fully-qualified # 🤝 E3.0 handshake\n'
                '1F91D 1F3FB ; fully-qualified # 🤝🏻 E14.0 handshake: light skin tone\n'
                '1FAF1 1F3FB 200D 1FAF2 1F3FF ; fully-qualified # 🫱🏻‍🫲🏿 E14.0 handshake: light skin tone, dark skin tone\n'
                '1F44D ; fully-qualified # 👍 E0.6 thumbs up\n')
        self.assertEqual(prepare.compile_tone_families({'emoji-test.txt': data}), '🤝\t🤝🏻\t🫱🏻‍🫲🏿\n')

    def test_all_generated_families_have_one_owner_per_sequence(self):
        path = Path(__file__).resolve().parents[1] / 'src-data-tools/emoji/tone-families.tsv'
        seen = set()
        for line in path.read_text().splitlines():
            choices = line.split('\t')
            self.assertGreater(len(choices), 1)
            for sequence in choices:
                self.assertNotIn(sequence, seen)
                seen.add(sequence)

    def test_emoji_classification_requires_a_fully_qualified_sequence(self):
        data = ('00A9 FE0F ; fully-qualified # ©️ E0.6 copyright\n'
                '00A9 ; unqualified # © E0.6 copyright\n'
                '1F44D 1F3FD ; fully-qualified # 👍🏽 E1.0 thumbs up: medium skin tone\n')
        self.assertEqual(prepare.compile_emoji_sequences({'emoji-test.txt': data}), '©️\n👍🏽\n')
