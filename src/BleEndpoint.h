#ifndef BLEENDPOINT_H
#define BLEENDPOINT_H

#include <stdint.h>
#include <stdlib.h>

/**
 * Represents a BLE endpoint for communication.
 */
class BleEndpoint {
public:
    virtual bool Send(const uint8_t* data, size_t length) = 0;
};


#endif // BLEENDPOINT_H