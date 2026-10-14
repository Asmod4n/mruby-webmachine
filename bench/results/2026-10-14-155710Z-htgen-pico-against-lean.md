# htgen: picohttpparser against a reader that takes only what it needs

What: two htgen binaries against one webmachine-serve, GET /hello over
AF_UNIX, one server thread, one htgen thread. Runs of 5 s, the two arms
alternated: pico, lean, pico, lean.

- pico: htgen as it is, phr_parse_response and a loop over the fields
  for Content-Length.
- lean: -DHTGEN_LEAN. It finds the end of the head with
  std::string_view::find("\r\n\r\n"), checks "HTTP/" and the first digit
  of the status, finds "\r\nContent-Length:" (case-sensitive) and reads
  the number with std::from_chars.

Both: htgen at 3403863 with the local changes (no 4096 cap,
the lean reader under HTGEN_LEAN), g++ 16, -O3 -march=native, each arm
in its own binary. Server at commit f3a95b2.
Machine:  Intel(R) Xeon(R) Processor @ 2.10GHz, 4 vCPU, kernel 6.18.44-fc-v114, podman,
seccomp=unconfined. The noise floor was not measured.

| conns | arm | n | median rps | min | max |
|---|---|---|---|---|---|
| 2048 | pico | 5 | 395143 | 387349 | 403613 |
| 2048 | lean | 5 | 394866 | 381712 | 402549 |
| 8192 | pico | 15 | 253398 | 227012 | 313036 |
| 8192 | lean | 15 | 267872 | 233776 | 319146 |

```
2048 1 pico bad=0 rps=403613 
2048 1 lean bad=0 rps=394866 
2048 2 pico bad=0 rps=394547 
2048 2 lean bad=0 rps=381712 
2048 3 pico bad=0 rps=395143 
2048 3 lean bad=0 rps=395428 
2048 4 pico bad=0 rps=387349 
2048 4 lean bad=0 rps=402549 
2048 5 pico bad=0 rps=401649 
2048 5 lean bad=0 rps=387236 
8192 1 pico bad=0 rps=280486 
8192 1 lean bad=0 rps=304758 
8192 2 pico bad=0 rps=253398 
8192 2 lean bad=0 rps=268212 
8192 3 pico bad=0 rps=240254 
8192 3 lean bad=0 rps=236319 
8192 4 pico bad=0 rps=246671 
8192 4 lean bad=0 rps=267872 
8192 5 pico bad=0 rps=251141 
8192 5 lean bad=0 rps=271706 
8192 6 pico bad=0 rps=280041 
8192 6 lean bad=0 rps=241748 
8192 7 pico bad=0 rps=231141 
8192 7 lean bad=0 rps=241295 
8192 8 pico bad=0 rps=244718 
8192 8 lean bad=0 rps=238860 
8192 9 pico bad=0 rps=227012 
8192 9 lean bad=0 rps=247894 
8192 10 pico bad=0 rps=235334 
8192 10 lean bad=0 rps=272693 
8192 11 pico bad=0 rps=268842 
8192 11 lean bad=0 rps=233776 
8192 12 pico bad=0 rps=261620 
8192 12 lean bad=0 rps=291686 
8192 13 pico bad=0 rps=274667 
8192 13 lean bad=0 rps=256904 
8192 14 pico bad=0 rps=264577 
8192 14 lean bad=0 rps=319146 
8192 15 pico bad=0 rps=313036 
8192 15 lean bad=0 rps=273190 
```
