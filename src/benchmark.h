#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <stddef.h>

#include "utils.h"

/*
 * 벤치마크용 일반 필드 값을 행 번호 기반 문자열로 생성하는 함수다.
 * 같은 입력에 항상 같은 값을 만들어 id 검색과 선형 탐색 비교에 재사용한다.
 */
int benchmark_generate_row_value(char *buffer, size_t buffer_size, int row_number);

/*
 * 메모리 기반 삽입/검색 벤치마크를 실행하고 결과를 표준 출력에 요약하는 함수다.
 * 100만 건 기준 데이터 위에서 1000회 삽입/조회 평균 시간을 계산해 출력한다.
 * 성공 시 SUCCESS, 준비나 측정 중 하나라도 실패하면 FAILURE를 반환한다.
 */
int benchmark_run(void);

#endif
