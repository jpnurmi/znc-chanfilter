ZNC channel filter module for identified clients
================================================

### Overview

The channel filter module maintains client specific channel lists
for identified clients. A typical use case is to have a subset of
channels visible for a mobile client.

### Usage

When an identified client connects ZNC first time, all channels are
joined. The list of channels is automatically updated when the client
joins and parts channels. Next time the identified client connects,
it joins the channels it had visible from the last session.

Module commands to manage identified clients:

    /msg *chanfilter addclient <identifier>
    /msg *chanfilter delclient <identifier>
    /msg *chanfilter listclients

### Identifiers

ZNC supports passing a client identifier in the password:

    username@identifier/network:password

or in the username:

    username@identifier/network

### Contact

Got questions? Contact jpnurmi@gmail.com or *jpnurmi* on Freenode.
