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

    trace_odata.reset = &(*this)["trace_flip"];
    trace_odata.reset_bit = 0u;
    trace_odata.status = &(*this)["trace_status1"];
    trace_odata.status_bit = 30u;
    trace_odata.buffer = &(*this)["trace_odata"];
    trace_odata.valid = 0xffffff; // 22 channels
    trace_odata.mask = &(*this)["keep"];
    trace_odata.decim = &(*this)["wave_samp_per"];

    decay_data.reset = &(*this)["decay_reset"];
    decay_data.reset_bit = 0u;
    decay_data.status = &(*this)["decay_ro_enable"];
    decay_data.status_bit = 0u;
    decay_data.buffer = &(*this)["decay_data"];
    decay_data.valid = 0xffff;
    decay_data.mask = &(*this)["decaykeep"];

    abuf_data.reset = &(*this)["abuf_reset"];
    abuf_data.reset_bit = 0u;
    abuf_data.status = &(*this)["abuf_ro_enable"];
    abuf_data.status_bit = 30u;
    abuf_data.buffer = &(*this)["abuf_data"];
    abuf_data.valid = 0xffff;
    abuf_data.mask = 0;

    adcbuf_dataB.reset = &(*this)["adcbuf_reset"];
    adcbuf_dataB.reset_bit = 0u;
    adcbuf_dataB.status = &(*this)["adcbuf_full"];
    adcbuf_dataB.status_bit = 0u;
    adcbuf_dataB.buffer = &(*this)["adcbuf_dataB"];
    adcbuf_dataB.valid = 0xff;
    adcbuf_dataB.mask = 0;
}

Simulator_HIRES::~Simulator_HIRES() {}

void Simulator_HIRES::reg_write(SimReg& reg, epicsUInt32 offset, epicsUInt32 newval)
{
    Simulator::reg_write(reg, offset, newval);

    banyan.process();
    trace_odata.process();
    decay_data.process();
    abuf_data.process();
    adcbuf_dataB.process();
}

void Simulator_HIRES::WF::process()
{
    if(reset->storage[0]&(1u<<reset_bit))
    {
        // clear reset
        reset->storage[0] &= ~(1u<<reset_bit);

        const epicsUInt32 selected = mask ? (*mask)[0] : valid;
        epicsUInt32 decim = this->decim ? (*this->decim)[0] : 1.0;

        for(size_t t=0, idx=0; selected && idx<buffer->size(); t+=decim) {
            phase += decim*2*PI/3600.;

            for(size_t sig=0; sig<32u && idx<buffer->size(); sig++) {
                if(!(selected & (1u<<sig)))
                    continue;

                size_t pair = sig>>1; // signals are pairs of I/Q
                bool Q = sig&1;

                double ph = phase + 2*PI*pair/32.0;

                double v = 0.0;
                // simulate pulse w/ constant amplitude and linear phase change
                if(t>=100 && t<1000) {
                    if(!Q) {
                        v = cos(ph) - sin(ph);
                    } else {
                        v = sin(ph) + cos(ph);
                    }
                    v *= 1024.0;
                    v += rand()*15.0/double(RAND_MAX);
                }

                (*buffer)[idx++] = v;
            }
        }

        // indicate ready
        (*status)[0] |= 1u<<status_bit;
    }
}
