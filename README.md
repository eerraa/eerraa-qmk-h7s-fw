# EERRAA QMK H7S

Firmware and the independent recovery bootloader share one Git root.
Agent work starts at [AGENTS.md](AGENTS.md); build and check commands run from
the component directory named below, not this root.

## Fresh clone

```sh
git clone --branch main https://github.com/eerraa/eerraa-qmk-h7s-fw.git eerraa-qmk-h7s
cd eerraa-qmk-h7s
git config core.hooksPath eerraa-qmk-h7s-fw/hooks
```

Git hook configuration is local and must be set once per clone. Git for Windows
provides the hook shell; Python 3.8+ must be on PATH. There are no submodules.
Firmware builds need ARM GCC, CMake 3.13+ and the selected generator's build
tool (MinGW Makefiles in the Windows guide). Follow the component build guide
rather than configuring this root as a CMake project. QMK CLI setup is not needed.

| Task | Working directory and guide |
|---|---|
| Keyboard firmware | [eerraa-qmk-h7s-fw](eerraa-qmk-h7s-fw/README.md) |
| Recovery bootloader | [eerraa-qmk-h7s-boot](eerraa-qmk-h7s-boot/README.md) |
| Affected validation | [firmware verification](eerraa-qmk-h7s-fw/docs/manual_verify.md) |

Documentation check, from the Git root:

```sh
cd eerraa-qmk-h7s-fw
python -X utf8 tools/era_doc_refs.py
```

Build tools and device programming are separate setup steps. Local ignored
worktrees, handover files, backups and `.mixdog/` are not downloaded by cloning.
Old commits retain their original layout; inspect them in a separate worktree.
