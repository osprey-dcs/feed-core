#include <iostream>
#include <stdexcept>
#include <algorithm>

#include <epicsMath.h>
#include <errlog.h>

#include "simulator.h"

#define PI (3.141592653589793)

// 2 * PI / 360
#define TWOPI_360 (0.017453292519943295)

static inline double deg2rad(double deg) {
    return deg * TWOPI_360;
}

Simulator_HIRES::Simulator_HIRES(const osiSockAddr& ep,
              const JBlob& blob,
              const values_t& initial)
    :Simulator(ep, blob, initial)
{
    banyan.reset = &(*this)["banyan_reset"];
    banyan.reset_bit = 0u;
    banyan.status = &(*this)["banyan_status"];
    banyan.status_bit = 30u;
    banyan.buffer = &(*this)["banyan_data"];
    banyan.valid = 0xfff; // 12 channels
    banyan.mask = 0; // no mask
}

Simulator_HIRES::~Simulator_HIRES() {}

void Simulator_HIRES::reg_write(SimReg& reg, epicsUInt32 offset, epicsUInt32 newval)
{
    Simulator::reg_write(reg, offset, newval);

    banyan.process();
}

void Simulator_HIRES::WF::process()
{
    if(reset->storage[0]&(1u<<reset_bit))
    {
        // clear reset
        reset->storage[0] &= ~(1u<<reset_bit);

        const epicsUInt32 selected = mask ? mask->storage[0] : valid;

        for(size_t t=0, idx=0; selected && idx<buffer->storage.size(); t++) {
            for(size_t sig=0; sig<32u; sig++) {
                if(!(selected & (1u<<sig)))
                    continue;

                buffer->storage[idx++] = seed + sig*10u + t*5u;
            }
        }

        // indicate ready
        status->storage[0] |= 1u<<status_bit;
    }
}
