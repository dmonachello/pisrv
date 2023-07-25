Raspberry Pi server
This is still a work in progress. Lots of TBD!!!

Overview of major chunks:
HelloProtocol:
	The helloProtocol builds a node list by sending and receiving UDP broadcasts. 
	The hello protocol creates a new tcpCmdClient when a new node is discovered.
	UDP Port Number: 32000


msgAPI:
	Messages from the top (brain) are sent out through the msgAPI using the node list built
	with the helloProtocol. The tcpCmdClient socket fd is used to send the msg.
        Responses to these msgs also come through the tcpCmdClient.


tcpCmdClient:
	The tcpCmdClient receives messages from other nodes and feeds them back up to the 
	msgAPI and then to its client (brain).


tcpCmdSrv:
	The tcpCmdSrv receives msgs from other nodes tcpCmdClient. It can also commands
	from telnet/putty) and executes the command on the local CLI. Events resulting from 
	the CLI commands can be relayed up to the brain.

tcpConnSrv:
	tcpConnSrv receives connect requests from the other nodes helloProtocol and creates 
	new tcpCmdSrv devices.
	TCP port number:32000

+------+                           +-------+
| brain |                          | brain |
+------+                           +-------+
   |                                  |
+-----+                            +-----+
| MSG |                            | CLI |
| API |                            |     |
+-----+                            +-----+
   |                                  |
   |                                  |
+------+                            +------+
| TCP  |                            | TCP  |
| cmd  |                            | cmd  |
|client|                            |server|
+------+                            +------+
   |                                   |
   |                                   |
+------+                           +------+
| TCP  |                           | TCP  |
|socket|                           |socket|
+------+                           +------+
   |                                  |
   |                                  |
   +----------------------------------+


debug counters

- events
- network events
- new nodes detected
- nodes deleted
- multiple fds ready simultaneously
- counters by poll revent type
	NVAL
	HUP
	ERR
	POLLIN
	???
-
