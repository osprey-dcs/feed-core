LBNL Embedded Ethernet Protocol
===============================

A Request/Reply style protocol using UDP on port 50006.
A Device (Server) receives Requests on port 50006 and responds to each valid Request with a single Reply.

Requests and Replies act on 32-bit registers in a 64MB address space.

UDP Message Format
------------------

<pre>
           0       1         2         3
      +--------+--------+--------+--------+
   00 |              Header...            |
      +--------+--------+--------+--------+
   04 |           ...Header               |
      +--------+--------+--------+--------+
   08 |  Bits  |      Address 1           |
      +--------+--------+--------+--------+
   0C |             Data 1                |
      +--------+--------+--------+--------+
   10 |  Bits  |      Address 2           |
      +--------+--------+--------+--------+
   14 |             Data 2                |
      +--------+--------+--------+--------+
   18 |  Bits  |      Address 3           |
      +--------+--------+--------+--------+
   1C |             Data 3                |
      +--------+--------+--------+--------+
      ...  Repeat Bits/Address/Data
</pre>

Request and Reply messages have the same format.
Each UDP message, request or reply, is composed of a 8 byte header followed by between 3 and 127 address/data pairs.
Total message size _must_ be between 32 and 1024 bytes inclusive.
Messages size _should_ be a multiple of 8 bytes.
Recipients _must_ truncate messages to a multiple of 8 bytes.

Message fields are in Most Significant Byte first (MSB or network) byte order.

It is _suggested_ to pad messages shorter than 32 bytes with reads of address 0.

Message Fields
--------------

### Header

Echoed without modification from Request to Reply.
May be assigned arbitrarily by a Requester to aid in Reply processing.

### Bits

This 1 byte field selects the operation in which the Address and Data.

* 0x01 This least significant bit is _set_ for a _Read_ operation, and _cleared_ for a _Write_.  This bit is echoed in a Reply.

* 0xfe The remaining 7 bits are not currently used and should be zeroed in Requests and ignored in Replies.

### Address

This 3 byte (24 bit) address selects 4 bytes in the 64MB device address space.

For example, address 0 selects bytes 0-3 while address 1 selects bytes 4-7.

MSB order.

### Data

When Bits[0] is set (Read operation) this field is ignored in Requests and filled in for Replies.

When Bits[0] is clear (Write operation) this field contains the value to be written, which is _echoed_ in Replies.

To read back the actual value of a register after a write operation, a Read operation with the same address may be added following a Write within the same message.

Example message
---------------

Request

<pre>
 00 | 6c656570 89abcdef 
 08 | 01000000 00000000
 10 | 00010000 12345678
 18 | 01010000 00000000
</pre>

This Request consists of:

* Arbitrarily chosen Header of 0x6c65657089abcdef
* A read of address 0 (data ignored)
* A write of address 0x10000 with data value 0x12345678.
* A read of address 0x10000 (data ignored)

A corresponding Reply might be:

<pre>
 00 | 6c656570 89abcdef 
 08 | 01000000 48656c6c
 16 | 00010000 12345678
 18 | 01010000 00345678
</pre>

* Header echoed from Request
* Address 0 reads 0x48656c6c
* A write of address 0x10000 with 0x12345678 is echoed.
* Address 0x10000 reads 0x00345678 (perhaps due to truncation)

Required Registers
------------------

### 0x000000 - 0x000003

The first 4 registers will read back the 16 byte constant value "Hello World!\r\n\r\n".

<pre>
00 | 48656c6c 6f20576f 726c6421 0d0a0d0a
</pre>

### 0x000800 - 0x000fff

This 2048 registers access static configuration data.
See section Configuration ROM.

Configuration ROM Format
------------------------

The register range 0x800 - 0xfff holds static data describing the device.
In each 4 byte register, only the 2 lower bytes are used.

The ROM holds a series of variable length records concatenated together.
Each record begins with a 2 byte Descriptor consisting of a type code in the upper 2 bit, and a 14 bit length.
The length has units of _registers_.

### Type 0

Indicates the end of the ROM.

### Type 1

The bytes following the Descriptor are an ASCII string.

When multiple Type 1 headers are encountered, they _must_ be interpreted as:

1. Label of firmware

### Type 2

The bytes following the Descriptor are a variable length integer (in MSB).

A length of 10 (20 bytes valid data) is interpreted as a SHA1 hash.

When multiple Type 2 headers are encountered, they _must_ be interpreted as:

1. A Hash of the JSON text.
2. Git revision of firmware

### Type 3

The bytes following the Descriptor are a zlib compressed (cf. RFC's 1950, 1951, and 1952) string in the JSON format.
This is described in the JSON Information section.

### Example

<pre>
 00 | 00004003 00004865 00006c6c 00006f00
 10 | 00000000
</pre>

Contains:

* Type 1 Descriptor with length 3 holding the string "Hello\0".
* Type 0 Descriptor indicating end of ROM

JSON Information
----------------

The JSON blob encoded in the Configuration ROM will contain a Object (aka. mapping or dictionary).
The keys of this dictionary are symbolic register names.

The value associated with each register is also an Object containing the keys:

### "access":"r" | "w" | "rw"

Note whether the register may be read and/or written.

### "base_addr": Number

The Address of this register

### "addr_width": Number

????

### "data_width": Number

The number of active data bits.  A width of 8 indicates that valid bits are: 0x000000ff

### "sign": "unsigned" | "signed"

Whether values should be interpreted as unsigned or signed (2's complement) integers.

### "description": String

An arbitrary string describing this register.

### Example

<pre>
    "J18_debug": {
        "access": "r",
        "addr_width": 0,
        "base_addr": 63,
        "data_width": 4,
        "sign": "unsigned"
    },
</pre>
