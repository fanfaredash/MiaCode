# bridge

This folder only exists for legacy integration notes.

The primary application path is now native C++:
- native parser
- native timeline metadata
- native preview audio
- native Qt preview rendering

The old Python preview-session bridge is optional, disabled by default, and kept only as a fallback while the last legacy paths are being removed.

