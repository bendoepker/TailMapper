# TailMapper
Map specific remote subnets into your local subnet

## TailMapper.exe
Manage the mappings on your machine

## TailMapperExpose.exe
Manage the exposed mappings on the remote machine

# General Architecture
```mermaid
---
config:
    theme: 'neutral'
---
flowchart LR
    ts[TailScale] --> tnc1[TailNet Client 1]
    tnc1 --> plc1[PLC @ 10.0.0.1]
    tnc1 --> e1[Ewon @ 10.0.0.2]
    tnc1 --> h1[HMI @ 10.0.0.3]
    ts -.- tnc2[TailNet Client 2]
    tnc2 -.- plc2[PLC @ 10.0.0.1]
    tnc2 -.- e2[Ewon @ 10.0.0.2]
    tnc2 -.- h2[HMI @ 10.0.0.3]
    ts -.- tnc3[TailNet Client 3]
    tnc3 -.- plc3[PLC @ 10.0.0.1]
    tnc3 -.- e3[Ewon @ 10.0.0.2]
    tnc3 -.- h3[HMI @ 10.0.0.3]
    la[Local App] --> sr[Send Request to 10.0.0.1]
    sr --> wfp[Windows Filtering Platform]
    wfp -.- rrt([Routes request through the TailNet])
    wfp --> ts
    tm[TailMapper] --> wd[WinDivert]
    tm -.- srt([Set 10.0.0.0/24 to route to TailNet Client 1])
    wd[WinDivert] -.- sc([Set Windows Internal IP Mapping Rules])
    wd --> wfp

    linkStyle 0,1,2,3,12,13,15 stroke:green
    linkStyle 4,5,6,7,8,9,10,11 stroke:red
```

# Thread Layout
TLDR; The main thread is the communication layer between the GUI, Tailscale Runner, and WinDivert
```mermaid
---
config:
    theme: 'neutral'
---
flowchart LR
    mt[Main Thread] --> |Spawn| gt[GUI Thread]
    gt --> |User input| mt
    mt -.- mtd([Central communication between WinDivert, GUI, and Tailscale Runner])
    mt --> |Spawn| trt[Tailscale Runner Thread]
    mt --> |Add / Remove mapping rules| wdv[WinDivert]
    gt -.- gtd([Send all user interactions to the main thread])
    trt -.- trtd([Synchronize with tailscale state, update Global state and send signals to main thread])
```

# Building
```powershell
cmake --build --preset dev
```

# Running
```powershell
./build/tailmapper.exe
```
