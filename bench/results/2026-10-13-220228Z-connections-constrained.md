# Requests per second against open connections, on a constrained host

What: rps of webmachine-serve, one server thread, GET /hello over
AF_UNIX, answer rendered at compile time. htgen is the client. htgen
takes at most 4096 connections, so one htgen process runs for each 4096.
Each step runs 3 times for 5 s.

Commit: 787a515
Server: g++ 16 (Debian sid), build_config_release.rb, -O3 -march=x86-64-v3 -std=c++26 -freflection.
Machine:  Intel(R) Xeon(R) Processor @ 2.80GHz, 4 vCPU, VM in a cloud sandbox, kernel 6.18.44-fc-v114.
Container: docker, seccomp=unconfined, nofile 20000 (hard limit of the sandbox), memlock 8192 KiB.
Not pinned, default nice, run as root inside the container.

The rule "both sides at 90 percent of a core" is not met in most rows.
The owner asked for this host as it is.

Rows marked "short" lost one or more htgen processes to
"queue_init: Cannot allocate memory": the io_uring rings of all htgen
processes are charged to one memlock budget of 8192 KiB. Those rows
had fewer connections than they name.

```
nofile=20000 memlock=8192 nproc=4 kernel=6.18.44-fc-v114
  conns  rep        rps     srv%     cli%   kern%    bad
     64    1     330653      89%      86%     87%      0
     64    2     304039      90%      82%     83%      0
     64    3     307255      92%      84%     84%      0
    256    1     309841      92%      86%     85%      0
    256    2     309128      93%      85%     85%      0
    256    3     282883      93%      86%     86%      0
   1024    1     214949      86%      86%     85%      0
   1024    2     226780      92%      88%     85%      0
   1024    3     230678      85%      88%     85%      0
   4096    1     201688      94%      86%     86%      0
   4096    2     214448      93%      89%     86%      0
   4096    3     207954      94%      88%     86%      0
   8192    1     188488      98%      92%     89%      0
   8192    2     178110      98%      89%     88%      0
   8192    3     174459      98%      85%     87%      0
  12288    1     152753      98%      93%     90%      0
  12288    2     166176      98%      93%     89%      0
  12288    3     162824      98%      92%     88%      0
  (one htgen process refused: short)
  16384    1     166449      99%      91%     89%      0
  16384    2     151768      96%      91%     88%      0
  16384    3     157450      99%      92%     90%      0
  (one htgen process refused: short)
  (one htgen process refused: short)
  19000    1     160638      99%      85%     87%      0
  (one htgen process refused: short)
  19000    2     160091      97%      88%     87%      0
  (one htgen process refused: short)
  (one htgen process refused: short)
  19000    3     161353      98%      79%     87%      0
```

## Not valid above 4096 connections

A row above 4096 connections ran more than one htgen process. On a
machine with 4 vCPU only one server thread and one htgen thread are
measurable. Every row with more than one htgen process is not valid.
