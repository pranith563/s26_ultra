#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

static void fail(const char* operation) {
    std::perror(operation);
    std::exit(1);
}

static void sync_buffer(int fd, unsigned long long flags) {
    dma_buf_sync sync{flags};
    int result;
    do {
        result = ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
    } while (result < 0 && (errno == EINTR || errno == EAGAIN));
    if (result < 0) fail("DMA_BUF_IOCTL_SYNC");
}

int main(int argc, char** argv) {
    const char* heap_path = argc > 1 ? argv[1] : "/dev/dma_heap/system";
    constexpr size_t size = 65536;
    const int heap = open(heap_path, O_RDONLY | O_CLOEXEC);
    if (heap < 0) fail("open heap");
    dma_heap_allocation_data allocation{};
    allocation.len = size;
    allocation.fd_flags = O_RDWR | O_CLOEXEC;
    if (ioctl(heap, DMA_HEAP_IOCTL_ALLOC, &allocation) < 0) fail("DMA_HEAP_IOCTL_ALLOC");
    close(heap);
    const int fd = static_cast<int>(allocation.fd);
    auto* buffer = static_cast<unsigned char*>(mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    if (buffer == MAP_FAILED) fail("mmap");
    sync_buffer(fd, DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE);
    std::memset(buffer, 0x5a, size);
    sync_buffer(fd, DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE);
    if (munmap(buffer, size) < 0) fail("munmap");

    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets) < 0) fail("socketpair");
    const pid_t child = fork();
    if (child < 0) fail("fork");
    char payload = 'F';
    iovec iov{&payload, sizeof(payload)};
    alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int))]{};
    msghdr message{};
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    if (child == 0) {
        close(fd);
        close(sockets[0]);
        if (recvmsg(sockets[1], &message, MSG_CMSG_CLOEXEC) != 1) fail("recvmsg");
        const cmsghdr* header = CMSG_FIRSTHDR(&message);
        if ((message.msg_flags & MSG_CTRUNC) || !header || header->cmsg_level != SOL_SOCKET ||
            header->cmsg_type != SCM_RIGHTS || header->cmsg_len != CMSG_LEN(sizeof(int))) {
            std::fprintf(stderr, "Invalid SCM_RIGHTS message\n");
            return 1;
        }
        int received_fd;
        std::memcpy(&received_fd, CMSG_DATA(header), sizeof(received_fd));
        buffer = static_cast<unsigned char*>(mmap(nullptr, size, PROT_READ, MAP_SHARED, received_fd, 0));
        if (buffer == MAP_FAILED) fail("child mmap");
        sync_buffer(received_fd, DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ);
        bool valid = true;
        for (size_t i = 0; i < size; ++i) {
            if (buffer[i] != 0x5a) { valid = false; break; }
        }
        sync_buffer(received_fd, DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ);
        munmap(buffer, size);
        close(received_fd);
        close(sockets[1]);
        _exit(valid ? 0 : 1);
    }
    close(sockets[1]);
    cmsghdr* header = CMSG_FIRSTHDR(&message);
    header->cmsg_level = SOL_SOCKET;
    header->cmsg_type = SCM_RIGHTS;
    header->cmsg_len = CMSG_LEN(sizeof(fd));
    std::memcpy(CMSG_DATA(header), &fd, sizeof(fd));
    if (sendmsg(sockets[0], &message, 0) != 1) fail("sendmsg");
    close(sockets[0]);
    close(fd);
    int status;
    if (waitpid(child, &status, 0) < 0) fail("waitpid");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::fprintf(stderr, "Child buffer verification failed\n");
        return 1;
    }
    std::printf("PASS uid=%u heap=%s bytes=%zu allocation,mmap,CPU-sync,SCM_RIGHTS,child-readback\n",
                static_cast<unsigned>(getuid()), heap_path, size);
    return 0;
}
