# RemoteOps – Remote System Monitoring and Management Tool

## 1. Project Overview

RemoteOps is a client-server based remote system monitoring and management tool implemented in C using BSD sockets.

The system consists of:

- **Agent** – Server program running on the managed machine.
- **Controller** – Client program used by the administrator.

TCP is used for the main control channel, while UDP is used for periodic system monitoring.

---

## 2. Personalisation

Registration Number: **IT24100430**

| Item | Personalised Value |
|---|---|
| Agent TCP Port | 9410 |
| UDP Monitoring Port | 9411 |
| Session ID | SID:0340 |
| Authentication Token | OPS-0430 |
| Agent Source File | agent_430.c |
| Controller Source File | controller_430.c |
| Makefile | Makefile_430 |
| Log File | remoteops_IT24100430.log |
| Storage Path | ./agentfiles/IT24100430/ |
| Submission Archive | IE3090_IT24100430.zip |

---

## 3. Project Structure

```text
remoteops/
├── agent_430.c
├── controller_430.c
├── Makefile_430
├── README.md
├── remoteops_IT24100430.log
└── agentfiles/
    └── IT24100430/
```
 
## 4. Main Features

- TCP client-server communication
- Authentication using a personalised token
- System information monitoring
- Process listing
- Whitelisted command execution
- File upload using PUT
- File download using GET
- UDP-based periodic monitoring
- Multiple simultaneous Controller connections using pthreads
- Timestamped event logging
- Graceful and ungraceful disconnect handling

---

## 5. Concurrency Model

The Agent uses a thread-per-connection model with POSIX pthreads.

A separate thread is created for each connected Controller. This allows multiple Controllers to communicate with the Agent simultaneously.

The implementation was tested with five simultaneous Controller connections.

---

## 6. Communication

TCP port: 9410

TCP is used for authentication, commands, and file transfers.

UDP port: 9411

UDP is used for periodic monitoring data during MONITOR START.

---

## 7. Authentication

The Controller must authenticate before executing other commands.

Authentication token: OPS-0430

Session ID: SID:0340

---

## 8. File Storage and Logging

Uploaded files are stored in:

./agentfiles/IT24100430/

The Agent maintains the following timestamped log file:

remoteops_IT24100430.log

The log records connections, commands, file transfers, and disconnect events.

---

## 9. Build Instructions

Compile the project using the personalised Makefile:

make -f Makefile_430

To remove generated executables:

make -f Makefile_430 clean

---

## 10. Running the System

Start the Agent:

./agent_430

Start the Controller from another terminal:

./controller_430

---

## 11. Testing

The following functions were tested:

- Authentication
- SYSINFO
- LISTPROC
- EXEC DATE
- EXEC UPTIME
- EXEC DISKFREE
- EXEC HOSTNAME
- EXEC WHOAMI
- Rejection of non-whitelisted EXEC commands
- PUT file upload
- GET file download
- MONITOR START
- MONITOR STOP
- Controller disconnect handling
- Five simultaneous Controller connections

File transfer testing verified the transferred file size and contents.

---

## 12. Development Process

The project was developed incrementally using Git commits.

1. Initial RemoteOps implementation
2. TCP message framing
3. Logging foundation
4. Complete required logging
5. Ungraceful disconnect handling
6. Connection logging and concurrency testing
7. Personalised Makefile and build support
8. Final testing and documentation improvements

---

## 13. Requirements Summary

The implementation provides TCP control communication, authentication, system monitoring, process listing, whitelisted command execution, file transfer, UDP monitoring, concurrent Controller handling, and timestamped logging.

