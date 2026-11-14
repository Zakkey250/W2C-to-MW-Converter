"""Game files managed by the converter and its standalone rollback utility."""

import json
from pathlib import Path
import re


CORE = ("TRACKS/L2RA.BUN", "GLOBAL/InGameA.bun")
STREAM = "TRACKS/STREAML2RA.BUN"
RULES = json.loads(Path(__file__).with_name("rules.json").read_bytes())


def target_core(value):
    if (not isinstance(value, dict) or set(value) != {*CORE, STREAM}
            or any(not isinstance(digest, str) or not re.fullmatch("[0-9a-f]{64}", digest)
                   for digest in value.values())):
        raise ValueError("Invalid MW target resource fingerprints.")
    return value
