---
name: Step 6 형태소 토큰화 + 형태소 N-gram + 백오프 완료
description: Kiwi 형태소 분석, 토큰 ID 시퀀스, 형태소 N-gram 3/5/7/10, 백오프 시연까지 완료
type: project
---

## Step 6 완료 내용

글자 단위 N-gram(Step 5)에서 **형태소 단위 N-gram**으로 전환. Python(kiwipiepy)으로 전처리.

### 6-1. Kiwi 형태소 분석 (step06_1_tokenize.py)
- data_cleaned.txt(35,034문장) → tokens.txt
- `<SP>` 토큰으로 원문 띄어쓰기 위치 보존 (N-gram에서 띄어쓰기도 학습)
- 종성 자모(ᆫ,ᆯ 등) 후처리: 앞 토큰에 받침으로 병합 (`크 ᆫ` → `큰`)
  - 불규칙 활용(`만들+ᆫ`→`만든`)은 음운 규칙 필요하므로 미처리 (모델 학습에는 영향 없음)
- 결과: 1,476,464토큰 (그 중 <SP> 450,470개, 형태소 1,025,994개)

### 6-2. 토큰 ID 시퀀스 (step06_2_token_id.py)
- tokens.txt → vocab.txt + token_ids.txt
- 특수 토큰: PAD(0), BOS(1), EOS(2), <SP>(3)
- 어휘 크기: 16,640 (특수 3 + 일반 16,637)
- 빈도순 ID 부여 (자주 쓰는 토큰 = 작은 ID)

### 6-3. 형태소 N-gram 학습 + 생성 (step06_3_ngram.py)
- 3/5/7/10-gram 순차 학습·생성
- BOS 패딩(N-1개) + 문장 + EOS로 문장 경계 학습
- 글자 N-gram 대비 같은 N에서 2~3배 넓은 범위 커버 → 품질 향상

### 6-4. 백오프 시연 (step06_4_backoff.py)
- 후보 수 기반 백오프: 후보가 MIN_CANDIDATES(5) 미만이면 N을 낮춤
- "돈을 " 시작으로 백오프 없음(후보 1개도 사용) vs 백오프 적용(후보 5개 미만이면 N 낮춤) 비교
- 백오프 없으면 10-gram이 원문 암기 재생, 백오프 적용하면 다양한 문장 생성

### 통계 비교

| | 글자 단위 (Step 5) | 형태소 단위 (Step 6) |
|---|---|---|
| 어휘 크기 | ~11,000 | 16,640 |
| 총 토큰 수 | ~190만 | ~148만 |
| 5-gram 컨텍스트 범위 | 직전 4글자 | 직전 4형태소 (~8-12글자) |

### 파일 구조
```
step06_tokenizer/
├── data_cleaned.txt          (step05에서 복사)
├── step06_1_tokenize.py      (Kiwi 형태소 분석 + <SP> + 종성 병합)
├── tokens.txt                (탭 구분 형태소 토큰)
├── step06_2_token_id.py      (어휘 사전 + ID 변환)
├── vocab.txt                 (ID/토큰/빈도)
├── token_ids.txt             (문장별 토큰 ID 시퀀스)
├── step06_3_ngram.py         (3/5/7/10-gram 학습+생성)
├── step06_4_backoff.py       (백오프 시연, 후보 수 기반)
├── output_morpheme_n3.txt
├── output_morpheme_n5.txt
├── output_morpheme_n7.txt
├── output_morpheme_n10.txt
└── output_backoff.txt        (백오프 비교 결과)
```

### 다음 단계
N-gram의 근본 한계 확인 완료 → 신경망으로 전환 예정.
