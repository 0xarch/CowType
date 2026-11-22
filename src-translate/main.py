#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Private one-shot helper; text travels over stdin/stdout, never process arguments."""
import sys
from translation_config import TranslationError, load
from translation_client import MAX_INPUT, translate


def main():
    try:
        line = sys.stdin.buffer.readline(MAX_INPUT * 2 + 2)
        if len(line) > MAX_INPUT * 2 + 1 or not line.endswith(b'\n'):
            raise TranslationError('input')
        text = bytes.fromhex(line.decode('ascii').strip()).decode('utf-8')
        result = translate(text, load())
        sys.stdout.write('F\t' + result.encode().hex() + '\n')
    except TranslationError as error:
        sys.stdout.write('E\t' + error.code + '\n')
    except Exception:
        # Do not serialize exceptions, endpoint URLs, keys or request contents.
        sys.stdout.write('E\ttranslate.error.response\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
