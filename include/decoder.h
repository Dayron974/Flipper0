#pragma once
#include <Arduino.h>
void analyserRAW(const int32_t* d, size_t n, uint32_t perdus, uint32_t id);
void analyserFSK(const uint8_t* d, size_t n, bool crcOK, bool tronque, uint32_t id);
void testDecodeur();
