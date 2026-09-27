'출력 계약의 음성 회귀: 제품 파일을 바꾸지 않고 생성된 복사본에 결함을 주입한다.'
from pathlib import Path
import os
import subprocess
from run import ROOT, HERE, BUILD, gcc
from rgb_frame_cases import generate


def main() -> None:
    out = BUILD/'rgb-frame-selftest'
    out.mkdir(parents=True, exist_ok=True)
    generated = generate(ROOT, out, 'brick60')
    original = generated.read_text(encoding='utf-8')
    layout = ['-mno-ms-bitfields'] if os.name == 'nt' else []
    flags = ['-std=gnu11','-O2','-g','-Wall','-Wextra','-Werror','-Wno-unused-function',*layout,
             f'-I{HERE}',f'-I{HERE/"ws2812_include"}',f'-I{ROOT/"src/common/hw/include"}',f'-I{ROOT/"src/ap/modules/qmk/quantum"}']
    def check(name: str, text: str, expected: str) -> None:
        source=out/(name+'.c'); binary=out/(name+('.exe' if os.name=='nt' else ''))
        source.write_text(text,encoding='utf-8')
        build=subprocess.run([gcc(),*flags,str(source),'-o',str(binary)],capture_output=True,timeout=90)
        (out/(name+'.build.log')).write_bytes(build.stdout+build.stderr)
        if expected=='compile':
            assert build.returncode!=0 and b'WS2812 reset too short' in build.stderr, (name,build.stderr)
        else:
            assert build.returncode==0,(name,build.stderr.decode(errors='replace'))
            result=subprocess.run([str(binary)],capture_output=True,timeout=30)
            (out/(name+'.run.log')).write_bytes(result.stdout+result.stderr)
            if expected=='pass': assert result.returncode==0,(name,result.stderr)
            else: assert result.returncode!=0 and b'assert' in result.stderr.lower(),(name,result.returncode,result.stderr)
        print('PASS:',name,'accepted' if expected=='pass' else 'rejected at '+expected,flush=True)

    check('positive',original,'pass')
    needle='if (rgblight_effect_pulse_presentation_pending()) expired = false;'
    assert original.count(needle)==1
    check('erased_short_pulse',original.replace(needle,'/* no output fence */'),'runtime')
    needle='rgblight_host_led_pending = false;\n    __set_PRIMASK(irq_state);'
    assert original.count(needle)==1
    check('mailbox_read_clear_race',original.replace(needle,'__set_PRIMASK(irq_state);\n    rgblight_host_led_pending = false;'),'runtime')
    needle='  (void)led_state;\n  rgblight_indicator_request_host_refresh();'
    assert original.count(needle)==1
    check('stale_layer_repost',original.replace(needle,'  rgblight_indicator_post_host_event(led_state);'),'runtime')
    driver=(ROOT/'src/hw/driver/ws2812.c').read_text(encoding='utf-8')
    shortened=driver.replace('#define WS2812_RESET_US 320U','#define WS2812_RESET_US 50U')
    assert shortened!=driver
    copy=out/'short_reset_driver.c';copy.write_text(shortened,encoding='utf-8')
    needle=(ROOT/'src/hw/driver/ws2812.c').as_posix()
    mutant=original.replace(needle,copy.as_posix())
    check('short_reset_static_guard',mutant,'compile')
    # Also prove that the receiver detects the bad framing without the static guard.
    shortened='\n'.join(line for line in shortened.splitlines() if '"WS2812 reset too short"' not in line)+'\n'
    copy.write_text(shortened,encoding='utf-8')
    check('short_reset_receiver',mutant,'runtime')
    print('RGB frame selftest passed: positive baseline and five negative controls.')


if __name__=='__main__':
    main()
