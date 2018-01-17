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

static inline double rad2deg(double deg) {
    return deg / TWOPI_360;
}

Simulator_RFS::Simulator_RFS(const osiSockAddr& ep,
              const JBlob& blob,
              const values_t& initial)
    :Simulator(ep, blob, initial)
#define INITREG(NAME) ,NAME((*this)[#NAME])
    INITREG(circle_buf_flip)
    INITREG(llrf_circle_ready)
#undef INITREG
    ,phase(0.0)
    ,circle_count(0u)
{
    for(unsigned i=0; i<2; i++) {
        shell_X_dsp_chan_keep[i] = &(*this)[SB()<<"shell_"<<i<<"_dsp_chan_keep"];
        shell_X_dsp_tag[i] = &(*this)[SB()<<"shell_"<<i<<"_dsp_tag"];
        shell_X_circle_data[i] = &(*this)[SB()<<"shell_"<<i<<"_circle_data"];
        shell_X_slow_data[i] = &(*this)[SB()<<"shell_"<<i<<"_slow_data"];
    }
}

Simulator_RFS::~Simulator_RFS() {}

void Simulator_RFS::reg_write(SimReg& reg, epicsUInt32 offset, epicsUInt32 newval)
{
    if(&reg == &circle_buf_flip && offset==0u && newval!=0u) {
        // Arm for waveform acquisition
        newval &= 3; // only two channels supported

        // clear ready bit(s)
        llrf_circle_ready.storage[0] &= ~newval;

        for(unsigned i=0; i<2; i++) {
            if(newval & (1<<i))
                acquire(i);
        }

        // set ready bits
        llrf_circle_ready.storage[0] |= newval;

        phase = fmod(phase + 5.0, 360.0);
    }

    Simulator::reg_write(reg, offset, newval);
}


void Simulator_RFS::acquire(unsigned instance)
{
    // our simulated waveform is a sine with a linear modulation
    SimReg::storage_t& arr = shell_X_circle_data[instance]->storage;
    size_t i, N = arr.size();
    double T, Tinc = 2*PI*5 / (N / 12); // show 5 periods when all channels are selected

    epicsUInt32 mask = shell_X_dsp_chan_keep[instance]->storage[0] & 0xfff;
    if(!mask)
        return; // no-op

    epicsUInt32 tag = shell_X_dsp_tag[instance]->storage[0] & 0xffff;

    epicsInt32 amp = 10000, min = 0, max = 0;

    for(i=0, T=0.0; i < N; T+=Tinc)
    {
        const double mod = 1 - T * 0.5 / (2*PI*5);
        epicsInt32 I = amp * mod * sin(T);
        if(I<min || T==0.0)
            min = I;
        if(I>max || T==0.0)
            max = I;

        // each pair is phase shifted to give visual difference
        double pha = phase;

        // six I/Q pairs
        for(unsigned ch = 0; ch < 12; ch += 2, pha += 10.0) {
            // I
            if((mask&(0x800>>ch)) && i<N)
                arr[i++] = amp * mod * sin(T + deg2rad(pha));
            // Q
            if((mask&(0x800>>(ch+1))) && i<N)
                arr[i++] = amp * mod * cos(T + deg2rad(pha));
        }
    }

    SimReg::storage_t& slow = shell_X_slow_data[instance]->storage;
    if(slow.size()<=42)
        throw std::runtime_error("shell_X_slow_data register size too small");

    std::fill(slow.begin(), slow.end(), 0);
    slow[17] = (circle_count>>8)&0xff;
    slow[18] = (circle_count>>0)&0xff;
    // 19, 20 - circle_stat not modeled
    // adc min/max
    for(unsigned i=21; i<=31; i+=4) {
        slow[i+0] = (min>>8)&0xff;
        slow[i+1] = (min>>0)&0xff;
        slow[i+2] = (max>>8)&0xff;
        slow[i+3] = (max>>0)&0xff;
    }
    slow[33] = slow[34] = tag;
    // 35-42 timestamp counter not modeled
}
