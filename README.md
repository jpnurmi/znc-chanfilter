A channel filter module for ZNC
===============================

### Overview

The channel filter module maintains client specific channel lists
for identified clients. A typical use case is to have a subset of
channels for a mobile client.

### Usage

The module detects identified clients automatically, and starts
maintaining client specific lists of channels. When an identified
client connects ZNC first time, all channels are joined. The list
of channels is automatically updated when the identified client
joins and parts channels. Next time the identified client connects,
it will join the channels it had active from the last session.

It is possible to manage the list of identified clients using the
following module commands:

    /msg *chanfilter addclient <identifier>
    /msg *chanfilter removeclient <identifier>
    /msg *chanfilter listclients

### Identifiers

ZNC supports passing a client identifier in the password:

    username@identifier/network:password

or in the username:

    username@identifier/network

### Contact

Got questions? Contact jpnurmi@gmail.com or *jpnurmi* on Freenode.
