# nginx: requests per second against open connections, on a constrained host

The same run as 2026-10-14-*-connections-constrained-podman.md, with
nginx in place of webmachine-serve. Not alternated with the
webmachine run; the two ran one after the other on the same day.

nginx:  nginx/1.30.4 (Debian sid package nginx-light), one worker,
epoll, access_log off, keepalive_requests 1000000000, AF_UNIX.
The body is the same 39 bytes: <html><body>Hello, World!</body></html>,
Content-Type text/html; charset=utf-8, from "return 200".
nginx adds the fields Server and Connection to the head.
Container: podman, seccomp=unconfined, nofile 20000, memlock 8192 KiB.
srv% is the nginx worker. The 19000 rows are whole: nginx takes no
io_uring ring, so the memlock budget held all five htgen processes.

nginx.conf:

```
worker_processes 1;
worker_rlimit_nofile 20000;
pid /tmp/nginx.pid;
error_log stderr warn;
events { worker_connections 20000; use epoll; multi_accept on; }
http {
  access_log off;
  server_tokens off;
  keepalive_requests 1000000000;
  keepalive_timeout 600s;
  server {
    listen unix:/run/ngx/s.sock backlog=4096;
    location = /hello {
      default_type "text/html; charset=utf-8";
      return 200 "<html><body>Hello, World!</body></html>";
    }
  }
}
```

```
nginx nginx version: nginx/1.30.4 worker=13
nofile=20000 memlock=8192 nproc=4 kernel=6.18.44-fc-v114
  conns  rep        rps     srv%     cli%   kern%    bad
     64    1     151006      95%      53%     60%      0
     64    2     139845      94%      54%     59%      0
     64    3     134743      98%      55%     61%      0
    256    1     138482      98%      50%     63%      0
    256    2     146478      98%      56%     60%      0
    256    3     161614      95%      64%     61%      0
   1024    1     155951      97%      57%     61%      0
   1024    2     130562      97%      51%     61%      0
   1024    3     140962      95%      52%     65%      0
   4096    1     137308      96%      56%     65%      0
   4096    2     134446      97%      57%     63%      0
   4096    3     130287      96%      54%     64%      0
   8192    1     107132      97%      61%     65%      0
   8192    2     101049      98%      55%     64%      0
   8192    3      88430      98%      52%     67%      0
  12288    1     100981      97%      54%     67%      0
  12288    2      98937      98%      53%     67%      0
  12288    3      79276      97%      50%     66%      0
  16384    1      84490      97%      65%     66%      0
  16384    2      82785      98%      52%     69%      0
  16384    3      87532      98%      52%     64%      0
  19000    1      83406      98%      58%     65%      0
  19000    2      82625      98%      57%     65%      0
  19000    3      78482      98%      52%     65%      0
```

## Not valid above 4096 connections

A row above 4096 connections ran more than one htgen process. On a
machine with 4 vCPU only one server thread and one htgen thread are
measurable. Every row with more than one htgen process is not valid.
