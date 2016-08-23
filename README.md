# torrent-client
A homemade torrent client. I'm writing this from scratch because I want to 
learn more about the BitTorrent protocol and peer-to-peer networks in general.

## Short term goals
Implement [BEP 0003](http://www.bittorrent.org/beps/bep_0003.html):
 * Communicate with the tracker. ✅
 * Download a torrent from a peer. ✅
 * Seed a torrent for another peer.
 * Multiple peers.
 * Multiple torrents.

## Future goals
Implement other BEPs. Particularly 
[DHT](http://www.bittorrent.org/beps/bep_0005.html), 
[multitracker metadata](http://www.bittorrent.org/beps/bep_0012.html), 
and [UDP](http://www.bittorrent.org/beps/bep_0015.html).

Add the ability to download only particular files from a torrent. Every torrent 
client I've used has had this feature. But sometimes torrents come all wrapped 
up inside a .zip or .rar archive, which are opaque to the torrent client, and 
I'm forced to download the whole thing. I want the client to peer into the 
archive and download only the desired files inside it. I haven't used a 
torrent client with this feature before.
