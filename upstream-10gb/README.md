# 원본 저장소 자료 (MSI RTX 3080 10GB)

이 폴더는 [rough2515-hub/rtx3080-unlock-public](https://github.com/rough2515-hub/rtx3080-unlock-public) v0.10에 포함된 원작자의 측정·로그입니다. **이 저장소 소유자의 PC에서 측정한 것이 아닙니다.** 단위 테스트가 모의 하드웨어 fixture로 사용하기 때문에 보관합니다.

원작자 환경:

- MSI RTX 3080 10GB: PCI `10de:2206`, subsystem `1462:3890`, VBIOS `94.02.59.00.aa`
- ASUS TUF GAMING B660M-PLUS D4, Intel i5-13600K / UHD 770 화면 출력
- Windows 11 Pro build 26200, NVIDIA 610.60
- SS0=`0x88888888`, SS1=`0x8` 정책

원작자 측정 (4096×4096 GEMM, 별도 CUDA/cuBLAS 실행 파일):

| 측정 | 이전 TFLOPS | 적용 후 TFLOPS | 변화 |
| --- | ---: | ---: | ---: |
| FP32 pedantic | 22.057145 | 21.922047 | −0.61% |
| FP16 Tensor / FP32 누산 | 60.874030 | 109.217226 | +79.42% |
| BF16 Tensor / FP32 누산 | 60.910719 | 113.090926 | +85.67% |

- [results/benchmark.json](results/benchmark.json): 원작자 측정 자료
- [evidence/boot-03/REPORT.TXT](evidence/boot-03/REPORT.TXT): 원작자 카드의 읽기 전용 프로브 결과 (테스트 fixture)
- [evidence/unlock-boot-09/REPORT.TXT](evidence/unlock-boot-09/REPORT.TXT): 원작자 카드의 UEFI 언락 실행 로그
