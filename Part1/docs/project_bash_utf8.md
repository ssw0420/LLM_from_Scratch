---
name: bash sort가 UTF-8 깨뜨림
description: Windows bash의 sort 명령이 UTF-8 한글 텍스트를 손상시킴. Python csv 모듈 사용할 것.
type: project
---

bash `sort` 명령으로 한글 UTF-8 텍스트를 처리하면 인코딩이 깨진다.

Step 1 전처리 중 bash sort 사용 시 한글이 손상되는 문제가 발생했다. 한글 텍스트 정렬/처리는 Python `csv` 모듈 등을 사용해야 안전하다.
