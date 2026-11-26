cp33010@atlantica:~/kmeans-t4-comp-par$ scontrol show node atlantica06 | grep -E "CPUTot|Sockets|CoresPerSocket|Thread
sPerCore|State"
NodeName=atlantica06 Arch=x86_64 CoresPerSocket=4 
   CPUAlloc=0 CPUTot=16 CPULoad=0.00
   RealMemory=16000 AllocMem=0 FreeMem=7501 Sockets=2 Boards=1
   State=IDLE ThreadsPerCore=2 TmpDisk=0 Weight=1 Owner=N/A MCS_label=N/A
cp33010@atlantica:~/kmeans-t4-comp-par$ scontrol show node atlantica07 | grep -E "CPUTot|Sockets|CoresPerSocket|Thread
sPerCore|State"
NodeName=atlantica07 Arch=x86_64 CoresPerSocket=4 
   CPUAlloc=0 CPUTot=16 CPULoad=0.00
   RealMemory=16000 AllocMem=0 FreeMem=9759 Sockets=2 Boards=1
   State=IDLE ThreadsPerCore=2 TmpDisk=0 Weight=1 Owner=N/A MCS_label=N/A
cp33010@atlantica:~/kmeans-t4-comp-par$ scontrol show node atlantica08 | grep -E "CPUTot|Sockets|CoresPerSocket|Thread
sPerCore|State"
NodeName=atlantica08 Arch=x86_64 CoresPerSocket=4 
   CPUAlloc=0 CPUTot=16 CPULoad=0.00
   RealMemory=16000 AllocMem=0 FreeMem=11637 Sockets=2 Boards=1
   State=IDLE ThreadsPerCore=2 TmpDisk=0 Weight=1 Owner=N/A MCS_label=N/A
cp33010@atlantica:~/kmeans-t4-comp-par$ scontrol show node atlantica09 | grep -E "CPUTot|Sockets|CoresPerSocket|Thread
sPerCore|State"
NodeName=atlantica09 Arch=x86_64 CoresPerSocket=4 
   CPUAlloc=0 CPUTot=16 CPULoad=0.01
   RealMemory=16000 AllocMem=0 FreeMem=12245 Sockets=2 Boards=1
   State=IDLE ThreadsPerCore=2 TmpDisk=0 Weight=1 Owner=N/A MCS_label=N/A