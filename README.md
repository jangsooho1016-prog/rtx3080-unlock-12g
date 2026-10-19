# RTX 3080 12G Unlock

MSI RTX 3080 SUPRIM X 12G LHR(`10de:220a`)에서 실행한 Windows용 UEFI 연산 언락 실험입니다. USB로 UEFI 앱을 실행해 GPU의 SS0 선택 레지스터를 바꾼 뒤, 재부팅 없이 Windows로 이어서 부팅합니다.

[rough2515-hub/rtx3080-unlock-public](https://github.com/rough2515-hub/rtx3080-unlock-public)(MSI RTX 3080 10GB용)을 이 카드에 맞게 포팅했습니다. 원본 저장소의 측정·로그는 [upstream-10gb/](upstream-10gb/README.md)에 따로 보관했고, 이 문서의 내용은 모두 아래 PC에서 직접 확인한 것입니다.

**이 PC 한 대에서 확인한 실험입니다.** 카드·PC별 값은 읽기 전용 프로브로 측정해 `unlock/board_measured.h`에 넣어야 빌드되며, 값이 맞지 않으면 실행을 거부합니다. 다른 카드에서는 프로브부터 다시 해야 합니다. 자세한 차이와 절차는 [docs/PORT-3080-12G.md](docs/PORT-3080-12G.md)에 있습니다.

## 확인한 환경 (2026-10-19)

- GPU: MSI RTX 3080 SUPRIM X 12G LHR, PCI `10de:220a`, subsystem `1462:3897`, VBIOS `94.02.85.00.6F`, BDF `0000:01:00.0`
- 메인보드: ASUS TUF GAMING Z690-PLUS D4 (BIOS 0807)
- CPU / 화면: Intel i7-12700K, UHD 770 내장그래픽 출력 (Primary Display = IGFX)
- OS / 드라이버: Windows 11 Home build 26100, NVIDIA 610.88
- 언락 정책: 기본 빌드는 SS0=`0x88888888`만 쓰고 SS1은 cold 값 `0x7` 유지. `-Ss1Full` 빌드는 SS1도 `0x8`로 씀 (둘 다 실행 확인)

## 실행 결과

**SS0 전용 빌드** — [UEFI 실행 로그](evidence/12g-unlock-run00/REPORT.TXT) 요약:

- 사전 검사(카드·화면·cold 상태·PROM 해시·자산 해시) 통과
- FWSEC: fuse 버전 2 서명으로 실행, WPR2가 기대 위치(`0x2ffe00000`)에 설정됨
- Booter(V67) 실행 후 `SS0: 0x40132104 → 0x88888888`, SS1 `0x7` 쓰기 없음
- FLR 후에도 `SS0=88888888 SS1=00000007` 유지, `FINAL status=0 ... clean=1`
- 이후 Windows 부팅, NVIDIA 610.88 드라이버로 CUDA 정상 동작

**SS0 + SS1 빌드 (`-Ss1Full`)** — [UEFI 실행 로그](evidence/12g-unlock-ss1-run00/REPORT.TXT) 요약:

- 같은 FWSEC·Booter 경로 후 `SS0: 0x40132104 → 0x88888888`, `SS1: 0x7 → 0x8`, 쓰기 후 readback 일치
- FLR 후에도 `SS0=88888888 SS1=00000008` 유지, `FINAL status=0 ... clean=1`
- 같은 전원 주기에서 앱이 다시 실행됐을 때는 cold 상태가 아니라서(`plm=0xffffffff`, SS0/SS1 이미 변경) 아무것도 하지 않고 거부됨([로그](evidence/12g-unlock-ss1-run01-recheck/REPORT.TXT))

## 측정 결과

PyTorch `torch.mm` 행렬곱, 7회 측정 중앙값입니다([analysis/gemm_bench.py](analysis/gemm_bench.py)). 전력 제한은 MSI Afterburner 설정입니다.

**8192×8192, 전력 제한 100%** ([언락 전](results/12g/before-8192-pl100.json) / [SS0](results/12g/after-8192-pl100.json) / [SS0+SS1](results/12g/after-8192-pl100-ss1.json))

| 측정 | 언락 전 | SS0만 | SS0 + SS1 | 언락 전 대비 (SS0+SS1) |
| --- | ---: | ---: | ---: | ---: |
| FP32 | 23.84 | 23.78 | 24.04 | +0.8% |
| TF32 Tensor | 31.17 | 31.17 | 60.37 | **+93.7%** |
| FP16 Tensor / FP32 누산 | 62.39 | 120.08 | 120.53 | **+93.2%** |
| BF16 Tensor / FP32 누산 | 62.36 | 123.56 | 123.58 | **+98.2%** |

(단위: TFLOPS)

**4096×4096, 전력 제한 70%** ([전](results/12g/before-4096-pl70.json) / [후](results/12g/after-4096-pl70.json))

| 측정 | 전 TFLOPS | 후 TFLOPS | 변화 |
| --- | ---: | ---: | ---: |
| FP32 | 16.66 | 16.11 | −3.3% |
| TF32 Tensor | 28.50 | 28.97 | +1.7% |
| FP16 Tensor / FP32 누산 | 58.76 | 81.45 | +38.6% |
| BF16 Tensor / FP32 누산 | 60.19 | 86.65 | +44.0% |

이 카드에서는 SS0가 FP16/BF16(FP32 누산), SS1이 TF32 처리량에 대응했습니다. 각 선택자를 바꿨을 때 해당 연산만 약 2배가 되고 나머지는 그대로였습니다. 다만 레지스터 필드별 의미가 공개 자료로 확인된 것은 아닙니다. 전력 제한 70%에서는 언락 후 처리량이 전력에 막혀 상승폭이 작았습니다. 8192/70% 언락 후 값은 [after-8192-pl70.json](results/12g/after-8192-pl70.json)에 있습니다(대응하는 언락 전 측정 없음).

이 수치는 행렬곱 처리량이며 게임 성능, 모든 AI 작업의 향상률, 계산 정확도, 장시간 안정성을 보장하지 않습니다. 효과는 전원을 완전히 끄면 사라지며, 사용할 때마다 USB로 다시 실행해야 합니다.

## 사용 순서

1. **프로브**: `tools\build-probe.ps1`로 읽기 전용 프로브를 빌드해 USB로 부팅하고, 결과로 `tools\make_board_profile.py`를 실행해 `unlock\board_measured.h`를 만듭니다. 이 PC의 프로브 결과는 [evidence/12g-probe-run00/REPORT.TXT](evidence/12g-probe-run00/REPORT.TXT)입니다.
2. **실행 자산 5개**: NVIDIA 610.43.03 소스와 `nvidia-utils` 패키지에서 생성합니다. 생성 방법은 [docs/PORT-3080-12G.md](docs/PORT-3080-12G.md#실행-자산-5개)에 있습니다. 결과는 `unlock/assets.json`의 크기·SHA256과 일치해야 합니다.
3. **빌드·패키지**: `unlock\build.ps1` → `tools\package_usb.py --assets <자산 폴더> --output <새 폴더>`
4. **실행**: [USB 준비 및 부팅 안내](docs/USB-BOOT.md)를 따릅니다. 완전 종료 → F8 → UEFI USB → `READY`에서 U 한 번 → `UEFI transaction completed and cleaned up` 후 재부팅 없이 Windows Boot Manager.

TF32까지 풀려면 `unlock\build.ps1 -Ss1Full`(SS1=`0x8`)로 빌드합니다.

## 빌드와 검사

Windows 개발 환경: LLVM/clang 12 + lld-link(VS2019 BuildTools), MSVC 환경(VS2022 Community), Python 3.9 이상.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\unlock\build.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\unlock\test-all.ps1
```

단위 테스트는 원본 10GB 카드의 기록을 모의 하드웨어로 재생합니다(`unlock/tests/board_fixture_3080_10g.h`). 자세한 내용은 [TESTING.md](TESTING.md)를 참고하세요.

## 출처와 라이선스

**GPL-2.0-only**입니다. 핵심 코드는 [Zagitta/gpu-unlock](https://github.com/Zagitta/gpu-unlock/tree/0ebdb1c6308b6a2072be5bf04513472942541956)에서, UEFI 포트는 [rough2515-hub/rtx3080-unlock-public](https://github.com/rough2515-hub/rtx3080-unlock-public)에서 가져왔습니다. SS1 보존 정책과 자산 생성 절차는 [IHaBiS02/ga107-unlock](https://github.com/IHaBiS02/ga107-unlock)을 참고했습니다. 포함된 EDK2 헤더와 관련 고지는 각각의 라이선스를 따릅니다. NVIDIA 펌웨어와 VBIOS(PROM) 이미지는 저장소에 포함하지 않습니다.

[LICENSE](LICENSE)와 [외부 코드 고지](THIRD_PARTY_NOTICES.md)를 확인하세요.
