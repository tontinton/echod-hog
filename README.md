A server able to communicate with multiple client concurrently on a single thread using non blocking sockets.

```sh
gcc main.c -O3 -o main && ./main
```

Beware though that this example will take 100% of your core, as it never ever sleeps. 💤
