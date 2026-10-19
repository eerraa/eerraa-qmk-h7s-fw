#pragma once

// V260823R1: 호스트 테스트에서 quantum/mousekey.h가 요구하는 최소 선언만 제공한다.
// 실제 펌웨어의 port/protocol/host.h는 hw_def.h/cli.h를 끌어오므로 호스트 빌드에 쓸 수 없다.
// 기본값 매크로는 stub이 아니라 진짜 mousekey.h에서 오게 두는 것이 이 파일의 목적이다.

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t report_id;
    uint8_t buttons;
    int8_t  x;
    int8_t  y;
    int8_t  v;
    int8_t  h;
} report_mouse_t;
