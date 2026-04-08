---
name: 학습 데이터 코퍼스 정보
description: data/ 폴더의 data_all.txt 소설 코퍼스 상세 정보 및 전처리 파생 파일
type: project
---

## data/ 폴더 (문장 코퍼스)

- `data/data_all.txt` — 한국어 소설/SF 텍스트 (35,850줄, 35,034 비어있지 않은 문장, 190만 글자, 4.5MB)
- `data/words.txt` — 용도 미확인 (별도 단어 목록으로 추정)

**data_all.txt 내용**: 번역 SF 소설 다수 포함 (랄프 124C41+, 타임머신 등). 대사(`"..."`)와 서술문 혼재.

## 전처리 파생 파일

- `step05_ngram_sentence/data_cleaned.txt` — data_all.txt에서 괄호/따옴표 제거 (Python으로 생성)
  - 제거 대상: `() {} [] "" '' "" '' 「」 《》` 등 16종
  - 제거 문자 수: 31,588개 (전체의 1.7%)
  - 줄 수 동일: 35,034줄

Step 5에서 문장 생성 학습용으로 사용. 사전(korean_dict/)과는 별개의 데이터 계통이다. 문장 단위 학습은 data/ 폴더, 단어 단위 학습은 korean_dict/ 데이터를 사용한다.
