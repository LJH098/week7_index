# SQL Processor Specification

이 프로젝트는 SQL 입력을 파싱하고, 파일 기반 CSV 저장소에 대해 `INSERT` / `SELECT`를 수행하는 경량 SQL 처리기입니다.

## 핵심 특징

- C99 기반 구현
- 파일 모드와 REPL 모드 지원
- Soft parser / Hard parser 계층 분리
- `data/<table>.csv` 파일 기반 저장
- `WHERE` 절 단일 조건 지원
- 메모리 내 해시/정렬 인덱스를 사용한 조건 검색
- `flock()` 기반 파일 잠금

## 지원 SQL

```sql
INSERT INTO users (id, name, age) VALUES (1, 'Alice', 30);
SELECT * FROM users;
SELECT name, age FROM users WHERE age > 27;
SELECT * FROM users WHERE name = 'Bob';
```

상세 구현 내용은 소스 코드의 각 모듈 주석과 테스트 케이스를 기준으로 확인할 수 있습니다.
