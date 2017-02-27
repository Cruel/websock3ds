Incomplete README.

## Limit

SSL secured websockets (wss://) aren't currently supported, so any site served over https cannot connect to it. Was going to add it, but Chrome seems to reject self-signed certs over websocket.

Some browsers lack websocket support, but not many. WebRTC (non-essential) is what seems to be the component with the least browser support, though mostly just lacking Safari and maybe Edge. WebRTC is used to get local IP of the machine to assist with "scanning" local network for 3DS, so without it, it will force the users in the UI to specify an IP address.

Even with WebRTC, local network scan can fail. It merely takes the local ip and scans a limit range (e.g. it's commonly 192.168.0.{0-255}).

## Compile

1. You need devkitARM + libctru, and [bannertool](https://github.com/Steveice10/bannertool) and [makerom](https://github.com/profi200/Project_CTR/tree/master/makerom)
2. You need a couple portlibs: wslay and nettle. I added both to my [portlib repo](https://github.com/Cruel/3ds_portlibs).
	- Clone that repo.
	- `make nettle`
	- `make wslay`
	- `sudo make install`
3. Just `make`

Nettle is only used for base64 string encoding and sha1 hashing, so could be done away with relatively easily if you don't like it. But [wslay](https://github.com/tatsuhiro-t/wslay) is required to easily handle websocket communication.

