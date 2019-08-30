Device Support
==============

The format of all INP/OUT link fields is: "key=value key2=value2".
Allowed keys are:

* name=
* reg=
* offset=
* step=
* wait=
* rbv=
* mask=
* value=
* retry=
* scale=  (only for aai/aao w/ FTVL=DOUBLE)

All INP/OUT links must specify a Device name with 'name='.
Use of other keys is described in the following.

The Device Supports used only in feed_base.template are not described.

The Device Supports provided by feed.dbd are as follows.

Register Write
~~~~~~~~~~~~~~

To write value(s) to a Device register. ::

    record(longout, "...") {
    ...
    record(ao, "...") {
    ...
    record(aao, "...") {
        field(DTYP, "FEED Register Write")
        field(OUT , "@name=<devicename> reg=<regname>")
        # optional in OUT
        #  offset=0  Index in register from which a (first) value taken
        #  wait=true Whether to delay completion until the write is acknowledged by the device.
        #  step=1  With aao, increment of Index between array elements
    }

Register Read
~~~~~~~~~~~~~

To read value(s) to a Device register. ::

    record(longin, "...") {
    ...
    record(ai, "...") {
    ...
    record(aai, "...") {
        field(DTYP, "FEED Register Read")
        field(INP , "@name=<devicename> reg=<regname>")
        # optional in OUT
        #  offset=0  Index in register from which a (first) value taken
        #  wait=true Whether to read a value from the Device, or use the previous read value.
        #  rbv=false Whether to copy the most recently read value (false) or most recently written (true)
        #  step=1  With aai, increment of Index between array elements
    }

Register Watch
~~~~~~~~~~~~~~

To periodically poll a status register until "current & <bitmask> == <expected>".

Every "<pollperiod>" seconds the register is read and compared.
Record processing does not complete until the condition is true, or a timeout occurs. ::

    record(bo, "...") {
        field(DTYP, "FEED Register Watch")
        field(OUT , "@name=<devicename> reg=<regname> retry=<pollperiod> mask=<bitmask> value=<expected>")
        #  offset=0  Index in register from which a (first) value taken
    }

Sync
~~~~

The special DTYP="FEED Sync" support exists to allow sequencing during (re)connection.
This asynchronous record will complete processing after every in-progress register read/write
can completed (or timed out). ::

    record(longin, "$(BASE)Init3_") {
        field(DTYP, "FEED Sync")
        field(INP , "@name=$(NAME)")
    }

Signals
-------

The Signals device supports allow some parameters specified in
an INP/OUT link to be changed after initialization via
a different record.

The association between two records is made with the 'signal=' parameter.
Which is a IOC wide unique identifier (unique to one of the  Register Read/Write DTYPs).

Signal names may be selected arbitrarily.
It is suggeste to use a combination of record name prefix,
device name, and/or register name.

In this example, the "$(P)Off-SP" controls, and overrides, the offset=
paramter for "$(P)-I". ::

    record(aai, "$(P)-I") {
        field(DTYP, "FEED Register Read")
        field(INP , "@name=<devicename> reg=<regname> signal=$(P):<regname>")
        ...
    }
    register(longout, "$(P)Off-SP") {
        field(DTYP, "FEED Signal Offset")
        field(OUT , "@signal=$(P):<regname>")
        ...
    }

Currently only the offset, step=, and scale= parameters may be override via the Signals mechanism.

Status Monitoring
-----------------

Basic status is reported through the "$(PREF)State-Sts" record.
This will show the current state of the device state machine:

* Error - Internal software fault occurs.  Write "$(PREF)Rst-Cmd" to clear.
* Idle - No IP address set with "$(PREF)Addr-SP"
* Searching - Waiting for initial response from Device
* Inspecting - Downloading ROM
* Running - Normal operating mode


Counters
~~~~~~~~

A number of event counters are provided.
These include number of packets sent and received, as well as number of timeouts.
Counters are exposed through records in 'feed_base.template' and by 'dbior'.

dbior
~~~~~

The 'dbior' IOC shell command will give infomation about all FEED Devices.

Simulator Logic
^^^^^^^^^^^^^^^

The simulator can be made to simulator some of the register handling logic
of some devices.  Currently only the RFS waveform acquisition logic is modeled.

The argument -L <name> is used to enable logic handling.
This will fail if the loaded register description doesn't
include all necessary registers.

eg. ::

    ./bin/linux-x86_64/feedsim -L rfs tests/jblob.json

Snapshot and Simulate
---------------------

To snapshot a actual device for simulation, run: ::

    python -m leep.cli <ip> json > capture.json
    python -m leep.cli <ip> dump -Z > capture.initial

This snapshot can then be simulated later: ::

    ./bin/linux-x86_64/feedsim capture.json capture.initial

Protocol
--------

The network protocol implemented by FEED is described in [proto.md](proto.md).
