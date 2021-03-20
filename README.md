# ns3-process
run ns3  in multiple processes mode.  
1 first version:  
```
./waf --run "scratch/ns3-p0"  
```
2 second version:  
build the client (request.cc).  
```
./waf --run "scratch/ns3-p1"  
```
The client will send request over socket to ns3-p1 to start the simulation.  
3 The blog on this repo:  
[1][Run ns3 with multiple processes](https://blog.csdn.net/u010643777/article/details/10723731)   


