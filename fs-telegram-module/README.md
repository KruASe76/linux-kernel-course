# fs-telegram-module

Tiny educational project for a Linux kernel development course: filesystem-based API for a mock messenger, implemented as a Linux kernel module.

Instead of relying on networking or complex IPC mechanisms, this module exposes chats as character devices mounted in the `/dev/telegram/` directory. Interaction with the messenger (reading chat history and sending messages) is handled natively through standard VFS syscalls (`read`, `write`, `open`, `close`).

## Architectural highlights

- **In-memory storage:** The entire message history is stored in the kernel's RAM using intrusive doubly linked lists (`struct list_head`).
- **Concurrency control:** Access to each chat state is thread-safe. It is protected by mutexes (`struct mutex`) to prevent race conditions during simultaneous reads/writes from multiple processes.
- **Miscdevice subsystem:** Utilizes the `miscdevice` API for automatic device creation.
- **VFS context management:** Chat instance identification during I/O operations is achieved by storing pointers in `file->private_data` during the `open()` syscall. The `container_of` macro is then used to resolve the parent structure.
- **Safe user-space boundaries:** Data transfer between user space and kernel space is strictly handled via `copy_to_user` / `copy_from_user` and the `simple_read_from_buffer` helper to correctly manage cursor offsets (`*ppos`).

## Project structure

- `fs_telegram.c` - The core kernel module source code.
- `telegram_client.c` - A user-space CLI utility (API client) for convenient interaction with the driver.

## Build instructions

Target Linux kernel version: `6.18.8`

```bash
# building for an out-of-tree kernel
make KDIR=/path/to/linux-6.18.8

# cleanup
make clean
```

## Installation & deployment (QEMU)

1. Copy compiled module file and client binary to the target FS:

   ```bash
   cp fs_telegram.ko telegram_client /path/to/rootfs
   ```

2. Spin up the virtual machine (QEMU or similar)
3. Load the kernel module:

   ```sh
   insmod fs_telegram.ko
   ```

   A success log should show up in `dmesg` and five chat devices should appear under `/dev/telegram/`

## Usage

### Method 1: direct VFS interaction (`cat` / `echo`)

Most simple and straightforward way

```bash
# read chat #1
cat /dev/telegram/chat_1

# send message to chat #2
echo "Hello from the terminal!" > /dev/telegram/chat_2
```

### Method 2: using the `telegram_client` cli tool

The client provides a cleaner output and handles specific kernel error codes gracefully (e.g., catching EMSGSIZE when a message exceeds the buffer limit)

```bash
# read chat #3
./telegram_client read 3

# send a message to chat #4
./telegram_client write 4 "Hello, kernel!"
```
