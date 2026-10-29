# Development log

These are the original working notes, kept as written during development (21–29 Oct 2026). They document each reverse-engineering step and the evidence behind it. Paths such as `D:\Diablo II Resurrected\_CowPortalDev\…` refer to the author's local test setup; the "please do this" sections were test instructions for that setup.

| File | Content |
|---|---|
| [HISTORY.md](HISTORY.md) | Chronological change log: files, reason, tests, result and open questions for every step |
| [01_investigation_cow_portal_plugin.md](01_investigation_cow_portal_plugin.md) | Feasibility: SDK capabilities, encrypted D2R.exe, D2MOO reference |
| [02_step1_probe_instructions.md](02_step1_probe_instructions.md) | Running the dump probe |
| [03_step2_re_findings.md](03_step2_re_findings.md) | Locating and decoding `ACT1Q4_CreateCowPortal` and `QUESTS_ChangeLevel` |
| [04_step3_install_and_test.md](04_step3_install_and_test.md) | First plugin test (0.1.0) |
| [05_v020_join_hook_and_placement.md](05_v020_join_hook_and_placement.md) | Game-start hook and campfire placement |
| [06_v020_install_and_test.md](06_v020_install_and_test.md) | Test plan for 0.2.0 |
| [07_v030_cross_act_fix.md](07_v030_cross_act_fix.md) | Client-signal timing (root cause later corrected in 08) |
| [08_v031_scan_safety_net.md](08_v031_scan_safety_net.md) | Cross-act waypoints bypass both hooks; frame scan |

The consolidated, current description is [../TECHNICAL.md](../TECHNICAL.md).
