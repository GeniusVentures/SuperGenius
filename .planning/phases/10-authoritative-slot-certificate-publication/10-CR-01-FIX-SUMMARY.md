# CR-01 exact-subject certificate fix

- Certificate records continue to be located exclusively at `/cert/<transaction GetSlotID()>`.
- Transaction consumers now require the loaded, validated certificate to bind the exact transaction: subject account and nonce, `NonceSubject.tx_hash`, decoded embedded-transaction hash, and the derived slot must all agree.
- The same binding protects incoming confirmation, processed-map insertion, outgoing predecessor recovery, replay predecessor validation, and producer UTXO-witness acceptance.
- Regression coverage writes a real signed certificate into CRDT for one of two distinct Mint V2 transactions sharing the same burn slot. Only the winner becomes `CONFIRMED`; the loser stays `VERIFYING`. Missing and malformed slot records also fail closed.

Verification:

- `cmake --build build/OSX/Release --target transaction_manager_certificate_fallback_test -j2`
- `build/OSX/Release/test_bin/transaction_manager_certificate_fallback_test` (13/13 passed)
- `build/OSX/Release/test_bin/consensus_pending_lifecycle_test --gtest_filter='*AuthoritativeSlot*:*Certificate*'` (7/7 passed)
- `build/OSX/Release/test_bin/consensus_slot_key_test` (6/6 passed)
