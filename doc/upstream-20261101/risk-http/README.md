# Review required: HTTP shutdown

Source: SL `1af02acdf5719fc56400499a80c2a052d1cb6f41`.
Branch: `risk/20261101-http-shutdown`, base main `e462dd1d1f`.
The exact full source diff is retained in [full-upstream.patch](full-upstream.patch).
Only its teardown-order hunk is applied to production; the hard-cancel hunk is
not applied. This is a **partial implementation**, not a full #6486 fix.

Applied: stop/tear down HttpService before deleting HTTPStats, which the service
depends on. A compiled production destroyService regression checks that ordering
and idempotence in a cooperative-stop fixture. Linux real-header syntax passes
for httprequest.cpp. Trial merges with #160/#161/#162 are text-clean.

Held risk: source adds mThread->cancel() followed by a ten-millisecond sleep if
the worker remains running. Current `_thread.h` implements cancel using Windows
TerminateThread or pthread_cancel. Neither the source hunk nor sleep establishes
a confirmed join; asynchronous/deferred platform cancellation may leave library
locks held or cleanup incomplete. This is particularly sensitive for shared
HTTP/curl, uploads, image fetches, MCP revocation and coordinated application
shutdown. These are code-inspection risks, not a claimed reproduced deadlock.

Prerequisites for a complete fix: prove worker exit before destroying its policy,
curl transport, queue and statistics; preserve callback/request ownership and
update/logout shutdown barriers. Inspect cooperative wakeup/cancellation and
bounded fallback on every supported platform. Do not reintroduce paid CI or
change native permissions to make the tests pass.

Decision for Marty: retain the order-only fix and authorize a separately tested
cooperative shutdown implementation, or accept/review the upstream hard-kill
policy. Recommend the former. Keep this draft isolated until native slow/hung
HTTP shutdown, curl/allocator lock ownership, uploads, live MCP clients and host
multi-character termination are exercised. No full viewer/Windows or real
network-worker test has been run; a clean merge is not lifecycle proof.
