var version = "1.0";

var socketConnected = null;
var connected = false;
var canceled = true;

var localIp = 0;
var serverIp = 0;
var ipPrefix;
var cancelTimer;

// Callback for when 3DS connects
function onOpen(e) {
    console.log("Connected!");
    $("#index").fadeIn();
    $("#loading").fadeOut();
    clearTimeout(cancelTimer);
}

// Callback for when 3DS disconnects
function onClose(e) {
    console.log("Disconnected!");
    $("#index").stop().fadeOut();
    if (!canceled)
        $("#loading").fadeIn();
}

// Callback for when 3DS sends data
function onMessage(e) {
    if (e.data instanceof ArrayBuffer) {
        console.log("Binary Data: " + e.data);
    } else {
        var parts = e.data.split(" ");
        if (parts[0] == "VERSION" && parts[1] != version) {
            var alert = makeAlert("danger", "Version " + parts[1] + " not compatible. Please install latest websock3ds server app and try again.");
            $("#intro .container").prepend(alert);
            $("#intro .container").fadeIn();
            canceled = true;
            socketConnected.close();
        } else {
            try {
                var data = JSON.parse(e.data);
            }
            catch(e) {
                console.log("Not JSON: " + e.data);
            }
        }
    }
}

// Creates websocket connection, and keeps retrying when closed.
function createConnection(ip, port) {
    var socket = new WebSocket("ws://" + ip + ":" + port);

    socket.onmessage = onMessage;
    socket.binaryType = "arraybuffer";

    socket.onerror = function(e) {
        console.log("Error: " + e.data);
    };

    socket.onopen = function(e) {
        if (canceled) {
            socket.close();
            return;
        }
        connected = true;
        serverIp = ip;
        socketConnected = socket;
        onOpen(e);
    };

    socket.onclose = function(e) {
        if (connected) {
            if (ip != serverIp)
                return;
            socketConnected = null;
            onClose(e);
        }
        connected = false;
        // Keep retrying connection when closed.
        setTimeout(function() {
            if (!canceled && (!socketConnected || socketConnected.readyState == WebSocket.CLOSED))
                createConnection(ip, port);
            else
                socket = null;
        }, 10);
    };
}

// Uses WebRTC to get local IP address. Used for network scan.
function getLocalIp() {
    window.RTCPeerConnection = window.RTCPeerConnection || window.mozRTCPeerConnection || window.webkitRTCPeerConnection;   //compatibility for firefox and chrome
    if (!window.RTCPeerConnection)
        return false;
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

function makeAlert(style, message){
	return $('<div />').addClass('alert alert-dismissable alert-'+style)
		.html('<button type="button" class="close" data-dismiss="alert" aria-hidden="true">&times;</button>' + message);
}

$(function(){
    // Check if browser even supports websockets first thing
    if (!("WebSocket" in window)) {
        $("#websocket-error").fadeIn();
        return;
    }

    // IP input fade in/out
    $("#use_ip").click(function(){
        if (this.checked) {
            $("#ip").fadeTo(400, 1.0);
        } else {
            $("#ip").fadeTo(400, 0.0);
        }
    });

    // QR code click to expand
    $("#qr").click(function(){
        if ($("#qr").toggleClass("expanded").hasClass("expanded")) {
            $("#qr").animate({
                width: "200px",
                "margin-right": "5px",
            }, 400);
        } else {
            $("#qr").animate({
                width: "60px",
                "margin-right": "60px",
            }, 400);
        }

    });

    // Force user to supply IP address if WebRTC isn't supported
    if (!getLocalIp()) {
        var alert = makeAlert("info", "No WebRTC support. Must supply IP.");
        $("#intro .container").prepend(alert);
        $("#use_ip").click().prop('disabled', true);
    }

    $("#intro .container").fadeIn();
    // $("#index").fadeIn();

    $("#btnStart").click(function(){
        var ip = $.trim($("#ip").val());
        var use_ip = $("#use_ip").is(':checked');
        if (use_ip && ip.length == 0)
            return;

        $("#loading").fadeIn();
        $("#intro .container").fadeOut(400, function(){
            $("#intro .alert").remove();

            if (use_ip) {
                createConnection(ip, 5050);
            } else if (localIp) {
                // Get local IP prefix, e.g. "192.168.0."
                ipPrefix = localIp.substring(0, localIp.lastIndexOf(".")+1);
                for (var i = 0; i < 256; ++i)
                    createConnection(ipPrefix + i, 5050);
            } else {
                return;
            }

            // Cancel the search for 3DS console after 1 minute
            canceled = false;
            cancelTimer = setTimeout(function(){
                if (connected || canceled)
                    return;
                $("#loading").fadeOut();
                $("#intro .container").fadeIn();
                var alert = makeAlert("info", "Failed to find 3DS console. Try again.");
                $("#intro .container").prepend(alert);
                canceled = true;
            }, 60000);
        });
    });

    $("#btnCancel").click(function(){
        clearTimeout(cancelTimer);
        if (connected || canceled)
            return;
        $("#loading").fadeOut();
        $("#intro .container").fadeIn();
        canceled = true;
    });

    $("#btnSendImage").click(function(){
        $("#file").click();
    });

    // Simple text sending to server
    $("#btnSend").click(function(){
        var text = $("#inputText").val();
        socketConnected.send(text);
    });

    // Image sending to server
    $("#file").change(function(e){
        var reader = new FileReader();
        var canvas = document.getElementById('imgCanvas');
        var context = canvas.getContext('2d');
        reader.onload = function(event){
            var img = new Image();
            img.onload = function(){
                var ratio = Math.min(canvas.width / img.width, canvas.height / img.height);
                var width = Math.round(img.width * ratio);
                var height = Math.round(img.height * ratio);
                var x = Math.round((canvas.width - width)/2);
                var y = Math.round((canvas.height - height)/2);
                context.clearRect(0, 0, canvas.width, canvas.height);
                context.drawImage(img, x, y, width, height);

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
