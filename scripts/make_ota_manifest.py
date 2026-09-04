#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--file', required=True)
parser.add_argument('--version', required=True)
parser.add_argument('--path', required=True)
parser.add_argument('--output', required=True)
args = parser.parse_args()
file_path = Path(args.file)
data = file_path.read_bytes()
Path(args.output).write_text(json.dumps({
    'version': args.version,
    'size': len(data),
    'sha256': hashlib.sha256(data).hexdigest(),
    'path': args.path,
}, indent=2) + '\n')
