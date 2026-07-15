#include "crc16.h"

uint16_t crc16(uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;

    for(size_t i=0;i<length;i++)
    {
        crc ^= data[i];

        for(int j=0;j<8;j++)
        {
            if(crc & 1)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }

    return crc;
}