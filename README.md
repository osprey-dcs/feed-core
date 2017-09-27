Building
--------

Requires:

* EPICS Base >=3.15.1
* zlib

```sh
sudo apt-get install libz-dev
cat <<EOF > configure/RELEASE.local
EPICS_BASE=/path/to/epics/base
EOF
make
```

Tools
-----

```sh
$ ./bin/linux-x86_64/feedsim -h
Usage: ./bin/linux-x86_64/feedsim [-hd] [-H <iface>[:<port>]] <json_file> [initials_file]
```

```sh
$ ./leep.py -h
usage: leep.py [-h] [-d] [-q] [-l] [-t TIMEOUT]
               host[:port] {reg,list,dump} ...

positional arguments:
  host[:port]           Server address
  {reg,list,dump}
    reg                 read/write registers
    list                list registers
    dump                dump registers

optional arguments:
  -h, --help            show this help message and exit
  -d, --debug
  -q, --quiet
  -l, --list            List register names
  -t TIMEOUT, --timeout TIMEOUT
```

Usage
-----

A JSON file is needed to run the simulator.
Either extract one from a live device with `leep.py list` or
use the testing file.

```sh
./bin/linux-x86_64/feedsim tests/jblob.json
```

In another terminate run:

To dump register infomation as JSON.

```sh
./leep.py localhost list
```

To read register(s) by name or address.
Example names token from tests/jblob.json

```sh
./leep.py localhost reg prc_dsp_prl_gain trace_i_buf
prc_dsp_prl_gain[0]     00000000
 ...
```

To write a register

```sh
$ ./leep.py localhost reg prc_dsp_prl_gain prc_dsp_prl_gain=42 prc_dsp_prl_gain
prc_dsp_prl_gain[0]     00000000
prc_dsp_prl_gain[0]     0000002a
prc_dsp_prl_gain[0]     0000002a
```

To print all non-zero register values in a format suitable
to use simulator initial values.

```sh
./leep.py localhost dump -Z > initial.dat
```
