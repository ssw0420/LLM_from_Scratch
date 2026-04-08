# Part1 프로젝트 컨텍스트

## 환경

- OS: Windows 11 Home (x64)
- IDE: VSCode (Claude Code 확장)
- 컴파일러: Visual Studio 2022 Community (`cl.exe`)
- Python: 3.13.7 (데이터 전처리용)
- 셸: Git Bash (기본), PowerShell (컴파일 시)

## 컴파일 방법

Claude Code Bash에서 cl.exe를 사용하려면 PowerShell 래퍼가 필요하다:

```
powershell.exe -NoProfile -Command "Import-Module 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Microsoft.VisualStudio.DevShell.dll'; Enter-VsDevShell -VsInstallPath 'C:\Program Files\Microsoft Visual Studio\2022\Community' -SkipAutomaticLocation; Set-Location 'c:\Users\user\Documents\GitHub\LLM_from_Scratch\Part1'; cl /nologo <파일명>.c; .\<파일명>.exe"
```

bash에서 cmd.exe로 vcvars64.bat을 호출하는 방식은 경로 공백/따옴표 충돌로 실패한다. PowerShell + Enter-VsDevShell 방식만 동작함.

## 콘솔 한글 출력

Windows 기본 코드 페이지 CP949에서 UTF-8 출력이 깨진다. C 코드에서 `SetConsoleOutputCP(65001)` (`windows.h` 필요)로 해결.

## 데이터 (korean_dict/)

- `kr_korean.csv` — 국립국어원 표준국어대사전 원본 (508,142줄, 단어+품사)
  - 출처: https://github.com/korean-word-game/db (Google Drive 다운로드)
- `words_only.txt` — 전처리 완료 (424,155개 단어)
- `kr_korean.zip` — 다운로드 원본 (삭제 가능)

### 전처리 과정 (Python으로 수행)

1. CSV에서 단어 컬럼 추출
2. `-` 시작 접사/어미 제거 (508,142 → 505,652)
3. 중복 제거 (505,652 → 426,729)
4. PUA(U+E000~F8FF) 포함 단어 제거 (426,729 → 424,155)

PUA 문자: 옛한글을 유니코드 사설 영역에 매핑한 것. 폰트 없으면 빈 네모로 표시됨.

## 진행 완료

### Step 0: hello.c

- Hello World, 파일 I/O (`fprintf`로 output.txt에 출력)

### Step 1: step01_charfreq.c (검증 완료)

- 한글 완성형(U+AC00~U+D7A3) 글자 빈도 분석, UTF-8 직접 구현
- Python 교차 검증으로 결과 일치 확인됨
- 전체 결과: output_charfreq.txt

### Step 2: 유니그램 샘플링 텍스트 생성

- step01의 글자 빈도를 확률 분포로 변환 → 누적 확률 테이블 구축 → 랜덤 샘플링
- gen1: output_charfreq.txt 파싱 방식 / gen2: words_only.txt 직접 카운팅 방식
- 결과: 의미 없는 글자 나열 (유니그램의 한계 확인)

### Step 3: 바이그램 모델

- 글자 단위 바이그램 (prev → curr) 해시맵으로 카운팅 → 누적 확률 → 샘플링
- **gen**: 고정 길이(200자) 글자 스트림 생성 (단어 경계 없음)
- **word**: CP_END(EOS) 토큰 도입 → 단어 단위 생성 가능. freq_start[] 별도 배열로 시작 분포 관리
- **bos_eos**: BOS/EOS를 BigramTable에 통합 → freq_start[] 별도 배열 제거, 생성 루프 단순화
  - CP_BOS(0x0002) → 첫글자 전이로 시작 분포 표현
  - 마지막글자 → CP_EOS(0x0001) 전이로 종료 학습
  - 생성: current=BOS에서 시작 → sample → EOS면 종료, 로직 하나로 통일
  - output_bigram_table.csv 출력 (앞글자,뒷글자,빈도 / UTF-8 BOM / 131,853행)
- 모델 성능(확률분포)은 word와 bos_eos가 동일. bos_eos는 코드 구조 개선(리팩터링)
- **verify**: 생성된 단어를 사전(words_only.txt)과 대조하여 O/X 표시 + 적중률 계산
  - 사전 42만 단어를 해시셋(체이닝)으로 로드 → 생성 단어와 완전 일치 비교
  - 결과: 30개 중 약 50% 적중. 짧은 단어는 잘 맞지만 길어질수록 적중률 하락

### Step 4: 트라이그램 모델

- 바이그램(직전 1글자) → 트라이그램(직전 2글자)으로 컨텍스트 확장
- 키: 단일 코드포인트 → (cp1, cp2) 쌍, 해시 함수도 hash_pair()로 변경
- BOS 패딩: N-gram은 단어 앞에 (N-1)개의 BOS를 붙임 (트라이그램 = BOS 2개)
- **step04_trigram.c**: BOS/EOS 통합 + CSV 출력(360,575행) + 사전 검증(30개)
- **step04_trigram_1000.c**: 1000개 대량 생성 검증
  - 결과: 773/1000 (77.3%) 적중 — 바이그램 ~50% 대비 큰 향상
- 컴파일 시 `/utf-8` 플래그 필수 (한글 문자열 리터럴 깨짐 방지)

### Step 5: N-gram 문장 생성

- 단어 생성(사전) → **문장 생성(소설 코퍼스)**으로 확장
- 데이터: `data/data_all.txt` (35,034문장, 190만 글자, 번역 SF 소설)
- **모든 문자를 토큰으로 처리**: 공백·쉼표·마침표 등도 한글과 동등한 코드포인트
- 자료구조 일반화: `TrigramRow(cp1,cp2)` → `NgramRow(context[N-1])`, 해시도 FNV-1a 기반 `hash_context()` 로 변경
- **step05_ngram_sentence.c**: 3/5/7-gram 순차 학습·생성 비교
  - 3-gram: 컨텍스트 37,921개 — 단어 경계 불안정, 문법 파괴
  - 5-gram: 컨텍스트 480,542개 — 어절 자연스러움, 절 간 연결 끊김
  - 7-gram: 컨텍스트 1,146,338개 — 원문급이지만 과적합(암기)
- **step05_1_cleaned.c**: 괄호·따옴표 제거 데이터(`data_cleaned.txt`)로 학습
  - Python 전처리로 `(){}[]""''「」《》` 등 31,588개 제거
  - 품질 차이 미미 (전체의 1.7% 제거, N-gram 동작 방식 동일)
- N-gram 한계 확인: N↑ → 품질↑이지만 원문 암기, N↓ → 장거리 의존성 불가
  - 데이터 분리 학습, 데이터양 증가로도 근본 한계 해결 불가 → 신경망 동기

### Step 6: 형태소 토큰화 + 형태소 N-gram + 백오프

- **글자 → 형태소 단위 전환**: Kiwi(kiwipiepy) 형태소 분석기로 Python 전처리
- **step06_1_tokenize.py**: Kiwi 형태소 분석 + `<SP>` 띄어쓰기 토큰 + 종성 자모 병합
  - `<SP>` 토큰: 원문 공백 위치를 N-gram 학습 대상으로 포함
  - 종성 병합: `크 ᆫ` → `큰`, `하 ᆯ` → `할` (불규칙 활용은 미처리)
  - 결과: 1,476,464토큰 (어휘 16,640개)
- **step06_2_token_id.py**: 어휘 사전(vocab.txt) + 토큰 ID 시퀀스(token_ids.txt)
  - 특수 토큰: PAD(0), BOS(1), EOS(2), `<SP>`(3)
  - LLM이 문자열이 아닌 정수 ID를 입력받는 구조를 도입
- **step06_3_ngram.py**: 형태소 N-gram 3/5/7/10-gram 학습·생성
  - 같은 N에서 글자 대비 2~3배 넓은 범위 커버 → 품질 향상
- **step06_4_backoff.py**: 후보 수 기반 백오프 시연
  - 후보 < 임계값(5개)이면 과적합 → N을 낮춤
  - 백오프 없음(원문 암기) vs 백오프 적용(다양한 생성) 비교

## 파일 구조

```
Part1/
├── .claude/settings.local.json
├── .gitignore
├── CLAUDE.md                    ← 이 파일
├── README.md
├── korean_dict/
│   ├── kr_korean.csv
│   ├── kr_korean.zip
│   └── words_only.txt
├── step00_hello/
│   ├── hello.c
│   └── output.txt
├── step01_charfreq/
│   ├── step01_charfreq.c
│   └── output_charfreq.txt
└── step02_unigram/
    ├── step02_unigram_gen.c     (output_charfreq.txt 파싱 방식)
    ├── step02_unigram_gen2.c    (words_only.txt 직접 카운팅 방식)
    ├── output_unigram.txt
    └── output_unigram2.txt
step03_bigram/
    ├── step03_bigram_gen.c      (고정 길이 글자 스트림)
    ├── step03_bigram_word.c     (EOS 토큰으로 단어 단위 생성)
    ├── step03_bigram_bos_eos.c  (BOS/EOS 통합 버전 + CSV 출력)
    ├── step03_bigram_verify.c   (생성 단어 사전 대조 검증)
    ├── output_bigram.txt
    ├── output_bigram_word.txt
    ├── output_bigram_bos_eos.txt
    ├── output_bigram_table.csv  (앞글자,뒷글자,빈도 — 131,853행)
    └── output_bigram_verify.txt (생성 단어 O/X + 적중률)
step04_trigram/
    ├── step04_trigram.c             (BOS/EOS 통합 + CSV + 검증 30개)
    ├── step04_trigram_1000.c        (1000개 대량 검증)
    ├── output_trigram_table.csv     (글자1,글자2,글자3,빈도 — 360,575행)
    ├── output_trigram_verify.txt    (30개 생성 O/X + 적중률)
    └── output_trigram_verify_1000.txt (1000개 생성 O/X + 적중률 77.3%)
data/
    ├── data_all.txt                 (소설 코퍼스 원본 — 35,034문장)
    └── words.txt
step05_ngram_sentence/
    ├── step05_ngram_sentence.c      (3/5/7-gram 문장 생성 — 원본 데이터)
    ├── step05_1_cleaned.c           (3/5/7-gram — 괄호/따옴표 제거 데이터)
    ├── data_cleaned.txt             (전처리 완료 데이터)
    ├── output_ngram_n3.txt          (원본 3-gram 결과)
    ├── output_ngram_n5.txt          (원본 5-gram 결과)
    ├── output_ngram_n7.txt          (원본 7-gram 결과)
    ├── output_cleaned_n3.txt        (정제 3-gram 결과)
    ├── output_cleaned_n5.txt        (정제 5-gram 결과)
    └── output_cleaned_n7.txt        (정제 7-gram 결과)
step06_tokenizer/
    ├── data_cleaned.txt             (step05에서 복사)
    ├── step06_1_tokenize.py         (Kiwi 형태소 분석 + <SP> + 종성 병합)
    ├── tokens.txt                   (탭 구분 형태소 토큰)
    ├── step06_2_token_id.py         (어휘 사전 + ID 변환)
    ├── vocab.txt                    (ID/토큰/빈도 — 16,640행)
    ├── token_ids.txt                (문장별 토큰 ID 시퀀스)
    ├── step06_3_ngram.py            (3/5/7/10-gram 학습+생성)
    ├── step06_4_backoff.py          (후보 수 기반 백오프 시연)
    ├── output_morpheme_n3.txt       (형태소 3-gram 결과)
    ├── output_morpheme_n5.txt       (형태소 5-gram 결과)
    ├── output_morpheme_n7.txt       (형태소 7-gram 결과)
    ├── output_morpheme_n10.txt      (형태소 10-gram 결과)
    └── output_backoff.txt           (백오프 비교 결과)
```
