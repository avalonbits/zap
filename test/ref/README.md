# The reference assembler

zap's whole purpose is to agree with ez80asm byte for byte, so the differential
corpus is only as good as its access to a reference. Building one from source
meant a clone and a `make` before any comparison could run, which made the
strongest test in the suite the easiest one to skip.

These are the **official v2.2 release binaries**, taken verbatim from
[AgonPlatform/agon-ez80asm](https://github.com/AgonPlatform/agon-ez80asm/releases/tag/v2.2)
(released 2026-08-08). They are not rebuilt, repacked or stripped: an artefact
that has been altered is no longer the thing whose behaviour we are claiming to
match.

    linux_x86_64/ez80asm    ez80asm-linux_x86_64.tar.gz
    linux_aarch64/ez80asm   ez80asm-linux_aarch64.tar.gz
    agon/ez80asm.bin        ez80asm.bin  -- runs on the Agon under MOS

    sha256
    0ff53f9614cd426a2967657d03b90cdda978930d256876bd23a5e683ef46e182  linux_x86_64/ez80asm
    bb75017ba2b6f5df7b8fad7e5b66ea618bb9f7898c535628e560450669fad3d8  linux_aarch64/ez80asm
    7407fb6cfcd351906a2157d7951b83305d146e78d9381af7433132ab283d18d9  agon/ez80asm.bin

`test/corpus.sh` picks the host binary from `uname -m` and needs no arguments.
The Agon build is here for the other half of the comparison: benchmark figures
in commit messages are zap and ez80asm assembling the same source on the same
emulated machine, and that is only reproducible if the reference binary is
pinned too.

The two release assets not kept here are `ez80asm.exe` and
`ez80asm-darwin_arm64.tar.gz`. Add them if anyone needs to run the corpus on
Windows or an Apple machine; nothing about the runner assumes they are absent
beyond the `uname -m` mapping in `corpus.sh`.

MIT licensed -- see `LICENSE.agon-ez80asm`, copied from the same release.

## Upgrading

Replace the binaries, update the hashes above, and re-run the corpus with
`--ref` pointing at both the old and the new one. A behaviour change in the
reference is a change in what zap is supposed to do, so it belongs in its own
commit with the diff in corpus results quoted in the message, not folded into
whatever work happened to be in flight.
