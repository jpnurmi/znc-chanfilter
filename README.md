A channel filter module for ZNC
===============================

### NOTICE

This module is WIP; it requires unmerged changes to ZNC:

https://github.com/znc/znc/pull/687

### Overview

The channel filter module maintains client specific channel lists
for identified clients. A typical use case is to have a subset of
channels for a mobile client.

### Identifiers

ZNC supports passing a client identifier in the password:

    username@identifier/network:password

or in the username:

    username@identifier/network

### Usage

The module detects identified clients automatically, and starts
maintaining client specific lists of channels. It is possible to
manage the list of identified clients using the following module
commands:

    /msg *chanfilter addclient <identifier>
    /msg *chanfilter removeclient <identifier>
    /msg *chanfilter listclients

When an identified client connects ZNC first time, no channels are
automatically joined. In other words, all channels are filtered out.
The list of channels is updated when the identified client joins and
parts channels. Next time the identified client connects, it will
automatically join the channels it had active from the last session.

### Contact

Got questions? Contact jpnurmi@gmail.com or *jpnurmi* on Freenode.
