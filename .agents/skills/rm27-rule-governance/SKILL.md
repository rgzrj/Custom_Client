---
name: rm27-rule-governance
description: Govern RM2027 rule sources, assumptions, and compliance. Use when interpreting rules, reviewing terminal behavior, or deciding whether a feature is permitted or sufficiently documented.
---

# RM27 Rule Governance

Keep implementation decisions traceable to official material while the 2027 rules are still provisional.

## Workflow

1. Read [references/source-index.md](references/source-index.md) before making a rule-dependent decision.
2. Classify every claim as `confirmed`, `provisional`, `project decision`, or `unknown`.
3. Record the exact document, section, and PDF page for confirmed or provisional claims.
4. Treat newer official documents as authoritative over older documents and community repositories.
5. When a rule change affects code, identify the impacted protocol, state, UI, simulator, and test paths before editing.
6. Append the completed change set to the change log section of `CLAUDE.md`.

## Hard boundaries

- Do not infer wire fields, ports, MQTT topics, QoS, Protobuf schemas, or robot-control formats from gameplay prose.
- Do not reverse engineer, modify, or interfere with the official referee system.
- The custom terminal must not use wireless transmitters or receivers.
- Do not implement a behavior that bypasses an official penalty or masking requirement. A video-only fallback is allowed only when the official rules and protocol still provide that video.
- The current project scope replaces the heavy-robot operator terminal. Other roles stay on official clients until the user expands the scope.
- Preserve future support for an integrated command view and manual fallback switching without forcing those features into the first heavy-operator MVP.

If a required official source is missing, proceed only with named interfaces, simulation, and tests that are explicitly marked non-official.
