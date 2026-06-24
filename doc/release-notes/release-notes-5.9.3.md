Notable changes
===============

Security hardening
------------------

Version 5.9.3 includes security-focused maintenance updates, including:

- A fix for **CVE-2024-52911** (authored by Decker; issue discovery credited
  to anticsdecoded).
- An additional denial-of-service hardening patch discovered by
  anticsdecoded.

Threading and lock stability
----------------------------

Several internal locking updates were included to improve runtime safety and
reduce risk around lock contention in wallet-related code paths:

- Improved unlock/lock handling in wallet thread paths.
- Added lock-debug logging for threads waiting on other threads to complete,
  improving diagnosability of lock waits and potential deadlock scenarios.

Auto-sweep efficiency
---------------------

The sweep flow has been reorganized to frontload note filtering so
`GetFilteredNotes` is run once per sweep operation instead of once per address,
reducing repeated work.

Dependency update
-----------------

- Updated `libsodium`.

Upgrade notes
-------------

This is a patch release focused on security and operational stability. All node
operators are encouraged to upgrade.

Changelog
=========

Cryptoforge:
  DoS security patch (issue discovered by anticsdecoded). (741957075c)
  Security patch CVE-2024-52911 (authored by Decker; issue discovered by
  anticsdecoded). (f7807d7a49)
  Reorganize sweep function to frontload `GetFilteredNotes`, and run it
  once instead of once per address. (a92e660418)
  Update unlock/lock wallet thread locks. (f621215a28)
  Add lock debugging for threads waiting for other threads to complete.
  (af24c05770)
  Update libsodium. (7cedb96735)
