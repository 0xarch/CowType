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
