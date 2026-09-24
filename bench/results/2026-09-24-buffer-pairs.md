# Receive buffer and send room as neighbours in one pool

2026-09-24, on the host (bench/measure_on_host.sh: uid 65534, nice -10, cores 1-3,
core 0 left over), -O2 -march=x86-64-v4, one clock, every arm its own
shared object, random interleaving, medians; the empty arm beside every
set is the noise floor, 2.4 to 3.2 ns. Host: Intel Xeon 2.80GHz, 4 cpus,
cascadelake, L1d 32 KiB, L2 4 MiB, L3 33 MiB, Linux 6.18.44.

One mapping of 32 MiB with huge pages (AnonHugePages 500 to 645 MB for
the process), pairs of receive buffer and send room side by side; the
pair moves round the whole pool, larger than L2, so each comes back
cold. Per call: a stream of 663 byte GETs copied into the receive
buffer as the kernel would, the head ends found with find, a 200 byte
head and a body written into the send room per request, the send room
read once as the kernel would. An answer that does not fit goes to a
cold overflow room. Today: a 4 KiB receive buffer and a separate cold
16 KiB room. Twenty repetitions; time per request is the median over
receive bytes / 663.

| receive / send | g++, body 0 / 1 KiB / 4 KiB | clang, body 0 / 1 KiB / 4 KiB |
|----------------|-----------------------------|-------------------------------|
| 4 / 4 KiB      | 228 / 327 / 1066 ns         | 200 / 392 / 1053 ns           |
| 4 / 8 KiB      | 236 / 363 / 1086 ns         | 190 / 398 / 926 ns            |
| 4 / 16 KiB     | 221 / 294 / 1077 ns         | 179 / 305 / 1061 ns           |
| 8 / 4 KiB      | 245 / 335 / 1047 ns         | 194 / 376 / 923 ns            |
| 8 / 8 KiB      | 248 / 343 / 1088 ns         | 214 / 358 / 787 ns            |
| 8 / 16 KiB     | 241 / 327 / 882 ns          | 213 / 392 / 914 ns            |
| 16 / 4 KiB     | 329 / 485 / 839 ns          | 236 / 508 / 803 ns            |
| 16 / 8 KiB     | 325 / 484 / 830 ns          | 237 / 368 / 1087 ns           |
| 16 / 16 KiB    | 273 / 441 / 1027 ns         | 306 / 497 / 888 ns            |
| today          | 230 / 371 / 796 ns          | 189 / 303 / 1013 ns           |

Spread 2 to 40 percent, most rows 10 to 30.

What it says: a 16 KiB receive buffer costs more per request with a
small body, under both compilers. Between 4 and 8 KiB, any send room,
and today, no row is faster beyond the spread. A 4 KiB pair is 8 KiB;
the pairing saves the 16 KiB room per connection, not time.
