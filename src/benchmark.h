#ifndef BENCHMARK_H
#define BENCHMARK_H

#include "utils.h"

/*
 * 벤치마크용 name 값을 하나 생성해 새 문자열로 반환한다.
 * 반환된 메모리는 호출자가 해제해야 한다.
 */
char *benchmark_generate_row_value(int index);

/*
 * 메모리 테이블과 B+ 트리 기준 벤치마크를 실행한다.
 * 성공 시 SUCCESS, 실패 시 FAILURE를 반환한다.
 */
int benchmark_run(int row_count);

#endif
