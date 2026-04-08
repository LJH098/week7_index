# SQL Processor

파일 기반 CSV 저장소 위에서 동작하는 경량 SQL 처리기입니다. `INSERT`와 `SELECT`를 지원하며, SQL 입력을 토크나이징(soft parser)과 구문 분석(hard parser)으로 나누어 처리합니다.

## Build

```bash
make
```

## Run

```bash
./sql_processor
./sql_processor tests/test_cases/basic_select.sql
```

## Test

```bash
make tests
```

## Docker

```bash
docker build -t sql-processor .
docker run -it sql-processor
```
