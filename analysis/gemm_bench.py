"""RTX 3080 언락 전/후 GEMM 처리량 비교용 벤치마크 (PyTorch).

사용법:
  python gemm_bench.py --label before     # 언락 전 (일반 부팅)
  python gemm_bench.py --label after      # 언락 후 (USB 언락 -> Windows)
  python gemm_bench.py --compare bench-before.json bench-after.json

측정 항목 (4096x4096 행렬곱, 반복 중앙값):
  FP32        : TF32 끔, CUDA 코어 FP32
  TF32 Tensor : SS1 쪽과 관련될 수 있는 항목 (SS0 전용 언락에서는 변화 없음이 정상)
  FP16 Tensor : FP16 입력, FP32 누산  <- SS0 언락 대상
  BF16 Tensor : BF16 입력, FP32 누산  <- SS0 언락 대상
"""
import argparse
import json
import statistics
import sys


def measure(torch, dtype, n, iters, repeats, tf32=False):
    torch.backends.cuda.matmul.allow_tf32 = tf32
    torch.backends.cudnn.allow_tf32 = tf32
    a = torch.randn(n, n, device="cuda", dtype=dtype)
    b = torch.randn(n, n, device="cuda", dtype=dtype)
    for _ in range(10):
        torch.mm(a, b)
    torch.cuda.synchronize()
    results = []
    for _ in range(repeats):
        start = torch.cuda.Event(enable_timing=True)
        end = torch.cuda.Event(enable_timing=True)
        start.record()
        for _ in range(iters):
            torch.mm(a, b)
        end.record()
        torch.cuda.synchronize()
        seconds = start.elapsed_time(end) / 1000.0
        results.append(2.0 * n * n * n * iters / seconds / 1e12)
    return statistics.median(results)


def run(label, n, iters, repeats):
    try:
        import torch
    except ImportError:
        sys.exit("PyTorch가 없습니다: pip install torch --index-url https://download.pytorch.org/whl/cu128")
    if not torch.cuda.is_available():
        sys.exit("CUDA GPU를 찾지 못했습니다.")
    name = torch.cuda.get_device_name(0)
    print(f"GPU: {name} | torch {torch.__version__} | CUDA {torch.version.cuda}")
    tests = [
        ("FP32", torch.float32, False),
        ("TF32 Tensor", torch.float32, True),
        ("FP16 Tensor / FP32 acc", torch.float16, False),
        ("BF16 Tensor / FP32 acc", torch.bfloat16, False),
    ]
    out = {"label": label, "gpu": name, "torch": torch.__version__, "n": n, "results": {}}
    for title, dtype, tf32 in tests:
        tflops = measure(torch, dtype, n, iters, repeats, tf32)
        out["results"][title] = tflops
        print(f"  {title:<24} {tflops:8.2f} TFLOPS")
    path = f"bench-{label}.json"
    with open(path, "w", encoding="utf-8") as f:
        json.dump(out, f, indent=2, ensure_ascii=False)
    print(f"저장: {path}")


def compare(before_path, after_path):
    with open(before_path, encoding="utf-8") as f:
        before = json.load(f)
    with open(after_path, encoding="utf-8") as f:
        after = json.load(f)
    print(f"{'항목':<24} {'전':>9} {'후':>9} {'변화':>9}")
    for key, b in before["results"].items():
        a = after["results"].get(key)
        if a is None:
            continue
        print(f"{key:<24} {b:9.2f} {a:9.2f} {(a / b - 1) * 100:+8.1f}%")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--label", help="결과 이름 (예: before, after)")
    p.add_argument("--compare", nargs=2, metavar=("BEFORE", "AFTER"))
    p.add_argument("--n", type=int, default=4096)
    p.add_argument("--iters", type=int, default=50)
    p.add_argument("--repeats", type=int, default=7)
    a = p.parse_args()
    if a.compare:
        compare(*a.compare)
    elif a.label:
        run(a.label, a.n, a.iters, a.repeats)
    else:
        p.print_help()


if __name__ == "__main__":
    main()
