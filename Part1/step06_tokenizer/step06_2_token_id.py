"""
Step 6-2: 토큰 → ID 시퀀스 변환
입력: tokens.txt (형태소 단위, 공백 구분)
출력:
  - vocab.txt       (어휘 사전: ID \t 토큰 \t 빈도)
  - token_ids.txt   (각 문장의 토큰 ID 시퀀스, 공백 구분)

LLM은 문자열이 아니라 정수 ID를 입력받는다.
이 스크립트는 어휘 사전을 만들고, 각 토큰을 ID로 변환한다.

특수 토큰:
  ID 0: <PAD>  — 패딩 (나중에 배치 처리시 길이 맞춤용, 지금은 예약만)
  ID 1: <BOS>  — 문장 시작 (Begin of Sentence)
  ID 2: <EOS>  — 문장 끝 (End of Sentence)
"""

import sys
import io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8")

from collections import Counter

INPUT_FILE = "tokens.txt"
VOCAB_FILE = "vocab.txt"
OUTPUT_FILE = "token_ids.txt"

# --- 1단계: 전체 토큰 빈도 집계 ---
freq = Counter()
with open(INPUT_FILE, "r", encoding="utf-8") as f:
    for line in f:
        tokens = line.strip().split("\t")
        freq.update(tokens)

print(f"고유 토큰 수: {len(freq):,}")
print(f"총 토큰 수: {sum(freq.values()):,}")

# --- 2단계: 어휘 사전 구축 (빈도순 정렬, 특수 토큰 먼저) ---
special_tokens = ["<PAD>", "<BOS>", "<EOS>"]
# 빈도 높은 순으로 정렬 → 자주 쓰는 토큰이 작은 ID를 받음
sorted_tokens = [tok for tok, _ in freq.most_common()]

vocab = {}  # 토큰 → ID
for i, tok in enumerate(special_tokens):
    vocab[tok] = i
for tok in sorted_tokens:
    if tok not in vocab:
        vocab[tok] = len(vocab)

id_to_token = {v: k for k, v in vocab.items()}

# 어휘 사전 저장
with open(VOCAB_FILE, "w", encoding="utf-8") as f:
    f.write("ID\tToken\tFreq\n")
    for tok_id in range(len(vocab)):
        tok = id_to_token[tok_id]
        f.write(f"{tok_id}\t{tok}\t{freq.get(tok, 0)}\n")

print(f"어휘 크기: {len(vocab):,} (특수 토큰 {len(special_tokens)}개 포함)")

# --- 3단계: 문장을 ID 시퀀스로 변환 ---
total_sentences = 0
with open(INPUT_FILE, "r", encoding="utf-8") as fin, \
     open(OUTPUT_FILE, "w", encoding="utf-8") as fout:
    for line in fin:
        tokens = line.strip().split("\t")
        if not tokens:
            continue
        ids = [str(vocab[tok]) for tok in tokens]
        fout.write(" ".join(ids) + "\n")
        total_sentences += 1

print(f"변환 완료: {total_sentences:,}문장 → {OUTPUT_FILE}")

# --- 4단계: 통계 출력 ---
print(f"\n=== 빈도 Top-20 토큰 ===")
print(f"{'ID':>6}  {'빈도':>8}  토큰")
print("-" * 30)
for tok, cnt in freq.most_common(20):
    print(f"{vocab[tok]:>6}  {cnt:>8,}  {tok}")

# 샘플 확인: 첫 3문장
print(f"\n=== 샘플: 첫 3문장의 ID 시퀀스 ===")
with open(INPUT_FILE, "r", encoding="utf-8") as ftok, \
     open(OUTPUT_FILE, "r", encoding="utf-8") as fid:
    for i in range(3):
        tok_line = ftok.readline().strip()
        id_line = fid.readline().strip()
        tok_preview = " | ".join(tok_line.split("\t")[:10])
        print(f"토큰: {tok_preview} ...")
        print(f"  ID: {id_line[:80]}...")
        # 역변환 확인
        reconstructed = " ".join(id_to_token[int(x)] for x in id_line.split()[:10])
        print(f"역변환: {reconstructed} ...")
        print()
