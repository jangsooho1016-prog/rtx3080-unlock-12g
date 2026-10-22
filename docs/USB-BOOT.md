# USB 준비 및 부팅

이 안내는 README에 명시된 MSI RTX 3080 SUPRIM X 12G LHR / ASUS TUF GAMING Z690-PLUS D4 환경을 대상으로 합니다. 2026-10-19 이 PC에서 새 FAT32 USB로 실제 부팅·실행을 확인했습니다.

## 준비

- x64 UEFI 환경, Secure Boot 해제, Intel 내장그래픽에 연결한 화면. ASUS BIOS: `Advanced → System Agent (SA) Configuration → Graphics Configuration`에서 Primary Display=`IGFX`, iGPU Multi-Monitor=`Enabled`.
- 비어 있는 FAT32 USB. 포맷이 필요하면 USB의 기존 파일을 먼저 백업하고 대상 장치를 확인합니다.
- 빌드한 `BOOTX64.EFI` 또는 제공되는 USB 템플릿.
- `unlock/assets.json`과 이름·크기·SHA256이 일치하는 자산 5개. 자산 준비 방식은 [PORT-3080-12G.md](PORT-3080-12G.md#실행-자산-5개)를 참고합니다.

Windows 장치 암호화/BitLocker를 사용하는 PC에서는 펌웨어 설정 변경 전에 복구 키를 확인해 두세요.

## 로컬에서 패키지 만들기

저장소 루트에서 실행합니다. 먼저 EFI를 빌드한 뒤, 자산 5개를 로컬 `assets` 디렉터리에 준비합니다. 아래 명령은 새 로컬 폴더를 만들며 USB를 포맷하거나 자동으로 선택하지 않습니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\unlock\build.ps1
python .\tools\package_usb.py --assets .\assets --output .\usb-package
```

`--assets`를 생략하면 실행 자산을 뺀 템플릿이 만들어집니다. 템플릿의 `ASSETS/REQUIRED-ASSETS.json`은 필요한 파일 목록입니다. 자산 파일 자체가 아니므로, 5개 파일을 별도로 준비해야 합니다.

완성된 `usb-package` **안의 내용**을 USB 최상위에 복사합니다. `usb-package` 폴더 자체를 한 단계 아래에 넣지 않습니다.

```text
USB:/
  EFI/BOOT/BOOTX64.EFI
  RTX3080-UNLOCK/
    ASSETS/
      booter_load_ga102.bin
      v67-ga102-610.43.03.bin
      gsp_ga10x.bin
      gsp_boot_ga102.bin
      gsp_boot_desc_ga102.bin
    LICENSES/
    SOURCE.zip
  USB-README.md
  PACKAGE.json
  SHA256SUMS.txt
```

`RTX3080-UNLOCK` 폴더는 로그를 쓸 수 있어야 합니다. `BOOTX64.EFI`만 복사한 상태에서는 실행할 수 없습니다. 이 앱은 파일 내용과 보드 조건을 다시 검사합니다.

## RTX에 모니터가 연결된 경우 (rtx-gop-release 브랜치, 실기 미검증)

부팅 때 RTX 3080에 모니터가 연결되어 있으면 RTX 펌웨어 드라이버가 화면 출력(GOP) 자식 핸들을 만듭니다. 이전 빌드는 이 출력이 보이면 `stage=display`에서 거부했습니다(케이블을 뽑아야 했음).

이 브랜치는 Intel 화면이 콘솔인 경우에 한해 다음과 같이 처리합니다.

1. U를 누르기 전까지의 화면 검사에서는 RTX GOP 출력(경로 `Pci(1,0)/Pci(0,0)`, 시스템 콘솔 아님)을 `RTX output tolerated until firmware driver release`로 기록하고 허용합니다. Intel 물리 GOP가 정확히 하나라는 조건은 그대로입니다.
2. 소유권 검사에서 그 출력이 RTX 펌웨어 드라이버 하나가 만든 자식(`BY_CHILD_CONTROLLER`)이고, 각 자식이 GOP·RTX 경로·비콘솔임을 다시 확인합니다. 보이는 RTX 출력 수와 자식 수가 다르면 거부합니다.
3. U를 누르면 기존과 같이 `DisconnectController(ChildHandle=NULL)`로 드라이버를 해제합니다. UEFI 규약상 자식(GOP 출력과 그 콘솔 소비자)이 먼저 정리됩니다.
4. 해제 후 화면 검사와 트랜잭션 중 화면 검사는 이전과 같은 **엄격한** 검사입니다. RTX GOP가 남아 있으면 진행하지 않습니다.

RTX 쪽 모니터는 UEFI 단계에서 복구하지 않으므로 Windows 드라이버가 올라올 때까지 꺼져 있습니다. RTX 모니터만 연결되어 Intel 화면이 없는 환경은 여전히 지원하지 않습니다.

## 실행

1. Windows 작업을 저장하고 PC를 완전히 종료한 뒤 전원을 켭니다(빠른 시작이 켜져 있으면 Shift+시스템 종료). 이미 드라이버가 사용한 GPU의 warm 상태에서는 검사가 거부될 수 있습니다.
2. 메인보드의 부팅 메뉴에서 **UEFI USB**를 선택합니다. 이 PC(ASUS TUF GAMING Z690-PLUS D4)에서는 F8을 사용합니다.
3. `READY`까지 사전 검사가 끝나면 **U를 한 번** 누릅니다. Esc 또는 U 없이 30초 대기는 취소입니다.
4. 실행 중에는 USB를 뽑거나 재부팅하지 않습니다.
5. `UEFI transaction completed and cleaned up. Starting preloaded Windows Boot Manager.`가 표시되면 그대로 기다립니다. 앱이 미리 로드해 둔 Windows Boot Manager가 바로 실행되어 재부팅 없이 Windows로 부팅합니다.
6. 대신 `Returning to firmware`가 표시되면(아래 "Windows 자동 부팅" 참고) 펌웨어 메뉴에서 **Windows Boot Manager**를 직접 선택합니다. 중간에 재부팅하면 적용 상태가 유지되지 않을 수 있습니다.

USB 앱이 다시 열리면 Esc로 빠져나오세요. 같은 부팅에서 U를 다시 누르지 않습니다.

## Windows 자동 부팅 (체인로드)

기본 빌드는 다음 순서로 동작합니다.

1. USB의 `BOOTX64.EFI` 실행, 사전 검사, 자산 검증
2. **Windows Boot Manager 미리 로드**: USB가 아닌 파일 시스템 중 `\EFI\Microsoft\Boot\bootmgfw.efi`가 있는 볼륨이 정확히 하나일 때만 `LoadImage`로 메모리에 올려 둡니다. 실행(`StartImage`)은 하지 않으며, GPU에는 접근하지 않고 드라이버 연결(`ConnectController`)도 하지 않습니다.
3. `READY`에서 U → 언락 트랜잭션 → 로그·파일·GPU 프로토콜·자산 정리
4. **결과가 깨끗할 때만**(`FINAL status=0 ... clean=1 chainload=1`) 미리 로드한 Windows Boot Manager를 `StartImage`로 실행합니다. 펌웨어 부팅 항목과 같게 5분 watchdog을 걸고 넘기며, Windows Boot Manager가 이를 해제합니다.

`READY` 화면에 자동 부팅 여부가 표시됩니다.

- `After a clean result: preloaded Windows Boot Manager starts automatically` → 성공 시 자동으로 Windows 부팅
- `After a clean result: returns to firmware; choose Windows Boot Manager manually.` → 이전과 같이 직접 선택

미리 로드는 실행 조건이 아닙니다. 다음 경우에는 로드하지 않고 이전 동작(펌웨어로 복귀)을 유지하며, 이유는 `REPORT.TXT`의 `chainload preload:` 줄에 남습니다.

- Windows 볼륨을 찾지 못함(`candidates=0`): 펌웨어가 NVMe 드라이브를 아직 연결하지 않은 경우 등. 앱은 드라이브를 연결하지 않습니다.
- Windows가 설치된 내부 볼륨이 둘 이상(`candidates=2` 이상): 어느 쪽인지 앱이 고르지 않습니다.
- 다른 USB의 Windows 설치 미디어는 후보에서 제외합니다(`usb=`).

취소(Esc/시간 초과), 사전 검사 거부, 트랜잭션 오류, 로그·파일 닫기 실패에서는 미리 로드한 이미지를 `UnloadImage`로 해제하고(`chainload discarded`) 펌웨어로 돌아갑니다. `STOP / POWER OFF` 경로에서는 아무것도 실행하지 않습니다.

이전 동작(항상 펌웨어로 복귀)이 필요하면 `unlock\build.ps1 -NoChainload`로 빌드합니다. `-Ss1Full`과 함께 쓸 수 있습니다.

**체인로드 경로는 아직 이 PC에서 실제로 부팅해 확인하지 않았습니다.** 위의 2026-10-19 확인은 펌웨어로 돌아가 Windows Boot Manager를 직접 선택한 경로입니다. 체인로드에서는 펌웨어가 중간에 부팅 메뉴를 다시 거치지 않으므로, 처음 사용할 때 Windows에서 장치 상태와 CUDA 동작을 다시 확인하세요.

`STOP / POWER OFF`가 표시되면 전원 버튼으로 PC를 끕니다. 그 화면에서 Windows로 계속 진행하지 않습니다. 다시 시도하기 전에 `RTX3080-UNLOCK/RUNxx/REPORT.TXT`를 확인합니다.

## 적용 범위

UEFI 성공은 Windows에서의 성능 향상이나 장시간 안정성을 보장하지 않습니다. 먼저 장치 상태를 확인하고 자신의 작업에서 성능을 비교하세요. 다음 콜드부팅 뒤에도 효과가 남는지는 확인되지 않았습니다.

기존 Windows 디스크의 부트로더를 바꿀 필요는 없습니다. 체인로드도 디스크의 `bootmgfw.efi`를 읽기만 하고 수정하지 않습니다. USB 사용을 중단하려면 USB를 제거하고 Windows Boot Manager로 부팅합니다.
