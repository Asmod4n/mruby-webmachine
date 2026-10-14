# Requests per second against open connections, one server thread and one htgen thread

What: rps of webmachine-serve, one server thread, GET /hello over
AF_UNIX. One htgen process with one thread takes every connection.
htgen at 3403863 with one local change: the
check "--conns must be 1..4096" accepts every count of 1 or more.
3 runs of 5 s for each step.
Commit: c5de114.
Machine:  Intel(R) Xeon(R) Processor @ 2.10GHz, 4 vCPU, kernel 6.18.44-fc-v114.
Container: podman, seccomp=unconfined, nofile 20000, memlock 8192 KiB.

A row counts where the server and htgen each use 90 percent of a core
or more. From 8192 connections on, the server stays under 90 percent in
most runs: those rows measured a client that could not keep the server
busy.

```
nofile=20000 memlock=8192 nproc=4 kernel=6.18.44-fc-v114
  conns  rep        rps     srv%     cli%   kern%    bad
   1024    1     450624      94%      90%     89%      0
   1024    2     454760      91%      91%     89%      0
   1024    3     461166      92%      92%     90%      0
   2048    1     496489      96%      95%     91%      0
   2048    2     474057      94%      93%     90%      0
   2048    3     470355      95%      93%     90%      0
   4096    1     438764      95%      92%     90%      0
   4096    2     460706      97%      94%     91%      0
   4096    3     431806      95%      90%     93%      0
   6144    1     423243      93%      94%     93%      0
   6144    2     366410      89%      95%     91%      0
   6144    3     385723      90%      93%     93%      0
   8192    1     350991      89%      96%     92%      0
   8192    2     345575      89%      95%     90%      0
   8192    3     302069      89%      93%     91%      0
  10240    1     304937      83%      93%     92%      0
  10240    2     265428      85%      93%     92%      0
  10240    3     290722      87%      95%     92%      0
  12288    1     294426      90%      95%     91%      0
  12288    2     278332      85%      95%     91%      0
  12288    3     248006      88%      86%     90%      0
  14336    1     238488      82%      92%     92%      0
  14336    2     265062      87%      93%     93%      0
  14336    3     275237      92%      92%     94%      0
  16384    1     265814      92%      92%     93%      0
  16384    2     258148      83%      95%     90%      0
  16384    3     254554      85%      94%     92%      0
  19000    1     264055      87%      94%     93%      0
  19000    2     255968      86%      94%     94%      0
  19000    3     259074      82%      94%     92%      0
```
