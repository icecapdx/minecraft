# minecraft - Minecraft: Xbox 360 Edition Recompiled [XENON BRANCH]

Low-key just me attempting to see how far I can get with XenonRecomp.

# BUILDING

```bash
cmake -B build -S . \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++

# and

cmake --build build -j$(nproc)
```