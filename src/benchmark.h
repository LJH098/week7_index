#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <stddef.h>

/*
 * 벤치마크용 문자열 값을 생성한다.
 */
int benchmark_generate_row_value(char *buffer, size_t buffer_size,
                                 int row_number);

/*
 * 메모리 기반 삽입/조회 벤치마크를 실행한다.
 */
int benchmark_run(void);

#endif
