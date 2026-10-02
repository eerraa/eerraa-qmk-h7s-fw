#!/usr/bin/env python3
"""기존 공식 정의를 보존하고 USB 폴링 TEXT 테스트 후보를 별도 경로에 생성한다."""
import argparse
import json
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="테스트 JSON 출력 디렉터리")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source = root / "src/ap/modules/qmk/keyboards/era"
    output = args.output.resolve()
    if output.is_relative_to(source):
        parser.error("기존 보드 정의 경로에는 테스트 후보를 출력할 수 없습니다")
    candidates = []
    for path in sorted(source.glob("**/json/*-VIA.JSON")):
        raw = path.read_bytes().decode("utf-8")
        newline = "\r\n" if "\r\n" in raw else "\n"
        text = raw.replace("\r\n", "\n")
        anchor = '          "label": "USB POLLING",\n          "content": [\n'
        if text.count(anchor) != 1:
            raise ValueError(f"USB POLLING 메뉴를 하나로 식별할 수 없습니다: {path}")
        label = '''            {
              "showIf": "{id_firmware_version} >= 1",
              "label": "Keyboard USB setting (last read)",
              "type": "label",
              "content": ["id_qmk_usb_polling_current", 13, 4]
            },
'''
        candidate = text.replace(anchor, anchor + label)
        parsed = json.loads(candidate)
        original = json.loads(text)
        # Removing the single added node must reproduce every existing setting/layout.
        added = 0

        def remove_label(node):
            nonlocal added
            if isinstance(node, dict):
                return {key: remove_label(value) for key, value in node.items()}
            if isinstance(node, list):
                result = []
                for value in node:
                    if isinstance(value, dict) and value.get("content") == ["id_qmk_usb_polling_current", 13, 4]:
                        added += 1
                    else:
                        result.append(remove_label(value))
                return result
            return node

        if remove_label(parsed) != original or added != 1:
            raise ValueError(f"후보 정의가 단일 label 추가 범위를 벗어났습니다: {path}")
        candidates.append((path.name, candidate.replace("\n", newline).encode("utf-8")))
    if len(candidates) != 5:
        raise ValueError(f"지원 보드 정의 5개가 필요합니다: {len(candidates)}")
    output.mkdir(parents=True, exist_ok=True)
    for name, data in candidates:
        (output / name).write_bytes(data)
        print(output / name)


if __name__ == "__main__":
    main()
