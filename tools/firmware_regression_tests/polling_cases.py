"""Compile production BootMode/router and keyboard-value switch bodies unchanged."""
import re
from pathlib import Path

from source_cases import function


def generate(root: Path, build: Path) -> Path:
    qmk = root / "src/ap/modules/qmk"
    header = (qmk / "quantum/via.h").read_text(encoding="utf-8")
    enums = "\n".join(re.findall(r"enum via_(?:command_id|keyboard_value_id|qmk_usb_polling_value)\s*\{.*?\};", header, re.S))
    usb = (root / "src/hw/driver/usb/usb.h").read_text(encoding="utf-8")
    mode = re.search(r"typedef enum UsbBootMode\b.*?} UsbBootMode_t;", usb, re.S).group(0)
    default_mode = re.search(r"#ifndef USB_BOOT_MODE_DEFAULT_VALUE\n.*?#endif", usb, re.S).group(0)
    boot = (qmk / "port/bootmode.c").read_text(encoding="utf-8")
    boot = boot[boot.index("static UsbBootMode_t pending_boot_mode"):boot.rindex("#endif")]
    routers = []
    for i, path in enumerate(sorted((qmk / "keyboards/era").glob("**/port/via_port.c"))):
        source = path.read_text(encoding="utf-8")
        if "via_handle_usb_polling_channel" in source:
            routers.append(function(source, "via_handle_usb_polling_channel").replace("via_handle_usb_polling_channel", f"router_{i}"))
    assert len(routers) == 5
    via = (qmk / "quantum/via.c").read_text(encoding="utf-8")
    cases = via[via.index("        case id_get_keyboard_value:"):via.index("        case id_dynamic_keymap_get_keycode:")]
    version = re.search(r"add_compile_definitions\(VIA_FIRMWARE_VERSION=(0x[0-9A-Fa-f]+)\)", (qmk / "CMakeLists.txt").read_text()).group(1)
    preamble = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define BOOTMODE_ENABLE
#define MATRIX_ROWS 1
#define MATRIX_COLS 8
typedef uint8_t matrix_row_t;
static uint32_t timer_read32(void) { return 0; }
static uint32_t via_get_layout_options(void) { return 0; }
static matrix_row_t matrix_get_row(uint8_t r) { (void)r; return 0; }
static void via_set_layout_options(uint32_t v) { (void)v; }
static void via_set_device_indication(uint8_t v) { (void)v; }
static bool era_state_sync_via_command(uint8_t *d, uint8_t n) { (void)d; (void)n; return false; }
static unsigned bumps, applies;
static const char *label = "8000 Hz (HS)";
static const char *usbHidGetPollingLabel(void) { return label; }
static void era_state_sync_bump_config(void) { bumps++; }
'''
    stubs = r'''
static UsbBootMode_t saved = USB_BOOT_MODE_HS_8K;
static UsbBootMode_t usbBootModeGet(void) { return saved; }
static bool usbBootModeScheduleApply(UsbBootMode_t m) { saved = m; applies++; return true; }
'''
    wrapper = "static void keyboard_value(uint8_t *data, uint8_t length) { uint8_t *command_id=data, *command_data=data+1; switch (*command_id) {\n" + cases + "default: assert(false); } }\n"
    names = ",".join(re.search(r"void (router_\d+)", r).group(1) for r in routers)
    tests = r'''
int main(void) {
  void (*routers[])(uint8_t *, uint8_t) = {ROUTERS};
  const char *labels[] = {"1000 Hz (FS)", "2000 Hz (HS)", "4000 Hz (HS)", "8000 Hz (HS)", "Unavailable"};
  for (unsigned i=0; i<5; i++) {
    uint8_t p[34], before[34];
    for (unsigned l=0; l<5; l++) {
      memset(p,0xA5,sizeof(p)); p[0]=id_custom_get_value;p[1]=13;p[2]=4;
      label=labels[l]; routers[i](p,32);
      assert(p[0]==id_custom_get_value && p[1]==13 && p[2]==4);
      assert(strcmp((char *)p+3,label)==0);
      for(unsigned j=3+strlen(label); j<32; j++) assert(p[j]==0);
      assert(p[32]==0xA5 && p[33]==0xA5);
    }
    for (unsigned n=0; n<32; n++) {
      memset(p,0xA5,sizeof(p));p[0]=id_custom_get_value;p[1]=13;p[2]=4;
      memcpy(before,p,sizeof(p));routers[i](p,n);
      assert(p[0]==(n ? id_unhandled : id_custom_get_value));
      assert(memcmp(p+1,before+1,33)==0);
      routers[i](NULL,n);
    }
    unsigned old_bumps=bumps, old_applies=applies;
    memset(p,0xA5,sizeof(p));p[0]=id_custom_set_value;p[1]=13;p[2]=4;
    memcpy(before,p,sizeof(p));routers[i](p,32);assert(memcmp(p,before,34)==0);
    p[0]=id_custom_save;routers[i](p,2);
    assert(bumps==old_bumps && applies==old_applies);
    p[0]=id_custom_get_value;p[2]=3;routers[i](p,32);assert(p[0]==id_unhandled);
    p[0]=id_custom_set_value;p[2]=1;p[3]=2;routers[i](p,32);
    p[0]=id_custom_get_value;routers[i](p,32);assert(p[3]==2);
    p[0]=id_custom_set_value;p[2]=2;p[3]=1;routers[i](p,32);
    assert(saved==USB_BOOT_MODE_HS_2K && applies==old_applies+1);
    label="8000 Hz (HS)";p[0]=id_custom_get_value;p[2]=4;routers[i](p,32);
    assert(strcmp((char *)p+3,"8000 Hz (HS)")==0);
  }
  unsigned reset_bumps=bumps, reset_applies=applies;
  UsbBootMode_t actual_mode=saved;
  bootmode_publish_defaults();
  assert(pending_boot_mode==USB_BOOT_MODE_DEFAULT_VALUE && pending_boot_mode_init);
  assert(saved==actual_mode && bumps==reset_bumps && applies==reset_applies);
  for (unsigned cmd=2; cmd<=3; cmd++) {
    uint8_t p[32], before[32];
    for(unsigned i=0;i<32;i++)p[i]=(uint8_t)(i+123);
    p[0]=cmd;p[1]=7;memcpy(before,p,32);keyboard_value(p,32);
    assert(p[0]==id_unhandled && memcmp(p+1,before+1,31)==0);
  }
  uint8_t v[32]={id_get_keyboard_value,id_firmware_version};keyboard_value(v,32);
  assert(v[2]==0 && v[3]==0 && v[4]==0 && v[5]==1);
  puts("PASS: production BootMode + 5 routers: ASCII/padding/short frames/read-only/pending/Apply; retired selector echo; support revision 1");
  return 0;
}
'''.replace("ROUTERS", names)
    out = build / "test_polling_wire.c"
    out.write_text(preamble + "\n#define VIA_FIRMWARE_VERSION " + version + "\n" + enums + mode + "\n" + default_mode + "\n" + stubs + boot + "\n".join(routers) + wrapper + tests, encoding="utf-8")
    return out
