# pwnat - NAT-to-NAT Communication Tool

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)]()
[![License](https://img.shields.io/badge/license-GPLv3-blue.svg)](COPYING-pingtunnel)

**pwnat**, created by Samy Kamkar, is a unique networking tool that allows a client behind a NAT to communicate directly with a server behind a separate NAT—**without** port forwarding, DMZ setup, UPnP, or any third-party middleman.

---

## 🚀 What does pwnat do? (Plain English)

Normally, if you want to host a server at home (like a Minecraft server or a web server), you have to go into your router settings and "forward a port." Without this, your router blocks any incoming requests from the internet because it doesn't know which device inside your house should receive them.

**pwnat breaks this rule.** It allows two devices, both "trapped" behind their respective routers, to talk to each other directly.

### How it works:
1. **The "Traceroute" Trick:** The server sends out "fake" requests to a non-existent IP address (3.3.3.3).
2. **The Client's Signal:** When a client wants to connect, it sends a special ICMP "Time Exceeded" packet to the server.
3. **The Router's Mistake:** The server's router sees this packet and thinks, *"Oh, this is just a message from a router on the internet telling me that my previous request to 3.3.3.3 failed."*
4. **The Pinhole:** Because the router thinks this is a response to something the server sent out, it lets the packet through. This reveals the client's IP to the server.
5. **Direct Connection:** Once they know each other's IPs, they perform "UDP Hole Punching" to establish a direct, fast connection for tunneling your data.

---

## 🛠 Modern Enhancements (v2.0)

This modernized version of pwnat includes:
- **Sliding Window Protocol:** Uses sequence numbers and a sliding window to allow multiple packets in flight. This significantly increases speed over high-latency or high-packet-loss connections.
- **Optional Encryption:** Secure your tunnel with a shared secret key (`-k` flag). Data is encrypted via XOR before being sent over the UDP tunnel.
- **Scalable I/O:** Migrated from the old `select()` system to `poll()`. This allows pwnat to handle many more simultaneous connections efficiently.
- **Modern Build Systems:** Now supports both **CMake** and **Meson** for easy, cross-platform building.

---

## 📂 Use Cases

- **Accessing your Home PC:** Connect to your home desktop via SSH or Remote Desktop without touching router settings.
- **Game Servers:** Host private game sessions for friends when you don't have access to the router (e.g., in a dorm or apartment).
- **Secure File Transfer:** Tunnel FTP or SCP through NATs.
- **Development & Testing:** Quickly expose a local development server to a remote teammate.

---

## ⚡ Quick Start

### Build
Requirements: C11 compiler and either CMake 3.10+ or Meson.

**Using CMake:**
```bash
mkdir build && cd build
cmake ..
make
```

**Using Meson:**
```bash
meson setup build
meson compile -C build
```

### Server Mode (Behind NAT B)
Allow anyone to proxy through you:
```bash
sudo ./pwnat -s -k mypassword
```

### Client Mode (Behind NAT A)
Connect to `google.com:80` through your pwnat server:
```bash
sudo ./pwnat -c 8000 <server_public_ip> google.com 80 -k mypassword
```
Now, open your browser and go to `http://localhost:8000`!

---

## 🧪 Testing

To verify pwnat is working correctly in your environment:

1. **Local Loopback Test:**
   Run both server and client on the same machine to test the tunnel logic:
   - Terminal 1 (Server): `sudo ./pwnat -s -k test`
   - Terminal 2 (Client): `sudo ./pwnat -c 8001 127.0.0.1 127.0.0.1 80 -k test`
   - Terminal 3: `curl http://localhost:8001` (If you have a local web server on port 80).

2. **Network Test:**
   The best way to test is between two machines on different networks (e.g., your home and a mobile hotspot). Ensure you use the **public IP** of the server machine.

3. **Performance Check:**
   Use a tool like `iperf` through the pwnat tunnel to see the speed improvements from the new sliding window protocol.

---

## ⚙️ Usage Details

```
usage: ./pwnat <-s | -c> [-k key] <args>

  -c    client mode
        args: [local ip] <local port> <proxy host> [proxy port (def:2222)] <remote host> <remote port>

  -s    server mode
        args: [local ip] [proxy port (def:2222)] [[allowed host]:[allowed port] ...]

  -k    encryption key (shared secret)
  -6    use IPv6
  -v    verbose output (use -vv or -vvv for more detail)
  -h    show this help
```

---

## 📝 License & Original Authors
- Original pwnat by **Samy Kamkar** (http://samy.pl/pwnat)
- Based on `udptunnel` by **Daniel Meekins**.
- Modernized and enhanced by the open-source community.
- Distributed under **GPLv3**.
