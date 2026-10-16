# Requests per second against open connections, on a constrained host, under podman

The same run as 2026-10-13-220228Z-connections-constrained.md, with
podman 4.9.3 (Ubuntu package) in place of docker. Commit 617c023.
Container: podman, seccomp=unconfined, nofile 20000, memlock 8192 KiB.
Under the default seccomp profile of podman, io_uring_setup gives ENOSYS.

Rows at 19000 are short: one or more htgen processes were refused by
the memlock budget ("queue_init: Cannot allocate memory").

```
nofile=20000 memlock=8192 nproc=4 kernel=6.18.44-fc-v114
  conns  rep        rps     srv%     cli%   kern%    bad
     64    1     335218      90%      84%     84%      0
     64    2     341272      91%      86%     85%      0
     64    3     302966      92%      82%     83%      0
    256    1     334272      97%      90%     84%      0
    256    2     362924      97%      86%     83%      0
    256    3     315744      95%      88%     84%      0
   1024    1     240034      90%      89%     87%      0
   1024    2     217430      90%      84%     87%      0
   1024    3     238522      93%      88%     85%      0
   4096    1     205726      94%      86%     87%      0
   4096    2     207881      91%      85%     88%      0
   4096    3     224297      92%      92%     85%      0
   8192    1     183518      98%      88%     87%      0
   8192    2     172677      97%      82%     86%      0
   8192    3     181380      98%      85%     88%      0
  12288    1     162612      97%      84%     87%      0
  12288    2     161270      98%      87%     88%      0
  12288    3     163215      98%      82%     88%      0
  16384    1     160370      99%      91%     88%      0
  16384    2     155599      99%      88%     87%      0
  16384    3     180150      99%      86%     87%      0
  (one htgen process refused: short)
  (one htgen process refused: short)
  19000    1     175147      99%      82%     87%      0
  (one htgen process refused: short)
  19000    2     163515      98%      85%     89%      0
  (one htgen process refused: short)
  19000    3     155546      98%      83%     89%      0
```

## Not valid above 4096 connections

A row above 4096 connections ran more than one htgen process. On a
machine with 4 vCPU only one server thread and one htgen thread are
measurable. Every row with more than one htgen process is not valid.
