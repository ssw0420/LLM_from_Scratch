---
name: Step 3 바이그램 모델 완료
description: Step 3 바이그램 모델 전체 완료 — gen/word/bos_eos/verify 4단계 진행, 다음은 Step 4
type: project
---

Step 3 바이그램 모델이 완료되었다.

**구현 흐름:**
1. gen — 고정 길이 글자 스트림 (단어 경계 없음)
2. word — EOS 토큰 도입으로 단어 단위 생성, freq_start[] 별도 배열
3. bos_eos — BOS/EOS를 BigramTable에 통합, 코드 구조 단순화 + CSV 출력
4. verify — 생성 단어를 사전(42만 단어 해시셋)과 대조, O/X + 적중률 표시

**verify 결과:** 30개 중 약 50% 적중. 짧은 단어(1~2글자)는 높은 적중률, 길어질수록 하락.

**기술 메모:**
- DictSet 해시 테이블의 buckets 배열(52만 포인터)은 힙 할당 필수 — 스택에 두면 스택 오버플로
- cl.exe에 `/utf-8` 플래그 필요 (한글 소스 C4819 경고 방지)

바이그램 모델 학습 → 생성 → 검증까지 진행한 단계.
