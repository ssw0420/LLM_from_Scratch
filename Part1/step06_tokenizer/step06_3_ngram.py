"""
Step 6-3/6-4: 형태소 N-gram 학습 + 문장 생성
입력: token_ids.txt (각 줄 = 한 문장의 토큰 ID 시퀀스)
      vocab.txt    (ID → 토큰 역매핑용)

과정:
  1. 각 문장 앞에 BOS를 (N-1)개, 뒤에 EOS 1개 추가
  2. 슬라이딩 윈도우로 N-gram 빈도 카운팅
  3. 컨텍스트별 누적 확률 테이블 구축
  4. BOS에서 시작 → 샘플링 → EOS면 종료

3, 5, 7, 10-gram 순서로 학습·생성하여 비교한다.
"""

import sys
import io
import random
from collections import defaultdict

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8")

random.seed(42)

TOKEN_IDS_FILE = "token_ids.txt"
VOCAB_FILE = "vocab.txt"

PAD_ID = 0
BOS_ID = 1
EOS_ID = 2

# --- 어휘 로드 (ID → 토큰 문자열) ---
id_to_token = {}
with open(VOCAB_FILE, "r", encoding="utf-8") as f:
    f.readline()  # 헤더 스킵
    for line in f:
        parts = line.strip().split("\t")
        tok_id = int(parts[0])
        tok = parts[1]
        id_to_token[tok_id] = tok

# --- 문장 ID 시퀀스 로드 ---
sentences = []
with open(TOKEN_IDS_FILE, "r", encoding="utf-8") as f:
    for line in f:
        ids = list(map(int, line.strip().split()))
        if ids:
            sentences.append(ids)

print(f"로드 완료: {len(sentences):,}문장, 어휘 {len(id_to_token):,}개\n")


def build_ngram(sentences, n):
    """N-gram 빈도 테이블 구축.
    컨텍스트 = 직전 (N-1)개 토큰 ID의 튜플
    값 = {다음_토큰_ID: 빈도} 딕셔너리
    """
    table = defaultdict(lambda: defaultdict(int))
    for sent in sentences:
        # BOS 패딩 + 문장 + EOS
        padded = [BOS_ID] * (n - 1) + sent + [EOS_ID]
        for i in range(len(padded) - (n - 1)):
            context = tuple(padded[i:i + n - 1])   # 직전 (N-1)개
            next_tok = padded[i + n - 1]            # 예측 대상
            table[context][next_tok] += 1
    return table


def sample_next(table, context):
    """컨텍스트에 대한 다음 토큰을 확률적으로 샘플링."""
    candidates = table.get(context)
    if not candidates:
        return EOS_ID  # 학습되지 않은 컨텍스트 → 종료
    tokens = list(candidates.keys())
    weights = list(candidates.values())
    return random.choices(tokens, weights=weights, k=1)[0]


def generate_sentence(table, n, max_tokens=200):
    """BOS에서 시작하여 EOS가 나올 때까지 샘플링."""
    context = [BOS_ID] * (n - 1)
    result = []
    for _ in range(max_tokens):
        next_tok = sample_next(table, tuple(context))
        if next_tok == EOS_ID:
            break
        result.append(next_tok)
        context = context[1:] + [next_tok]
    return result


def ids_to_text(ids):
    """토큰 ID 시퀀스를 사람이 읽을 수 있는 텍스트로 변환.
    <SP> 토큰은 공백으로 변환하고, 나머지 토큰은 직접 이어붙인다.
    """
    text = ""
    for i in ids:
        tok = id_to_token.get(i, "?")
        if tok == "<SP>":
            text += " "
        else:
            text += tok
    return text


# --- N별 학습 및 생성 ---
n_values = [3, 5, 7, 10]
num_generate = 10

for n in n_values:
    print(f"{'='*60}")
    print(f"  {n}-gram 학습 중...")
    table = build_ngram(sentences, n)
    num_contexts = len(table)
    total_entries = sum(len(v) for v in table.values())
    print(f"  컨텍스트 수: {num_contexts:,}")
    print(f"  전이 엔트리: {total_entries:,}")
    print(f"  생성 ({num_generate}문장):")
    print(f"{'='*60}")

    output_lines = []
    for i in range(num_generate):
        ids = generate_sentence(table, n)
        text = ids_to_text(ids)
        line = f"  [{i+1:2d}] {text}"
        print(line)
        output_lines.append(text)

    # 결과 파일 저장
    output_file = f"output_morpheme_n{n}.txt"
    with open(output_file, "w", encoding="utf-8") as f:
        f.write(f"{n}-gram 형태소 단위 문장 생성 (컨텍스트: {num_contexts:,})\n")
        f.write(f"전이 엔트리: {total_entries:,}\n\n")
        for j, text in enumerate(output_lines):
            f.write(f"[{j+1:2d}] {text}\n")
    print(f"  → 저장: {output_file}\n")
