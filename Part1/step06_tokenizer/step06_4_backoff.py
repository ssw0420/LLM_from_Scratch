"""
Step 6-4: N-gram 백오프(Backoff) 시연

백오프란?
  높은 N-gram은 품질이 좋지만, 후보가 적으면 과적합(암기)이다.
  후보 수가 충분할 때까지 N을 낮추는 것이 백오프.

  10-gram 후보 1개 → 암기 → N을 낮춤
   7-gram 후보 3개 → 부족 → N을 더 낮춤
   5-gram 후보 15개 → 충분! → 여기서 샘플링

출력: output_backoff.txt
"""

import sys
import io
import random
from collections import defaultdict

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8")

random.seed(42)

TOKEN_IDS_FILE = "token_ids.txt"
VOCAB_FILE = "vocab.txt"
OUTPUT_FILE = "output_backoff.txt"

PAD_ID = 0
BOS_ID = 1
EOS_ID = 2
SP_ID = 3

# 백오프 임계값: 후보가 이 수 이상이면 "충분하다"고 판단
MIN_CANDIDATES = 5

# --- 어휘 로드 ---
id_to_token = {}
token_to_id = {}
with open(VOCAB_FILE, "r", encoding="utf-8") as f:
    f.readline()
    for line in f:
        parts = line.strip().split("\t")
        tok_id = int(parts[0])
        tok = parts[1]
        id_to_token[tok_id] = tok
        token_to_id[tok] = tok_id

# --- 문장 로드 ---
sentences = []
with open(TOKEN_IDS_FILE, "r", encoding="utf-8") as f:
    for line in f:
        ids = list(map(int, line.strip().split()))
        if ids:
            sentences.append(ids)


def build_ngram(sentences, n):
    table = defaultdict(lambda: defaultdict(int))
    for sent in sentences:
        padded = [BOS_ID] * (n - 1) + sent + [EOS_ID]
        for i in range(len(padded) - (n - 1)):
            context = tuple(padded[i:i + n - 1])
            next_tok = padded[i + n - 1]
            table[context][next_tok] += 1
    return table


def ids_to_text(ids):
    text = ""
    for i in ids:
        tok = id_to_token.get(i, "?")
        if tok == "<SP>":
            text += " "
        else:
            text += tok
    return text


# --- N-gram 테이블 구축 ---
n_values = [3, 5, 7, 10]
tables = {}
for n in n_values:
    print(f"{n}-gram 학습 중...")
    tables[n] = build_ngram(sentences, n)
print("학습 완료.\n")

backoff_order = [10, 7, 5, 3]


def get_context(context_full, n):
    """context_full에서 N-gram용 컨텍스트 (N-1)개를 추출."""
    ctx_len = n - 1
    if len(context_full) >= ctx_len:
        return tuple(context_full[-ctx_len:])
    else:
        padding = [BOS_ID] * (ctx_len - len(context_full))
        return tuple(padding + list(context_full))


def sample_with_backoff(tables, context_full, backoff_order, min_cand):
    """
    백오프로 다음 토큰 샘플링.
    높은 N부터 시도하되, 후보 수가 min_cand 미만이면 더 낮은 N으로 후퇴.
    가장 낮은 N(3-gram)에서는 후보 수와 관계없이 사용.

    반환: (선택된 토큰, 사용된 N, 컨텍스트, 후보 수, 시도 로그)
    """
    tried = []  # 각 N에서의 시도 결과 기록

    for n in backoff_order:
        context = get_context(context_full, n)
        candidates = tables[n].get(context)
        num_cand = len(candidates) if candidates else 0

        if candidates:
            # 마지막 N(가장 낮은)이면 무조건 사용
            is_last = (n == backoff_order[-1])
            if num_cand >= min_cand or is_last:
                tokens = list(candidates.keys())
                weights = list(candidates.values())
                chosen = random.choices(tokens, weights=weights, k=1)[0]
                tried.append((n, num_cand, "사용"))
                return chosen, n, context, num_cand, tried
            else:
                tried.append((n, num_cand, f"후보 부족({num_cand}<{min_cand})"))
        else:
            tried.append((n, 0, "미스"))

    return EOS_ID, 0, (), 0, tried


def generate_with_log(prefix_tokens, tables, backoff_order, min_cand, max_tokens=150):
    """prefix에서 시작하여 백오프로 문장 생성."""
    max_ctx = max(backoff_order) - 1
    bos_padding = [BOS_ID] * max_ctx
    context_full = bos_padding + list(prefix_tokens)

    result_ids = list(prefix_tokens)
    log_entries = []

    for _ in range(max_tokens):
        next_tok, used_n, ctx, num_cand, tried = sample_with_backoff(
            tables, context_full, backoff_order, min_cand
        )
        if next_tok == EOS_ID:
            log_entries.append(("EOS", used_n, num_cand, tried))
            break
        result_ids.append(next_tok)
        context_full.append(next_tok)
        log_entries.append((id_to_token.get(next_tok, "?"), used_n, num_cand, tried))

    return result_ids, log_entries


# =====================================================
# 출력 시작
# =====================================================
prefix_tokens = [token_to_id["돈"], token_to_id["을"], token_to_id["<SP>"]]
out_lines = []


def out(s=""):
    print(s)
    out_lines.append(s)


out("=" * 70)
out(f"  백오프(Backoff) 시연 — 시작어: \"돈을 \"")
out(f"  임계값: 후보 {MIN_CANDIDATES}개 이상이면 충분, 미만이면 N을 낮춤")
out("=" * 70)
out()

# --- 먼저 각 N-gram에서 "돈을 " 뒤의 후보 수 비교 ---
out("▶ 각 N-gram에서 \"돈을 \" 뒤의 후보 수:")
out()
for n in n_values:
    context = get_context([BOS_ID] * (max(backoff_order) - 1) + prefix_tokens, n)
    ctx_text = [id_to_token[i] for i in context]
    candidates = tables[n].get(context)
    num_cand = len(candidates) if candidates else 0

    if candidates:
        total = sum(candidates.values())
        top3 = sorted(candidates.items(), key=lambda x: -x[1])[:3]
        top_str = ", ".join(f"\"{id_to_token[t]}\"({c}회)" for t, c in top3)
        status = f"후보 {num_cand}개"
        if num_cand < MIN_CANDIDATES:
            status += f" → 부족({num_cand}<{MIN_CANDIDATES}) → 백오프!"
        else:
            status += f" → 충분! 여기서 샘플링"
        out(f"  {n:2d}-gram  {status}")
        out(f"          상위: {top_str}")
    else:
        out(f"  {n:2d}-gram  후보 0개 → 미스 → 백오프!")
    out()

out("  → 10/7/5-gram 모두 후보 1개(암기)이므로 3-gram까지 내려감")
out("     3-gram은 \"을 <SP>\" 뒤에 올 수 있는 2,960개 후보를 가짐")
out()

# --- 백오프 문장 생성 ---
out("=" * 70)
out("  백오프 문장 생성 (5문장, 상세 로그)")
out("=" * 70)
out()

for sent_i in range(5):
    result_ids, log = generate_with_log(
        prefix_tokens, tables, backoff_order, MIN_CANDIDATES
    )
    text = ids_to_text(result_ids)

    out(f"  [{sent_i+1}] {text}")
    out()
    out(f"      토큰별 백오프 과정:")

    for step, (tok, used_n, num_cand, tried) in enumerate(log):
        # 시도 과정 표시
        tried_str = ""
        for tn, tc, reason in tried:
            if reason == "사용":
                tried_str += f"{tn}g({tc}개)→사용"
            else:
                tried_str += f"{tn}g({reason})→"

        if tok == "EOS":
            out(f"        #{step+1:3d}  {tried_str}  [문장 끝]")
        else:
            out(f"        #{step+1:3d}  {tried_str}  \"{tok}\"")
    out()

    # 백오프 통계
    n_counts = {}
    for _, used_n, _, _ in log:
        if used_n > 0:
            n_counts[used_n] = n_counts.get(used_n, 0) + 1
    total_steps = sum(n_counts.values())
    out(f"      사용 비율:")
    for n in backoff_order:
        cnt = n_counts.get(n, 0)
        if cnt > 0:
            pct = cnt * 100 // total_steps
            bar = "█" * (pct // 3)
            out(f"        {n:2d}-gram: {cnt:3d}회 ({pct:3d}%) {bar}")
    out()

# --- 비교: 백오프 없이 vs 있을 때 ---
out("=" * 70)
out("  비교: 백오프 없이(10-gram 고정) vs 백오프 적용")
out("=" * 70)
out()

random.seed(99)  # 동일 시드로 비교

# 백오프 없이 (10-gram만, 후보 1개도 사용)
result_no, log_no = generate_with_log(
    prefix_tokens, tables, [10, 7, 5, 3], min_cand=1  # 1개면 무조건 사용 = 백오프 없음
)
text_no = ids_to_text(result_no)

random.seed(99)

# 백오프 적용
result_bo, log_bo = generate_with_log(
    prefix_tokens, tables, backoff_order, min_cand=MIN_CANDIDATES
)
text_bo = ids_to_text(result_bo)

out(f"  백오프 없음 (후보 1개도 사용):")
out(f"    {text_no}")
n_counts_no = {}
for _, used_n, _, _ in log_no:
    if used_n > 0:
        n_counts_no[used_n] = n_counts_no.get(used_n, 0) + 1
stats_no = ", ".join(f"{n}g:{n_counts_no.get(n,0)}" for n in backoff_order if n_counts_no.get(n,0)>0)
out(f"    사용: {stats_no}")
out()
out(f"  백오프 적용 (후보 {MIN_CANDIDATES}개 미만이면 N을 낮춤):")
out(f"    {text_bo}")
n_counts_bo = {}
for _, used_n, _, _ in log_bo:
    if used_n > 0:
        n_counts_bo[used_n] = n_counts_bo.get(used_n, 0) + 1
stats_bo = ", ".join(f"{n}g:{n_counts_bo.get(n,0)}" for n in backoff_order if n_counts_bo.get(n,0)>0)
out(f"    사용: {stats_bo}")
out()

out("=" * 70)
out("  정리")
out("=" * 70)
out()
out("  후보 수 = 다양성의 척도:")
out("    후보 1개 → 선택지 없음 → 원문 그대로 재생 (암기/과적합)")
out("    후보 많음 → 다양한 선택 → 새로운 문장 생성 가능")
out()
out("  백오프 = 과적합 회피 전략:")
out("    높은 N에서 후보가 부족하면 → N을 낮춰서 후보를 늘림")
out("    낮은 N은 컨텍스트가 짧아 덜 정확하지만, 암기는 피함")
out()
out(f"  임계값({MIN_CANDIDATES}개)은 튜닝 가능한 하이퍼파라미터:")
out("    높이면 → 더 자주 백오프 → 다양하지만 문맥 약해짐")
out("    낮추면 → 덜 백오프 → 정확하지만 암기에 가까워짐")
out()

# 파일 저장
with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
    f.write("\n".join(out_lines) + "\n")
print(f"\n→ 결과 저장: {OUTPUT_FILE}")
