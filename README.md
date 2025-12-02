# Group 3: ComputerNetworks_WifiPrototype
Chance Besancenez, Matt Bracker, Laurel Leuwerke

**Description:**
A prototype client-server connection over IEEE 802.11 setup on Raspberry Pi Zero 2 W hardware.
 Design is intended to be used with one host and anywhere from 1-10 clients. 
 

**Setup**
1) Image the host using the raspberry pi imager software ensuring SSH is enabled with a username and password. 
2) Follow the steps listed in https://forums.raspberrypi.com/viewtopic.php?t=376578 to setup the host as a USB gadget for SSH access without using wifi.
3) SSH into the raspberry pi host using credentials created in step 1
4) In linux environment open nmtui as a sudo user and setup the wireless access point using the following steps:
   -Set up broadcasting Wifi network region as US (or wherever you are working on this from)
   -Create a new connection in wireless connections
   -Write server name in SSID
   -Set mode to Access Point
   -Set channel to 2.4 GHz (B/G)
   -Set security as WPA & WPA2 Personal and write a password
   -In IPv4 Configuration, set the addresses for the access point as 192.168.25.1/24 to give the private network access to all 256 addresses for use.
   -In gateway set the IP as 192.168.25.1 for the host
   - Click Ok and activate the netwrok
 5) transfer server.c and client.c to the host pi
 6) Image the client raspberry pi's, this time selecting the created access point network as the pi's wifi connection. Ensure SSH is enabled.
 7) Plug in client pi's, host should now be able to SSH into them
 8) Using SFTP transfer the client.c file to the client devices for use
 9) Pi network is now ready to be used

**How to Use**
Server 
 -Simply compile and run ./server to begin running the server. It will begin listening on port 8080
 - To exit, use CTRL+C to break out of the program and close all sockets

 Client
  - Compile and run the command ./client 192.168.25.1 OPTION , where OPTION is either 1 for direct chat messaging between clients and 2 is flood mode
       *192.168.25.1 in this instance is the host servers IP address on the access point
  - When in option 1 (direct chat messaging), clients can communicate to each other through the server
  - When in option 2 (flood mode), the clients send messages every 33 mS to the server to flood the network with packets and test the TDMA implementation.
  - To exit, use CTRL+C to break out of the program and close all sockets

  **Resources:**

    https://datasheets.raspberrypi.com/rpizero2/raspberry-pi-zero-2-w-product-brief.pdf

    https://www.raspberrypi.com/documentation/computers/configuration.html

    https://github.com/raspberrypi/documentation/blob/develop/documentation/asciidoc/computers/configuration/host-wireless-network.adoc
