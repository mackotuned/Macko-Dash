import json
import math
from pathlib import Path


PROTOCOL_DIR = Path(__file__).parents[2] / "main" / "canbus" / "protocols"

EXPECTED = {
    "ecumasters_black": {"speed": (100, 100.0), "coolant_temp": (50, 122.0), "map": (100, -0.1921)},
    "emtron": {"speed": (1000, 100.0), "coolant_temp": (1000, 122.0), "map": (1000, -0.1921)},
    "haltech": {"speed": (1000, 100.0), "coolant_temp": (500, 122.0), "map": (1000, -0.1921)},
    "hondata": {"speed": (100, 100.0), "coolant_temp": (50, 122.0), "map": (1000, -0.1921)},
    "link_g4x": {"speed": (100, 100.0), "coolant_temp": (100, 122.0), "map": (100, 0.0)},
    "maxxecu": {"speed": (1000, 100.0), "coolant_temp": (500, 122.0), "map": (1000, -0.1921)},
    "megasquirt": {"speed": (100, 36.0), "coolant_temp": (500, 122.0), "map": (1000, -0.1921)},
}


def signals_by_name(path: Path) -> dict[str, dict]:
    document = json.loads(path.read_text(encoding="utf-8"))
    signals = {}
    for frame in document["frames"]:
        for signal in frame["signals"]:
            signals.setdefault(signal["name"], signal)
    return signals


for protocol, expectations in EXPECTED.items():
    signals = signals_by_name(PROTOCOL_DIR / f"{protocol}.json")
    for name, (raw, expected) in expectations.items():
        signal = signals[name]
        decoded = raw * signal.get("scale", 1.0) + signal.get("offset_val", 0.0)
        assert math.isclose(decoded, expected, abs_tol=0.001), (
            f"{protocol}.{name}: decoded {decoded}, expected canonical {expected}"
        )

print("Protocol unit contract passed: speed=KPH, temperature=F, pressure=PSI")