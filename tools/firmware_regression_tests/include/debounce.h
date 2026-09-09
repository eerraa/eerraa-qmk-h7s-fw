#pragma once
#include "matrix.h"
void debounce_init(uint8_t);
void debounce_free(void);
bool debounce(matrix_row_t[], matrix_row_t[], uint8_t, bool);
