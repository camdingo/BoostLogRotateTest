# Boost.Log Rotation Deadlock Debugging Guide

## The Problem

You're experiencing a deadlock during log rotation at 100MB, with the hang occurring in `open64()` from libc. This is a known issue that can occur when:

1. **File descriptor conflicts**: The same file is being opened while it's still open
2. **Filesystem locking**: NFS or other network filesystems can have locking issues
3. **Synchronous sink contention**: Multiple threads competing for the sink lock during rotation
4. **Signal handlers or atexit handlers**: Interfering with file operations

## Common Root Causes

### 1. Synchronous Sink + Multiple Threads
When using synchronous sinks, ALL logging threads block on a mutex during rotation. If rotation involves:
- Opening the same file (which may still be open)
- Filesystem operations that block
- Any callback that logs (creates reentrancy)

This can deadlock.

### 2. File System Issues
- NFS can have stale file handle issues
- Some filesystems don't handle rapid open/close/rename well
- File descriptor leaks can exhaust available FDs

### 3. Auto-Flush + Rotation
The combination of auto_flush + rotation can cause issues if:
- Flush is called during rotation
- File is closed but flush is still pending

## Testing Strategies

### Test 1: Reproduce with Sync vs Async
```cpp
const bool USE_ASYNC = false;  // Change to true
```
- If async works but sync doesn't → sync sink mutex contention
- Both hang → deeper filesystem/file descriptor issue

### Test 2: Disable Auto-Flush
```cpp
const bool AUTO_FLUSH = false;
```
- If this fixes it → flush/rotation race condition
- Still hangs → not flush related

### Test 3: Increase Thread Count
```cpp
const int NUM_THREADS = 8;  // or 16
```
- More threads = more contention = higher chance to reproduce

### Test 4: Monitor File Descriptors
```bash
# In another terminal while app is running
watch -n 1 'lsof -p $(pgrep log_app) | grep app.log'
```
Look for multiple open file descriptors to the same file.

### Test 5: Use strace to Find the Hang
```bash
strace -f -tt -T ./log_app 2>&1 | tee strace.log
```
When it hangs, check the last system calls. Look for:
- `open()` or `open64()` that doesn't return
- Multiple threads in `futex()` (waiting on mutex)
- `fcntl()` or `flock()` operations

### Test 6: Check for Reentrancy
Add this to rotation callback:
```cpp
void on_rotation(const std::string& filename) {
    // BAD - This will deadlock!
    // BOOST_LOG_TRIVIAL(info) << "Rotation occurred";
    
    // GOOD - Use stdio
    std::cout << "Rotation occurred" << std::endl;
}
```

## Known Solutions

### Solution 1: Use Async Sink
```cpp
const bool USE_ASYNC = true;
```
Async sinks decouple logging from file I/O, reducing contention.

### Solution 2: Use Separate Files (Target Directory)
Instead of reusing the same filename:
```cpp
logging::add_file_log(
    keywords::file_name = "app_%N.log",
    keywords::rotation_size = 100 * 1024 * 1024,
    keywords::target = "logs",
    keywords::max_files = 5
);
```

### Solution 3: Disable Auto-Flush During Heavy Load
Only flush periodically or on important messages:
```cpp
keywords::auto_flush = false
```

### Solution 4: Custom File Collector
Implement a collector that handles the rotation more carefully:
```cpp
auto backend = boost::make_shared<sinks::text_file_backend>();
backend->set_file_collector(sinks::file::make_collector(...));
```

### Solution 5: External Log Rotation
Don't use Boost.Log's rotation. Let the OS handle it:
- Use logrotate (Linux)
- Write to a continuous file
- Let external tools handle rotation

## Diagnostic Commands

### Check for deadlock with gdb:
```bash
gdb -p $(pgrep log_app)
(gdb) info threads
(gdb) thread apply all bt
```

Look for threads stuck in:
- `__lll_lock_wait` (mutex wait)
- `open64()`
- File backend operations

### Check filesystem:
```bash
df -h .           # Check disk space
df -i .           # Check inode availability
mount | grep nfs  # Check if on NFS
```

### Monitor during rotation:
```bash
# Terminal 1: Run app
./log_app

# Terminal 2: Watch file operations
sudo inotifywait -m app.log

# Terminal 3: Watch system calls
sudo strace -p $(pgrep log_app) -e trace=open,openat,close,write
```

## Workarounds for Production

1. **Switch to async sink** (easiest, usually fixes it)
2. **Increase rotation size** to reduce frequency
3. **Use dated/numbered filenames** instead of same name
4. **Disable rotation entirely** and use external rotation
5. **Add a rotation mutex** in your code to serialize rotations

## Code Instrumentation

Add this to detect where threads are during hang:

```cpp
// Before rotation
std::cout << "[TID:" << std::this_thread::get_id() 
          << "] About to log (rotation imminent)" << std::endl;

BOOST_LOG_SEV(lg, logging::trivial::info) << "Message";

std::cout << "[TID:" << std::this_thread::get_id() 
          << "] Log completed" << std::endl;
```

If you see "About to log" but never "Log completed", the thread is stuck in the log call.
