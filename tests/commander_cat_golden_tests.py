import json
from pathlib import Path
import subprocess
import sys

fixture = json.loads((Path(__file__).parent/'commander_cat_rule_cases.json').read_text(encoding='utf-8'))
for case in fixture['cases']:
    output=subprocess.check_output([sys.argv[1],case['cat_id'],','.join(case['initial'])],text=True).strip().split('\t')
    assert output == [case['tier'],str(case['protected']).lower()], (case['name'],output)
print(f"{len(fixture['cases'])} cross-platform golden cases passed")
