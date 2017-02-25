var socketConnected = null;
var connected = false;
var foundDevice = false;

var localIp = 0;
var serverIp = 0;
var tt = 1;
var ipPrefix;

function onOpen(e) {
    console.log("Connected!");
    socketConnected.send("test message!");
}

function onClose(e) {
    console.log("Disconnected!");
}

function onMessage(e) {
    if (e.data instanceof ArrayBuffer) {
        console.log("Binary: " + e.data);
    } else {
        console.log("Message: " + e.data);
    }
}

function createConnection(ip, port) {
    var socket = new WebSocket("ws://" + ip + ":" + port);

    socket.onmessage = onMessage;
    socket.binaryType = "arraybuffer";

    socket.onerror = function(e) {
        console.log("Error: " + e.data);
    };

    socket.onopen = function(e) {
        connected = true;
        serverIp = ip;
        socketConnected = socket;
        onOpen(e);
    };

    socket.onclose = function(e) {
        console.log("closed:" + e.code);
        if (connected) {
            if (ip != serverIp)
                return;
            socketConnected = null;
            onClose(e);
        }
        connected = false;
        // Keep retrying connection when closed
        setTimeout(function() {
            if (!socketConnected || socketConnected.readyState == WebSocket.CLOSED)
                createConnection(ip, port);
            else
                socket = null;
        }, 10);
    };
}

// Uses WebRTC to get local IP address. Used for network scan.
function getLocalIp() {
    window.RTCPeerConnection = window.RTCPeerConnection || window.mozRTCPeerConnection || window.webkitRTCPeerConnection;   //compatibility for firefox and chrome
    var pc = new RTCPeerConnection({iceServers:[]}), noop = function(){};
    pc.createDataChannel("");    //create a bogus data channel
    pc.createOffer(pc.setLocalDescription.bind(pc), noop);    // create offer and set local description
    pc.onicecandidate = function(ice){  //listen for candidate events
        if(!ice || !ice.candidate || !ice.candidate.candidate)  return;
        localIp = /([0-9]{1,3}(\.[0-9]{1,3}){3}|[a-f0-9]{1,4}(:[a-f0-9]{1,4}){7})/.exec(ice.candidate.candidate)[1];
        pc.onicecandidate = noop;
    };
    return true;
}

$(function(){
    if (!("WebSocket" in window)) {

        return;
    }

    // Scan local network for 3DS console IP address.
    if (!getLocalIp()) {
        return;
    }
    // Give some time for local IP to be found.
    setTimeout(function(){
        var ip = localIp;
        if (ip) {
            // Get local IP prefix, e.g. "192.168.0."
            ipPrefix = ip.substring(0, ip.lastIndexOf(".")+1);
            console.log(ipPrefix);
        }
        for (var i = 0; i < 256; ++i)
            createConnection(ipPrefix + i, 5050);
    }, 1000);

    // Simple text sending to server
    $("#btnSend").click(function(){
        var text = $("#inputText").val();
        socketConnected.send(text);
    });

    // Image sending to server
    $("#imageLoader").change(function(e){
        var reader = new FileReader();
        var canvas = document.getElementById('imgCanvas');
        var context = canvas.getContext('2d');
        reader.onload = function(event){
            var img = new Image();
            img.onload = function(){
                var ratio = Math.min(400 / img.width, 240 / img.height);
                var width = Math.round(img.width * ratio);
                var height = Math.round(img.height * ratio);
                var x = Math.round((canvas.width - width)/2);
                var y = Math.round((canvas.height - height)/2);
                context.clearRect(0, 0, canvas.width, canvas.height);
                context.drawImage(img, x, y, width, height);
                console.log("w:" + img.width + " h:" + img.height);
                console.log("w:" + img.width*ratio + " h:" + img.height*ratio);

                var imgData = context.getImageData(0, 0, canvas.width, canvas.height);
                var buffer = new ArrayBuffer(imgData.data.length);
                var bytes = new Uint8Array(buffer);

                var i = 0;
                for (var x=0; x<canvas.width; x++)
                    for (var y=canvas.height-1; y>=0; y--) {
                        var offset = (y * canvas.width + x) * 4;
                        bytes[i++] = 0xFF
                        bytes[i++] = imgData.data[offset+2];
                        bytes[i++] = imgData.data[offset+1];
                        bytes[i++] = imgData.data[offset];
                    }

                socketConnected.send(buffer);
            }
            img.src = event.target.result;
        }
        reader.readAsDataURL(e.target.files[0]);
    });
});
