#pragma once
#include <Arduino.h>

// Decodeur B03 adapte a la capture A05 (durees signees en us : + = haut, - = bas)
void analyserRAW(const int32_t* d, size_t n, uint32_t perdus, uint32_t id);
void analyserFSK(const uint8_t* d, size_t n, bool crcOK, bool tronque, uint32_t id);
void testDecodeur();