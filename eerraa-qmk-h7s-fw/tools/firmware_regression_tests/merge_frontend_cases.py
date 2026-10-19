"""실제 matrix/action/host/SOCD 경로의 병합 provenance와 무보고 경계를 검사한다."""
import re
from pathlib import Path

from rgb_input_cases import function, generate as generate_rgb


def generate(root: Path, build: Path, source_root: Path | None = None) -> Path:
    selected = source_root or root
    qmk = selected / 'src/ap/modules/qmk'
    read = lambda path: path.read_bytes().decode('utf-8').replace('\r\n', '\n')
    source = generate_rgb(root, build, source_root)
    contents = read(source)
    util = read(qmk / 'quantum/action_util.c')
    socd = read(qmk / 'port/kill_switch.c')
    prefix = socd[socd.index('#define KILL_SWITCH_MAX_CH'):socd.index('void keyboard_report_filter(')]
    # Configuration loading/writes are adapters; pair eligibility, state and
    # the production report filter run without rewriting their statements.
    prefix = re.sub(r'^EECONFIG_DEBOUNCE_HELPER[^\n]*\n', '', prefix, flags=re.M)
    prefix = re.sub(r'^static void via_qmk_kill_switch_[^\n]*\n', '', prefix, flags=re.M)
    original_filter = function(util, 'keyboard_report_filter').rstrip()
    assert contents.count(original_filter) == 1
    contents = contents.replace(original_filter, prefix + function(socd, 'keyboard_report_filter') + function(socd, 'kill_switch_is_use'))
    original_assertions = '#include "test_rgb_input_assertions.h"'
    assert contents.count(original_assertions) == 1
    contents = contents.replace(original_assertions,
        '#define main merge_rgb_regression_main\n' + original_assertions +
        '\n#undef main\n#include "test_merge_frontend_assertions.h"')
    contents = '#define KILL_SWITCH_ENABLE\n' + contents
    output = build / 'test_merge_frontend.c'
    output.write_bytes(contents.encode('utf-8'))
    return output
