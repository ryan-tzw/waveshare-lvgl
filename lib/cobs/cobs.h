#ifndef COBS_H
#define COBS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

size_t cobs_encode(const uint8_t *input, size_t length, uint8_t *output,
                   size_t output_size);

size_t cobs_decode(const uint8_t *input, size_t length, uint8_t *output,
                   size_t output_size);

#endif