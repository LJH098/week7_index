#include "benchmark.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 테스트 조건을 검사하고 실패 시 간단한 메시지를 출력하는 함수다.
 * benchmark 모듈의 가벼운 단위 검증에서 공통으로 사용한다.
 */
static int assert_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "[FAIL] %s\n", message);
        return FAILURE;
    }
    return SUCCESS;
}

/*
 * benchmark_generate_row_value()의 정상/실패 경로를 점검하는 테스트 함수다.
 * 값 포맷이 안정적으로 생성되는지와 버퍼 부족 실패를 함께 확인한다.
 */
int main(void) {
    char buffer[MAX_VALUE_LEN];
    char tiny_buffer[4];

    if (assert_true(benchmark_generate_row_value(buffer, sizeof(buffer), 42) == SUCCESS,
                    "benchmark row value generation should succeed for normal buffer") != SUCCESS ||
        assert_true(strcmp(buffer, "user_000042") == 0,
                    "generated benchmark value should follow deterministic format") != SUCCESS ||
        assert_true(benchmark_generate_row_value(tiny_buffer, sizeof(tiny_buffer), 42) == FAILURE,
                    "benchmark row value generation should fail for tiny buffer") != SUCCESS ||
        assert_true(benchmark_generate_row_value(buffer, sizeof(buffer), 0) == FAILURE,
                    "benchmark row value generation should reject non-positive row number") != SUCCESS) {
        return EXIT_FAILURE;
    }

    puts("[PASS] benchmark");
    return EXIT_SUCCESS;
}
