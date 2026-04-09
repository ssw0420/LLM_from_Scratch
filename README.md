# LLM from Scratch

**Claude Code (Opus)를 활용하여** LLM(Large Language Model)을 밑바닥부터 만들어가는 프로젝트입니다. 홍정모 연구소의 **LLM from Scratch** 강의를 기반으로 진행합니다.

### Part1 - Statistical Language Model (N-gram)

**Part1**에서는 **통계적 언어 모델(N-gram)** 을 다룹니다. N-gram 모델을 글자 단위부터 형태소 단위까지 직접 구현하며, 통계적 접근의 원리와 한계를 체험합니다. 모든 과정은 **Claude Code (Opus)** 와의 AI 페어 프로그래밍으로 진행됩니다.

> **[전체 진행 과정 보기 (HTML)](https://ssw0420.github.io/LLM_from_Scratch/Part1/docs/overview.html)** — 각 Step별 코드, 결과물, 설계 의도를 한눈에 볼 수 있습니다.

## 결과 미리보기

```
[바이그램 — 단어 생성]  주의(O) 신령(O) 패라기(X) 초대체(X) 농장(O)  → 사전 적중 ~50%

[트라이그램 — 단어 생성]  → 사전 적중 77.3% (1000개 검증)

[글자 3-gram — 문장 생성]
"마틴에게 확실히 지금 크레용을 알았어. 그렇게 되어 있었습니다.
 짙은 안개가 입고 있어야 돌아가면 도어의 촬영해 걷기 시간이며..."
→ 단어 경계 불안정, 문법 파괴

[형태소 5-gram — 문장 생성]
"당신은 로봇 놈아! 손대지 말게. 구리를 고스란히 백 퍼센트 에너지로
 바꿀 수가 없는 상태에서 특별한 방에서 나가려고 야단법석을 떨고..."
→ 같은 5-gram이지만 형태소 단위가 2~3배 넓은 범위를 커버
```

N을 올리면 품질이 좋아지지만 원문을 암기하고, N을 내리면 장거리 의존성을 잡지 못합니다 — 이것이 N-gram의 근본 한계이며, 신경망으로 전환하는 동기입니다.

## 진행 단계

| Step | 주제 | 언어 | 핵심 개념 |
|------|------|------|-----------|
| 0 | Hello World + 파일 I/O | C | 환경 세팅 |
| 1 | 한글 글자 빈도 분석 | C | UTF-8 디코딩, 유니코드 |
| 2 | 유니그램 샘플링 | C | 확률 분포, 누적 확률, 랜덤 샘플링 |
| 3 | 바이그램 단어 생성 | C | 조건부 확률, BOS/EOS, 해시맵, 사전 검증 |
| 4 | 트라이그램 단어 생성 | C | 컨텍스트 확장, N-gram 일반화 |
| 5 | N-gram 문장 생성 | C | 소설 코퍼스, 3/5/7-gram 비교, 과적합 |
| 6 | 형태소 토큰화 + N-gram | Python | Kiwi 형태소 분석, 토큰 ID, 백오프 |

각 Step의 상세 내용은 [overview.html](https://ssw0420.github.io/LLM_from_Scratch/Part1/docs/overview.html) 또는 [Part1/docs/](Part1/docs/) 폴더에서 확인할 수 있습니다.

## 환경

| 항목 | 용도 |
|------|------|
| **Claude Code (Opus)** | AI 페어 프로그래밍 — 코드 작성, 개념 설명, 디버깅 |
| C (`cl.exe`, VS2022) | Step 0~5: 저수준 구현 (UTF-8, 해시맵, N-gram) |
| Python 3.13.7 | Step 6: 데이터 전처리, 형태소 분석 (Kiwi) |
| VSCode | IDE (Claude Code 확장 사용) |

## 데이터

| 데이터 | 경로 | 설명 |
|--------|------|------|
| 사전 | `Part1/korean_dict/words_only.txt` | 국립국어원 표준국어대사전 424,155 단어 |
| 소설 코퍼스 | `Part1/data/data_all.txt` | 번역 SF 소설 35,034문장, 190만 글자 |

- 사전 출처: [korean-word-game/db](https://github.com/korean-word-game/db)

## AI 활용 방식

이 프로젝트는 **Claude Code의 Opus 모델**과 페어 프로그래밍으로 진행됩니다:

- 각 Step의 목표를 설정하고 Claude Code와 함께 구현
- Claude Code가 코드 작성, 컴파일, 실행, 결과 분석을 수행
- 개념 설명과 설계 논의를 통해 "왜 이렇게 하는가"를 함께 정리
- Claude Code의 메모리 시스템으로 세션 간 컨텍스트 유지

## 프로젝트 기록 (docs/)

`Part1/docs/` 폴더에 각 Step별 상세 진행 기록이 있습니다. Claude Code의 메모리 시스템에서 관리되던 프로젝트 기록을 공개용으로 정리한 것입니다.

> **참고:** Claude Code 메모리에는 프로젝트 기록 외에 작업 스타일/피드백 메모도 있으나, 개인 설정에 해당하므로 이 저장소에는 포함하지 않았습니다.

## 파일 구조

```
LLM_from_Scratch/
├── README.md              ← 이 파일
└── Part1/
    ├── CLAUDE.md          (프로젝트 컨텍스트 — AI 어시스턴트용)
    ├── docs/              (진행 기록 + overview.html)
    ├── korean_dict/       (사전 데이터)
    ├── data/              (소설 코퍼스)
    ├── step00_hello/      (C)
    ├── step01_charfreq/   (C)
    ├── step02_unigram/    (C)
    ├── step03_bigram/     (C)
    ├── step04_trigram/    (C)
    ├── step05_ngram_sentence/ (C)
    └── step06_tokenizer/  (Python)
```
