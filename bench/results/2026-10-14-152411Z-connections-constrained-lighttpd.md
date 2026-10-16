# lighttpd: requests per second against open connections, on a constrained host

The same run as the podman run of webmachine-serve, with lighttpd in its
place. Not alternated; the runs followed each other on the same day.

lighttpd 1.4.85 (Debian sid package), one process (server.max-worker = 0),
epoll, AF_UNIX. The body is the same 39 bytes from a static file,
Content-Type text/html; charset=utf-8. lighttpd adds Last-Modified and
Accept-Ranges to the head.
Container: podman, seccomp=unconfined, nofile 20000, memlock 8192 KiB.

Limits of lighttpd on this host: it takes at most one tenth of
server.max-fds as connections, so 2000 here. It closes a connection after
65535 requests (the largest value of server.max-keep-alive-requests),
and htgen does not open it again. No step reached that count.

lighttpd.conf:

```
server.document-root = "/s/lig/www"
server.bind = "/run/lig/s.sock"
server.max-worker = 0
server.max-fds = 20000
server.max-connections = 19900
server.max-keep-alive-requests = 65535
server.max-keep-alive-idle = 600
server.event-handler = "linux-sysepoll"
server.tag = ""
server.errorlog = "/dev/stderr"
mimetype.assign = ( "" => "text/html; charset=utf-8" )
static-file.etags = "disable"
```

```
lighttpd/1.4.85 (ssl) - a light and fast webserver
nofile=20000 memlock=8192 nproc=4 kernel=6.18.44-fc-v114
  conns  rep        rps     srv%     cli%   kern%    bad
     64    1     199661      96%      64%     67%      0
     64    2     172867      96%      60%     70%      0
     64    3     199672      96%      66%     69%      0
    256    1     169588      98%      62%     71%      0
    256    2     180822      97%      63%     71%      0
    256    3     143981      91%      58%     69%      0
   1024    1     194075      96%      71%     68%      0
   1024    2     191959      97%      69%     70%      0
   1024    3     188089      96%      66%     69%      0
   1900    1     195699      96%      68%     72%      0
   1900    2     182394      97%      66%     70%      0
   1900    3     182656      95%      67%     69%      0
```

## Not valid above 4096 connections

A row above 4096 connections ran more than one htgen process. On a
machine with 4 vCPU only one server thread and one htgen thread are
measurable. Every row with more than one htgen process is not valid.
