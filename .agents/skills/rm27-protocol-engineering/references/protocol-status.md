# Protocol status

## Confirmed transport-level facts

The RM2027 preview states:

- Video: UDP.
- Competition data: MQTT plus Protobuf.
- Physical connection: wired 1 GbE RJ45.
- Wireless transmit and receive devices are prohibited in the custom terminal.

## Not yet defined in the repository

- Broker address and authentication.
- MQTT topics, QoS, retained-message behavior, session settings, and keepalive.
- Protobuf files, package names, field meanings, enums, versions, and compatibility rules.
- UDP port, framing, fragmentation, codec signalling, sequence numbers, and timing.
- Heavy-robot motion-control path and command format.
- Event-command acknowledgements, rate limits, and error codes.

The RM2026 rule manual does not fill these gaps. It describes allowed behavior and refers protocol structure to a separate communication protocol.

## Allowed work before the protocol arrives

- Define adapter interfaces and domain types without official field names.
- Build a simulator namespace such as `sim.v1` that cannot be mistaken for the official protocol.
- Test connection, freshness, queueing, reconnect, and failure behavior through dependency injection.
- Add official schemas later behind the same adapters.
