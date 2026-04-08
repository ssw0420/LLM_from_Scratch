"""
Step 6-1: Kiwi 형태소 분석으로 토큰화
입력: data_cleaned.txt (35,034문장, 글자 단위 원문)
출력: tokens.txt (형태소 토큰, 탭 구분, 줄바꿈 = 문장 경계)

어절 경계(띄어쓰기) 보존 방식:
  Kiwi 토큰의 start 위치를 보고, 원문에서 해당 토큰 앞에 공백이 있었으면
  특수 토큰 <SP>를 삽입한다.

  예: "랄프는 울었습니다"
      → 랄프 \t 는 \t <SP> \t 울 \t 었 \t 습니다

  <SP>가 하나의 토큰으로 N-gram에 참여하므로,
  모델이 "어디에서 띄어쓰기를 하는가"도 학습하게 된다.

  출력에서 탭(\t)은 토큰 구분자이고, <SP>는 원문의 공백 위치를 나타낸다.
"""

import sys
import io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8")

from kiwipiepy import Kiwi

kiwi = Kiwi()

INPUT_FILE = "data_cleaned.txt"
OUTPUT_FILE = "tokens.txt"
SP_TOKEN = "<SP>"

# 유니코드 종성 자모 (U+11A8 ~ U+11C2) → 종성 인덱스 (1~27)
# Kiwi가 'ᆫ'(U+11AB), 'ᆯ'(U+11AF) 등을 별도 토큰으로 분리하는 경우가 있다.
# 이를 앞 토큰의 마지막 글자에 받침으로 합친다.
JONGSEONG_START = 0x11A8  # ᆨ(ㄱ)
JONGSEONG_END = 0x11C2    # ᆿ(ㅎ)


def is_trailing_jamo(s):
    """문자열이 종성 자모 1글자인지 확인."""
    return len(s) == 1 and JONGSEONG_START <= ord(s) <= JONGSEONG_END


def merge_jongseong(prev_tok, jamo):
    """앞 토큰의 마지막 글자에 종성 자모를 받침으로 합친다.
    한글 완성형 = (초성 × 21 + 중성) × 28 + 종성 + 0xAC00
    받침 없는 글자의 종성 인덱스는 0이므로, 종성 값을 더하면 된다.
    """
    if not prev_tok:
        return prev_tok + jamo  # 앞 토큰이 없으면 합칠 수 없음

    last_char = prev_tok[-1]
    cp = ord(last_char)

    # 한글 완성형 범위 확인
    if not (0xAC00 <= cp <= 0xD7A3):
        return prev_tok + jamo  # 한글이 아니면 합칠 수 없음

    # 현재 종성 인덱스 확인 (0이면 받침 없음)
    current_jongseong = (cp - 0xAC00) % 28
    if current_jongseong != 0:
        return prev_tok + jamo  # 이미 받침이 있으면 합칠 수 없음

    # 종성 자모의 인덱스 (U+11A8=1, U+11A9=2, ...)
    jong_index = ord(jamo) - JONGSEONG_START + 1
    new_char = chr(cp + jong_index)
    return prev_tok[:-1] + new_char


def tokenize_with_spacing(line):
    """문장을 형태소 분석하고, 원문 공백 위치에 <SP> 토큰을 삽입.
    종성 자모 토큰은 앞 토큰에 병합한다."""
    tokens = kiwi.tokenize(line)
    result = []
    for t in tokens:
        # 이 토큰 앞에 원문 공백이 있었는지 확인
        if t.start > 0 and line[t.start - 1] == ' ':
            result.append(SP_TOKEN)

        # 종성 자모이면 앞 토큰에 병합
        # <SP>가 사이에 끼었을 수 있으므로 제거 후 병합
        if is_trailing_jamo(t.form):
            # 직전이 <SP>이면 제거하고 그 앞 토큰에 병합
            if result and result[-1] == SP_TOKEN:
                result.pop()
            if result and result[-1] != SP_TOKEN:
                result[-1] = merge_jongseong(result[-1], t.form)
            else:
                result.append(t.form)  # 병합 불가능하면 그대로 추가
        else:
            result.append(t.form)
    return result


# --- 샘플 확인: 첫 5문장 ---
print("=== 샘플 형태소 분석 (띄어쓰기 보존) ===")
with open(INPUT_FILE, "r", encoding="utf-8") as f:
    for i, line in enumerate(f):
        if i >= 5:
            break
        line = line.strip()
        if not line:
            continue
        tokens = tokenize_with_spacing(line)
        print(f"원문: {line}")
        print(f"토큰: {' | '.join(tokens)}")
        # 복원 확인
        text = ""
        for tok in tokens:
            if tok == SP_TOKEN:
                text += " "
            else:
                text += tok
        print(f"복원: {text}")
        print()

# --- 전체 토큰화 ---
print("=== 전체 토큰화 시작 ===")
total_sentences = 0
total_tokens = 0
sp_count = 0

with open(INPUT_FILE, "r", encoding="utf-8") as fin, \
     open(OUTPUT_FILE, "w", encoding="utf-8") as fout:
    for line in fin:
        line = line.strip()
        if not line:
            continue
        tokens = tokenize_with_spacing(line)
        fout.write("\t".join(tokens) + "\n")
        total_sentences += 1
        total_tokens += len(tokens)
        sp_count += tokens.count(SP_TOKEN)

print(f"완료: {total_sentences:,}문장, {total_tokens:,}토큰")
print(f"  그 중 <SP> 토큰: {sp_count:,}개")
print(f"  형태소 토큰: {total_tokens - sp_count:,}개")
print(f"평균 토큰/문장: {total_tokens / total_sentences:.1f}")
print(f"출력: {OUTPUT_FILE}")
