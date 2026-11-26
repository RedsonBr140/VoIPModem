# VoIPModem — Control API

The running service (`VoIPModem.exe` in a console or as a Windows
service, or `voipmodem` on Linux) is operated through this API, the same
on both. VoIPModem Manager uses nothing else, and any other client (a
script, a monitoring check) can do the same.

## Transport

- TCP, `[Control] BindAddress`:`Port` (default `127.0.0.1:7880`; `Port=0`
  turns the API off).
- One JSON object per line, UTF-8, each way. A client sends a request and
  reads one response line; responses come in request order.

```
-> {"id": 7, "cmd": "line.hangup", "args": {"id": "Line1"}}
<- {"id": 7, "ok": true, "result": {}}
<- {"id": 8, "ok": false, "error": "no such line"}
```

`id` is optional and echoed back as given. `args` is optional.

**Authentication.** With `[Control] Password` set, the first request must
be `hello` with `{"password": "..."}`; everything else before that is
refused with `authentication required...`, and a wrong password with
`wrong password`. The password crosses the network in clear text: keep the
API on 127.0.0.1, or on a trusted network only.

A request that changes something may take up to 30 s to be answered (it
waits for the service's main thread); give the socket a longer receive
timeout.

## Commands

### `hello`

Args: `password` (when required), `client` (free text). Result:

```json
{"service": "VoIPModem", "version": "0.0.2", "protocol": 1, "host": "SRV-GW01",
 "platform": "Windows, 32-bit", "engine": "libv90modem 0.7.0", "pjsip": "2.17",
 "configPath": "C:\\VoIPModem\\voipmodem.ini", "runningAsService": true,
 "passwordRequired": false}
```

`pjsip` is empty when SIP is off (`[SIP] LocalPort=0`). On Linux,
`platform` is for example `"Linux, 64-bit"`.

### `status`

Everything the Manager's main window shows. Result:

- `service`: `host`, `version`, `startedAt`, `uptimeSeconds`, `configPath`,
  `configLoadedAt`, `logFile`, `cpuPercent` (this process),
  `runningAsService`, `restartRequired` (saved settings that only a restart
  applies).
- `sip`: `enabled`, `localPort`, `transport`, `server`, `lines` (lines with
  a SIP account), `registered`.
- `media`: `preferredCodec`, `ptimeMs`, `jitterMeanMs`, `jitterMaxMs`,
  `lossPercent` (over the calls up now, else the last ones; -1 = none yet),
  `rtpKbps` (both directions, all lines).
- `defaults`: `autoAnswerRings`, `modulations`, `errorControl`,
  `compression` (the `[Defaults]` in force).
- `lines`: one object per line, in ini order:

| Field | Meaning |
|---|---|
| `id` | line name (`[Modem.<id>]`; `""` for a bare `[Modem]`) |
| `comPort`, `extension`, `server` | as configured |
| `state` | `Waiting`, `Idle (AT)`, `Ringing`, `Answering`, `Dialing`, `Handshake`, `Connected`, `Online command`, `Hanging up`, `No carrier`, `Port closed`, `No COM port`, `Not registered`, `SIP error`, `Out of service`, `Unregistered`, `No SIP` |
| `kind` | `up` (connected), `neg` (ringing to training), `idle`, `err`, `off` |
| `direction` | `in`, `out`, or `""` without a call |
| `remoteParty` | caller or called number |
| `modulation`, `txBps`, `rxBps`, `snrDb` | the modem link (`Training` before connect; `snrDb` -1 = not measured) |
| `callSeconds` | since the call began ringing / dialing; -1 = no call |
| `bytesTx`, `bytesRx` | data to / from the far end on this call |
| `retrains`, `errors` | this call: retrains, bad LAPM frames (or start-stop framing errors) |
| `errorControl`, `compression` | `V.42 LAPM` / `none` ...; `V.44`, `V.42bis`, `none` |
| `codec`, `jitterMeanMs`, `jitterMaxMs` | the call's RTP stream |
| `registration`, `registrationCode` | `registered`, `registering`, `failed`, `unregistered`, `none` (no SIP); last REGISTER status |
| `inService`, `portOpen`, `fault` | `fault` says why a line is faulted |
| `autoAnswerRings` | S0 now |
| `canAnswer`, `canHangUp` | what the line can do now |
| `pendingChanges` | saved settings wait for the call to end |
| `driver` | Windows modem driver on the other side of the com0com pair: `"Standard 56000 bps Modem on COM4"`, `"(not bound) COM4"`, `"(no com0com pair)"` (always this on Linux) |

- `stats` (since the service started): `incomingOffered`, `callsAnswered`,
  `trained`, `failedToTrain`, `rejectedBusy`, `hungUpEarly`,
  `outgoingDialled`, `outgoingConnected`, `completedCalls`,
  `averageCallSeconds`, `longestCallSeconds` (connected time),
  `peakBusyLines`, `bytesIn`, `bytesOut`, `atCommands`, `retrains`,
  `renegotiations`, `connects`.
- `carriers`: `[{"mode": "V.90 56000", "count": 12}, ...]`, most frequent
  first (rate = the faster direction).

### `log`

Args: `after` (sequence number; 0 = from the oldest kept), `max` (default
500). Result: `entries` (`seq`, `time`, `level`, `message`, oldest first),
`last` (newest sequence number), `file`. The service keeps the last 2000
lines; a `last` below what a client has seen means the service restarted.

### Lines

| Command | Args | Does |
|---|---|---|
| `line.answer` | `id` | answers the ringing call (as `ATA`) |
| `line.hangup` | `id` | ends the call, or refuses the ringing one |
| `line.reset` | `id` | closes and reopens the line (port and SIP account); drops a call |
| `line.service` | `inService`, optional `id` (else every line) | writes `InService=` to the ini; out of service = hang up and unregister |
| `line.add` | `line` | adds `[Modem.<id>]` and starts it; result `id` |
| `line.update` | `id`, `line` | writes the given keys; applied now, or when the call ends (`deferred`) |
| `line.remove` | `id`, optional `force` | removes the section; refused during a call unless `force` |

A `line` object may hold: `id` (add only; default `Line<n>`; letters,
digits, `-`, `_`), `comPort`, `sipExtension`, `sipPassword` (written only
when non-empty), `sipServer`, `sipRealm`, `baudRate`, `identification`,
`autoAnswerRings`, `modulations`, `errorControl`, `compression`,
`inService`, `localAudio`. A modem setting equal to `[Defaults]` is
removed from the line's section rather than written, so the line keeps
following the defaults. `line.add` needs `comPort` and `sipExtension`;
a COM port or extension another line uses is refused.

### SIP

`sip.register` — args `register` (default true): every in-service line
registers again, or every line unregisters (until the next register or
restart; not saved).

### Configuration

`config.get` — the ini as the service reads it: `path`, `sip` (`server`,
`localPort`, `transport`, `realm`, `registerExpiry`, `registerOnStart`),
`media` (`codec`, `jitterBufferMs`, `rtpPort`, `rtpPortRange`,
`echoCanceller`), `defaults` (`modulations`, `autoAnswerRings`,
`errorControl`, `compression`, `baudRate`, `identification`), `logging`
(`level`, `file`, `rotateKB`, `keep`, `traceAt`), `control` (`port`,
`bindAddress`, `passwordSet`), `lines` (each with the keys above,
effective values, and `passwordSet` instead of a password),
`restartRequired`.

`config.set` — the same groups, any subset of fields; `control` also
takes `password` (non-empty sets it) and `clearPassword: true`. Values
are checked before anything is written. Result: `restartRequired`.

`config.reload` — re-reads the ini (after editing it by hand) and applies
it like `config.set` does.

### Service

| Command | Args | Does |
|---|---|---|
| `service.stop` | | stops the service (a Windows service reports stopped) |
| `service.restart` | `force` | runs a fresh instance in the same process; refused during a call unless `force` |
| `selftest` | `modulations` (`V21`, `V22`, `V32BIS`, `V34`; default `V34`) | two libv90modem modems back to back on a perfect line: `connected`, `modulation`, `txBps`, `rxBps`, `snrDb`, `lineSeconds`, `wallSeconds`, `failure` |
| `system.ports` | | the service computer's `serialPorts` (`port`, `line`), `com0comPairs` (`a`, `b`, `lineA`, `lineB`), `modems` (`name`, `attachedTo`, `pairedWith`, `line`); all three empty on Linux |

## Example (PowerShell)

```powershell
$c = New-Object System.Net.Sockets.TcpClient("127.0.0.1", 7880)
$w = New-Object System.IO.StreamWriter($c.GetStream()); $r = New-Object System.IO.StreamReader($c.GetStream())
$w.WriteLine('{"cmd":"status"}'); $w.Flush(); ($r.ReadLine() | ConvertFrom-Json).result.lines | Format-Table id, state, modulation
```
