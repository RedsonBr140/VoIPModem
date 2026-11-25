# VoIPModem — Windows RAS dial-in guide

How to make a Windows server answer dial-up calls on VoIPModem's lines
with its own Routing and Remote Access (RRAS), so that a caller with a
modem gets a PPP session and an IP address. VoIPModem does no PPP itself
(see `ARCHITECTURE.md`); RRAS terminates PPP through Windows' modem
driver on the other side of each com0com pair.

Everything below was done on one real system and is marked as one of:

- **Verified**: done and seen working on the tested system.
- **Workaround**: needed today because of a VoIPModem limitation, to be
  removed once that is fixed.
- **Not tested**: listed so nobody takes it as working.

Addresses, names and numbers here are placeholders: `192.0.2.x` for the
PBX, `10.0.0.x` for the dial-in pool, `EXAMPLE\dialuser`, extension
`1001`, `<password>`.

## Tested environment

Tested 2026-11-24.

| Part | What was used |
|---|---|
| RAS server | Windows Server 2022 (build 20348), domain member, virtual machine |
| VoIPModem | 0.0.1 as a Windows service, libv90modem 0.6.0, two lines |
| Virtual ports | com0com 3.0.0.0, pairs COM3<->COM4 and COM5<->COM6 |
| Modem driver | "Standard 56000 bps Modem" (`mdmgen.inf`, section `Gen556F`) on COM4 and COM6 |
| PBX | Asterisk 20.12 (Issabel), PJSIP extensions, G.711 u-law only |
| FXS gateway | Cisco SPA112, firmware 1.4.1(SR5) |
| Caller | US Robotics Courier 56K on the SPA112's PHONE 1 port, dialing from Windows 11 dial-up networking |

VoIPModem opens COM3 and COM5; the modem driver and RRAS use COM4 and
COM6.

## 1. com0com

1. Create one pair per line. **Give both ends real COM names.** The
   default `CNCA0`/`CNCB0` names do not appear in the Windows modem
   wizard. In the com0com setup tool tick "use Ports class" on both ends
   (or set `PortName=COM#`), so both ports appear under Ports (COM &
   LPT) in Device Manager.
2. Leave baud-rate emulation, buffer overrun, plug-in mode and exclusive
   mode **off**.
3. Check which ports are paired with `setupc.exe list` (elevated); pairs
   are not always adjacent numbers.

**Driver signing.** com0com 3.0.0.0 "signed" showed Code 52 ("cannot
verify the digital signature") in Device Manager while Secure Boot was
on. Turning Secure Boot off in the VM's firmware fixed it, and the
3.0.0.0 driver then loaded normally (**verified**). The older signed
2.2.2.0 driver is the commonly reported alternative for systems that
must keep Secure Boot on; it was **not tested** here.

## 2. Modem driver

On each port VoIPModem does **not** open (COM4, COM6):

1. Device Manager > Add legacy hardware > Modems > "Don't detect my
   modem".
2. (Standard Modem Types) > **Standard 56000 bps Modem** > the port.
3. In the modem's properties, set Maximum Port Speed to **115200**.
4. Diagnostics > tick "Record a Log". Logs are written to
   `C:\Windows\ModemLogs`, one per modem, and only flushed when the port
   closes, so read them after the call has ended.

## 3. VoIPModem settings that matter for RAS

In `voipmodem.ini` (or the Manager's Settings and Line properties):

| Setting | Manager | ini | Why |
|---|---|---|---|
| SIP transport | Settings | `[SIP] LocalPort` non-zero | `0` disables SIP entirely; every line then fails `ATA` |
| Auto-answer **off** | Line: "Answer incoming calls on this line automatically" unticked | `AutoAnswerRingCount=0` | RRAS answers with `ATA` on `RING`. Windows sends `S0=0` during init in any case |
| Registrar port | Line: SIP server, or Settings | `SipServer=<host>:<port>` | Only if the PBX's SIP listener is not on 5060 |
| Compression **off** | Line: "V.42bis / V.44 compression" unticked | `Compression=0` | PPP stalled with it on; see Known issues |
| V.42 on | Line: "V.42 error correction" ticked | `ErrorControl=1` (default) | Left on in the verified sessions |
| Maximum rate V.34 or below | Line: Maximum rate "V.34 33600" | `Modulations=V21,V22,V32BIS,V34` | Required for callers through an analog gateway; see Results |

For the tested system, one line looked like this:

```ini
[SIP]
LocalPort=5060

[Modem.Line1]
ComPort=COM3
AutoAnswerRingCount=0
Modulations=V21,V22,V32BIS,V34
ErrorControl=1
Compression=0
SipServer=192.0.2.10:5060
SipExtension=1001
SipPassword=<password>
```

The second line is the same with `ComPort=COM5` and its own extension.

## 4. Unimodem init strings (workaround)

**Workaround.** VoIPModem 0.0.1 answers `ERROR` to `&C1`, `&D2`, `&K3`,
`N1` and `X4`, which the Standard 56000 bps Modem driver sends when RRAS
opens the port. Unimodem then closes the port, and RRAS never waits for
calls. A pending change to VoIPModem's command parser accepts these
commands; until it is released, take them out of the driver's strings:

1. Find the modem's driver key: the modem's Properties > Details > "Driver
   key". It ends in `\000n`. The full key is

   ```
   HKLM\SYSTEM\CurrentControlSet\Control\Class\{4D36E96D-E325-11CE-BFC1-08002BE10318}\000n
   ```

2. **Export the key first** (regedit > File > Export) so it can be put
   back.
3. `Init\2`: change `AT&FE0V1&C1&D2S95=47S0=0<cr>` to
   `AT&FE0V1S95=47S0=0<cr>`.
4. `Settings`: clear the data (leave the value, empty) of
   `FlowControl_Hard`, `SpeedNegotiation_On` and `Blind_Off`.
5. Repeat for each modem (each has its own `000n`).
6. **Reboot.** The Telephony service caches these strings. Restarting
   only the Telephony and Remote Access services worked once, but another
   time left the modem ports missing from RRAS until a reboot.

Expected in the modem log after the change: both init commands return
`OK`, then "Waiting for a call" and `ATS0=0`; when a call arrives, `RING`
followed by `ATA`.

Once the parser change is released, the stock strings should no longer
need editing; that has **not been tested** yet (see section 10).

## 5. RRAS

1. Install the **Remote Access** role with "DirectAccess and VPN
   (RAS)", plus "Routing" for NAT.
2. Routing and Remote Access > the server > Configure and Enable Routing
   and Remote Access > **Custom configuration** > **Dial-up access**
   (plus **NAT** and **LAN routing** when callers should reach the LAN or
   the internet through NAT).
3. Ports > Properties > each modem > Configure > tick **"Remote access
   connections (inbound only)"**.
4. Server Properties > IPv4: tick **Enable IPv4 Forwarding**; choose
   **Static address pool** on a subnet that is otherwise unused (for
   example `10.0.0.1`–`10.0.0.20`). The server takes the first address
   of the pool; callers get the rest.
5. Server Properties > Security > Authentication Methods: tick
   **MS-CHAP v2**. This Windows version does not offer the original
   MS-CHAP (v1) in RRAS, so callers must support MS-CHAP v2.
6. NAT (if wanted): under IPv4 > NAT, add the LAN adapter as the
   **public** interface with NAT enabled, and **Internal** as the
   **private** interface. The NAT statistics row for Internal stays at
   zero; that is normal.

Testing forwarding: the server's firewall drops pings to its own
addresses by default, so from the caller ping a host beyond the server,
not the server.

## 6. Who may dial in

Either way works (**verified**):

- **Per user**: the user's Properties > Dial-in > Network Access
  Permission: **Allow access**.
- **By group**, with a network policy:
  1. Open the network policy console from the RRAS console. That was
     enough; the full Network Policy Server role was not installed.
  2. New network policy: conditions **Windows Groups** = the dial-in
     group and **NAS Port Type** = **Async (Modem)**; access granted;
     authentication MS-CHAP v2. Move it to the top of the list.
  3. Each user's Dial-in tab: **Control access through NPS Network
     Policy**.
  4. The RAS server's computer account must be a member of the domain
     group **RAS and IAS Servers**.

Troubleshooting: RemoteAccess event **20269** in the System log is
logged both for a wrong user name or password (for example
`EXAMPLE\dialuser`) and for an authentication protocol that is not
allowed. Check both.

## 7. Results (verified)

**PPP session.** RRAS answered on `RING`, the port reached
"Authenticated", the Windows 11 client got an address from the pool and
reached the internet through NAT. This was with V.42 on and
V.42bis/V.44 off on the VoIPModem line.

**Modem link** through the SPA112, the Courier calling and VoIPModem
answering:

| Link | Result |
|---|---|
| V.32bis 14400/14400 | Connected, stable |
| V.34 31200/26400 | Connected, zero CRC and framing errors in RRAS |
| Default offer (V.92 and V.90 first) | "libv90modem: modem ended -- start-up failed", every time |

So, for now, limit the line's Maximum rate (`Modulations=`) to V.34 or
below for callers through an analog gateway.

This is the first data point with a physical modem through an FXS
gateway. It does not contradict the V.90 56000/26400 result in
`ROADMAP.md`, which was against a software modem on its own SIP
extension, with no analog leg.

## 8. SPA112 settings used

This is **the configuration that worked**, not a list of settings each
proven necessary.

| Page | Setting | Value |
|---|---|---|
| Line 1 | Proxy | the PBX, with its SIP port (e.g. `192.0.2.10:5060`) |
| Line 1 | Preferred Codec | G711u |
| Line 1 | Use Pref Codec Only | yes |
| Line 1 | G729a Enable, G726-32 Enable | no |
| Line 1 | Silence Supp Enable | no |
| Line 1 | Echo Canc Enable | no |
| Line 1 | FAX V21 Detect Enable, FAX CNG Detect Enable | no |
| Line 1 | FAX Passthru Method | None |
| Line 1 | FAX Process NSE | no |
| Line 1 | FAX Disable ECAN | yes |
| Line 1 | DTMF Tx Method | InBand |
| Line 1 | Network Jitter Level | high |
| Line 1 | Jitter Buffer Adjustment | no |
| Line 1 | Call Waiting Serv, Three Way Call Serv | no |
| Line 1 | Modem Line | yes |
| SIP | RTP Packet Size | 0.020 |
| User 1 | CW Setting | no |

## 9. Known issues

- **PPP data stalls with V.42bis/V.44 on.** The modem link connected and
  reported "V.42 LAPM connected, compression V.42bis", PPP authenticated,
  and then nothing more arrived from the client: RRAS "Bytes in" stayed
  at the post-login count for several minutes, with no CRC errors.
  Unticking "V.42bis / V.44 compression" on the VoIPModem line
  (`Compression=0`) made traffic flow on the next call; V.42 stayed on.
  Root cause not investigated: an open bug in VoIPModem's or
  libv90modem's data-mode compression.
- **Hang-up takes about 20 s.** VoIPModem 0.0.1 does not drive DCD from
  the call or act on DTR drops, so Unimodem waits, then sends `+++` and
  `ATH`. A pending change honors `&C`/`&D` (DCD only while the data link
  is up, DTR drop hangs up); it has unit tests but has not yet been
  confirmed against RRAS.
- **"Query Modem"** (modem Properties > Diagnostics) shows COMMAND NOT
  SUPPORTED for several identification commands when run while the line
  is ringing.
- **`PJSIP_EBUSY`** and "RTP socket bind ... Address already in use"
  lines appear in VoIPModem's log without breaking calls.
- **A modern client saturates the link.** Windows 11 opens dozens of
  background HTTPS connections as soon as it is online and fills a V.34
  link, so pings time out while bytes keep flowing. This is the client,
  not VoIPModem; period clients are the intended callers.

## 10. Not tested

None of these has been tried. Do not assume they work:

- Multilink PPP.
- More than two lines.
- Calls arriving from the PSTN through a SIP trunk, with a real modem on
  the far end.
- Windows Server 2003 or Windows XP as the RAS host.
- Legacy clients (Windows 9x, 2000, XP) dialing in.
- com0com 2.2.2.0 with Secure Boot on.
- VoIPModem calling out to the physical modem (RRAS here is inbound
  only).
- The stock Unimodem init strings with the pending parser change.
