#include "function/table/basic.h"

std::vector<uint8_t> initNumTable(){
    std::vector<uint8_t> numTable(67);
    for (uint8_t i = 0; i < 64; ++i) {
        uint64_t now = (1ULL << i);
        numTable[now % 67] = i;
    }
    return numTable;
}

const std::vector<uint8_t> kuzu::function::InternalIDBitSet::numTable = initNumTable();