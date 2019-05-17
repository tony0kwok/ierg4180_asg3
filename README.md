# ierg4180_asg3

## How to run

After cloning the project,
Please open your shell, and input

```
cd {PROJECT_PATH}
```

then make the project
```
make
```

run the server
```
./np_server --stat 1000 -lhost localhost --lport 4180 --sbufsize 1000 --rbufsize 1000 --servermodel threadpoo
```

run the client
```
 ./np_client --send --stat 1000 rhost localhost -rport 4180 --proto udp --pktsize 1000 --pktrate 1000 --pktnum 20 --sbufsize 1000
```

Please noticed the parameter in this project is declared by "--" instead of "-".
