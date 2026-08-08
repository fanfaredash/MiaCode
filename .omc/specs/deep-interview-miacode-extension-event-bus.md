# MiaCode Extension Event Bus Specification

Status: pending approval
Ambiguity: 14%
Threshold: 20%
Threshold source: default
Project type: brownfield

## Topology

| Component | Status | Description | Coverage |
|---|---|---|---|
| Unified event bus | Active | One stable subscription and dispatch contract for extension-visible host events | Event envelope, names, subscription lifecycle, compatibility |
| Core event coverage | Active | First delivery covers editor, timeline, preview, tabs, window, and workspace | Explicit initial namespaces and interaction boundaries |
| High-frequency governance | Active | Prevent pointer, wheel, drag, and playback events from blocking UI | Queuing, per-frame coalescing, filtering, ordering |
| Extension isolation and diagnostics | Active | Contain slow or failing subscribers and make behavior observable | Bounded queues, subscription suspension, DevTools metrics |

## Goal

Add a broad, low-latency extension event system that lets new host modules publish stable events without adding one-off runtime APIs. Preserve existing extension APIs while providing filtered namespace subscriptions, predictable delivery semantics, fault isolation, and measurable performance.

## Constraints

- Preserve all existing `onDid*` convenience APIs and route them through the new bus.
- Add `miacode.events.subscribe(nameOrPattern, options, callback)`; return a disposable subscription.
- Support exact names and namespace wildcards such as `timeline.*`.
- Event names and payload fields are stable, versioned public contracts.
- Ordinary events are queued and delivered asynchronously to extension JavaScript.
- Continuous high-frequency events are coalesced once per rendered frame, retaining the latest state.
- Boundary and transaction events are never coalesced or dropped and remain ordered per source.
- Each extension has an independent bounded delivery queue.
- Subscriber exceptions cannot escape into the host or affect other extensions.
- Sustained budget violations suspend only the offending subscription, not the entire extension.
- UI objects, raw Qt pointers, and renderer objects are never included in public event payloads.

## Initial Event Coverage

- `editor.focus.changed`
- `editor.text.changed`
- `editor.selection.changed`
- `timeline.interaction.started`
- `timeline.interaction.updated`
- `timeline.interaction.finished`
- `timeline.wheel`
- `timeline.seeked`
- `preview.playback.changed`
- `preview.position.changed`
- `ui.bottomTab.changed`
- `window.focus.changed`
- `workspace.document.opened`
- `workspace.document.saved`

Names may be normalized during implementation, but the hierarchy and semantic boundaries must remain equivalent. `timeline.interaction.finished` must expose enough information for an extension to call the existing editor-focus host route after a body click or drag.

## Event Envelope

Every callback receives a common envelope:

```js
{
  name: "timeline.interaction.finished",
  version: 1,
  sequence: 42,
  timestampMs: 123456789,
  source: "pointer",
  data: { kind: "drag", second: 12.5 }
}
```

Required common fields are `name`, `version`, `sequence`, `timestampMs`, `source`, and `data`. Sequence ordering is guaranteed within one event source, not globally across unrelated producers.

## Subscription Contract

```js
const subscription = miacode.events.subscribe(
  "timeline.*",
  { filter: { source: "pointer" } },
  event => handleTimelineEvent(event)
)

subscription.dispose()
```

- Filters are declarative equality filters over documented envelope or payload fields.
- Filtering occurs before enqueueing into the extension queue.
- Extension deactivation disposes all subscriptions automatically.
- Existing helpers such as `onDidSeek()` remain available and use the same dispatcher internally.

## Backpressure And Failure Policy

- Maintain one bounded queue per extension and per-subscription accounting.
- Coalescible entries replace an older queued entry with the same event name and source identity.
- Boundary events use reserved queue capacity and are never discarded.
- Callback failures are recorded and delivery continues unless the subscription exceeds the configured failure/budget threshold.
- A suspended subscription produces a visible diagnostic and can be restored by extension reload or an explicit future management API.
- Do not disable an entire extension because one subscription is slow.

## DevTools Metrics

Expose, per extension and subscription:

- events received and delivered
- events coalesced and dropped
- current and peak queue depth
- latest and rolling callback duration
- callback error count and recent error
- active, disposed, or suspended state

## Non-Goals

- Exposing arbitrary QObject signals to JavaScript.
- Providing raw QWidget, QQuickItem, scene graph, or renderer access.
- Guaranteeing global ordering among unrelated event producers.
- Making every existing host signal public in the first release.
- Replacing commands, providers, or Open Bridge request/response APIs.

## Acceptance Criteria

- [ ] Existing extension runtime tests and existing `onDid*` behaviors remain compatible.
- [ ] An extension can subscribe to an exact event, a namespace wildcard, and a declarative filter, then dispose each subscription.
- [ ] Timeline click/drag completion can trigger an extension callback and a subsequent editor-focus request.
- [ ] Drag, wheel, pointer, and playback-position floods produce at most one callback per event identity per rendered frame.
- [ ] Start/finish, focus, document transaction, and save events are not lost under continuous-event load.
- [ ] Events from one source arrive in monotonically increasing sequence order.
- [ ] A throwing callback does not prevent another extension from receiving the same event.
- [ ] A deliberately slow subscription is isolated and eventually suspended without freezing the UI or disabling its extension.
- [ ] DevTools reports delivery, coalescing, queue, timing, error, and suspension metrics.
- [ ] No event payload exposes raw Qt or renderer object references.
- [ ] New event producers can be registered without adding another JavaScript wrapper class method.

## Technical Context

- `src/extensions/EmbeddedExtensionRuntime.cpp` currently registers named callbacks through one-off `onDid*` methods.
- `src/extensions/ExtensionManager.cpp` currently dispatches events by string route and preserves extension identity.
- `src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp` owns timeline interaction boundaries.
- `src/app/mainwindow/sections/frame/MainWindow.ExtensionHostRequests.cpp` already contains the host-side `window/focusEditor` route.
- `src/extensions/ExtensionOpenBridge.cpp` owns stable facade descriptors and permissions.

## Assumptions Resolved

| Assumption | Resolution |
|---|---|
| Broad coverage requires publishing every Qt signal immediately | Rejected; establish one general bus and connect high-value event families first |
| Low latency requires synchronous JavaScript callbacks | Rejected; use queued delivery and frame coalescing to protect UI responsiveness |
| A new bus may replace old event APIs | Rejected; preserve helpers as compatibility wrappers |
| One slow extension may be disabled wholesale | Rejected; isolate queues and suspend only the offending subscription |

## Ontology

| Entity | Type | Fields | Relationships |
|---|---|---|---|
| Event | Immutable message | name, version, sequence, timestamp, source, data | Published by one host producer |
| Subscription | Extension-owned resource | pattern, filter, callback, state | Receives matching events and is disposable |
| Extension queue | Delivery boundary | capacity, depth, reserved boundary capacity | Isolates one extension from host and peers |
| Producer | Host adapter | namespace, source identity, event policy | Publishes normalized events to dispatcher |
| Diagnostic record | Observability state | counts, timings, errors, suspension | Aggregated per extension and subscription |

## Ontology Convergence

| Round | Stable entities | Change |
|---|---:|---|
| 0 | 3 | Bus, coverage, and performance governance identified |
| 1 | 4 | Delivery queue and boundary-event semantics added |
| 2 | 4 | Subscription pattern and compatibility contract stabilized |
| 3 | 5 | Diagnostic record and isolation policy finalized |

## Interview Transcript

1. Scope confirmed: unified bus, core event coverage, high-frequency governance, and extensible producer registration.
2. Delivery confirmed: asynchronous ordinary events, per-frame coalescing for continuous events, ordered non-droppable boundary events.
3. API confirmed: unified `events.subscribe`, namespace wildcards and filters, with existing helpers preserved.
4. Reliability confirmed: per-extension bounded queues, callback isolation, subscription-level suspension, and DevTools metrics.
