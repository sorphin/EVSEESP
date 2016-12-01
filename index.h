const char PAGE_Index[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
<style>
textarea {
  width: 95vw;
  height: 85vh;
}
</style>
<title>Web Socket Performance Test</title>
</head>

<body onload="connect('ws://'+location.host+':81/ws');">
<form onsubmit="return false;">
	<label>Connection Status:</label>
	<input type="text" id="connectionLabel" readonly="true" value="Idle"/>
    <br>
    <textarea id="output" readonly="true"></textarea>
    <br>
    <input type="button" value="Clear" onclick="clearText()">
</form>

<script type="text/javascript">
  var output = document.getElementById('output');
  var connectionLabel = document.getElementById('connectionLabel');
	var socket;

	function connect(host) {
		console.log('connect', host);
		if (window.WebSocket) {
			connectionLabel.value = "Connecting";
			if(socket) {
				socket.close();
	            socket = null;
			}
			
	    socket = new WebSocket(host);
	
	    socket.onmessage = function(event) {
	        output.value += event.data; // + "\r\n";
        	var textarea = document.getElementById('output');
			  	textarea.scrollTop = textarea.scrollHeight;
	    };
	    socket.onopen = function(event) {
	        isRunning = true;
	        connectionLabel.value = "Connected";
	    };
	    socket.onclose = function(event) {
	        isRunning = false;
	        connectionLabel.value = "Disconnected";
//           socket.removeAllListeners();
//           socket = null;
      };
	    socket.onerror = function(event) {
	        connectionLabel.value = "Error";
//           socket.removeAllListeners();
//           socket = null;
	    };
	  } else {
	    alert("Your browser does not support Web Socket.");
	  }
	}

  function clearText() {
     output.value="";
  }
</script>

</body>
</html>
)=====";

