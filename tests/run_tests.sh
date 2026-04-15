#!/bin/bash

echo "=== SQL Processor Test Suite ==="
PASS=0
FAIL=0

# 테스트 바이너리 하나를 실행하고 성공 여부를 집계하는 함수다.
# 표준 출력과 표준 에러를 임시 로그로 모아 실패 시 원인을 함께 보여준다.
run_unit_test() {
    local binary=$1
    if "$binary" >/tmp/sql_processor_test.log 2>&1; then
        echo "[PASS] $(basename "$binary")"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] $(basename "$binary")"
        cat /tmp/sql_processor_test.log
        FAIL=$((FAIL + 1))
    fi
}

# 실제 SQL 파일을 메인 바이너리로 실행해 기대 문자열이 나오는지 검사하는 함수다.
# 파일 기반 통합 시나리오를 빠르게 검증하기 위해 간단한 grep 기반 비교를 사용한다.
run_sql_test() {
    local test_name=$1
    local sql_file=$2
    local expected=$3
    local output

    rm -rf data
    mkdir -p data

    output=$(./sql_processor "$sql_file" 2>&1)
    if echo "$output" | grep -q "$expected"; then
        echo "[PASS] $test_name"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] $test_name"
        echo "Expected to find: $expected"
        echo "Actual output:"
        echo "$output"
        FAIL=$((FAIL + 1))
    fi
}

for binary in build/tests/test_tokenizer build/tests/test_parser \
              build/tests/test_storage build/tests/test_executor \
              build/tests/test_bptree \
              build/tests/test_table_runtime
do
    run_unit_test "$binary"
done

run_sql_test "Basic INSERT" "tests/test_cases/basic_insert.sql" "1 row inserted into users."
run_sql_test "Basic SELECT" "tests/test_cases/basic_select.sql" "Alice"
run_sql_test "WHERE equals" "tests/test_cases/select_where.sql" "Bob"
run_sql_test "WHERE id equals" "tests/test_cases/select_where_id.sql" "Bob"
run_sql_test "Edge cases" "tests/test_cases/edge_cases.sql" "Lee, Jr."
run_sql_test "Explicit id rejected" "tests/test_cases/duplicate_primary_key.sql" "Runtime table does not allow explicit id values."

echo ""
echo "Results: $PASS passed, $FAIL failed"

if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
