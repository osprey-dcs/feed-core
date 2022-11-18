Python API Usage
================

.. currentmodule:: leep

Setup
-----

The entry point for all usage is the :py:meth:`open` function.
This accepts a URI-list string which specifies the communications
protocol/mechanism to use, and an address.

For example, communicate with an IOC using ::

  dbLoadTemplate("../../db/rfs_logic.substitutions", "P=TST:,NAME=device")

with::

  import leep
  dev = leep.open('ca://TST:', instance=[0])

Note that usage of instance= is described below.

Alternately, direct access to a device with IP address 192.168.42.1
and the default port.::

  import leep
  dev = leep.open('leep://192.168.42.1', instance=[0])

The :py:meth:`open` function returns an object with
the methods of :py:class:`base.DeviceBase`.

Register Access
---------------

Register reads and writes are accomplished with the
:py:meth:`base.DeviceBase.reg_read` and
:py:meth:`base.DeviceBase.reg_write` methods.

A non-atomic read-modify-write of a register named 'foo' is accomplished with ::

  V, = dev.reg_read(['foo'])
  V |= 8
  dev.reg_write([('foo', V)])

Register name shorthand
^^^^^^^^^^^^^^^^^^^^^^^

To avoid use of potentially long register names,
the methods which accept a register name will perform
a search against all register names.
This search includes the provided name argument as a suffix
with the "instance" list.

The instance list if a concatenation of the instance= arguments
of the :py:meth:`open` function and the instance argument
of the method being called.  For example, if a device
has two registers named 'prefix_0_foo' and 'prefix_1_foo',
then the following are possible ways to specify one of them.

By complete name ::

  dev = leep.open("...address...")
  dev.reg_read(['prefix_1_foo'])

By device level instance ::

  dev = leep.open("...address...", instance=[1])
  dev.reg_read(['foo'])

By method level instance ::

  dev = leep.open("...address...")
  dev.reg_read(['foo'], instance=[1])

These three are equivalent.

An exception will be raised unless a search matches exactly one register name.

Waveform Acquisition
--------------------

The sequence for waveform acquisition is ::

  dev.set_channel_mask([0, 1])
  while True:
    dev.wait_for_acq()
    ch0, ch1 = dev.get_channels([0, 1])
    ...

Where the argument of the :py:meth:`set_channel_mask` method
is the list of channels to acquire.
When it is desired to coordinate acquisition with changes to
setting registers, then ::


  dev.set_channel_mask([0, 1])
  while True:
    dev.reg_write([('foo', 42)])
    dev.wait_for_acq(tag=True)
    ch0, ch1 = dev.get_channels([0, 1])
    ...
