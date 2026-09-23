# The reference assembler

zap aims to match ez80asm byte for byte, so the tests need an ez80asm to
compare against. These are the official v2.3 release binaries from
[AgonPlatform/agon-ez80asm](https://github.com/AgonPlatform/agon-ez80asm/releases/tag/v2.3)
(released 2026-09-13), copied as-is. They aren't rebuilt or stripped, so what
we test against is exactly what users run.

    linux_x86_64/ez80asm    from ez80asm-linux_x86_64.tar.gz
    linux_aarch64/ez80asm   from ez80asm-linux_aarch64.tar.gz
    agon/ez80asm.bin        ez80asm.bin, runs on the Agon under MOS

    sha256
    1be6dcaecceb17390f0f212e86d146ff626acc3c70183718e2c98d7032d3eefb  linux_x86_64/ez80asm
    b30631b7ecb3b1200e1b4e85d58aec7cfee69460c29a66e6a49ad6833aabf156  linux_aarch64/ez80asm
    c3aeba4cc6aaf77d60b176b74a3ebba2e537d99a096aa4e4a6cf1d29d0e99f31  agon/ez80asm.bin

The v2.2 binaries they replaced, in case you need to reproduce an older figure
(anything before 2026-09-21):

    0ff53f9614cd426a2967657d03b90cdda978930d256876bd23a5e683ef46e182  linux_x86_64/ez80asm
    bb75017ba2b6f5df7b8fad7e5b66ea618bb9f7898c535628e560450669fad3d8  linux_aarch64/ez80asm
    7407fb6cfcd351906a2157d7951b83305d146e78d9381af7433132ab283d18d9  agon/ez80asm.bin

`test/corpus.sh` picks the right host binary from `uname -m`. The Agon build is
used by the benchmarks, which run both assemblers on the same emulated machine.

The Windows and macOS builds aren't included. If you need them, add them and
extend the `uname -m` mapping in `corpus.sh`.

MIT licensed; see `LICENSE.agon-ez80asm`, copied from the same release.

## Upgrading

Replace the binaries, update the hashes above, and run the corpus against both
the old and new versions with `--ref`. A behaviour change in ez80asm changes
what zap should do, so make the upgrade its own commit and quote the corpus
differences in the message.
