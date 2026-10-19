"""Production RGBLight mode numbers for fixtures that cannot include rgblight.h."""
import re
from pathlib import Path

PULSE_MODES = ('PULSE_OFF_PRESS', 'PULSE_ON_PRESS', 'PULSE_OFF_PRESS_HOLD', 'PULSE_ON_PRESS_HOLD')


def mode_numbers(qmk: Path) -> dict[str, int]:
    """Enum value of each rgblight_modes.h entry with every effect enabled; RGBLIGHT_MODE_zero is 0."""
    text = (qmk / 'quantum/rgblight/rgblight_modes.h').read_text(encoding='utf-8')
    names = re.findall(r'^\s*_RGBM_\w+\(\s*(\w+)', text, re.M)
    return {name: index + 1 for index, name in enumerate(names)}


def pulse_defines(qmk: Path) -> str:
    numbers = mode_numbers(qmk)
    return ''.join(f'#define RGBLIGHT_MODE_{name} {numbers[name]}\n' for name in PULSE_MODES)


def check_fixture_numbers(qmk: Path, text: str, where: str) -> None:
    """A fixture that spells the numbers out must spell production's, or its Off/On cases swap silently."""
    numbers = mode_numbers(qmk)
    for name in PULSE_MODES:
        found = re.findall(rf'RGBLIGHT_MODE_{name}\s*=?\s*(\d+)', text)
        assert found and all(int(value) == numbers[name] for value in found), (where, name, found, numbers[name])
