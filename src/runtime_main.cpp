#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "ppc/ppc_config.h"
#include "ppc/ppc_context.h"
#include "ppc/ppc_recomp_shared.h"

namespace {

struct GuestMemory
{
    uint8_t* base = nullptr;
    size_t size = 0;
};

void print_usage(std::string_view exe_name)
{
    std::cerr << "Usage: " << exe_name << " [--xex <path>] [--entrypoint <hex>]\n"
              << "Defaults:\n"
              << "  --xex private/Default.xex\n"
              << "  --entrypoint 0x" << std::hex << PPC_CODE_BASE << std::dec
              << " (first recomp function)\n";
}

bool parse_hex_u64(std::string_view text, uint64_t& value)
{
    std::string trimmed(text);
    if (trimmed.rfind("0x", 0) == 0 || trimmed.rfind("0X", 0) == 0)
    {
        trimmed = trimmed.substr(2);
    }
    if (trimmed.empty())
    {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    value = std::strtoull(trimmed.c_str(), &end, 16);
    return errno == 0 && end != nullptr && *end == '\0';
}

GuestMemory allocate_guest_memory(size_t size)
{
    GuestMemory memory;
    memory.size = size;
#if defined(_WIN32)
    memory.base = static_cast<uint8_t*>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!memory.base)
    {
        std::cerr << "VirtualAlloc failed for guest memory.\n";
    }
#else
    void* mapped = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (mapped == MAP_FAILED)
    {
        std::cerr << "mmap failed for guest memory: " << std::strerror(errno) << "\n";
        return memory;
    }
    memory.base = static_cast<uint8_t*>(mapped);
#endif
    return memory;
}

void release_guest_memory(GuestMemory& memory)
{
    if (!memory.base)
    {
        return;
    }
#if defined(_WIN32)
    VirtualFree(memory.base, 0, MEM_RELEASE);
#else
    munmap(memory.base, memory.size);
#endif
    memory.base = nullptr;
    memory.size = 0;
}

bool load_file(const std::string& path, std::vector<uint8_t>& out)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        std::cerr << "Failed to open file: " << path << "\n";
        return false;
    }

    std::streamsize size = file.tellg();
    if (size < 0)
    {
        std::cerr << "Failed to read file size for: " << path << "\n";
        return false;
    }

    out.resize(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(out.data()), size))
    {
        std::cerr << "Failed to read file: " << path << "\n";
        return false;
    }

    return true;
}

bool load_xex_into_memory(uint8_t* base, const std::string& path)
{
    std::vector<uint8_t> xex_bytes;
    if (!load_file(path, xex_bytes))
    {
        return false;
    }

    if (xex_bytes.size() > PPC_IMAGE_SIZE)
    {
        std::cerr << "XEX is larger than PPC_IMAGE_SIZE (" << xex_bytes.size()
                  << " > " << PPC_IMAGE_SIZE << "), truncating.\n";
    }

    size_t copy_size = std::min<size_t>(xex_bytes.size(), PPC_IMAGE_SIZE);
    std::memcpy(base + PPC_IMAGE_BASE, xex_bytes.data(), copy_size);
    std::cerr << "Loaded " << copy_size << " bytes into guest memory at 0x"
              << std::hex << PPC_IMAGE_BASE << std::dec << ".\n";
    return true;
}

bool initialize_function_table(uint8_t* base)
{
    const uint64_t table_offset = PPC_IMAGE_BASE + PPC_IMAGE_SIZE;
    const size_t table_entries = static_cast<size_t>(PPC_CODE_SIZE * 2u);
    const size_t table_bytes = table_entries * sizeof(PPCFunc*);

    if (table_offset + table_bytes > PPC_MEMORY_SIZE)
    {
        std::cerr << "Function table exceeds guest memory size." << "\n";
        return false;
    }

    auto* table = reinterpret_cast<PPCFunc**>(base + table_offset);
    std::memset(table, 0, table_bytes);

    for (size_t index = 0; PPCFuncMappings[index].guest != 0; ++index)
    {
        const uint64_t guest_addr = PPCFuncMappings[index].guest;
        if (guest_addr < PPC_CODE_BASE)
        {
            continue;
        }
        const uint64_t entry_index = (guest_addr - PPC_CODE_BASE) * 2u;
        if (entry_index >= table_entries)
        {
            std::cerr << "Skipping mapping outside table: 0x" << std::hex << guest_addr << std::dec << "\n";
            continue;
        }
        table[entry_index] = PPCFuncMappings[index].host;
    }

    return true;
}

void initialize_context(PPCContext& ctx)
{
    std::memset(&ctx, 0, sizeof(ctx));
    ctx.fpscr.disableFlushModeUnconditional();
    ctx.r1.u64 = PPC_MEMORY_SIZE - 0x10000ull;
    ctx.r13.u64 = 0;
    ctx.r2.u64 = 0;
}

} // namespace

int main(int argc, char** argv)
{
    std::string xex_path = "private/Default.xex";
    uint64_t entrypoint = PPC_CODE_BASE;

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg(argv[i]);
        if (arg == "--xex" && i + 1 < argc)
        {
            xex_path = argv[++i];
            continue;
        }
        if (arg == "--entrypoint" && i + 1 < argc)
        {
            uint64_t parsed = 0;
            if (!parse_hex_u64(argv[++i], parsed))
            {
                std::cerr << "Invalid entrypoint value." << "\n";
                print_usage(argv[0]);
                return 1;
            }
            entrypoint = parsed;
            continue;
        }
        if (arg == "--help" || arg == "-h")
        {
            print_usage(argv[0]);
            return 0;
        }

        std::cerr << "Unknown argument: " << arg << "\n";
        print_usage(argv[0]);
        return 1;
    }

    GuestMemory memory = allocate_guest_memory(PPC_MEMORY_SIZE);
    if (!memory.base)
    {
        return 1;
    }

    if (!load_xex_into_memory(memory.base, xex_path))
    {
        release_guest_memory(memory);
        return 1;
    }

    if (!initialize_function_table(memory.base))
    {
        release_guest_memory(memory);
        return 1;
    }

    PPCContext ctx{};
    initialize_context(ctx);

    std::cerr << "Jumping to entrypoint 0x" << std::hex << entrypoint << std::dec << "...\n";

    if (entrypoint < PPC_CODE_BASE)
    {
        std::cerr << "Entry point not found in recomp table.\n";
        release_guest_memory(memory);
        return 1;
    }

    const uint64_t entry_index = (entrypoint - PPC_CODE_BASE) * 2u;
    auto* table = reinterpret_cast<PPCFunc**>(memory.base + PPC_IMAGE_BASE + PPC_IMAGE_SIZE);
    if (!table[entry_index])
    {
        std::cerr << "Entry point not found in recomp table.\n";
        release_guest_memory(memory);
        return 1;
    }

    table[entry_index](ctx, memory.base);

    std::cerr << "Execution returned from entrypoint." << "\n";
    release_guest_memory(memory);
    return 0;
}